/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon GPU thread and SPSC ring buffer implementation.
 *
 *          DuckStation-style lock-free SPSC ring with atomic wake_counter
 *          and platform counting semaphore.  The ring is strictly
 *          single-producer (FIFO thread) / single-consumer (GPU thread).
 *          No mutexes on the hot path.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
/*
 * Platform semaphore and dispatch headers MUST be included before any 86Box
 * headers, because plat.h defines `fallthrough` as a macro, which breaks
 * macOS <dispatch/dispatch.h> (it uses `__has_attribute(fallthrough)`).
 */
#if defined(__APPLE__)
#    include <dispatch/dispatch.h>
#elif defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <semaphore.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/videocommon.h>

#include "vc_thread.h"
#include "vc_render_pass.h"
#include "vc_batch.h"
#include "vc_display.h"
#include "vc_texture.h"
#include "vc_gpu_state.h"
#include "vc_readback.h"

/* -------------------------------------------------------------------------- */
/*  Platform counting semaphore                                                */
/*                                                                             */
/*  86Box's thread API only provides manual-reset events, which are not        */
/*  suitable for the DuckStation wake pattern (needs counting semantics).       */
/*  We implement a thin platform abstraction directly here.                     */
/* -------------------------------------------------------------------------- */

#if defined(_WIN32)

static void *
vc_sem_create(void)
{
    HANDLE h = CreateSemaphoreW(NULL, 0, 0x7FFFFFFF, NULL);
    return (void *) h;
}

static void
vc_sem_destroy(void *sem)
{
    if (sem)
        CloseHandle((HANDLE) sem);
}

static void
vc_sem_post(void *sem)
{
    ReleaseSemaphore((HANDLE) sem, 1, NULL);
}

static void
vc_sem_wait(void *sem)
{
    WaitForSingleObject((HANDLE) sem, INFINITE);
}

#elif defined(__APPLE__)

static void *
vc_sem_create(void)
{
    dispatch_semaphore_t s = dispatch_semaphore_create(0);
    return (void *) s;
}

static void
vc_sem_destroy(void *sem)
{
    if (sem)
        dispatch_release((dispatch_semaphore_t) sem);
}

static void
vc_sem_post(void *sem)
{
    dispatch_semaphore_signal((dispatch_semaphore_t) sem);
}

static void
vc_sem_wait(void *sem)
{
    dispatch_semaphore_wait((dispatch_semaphore_t) sem, DISPATCH_TIME_FOREVER);
}

#else /* POSIX (Linux, FreeBSD, etc.) */

static void *
vc_sem_create(void)
{
    sem_t *s = (sem_t *) malloc(sizeof(sem_t));
    if (s && sem_init(s, 0, 0) != 0) {
        free(s);
        return NULL;
    }
    return (void *) s;
}

static void
vc_sem_destroy(void *sem)
{
    if (sem) {
        sem_destroy((sem_t *) sem);
        free(sem);
    }
}

static void
vc_sem_post(void *sem)
{
    sem_post((sem_t *) sem);
}

static void
vc_sem_wait(void *sem)
{
    sem_wait((sem_t *) sem);
}

#endif

/* -------------------------------------------------------------------------- */
/*  Platform yield                                                             */
/* -------------------------------------------------------------------------- */

#if defined(_WIN32)

static void
vc_yield(void)
{
    SwitchToThread();
}

#else

#    include <sched.h>

static void
vc_yield(void)
{
    sched_yield();
}

#endif

/* -------------------------------------------------------------------------- */
/*  SPSC ring buffer                                                           */
/* -------------------------------------------------------------------------- */

int
vc_ring_init(vc_ring_t *ring)
{
    memset(ring, 0, sizeof(vc_ring_t));

    ring->buffer = (uint8_t *) malloc(VC_RING_SIZE);
    if (!ring->buffer)
        return -1;

    memset(ring->buffer, 0, VC_RING_SIZE);
    atomic_store_explicit(&ring->write_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->read_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->wake_counter, 0, memory_order_relaxed);

    ring->wake_sem = vc_sem_create();
    if (!ring->wake_sem) {
        free(ring->buffer);
        ring->buffer = NULL;
        return -1;
    }

    return 0;
}

void
vc_ring_destroy(vc_ring_t *ring)
{
    if (!ring)
        return;

    if (ring->wake_sem) {
        vc_sem_destroy(ring->wake_sem);
        ring->wake_sem = NULL;
    }

    if (ring->buffer) {
        free(ring->buffer);
        ring->buffer = NULL;
    }
}

/* Align a size up to VC_RING_ALIGN. */
static uint16_t
vc_ring_align(uint16_t size)
{
    return (uint16_t) ((size + VC_RING_ALIGN - 1) & ~(VC_RING_ALIGN - 1));
}

/* -------------------------------------------------------------------------- */
/*  Free space calculation                                                     */
/* -------------------------------------------------------------------------- */

static uint32_t
vc_ring_free_space(vc_ring_t *ring)
{
    uint32_t wp = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
    uint32_t rp = atomic_load_explicit(&ring->read_pos, memory_order_acquire);

    if (wp >= rp)
        return VC_RING_SIZE - (wp - rp) - 1; /* -1 to distinguish full from empty */
    return rp - wp - 1;
}

/* -------------------------------------------------------------------------- */
/*  Backpressure: spin-yield until space is available                           */
/* -------------------------------------------------------------------------- */

void
vc_ring_wait_for_space(vc_ring_t *ring, uint32_t needed)
{
    while (vc_ring_free_space(ring) < needed) {
        vc_ring_wake(ring); /* Ensure GPU thread is processing. */
        vc_yield();
    }
}

/* -------------------------------------------------------------------------- */
/*  DuckStation-style wake / sleep                                             */
/* -------------------------------------------------------------------------- */

void
vc_ring_wake(vc_ring_t *ring)
{
    int32_t old = atomic_fetch_add_explicit(&ring->wake_counter, 2,
                                            memory_order_release);
    if (old < 0)
        vc_sem_post(ring->wake_sem);
}

bool
vc_ring_sleep(vc_ring_t *ring)
{
    int32_t old = atomic_fetch_sub_explicit(&ring->wake_counter, 1,
                                            memory_order_acq_rel);
    if (old > 0)
        return true; /* More work pending, don't actually sleep. */

    vc_sem_wait(ring->wake_sem);
    return true;
}

/* -------------------------------------------------------------------------- */
/*  Push API (producer -- FIFO thread only)                                    */
/* -------------------------------------------------------------------------- */

void *
vc_ring_push(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size)
{
    total_size = vc_ring_align(total_size);

    uint32_t needed = total_size + (uint32_t) sizeof(vc_ring_cmd_header_t);
    if (needed < (uint32_t) total_size + VC_RING_ALIGN)
        needed = (uint32_t) total_size + VC_RING_ALIGN;

    vc_ring_wait_for_space(ring, needed);

    uint32_t wp = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);

    /* Check if we need a wraparound sentinel. */
    if (wp + total_size > VC_RING_SIZE) {
        vc_ring_cmd_header_t *wrap = (vc_ring_cmd_header_t *) &ring->buffer[wp];
        wrap->type                 = VC_CMD_WRAPAROUND;
        wrap->size                 = 0;
        wrap->reserved             = 0;

        atomic_store_explicit(&ring->write_pos, 0, memory_order_release);
        wp = 0;

        while (vc_ring_free_space(ring) < (uint32_t) total_size) {
            vc_ring_wake(ring);
            vc_yield();
        }
    }

    vc_ring_cmd_header_t *hdr = (vc_ring_cmd_header_t *) &ring->buffer[wp];
    hdr->type                 = cmd_type;
    hdr->size                 = total_size;
    hdr->reserved             = 0;

    uint32_t new_wp = (wp + total_size) & VC_RING_MASK;
    atomic_store_explicit(&ring->write_pos, new_wp, memory_order_release);

    return (void *) (hdr + 1);
}

void *
vc_ring_push_and_wake(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size)
{
    void *payload = vc_ring_push(ring, cmd_type, total_size);
    vc_ring_wake(ring);
    return payload;
}

/* -------------------------------------------------------------------------- */
/*  Frame resource management (command pools, command buffers, fences)          */
/* -------------------------------------------------------------------------- */

static int
vc_frame_resources_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    for (uint32_t i = 0; i < VC_NUM_FRAMES; i++) {
        VkCommandPoolCreateInfo pool_ci;
        memset(&pool_ci, 0, sizeof(pool_ci));
        pool_ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_ci.queueFamilyIndex = ctx->queue_family;

        VkResult result = vkCreateCommandPool(ctx->device, &pool_ci, NULL,
                                              &gpu_st->frame[i].cmd_pool);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: vkCreateCommandPool failed (%d) for frame %u\n",
                   result, i);
            return -1;
        }

        VkCommandBufferAllocateInfo alloc_ci;
        memset(&alloc_ci, 0, sizeof(alloc_ci));
        alloc_ci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_ci.commandPool        = gpu_st->frame[i].cmd_pool;
        alloc_ci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_ci.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(ctx->device, &alloc_ci,
                                          &gpu_st->frame[i].cmd_buf);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: vkAllocateCommandBuffers failed (%d) for frame %u\n",
                   result, i);
            return -1;
        }

        VkFenceCreateInfo fence_ci;
        memset(&fence_ci, 0, sizeof(fence_ci));
        fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        result = vkCreateFence(ctx->device, &fence_ci, NULL,
                               &gpu_st->frame[i].fence);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: vkCreateFence failed (%d) for frame %u\n",
                   result, i);
            return -1;
        }

        gpu_st->frame[i].submitted = 0;
    }

    gpu_st->frame_index = 0;
    return 0;
}

static void
vc_frame_resources_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    for (uint32_t i = 0; i < VC_NUM_FRAMES; i++) {
        if (gpu_st->frame[i].fence != VK_NULL_HANDLE) {
            vkDestroyFence(ctx->device, gpu_st->frame[i].fence, NULL);
            gpu_st->frame[i].fence = VK_NULL_HANDLE;
        }
        if (gpu_st->frame[i].cmd_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(ctx->device, gpu_st->frame[i].cmd_pool, NULL);
            gpu_st->frame[i].cmd_pool = VK_NULL_HANDLE;
            gpu_st->frame[i].cmd_buf  = VK_NULL_HANDLE;
        }
        gpu_st->frame[i].submitted = 0;
    }
}

/* -------------------------------------------------------------------------- */
/*  GPU thread: begin a new frame                                              */
/* -------------------------------------------------------------------------- */

static void
vc_gpu_begin_frame(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_frame_t *f = &gpu_st->frame[gpu_st->frame_index];

    /* Wait for this frame's previous submission to complete. */
    if (f->submitted) {
        vkWaitForFences(ctx->device, 1, &f->fence, VK_TRUE, UINT64_MAX);
        vkResetFences(ctx->device, 1, &f->fence);
        f->submitted = 0;
    }

    vkResetCommandBuffer(f->cmd_buf, 0);

    VkCommandBufferBeginInfo begin_ci;
    memset(&begin_ci, 0, sizeof(begin_ci));
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f->cmd_buf, &begin_ci);

    /* Begin render pass on the back framebuffer. */
    int          back_idx  = gpu_st->rp.back_index;
    VkRenderPass rp_handle = gpu_st->rp.fb[back_idx].first_frame
        ? gpu_st->rp.render_pass_clear
        : gpu_st->rp.render_pass_load;

    VkClearValue clear_values[2];
    memset(clear_values, 0, sizeof(clear_values));
    clear_values[1].depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo rp_begin;
    memset(&rp_begin, 0, sizeof(rp_begin));
    rp_begin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass               = rp_handle;
    rp_begin.framebuffer              = gpu_st->rp.fb[back_idx].framebuffer;
    rp_begin.renderArea.extent.width  = gpu_st->rp.fb[back_idx].width;
    rp_begin.renderArea.extent.height = gpu_st->rp.fb[back_idx].height;
    rp_begin.clearValueCount          = 2;
    rp_begin.pClearValues             = clear_values;

    vkCmdBeginRenderPass(f->cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    /* Dynamic viewport and scissor. */
    VkViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.width    = (float) gpu_st->rp.fb[back_idx].width;
    viewport.height   = (float) gpu_st->rp.fb[back_idx].height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(f->cmd_buf, 0, 1, &viewport);

    VkRect2D scissor;
    memset(&scissor, 0, sizeof(scissor));
    scissor.extent.width  = gpu_st->rp.fb[back_idx].width;
    scissor.extent.height = gpu_st->rp.fb[back_idx].height;
    vkCmdSetScissor(f->cmd_buf, 0, 1, &scissor);

    /* Bind pipeline and vertex buffer. */
    if (gpu_st->pipe.pipeline != VK_NULL_HANDLE)
        vkCmdBindPipeline(f->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          gpu_st->pipe.pipeline);

    if (gpu_st->batch.vertex_buffer != VK_NULL_HANDLE) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(f->cmd_buf, 0, 1,
                               &gpu_st->batch.vertex_buffer, &offset);
    }

    gpu_st->render_pass_active          = 1;
    gpu_st->rp.fb[back_idx].first_frame = 0;
}

/* -------------------------------------------------------------------------- */
/*  GPU thread: end current frame                                              */
/* -------------------------------------------------------------------------- */

void
vc_gpu_end_frame(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    if (!gpu_st->render_pass_active)
        return;

    vc_frame_t *f = &gpu_st->frame[gpu_st->frame_index];

    vkCmdEndRenderPass(f->cmd_buf);
    vkEndCommandBuffer(f->cmd_buf);

    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &f->cmd_buf;

    VkResult result = vkQueueSubmit(ctx->queue, 1, &submit, f->fence);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkQueueSubmit failed (%d)\n", result);
    }

    f->submitted               = 1;
    gpu_st->render_pass_active = 0;

    gpu_st->frame_index = (gpu_st->frame_index + 1) % VC_NUM_FRAMES;
    vc_batch_reset(ctx, gpu_st);
}

/* -------------------------------------------------------------------------- */
/*  GPU thread: handle VC_CMD_TRIANGLE                                         */
/* -------------------------------------------------------------------------- */

static void
vc_gpu_handle_triangle(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st, const void *payload)
{
    if (gpu_st->rp.fb[gpu_st->rp.back_index].framebuffer == VK_NULL_HANDLE)
        return;
    if (gpu_st->pipe.pipeline == VK_NULL_HANDLE)
        return;

    /* A triangle means Voodoo is actively rendering. */

    if (!gpu_st->render_pass_active)
        vc_gpu_begin_frame(ctx, gpu_st);

    vc_frame_t *f = &gpu_st->frame[gpu_st->frame_index];

    /* Extract push constants and vertices from payload. */
    const vc_push_constants_t *pc    = (const vc_push_constants_t *) payload;
    const vc_vertex_t         *verts = (const vc_vertex_t *) ((const uint8_t *) payload + sizeof(vc_push_constants_t));

    /* Bind texture descriptor set. */
    vc_texture_bind_current(ctx, gpu_st, f->cmd_buf);

    /* Append triangle to batch vertex buffer. */
    uint32_t first_vertex = gpu_st->batch.triangle_count * 3;

    if (vc_batch_append_triangle(ctx, gpu_st, verts) != 0) {
        /* Batch full -- reset and start fresh. */
        VC_LOG("VideoCommon: batch full (%u tris), resetting\n",
               gpu_st->batch.triangle_count);
        vc_batch_reset(ctx, gpu_st);
        first_vertex = 0;
        vc_batch_append_triangle(ctx, gpu_st, verts);
    }

    /* Push constants. */
    vkCmdPushConstants(f->cmd_buf, gpu_st->pipe.layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(vc_push_constants_t), pc);

    /* Dynamic depth state (EDS1).
     * fbzMode bit  4: depth test enable
     * fbzMode bit 10: depth write enable
     * fbzMode bits [7:5]: depth function (maps 1:1 to VkCompareOp) */
    if (ctx->caps.has_extended_dynamic_state) {
        uint32_t fbz = pc->fbzMode;

        VkBool32 depth_test  = (fbz & (1u << 4)) ? VK_TRUE : VK_FALSE;
        VkBool32 depth_write = (fbz & (1u << 10)) ? VK_TRUE : VK_FALSE;

        /* Voodoo depth function: bits [7:5], 0=NEVER..7=ALWAYS.
         * VkCompareOp enum: NEVER=0, LESS=1, EQUAL=2, LESS_OR_EQUAL=3,
         *   GREATER=4, NOT_EQUAL=5, GREATER_OR_EQUAL=6, ALWAYS=7.
         * Direct 1:1 mapping. */
        VkCompareOp depth_func = (VkCompareOp) ((fbz >> 5) & 7u);

        vkCmdSetDepthTestEnableEXT(f->cmd_buf, depth_test);
        vkCmdSetDepthWriteEnableEXT(f->cmd_buf, depth_write);
        vkCmdSetDepthCompareOpEXT(f->cmd_buf, depth_func);
    }

    /* Scissor from framebuffer dimensions (full viewport -- clip rect
     * support will be added when clipLeftRight/clipLowYHighY registers
     * are wired through the ring command). */
    {
        int back_idx = gpu_st->rp.back_index;
        VkRect2D scissor;
        memset(&scissor, 0, sizeof(scissor));
        scissor.extent.width  = gpu_st->rp.fb[back_idx].width;
        scissor.extent.height = gpu_st->rp.fb[back_idx].height;
        vkCmdSetScissor(f->cmd_buf, 0, 1, &scissor);
    }

    /* Draw this triangle. */

    vkCmdDraw(f->cmd_buf, 3, 1, first_vertex, 0);
}

/* -------------------------------------------------------------------------- */
/*  GPU thread: handle VC_CMD_SWAP                                             */
/* -------------------------------------------------------------------------- */

static void
vc_gpu_handle_swap(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{

    /* If no render pass is active, this is an "empty" swap (no triangles
       were submitted since the last swap).  Nothing to present. */
    if (!gpu_st->render_pass_active) {

        return;
    }

    /* Real swap with triangles.  Claim display ownership and reset the
       VGA timeout counter.  This is the ONLY place that sets display_owner
       to 1 — the timeout in vc_display_tick() is the ONLY place that
       clears it.  Neither of these affect vc_divert_to_gpu (triangle
       routing), which stays permanently set. */
    gpu_st->disp.display_owner = 1;
    gpu_st->disp.vga_ticks_since_present = 0;

    vc_frame_t *f = &gpu_st->frame[gpu_st->frame_index];

    /* All triangles were drawn individually in vc_gpu_handle_triangle,
       so no batch flush needed here. */

    /* End offscreen render pass. */
    vkCmdEndRenderPass(f->cmd_buf);
    gpu_st->render_pass_active = 0;

    /* Record readback copy: offscreen color -> staging buffer.
       This happens while the image is in COLOR_ATTACHMENT_OPTIMAL layout;
       vc_readback_record_copy transitions to TRANSFER_SRC and back. */
    vc_readback_record_copy(ctx, gpu_st, f->cmd_buf);

    /* If swapchain is available, do post-process blit + present. */
    if (gpu_st->disp.swapchain != VK_NULL_HANDLE) {
        int present_result = vc_display_present(ctx, gpu_st, f->cmd_buf,
                                                gpu_st->frame_index);

        if (present_result == 1) {
            /* Swapchain needs recreation. */
            vc_display_recreate_swapchain(ctx, gpu_st);
        }

        if (present_result == 0 || present_result == 1) {
            /* Submit was handled by vc_display_present (success or recreation).
               Skip the standalone submit below. */
            goto advance;
        }
    }

    /* No swapchain (or failed present): submit without present. */
    vkEndCommandBuffer(f->cmd_buf);

    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &f->cmd_buf;

    VkResult result = vkQueueSubmit(ctx->queue, 1, &submit, f->fence);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkQueueSubmit failed (%d)\n", result);
    }
    f->submitted = 1;

advance:
    /* Wait for this frame's submission to complete so readback data is valid.
       This is a synchronous hack -- acceptable for Glide detection, not for
       sustained rendering.  The full Phase 7 readback will be async. */
    if (f->submitted) {
        vkWaitForFences(ctx->device, 1, &f->fence, VK_TRUE, UINT64_MAX);
        /* Convert RGBA8 staging data to RGB565 and write to SW FB. */
        vc_readback_copy_to_sw_fb(ctx, gpu_st);
    }

    /* Swap front/back offscreen framebuffers. */
    vc_render_pass_swap(gpu_st);
    gpu_st->frame_index = (gpu_st->frame_index + 1) % VC_NUM_FRAMES;
    vc_batch_reset(ctx, gpu_st);
}

/* -------------------------------------------------------------------------- */
/*  GPU thread init / cleanup                                                  */
/* -------------------------------------------------------------------------- */

static vc_gpu_state_t *
vc_gpu_thread_init(vc_ctx_t *ctx)
{
    vc_gpu_state_t *gpu_st = (vc_gpu_state_t *) malloc(sizeof(vc_gpu_state_t));
    if (!gpu_st) {
        VC_LOG("VideoCommon: failed to allocate gpu_state\n");
        return NULL;
    }
    memset(gpu_st, 0, sizeof(vc_gpu_state_t));

    gpu_st->fb_width  = 640;
    gpu_st->fb_height = 480;

    /* Display state (surface/swapchain/post-process -- populated later
       when VCRenderer provides a VkSurfaceKHR). */
    vc_display_state_init(&gpu_st->disp);

    /* Copy divert_to_gpu_ptr from ctx (set before thread creation,
       so it is guaranteed visible here via the thread-creation
       happens-before edge).  vc_display_create() sets this to 1
       to permanently enable triangle routing to the VK ring. */
    gpu_st->disp.divert_to_gpu_ptr = ctx->divert_to_gpu_ptr;

    /* Frame resources. */
    if (vc_frame_resources_create(ctx, gpu_st) != 0)
        goto fail;

    /* Render passes. */
    if (vc_render_pass_create(ctx, gpu_st) != 0)
        goto fail;

    /* Framebuffers. */
    if (vc_render_pass_create_framebuffers(ctx, gpu_st, gpu_st->fb_width, gpu_st->fb_height) != 0)
        goto fail;

    /* Vertex buffer. */
    if (vc_batch_create(ctx, gpu_st) != 0)
        goto fail;

    /* Shaders. */
    if (vc_shaders_create(ctx, &gpu_st->shaders) != 0)
        goto fail;

    /* Textures. */
    if (vc_texture_create(ctx, &gpu_st->tex) != 0)
        goto fail;

    /* Readback staging buffer (GPU->SW FB copy for LFB reads). */
    if (vc_readback_create(ctx, gpu_st) != 0)
        goto fail;

    /* Pipeline. */
    if (vc_pipeline_create(ctx, &gpu_st->pipe, &gpu_st->shaders,
                           gpu_st->rp.render_pass_load,
                           gpu_st->tex.desc_layout)
        != 0)
        goto fail;

    /* Publish framebuffer dimensions for FIFO thread. */
    atomic_store_explicit(&ctx->fb_width, gpu_st->fb_width, memory_order_release);
    atomic_store_explicit(&ctx->fb_height, gpu_st->fb_height, memory_order_release);

    VC_LOG("VideoCommon: GPU thread init complete (%ux%u)\n",
           gpu_st->fb_width, gpu_st->fb_height);
    return gpu_st;

fail:
    VC_LOG("VideoCommon: GPU thread init FAILED\n");
    /* Partial cleanup -- destroy what was created. */
    vc_pipeline_destroy(ctx, &gpu_st->pipe);
    vc_readback_destroy(ctx, gpu_st);
    vc_texture_destroy(ctx, &gpu_st->tex);
    vc_shaders_destroy(ctx, &gpu_st->shaders);
    vc_batch_destroy(ctx, gpu_st);
    vc_render_pass_destroy_framebuffers(ctx, gpu_st);
    vc_render_pass_destroy(ctx, gpu_st);
    vc_frame_resources_destroy(ctx, gpu_st);
    free(gpu_st);
    return NULL;
}

static void
vc_gpu_thread_cleanup(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    if (!gpu_st)
        return;

    /* End any active render pass without submitting. */
    if (gpu_st->render_pass_active) {
        vc_frame_t *f = &gpu_st->frame[gpu_st->frame_index];
        vkCmdEndRenderPass(f->cmd_buf);
        vkEndCommandBuffer(f->cmd_buf);
        gpu_st->render_pass_active = 0;
    }

    vkDeviceWaitIdle(ctx->device);

    vc_readback_destroy(ctx, gpu_st);
    vc_display_destroy(ctx, gpu_st);
    vc_pipeline_destroy(ctx, &gpu_st->pipe);
    vc_texture_destroy(ctx, &gpu_st->tex);
    vc_shaders_destroy(ctx, &gpu_st->shaders);
    vc_batch_destroy(ctx, gpu_st);
    vc_render_pass_destroy_framebuffers(ctx, gpu_st);
    vc_render_pass_destroy(ctx, gpu_st);
    vc_frame_resources_destroy(ctx, gpu_st);

    free(gpu_st);
}

/* -------------------------------------------------------------------------- */
/*  GPU thread main loop                                                       */
/* -------------------------------------------------------------------------- */

void
vc_gpu_thread_func(void *param)
{
    vc_ctx_t  *ctx  = (vc_ctx_t *) param;
    vc_ring_t *ring = &ctx->ring;

    VC_LOG("VideoCommon: GPU thread started\n");

    vc_gpu_state_t *gpu_st = vc_gpu_thread_init(ctx);

    ctx->render_data = gpu_st;
    atomic_store_explicit(&ctx->running, 1, memory_order_release);

    while (atomic_load_explicit(&ctx->running, memory_order_acquire)) {
        /* Check for display surface changes (new surface, resize, teardown). */
        if (gpu_st)
            vc_display_tick(ctx, gpu_st);

        uint32_t rp = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
        uint32_t wp = atomic_load_explicit(&ring->write_pos, memory_order_acquire);

        if (rp == wp) {
            /* Ring is empty -- sleep until woken by a ring push or
               VGA blit notification.  The vc_display_tick() call above
               already consumed any pending VGA frame. */
            vc_ring_sleep(ring);
            continue;
        }

        vc_ring_cmd_header_t *hdr = (vc_ring_cmd_header_t *) &ring->buffer[rp];

        switch (hdr->type) {
            case VC_CMD_SHUTDOWN:
                VC_LOG("VideoCommon: GPU thread received SHUTDOWN\n");
                atomic_store_explicit(&ctx->running, 0, memory_order_release);
                break;

            case VC_CMD_WRAPAROUND:
                atomic_store_explicit(&ring->read_pos, 0, memory_order_release);
                continue;

            case VC_CMD_TRIANGLE:
                if (gpu_st)
                    vc_gpu_handle_triangle(ctx, gpu_st, (const void *) (hdr + 1));
                break;

            case VC_CMD_SWAP:
                if (gpu_st) {
                    /* Deferred renderer switch: first swap means the guest
                       is actively rendering, safe to switch from SW display
                       to VCRenderer.  Before this, VGA passthrough stays on
                       the normal Qt display path so Glide detection isn't
                       disrupted. */
                    if (!gpu_st->renderer_switch_done) {
                        gpu_st->renderer_switch_done = 1;
                        vc_notify_renderer_ready();
                    }
                    vc_gpu_handle_swap(ctx, gpu_st);
                }
                break;

            case VC_CMD_TEXTURE_UPLOAD:
                if (gpu_st)
                    vc_texture_handle_upload(ctx, gpu_st,
                        (const vc_tex_upload_payload_t *) (hdr + 1));
                break;

            case VC_CMD_TEXTURE_BIND:
                if (gpu_st)
                    vc_texture_handle_bind(ctx, gpu_st,
                        (const vc_tex_bind_payload_t *) (hdr + 1));
                break;

            default:
                VC_LOG("VideoCommon: GPU thread skipping cmd %d (size %d)\n",
                       hdr->type, hdr->size);
                break;
        }

        if (hdr->type != VC_CMD_WRAPAROUND) {
            uint16_t aligned_size = vc_ring_align(hdr->size);
            uint32_t new_rp       = (rp + aligned_size) & VC_RING_MASK;
            atomic_store_explicit(&ring->read_pos, new_rp, memory_order_release);
        }
    }

    vc_gpu_thread_cleanup(ctx, gpu_st);
    ctx->render_data = NULL;

    VC_LOG("VideoCommon: GPU thread exited\n");
}

/* -------------------------------------------------------------------------- */
/*  Thread lifecycle                                                           */
/* -------------------------------------------------------------------------- */

int
vc_start_gpu_thread(vc_ctx_t *ctx)
{
    if (vc_ring_init(&ctx->ring) != 0) {
        VC_LOG("VideoCommon: ring buffer init failed\n");
        return -1;
    }

    ctx->gpu_thread = thread_create(vc_gpu_thread_func, ctx);
    if (!ctx->gpu_thread) {
        VC_LOG("VideoCommon: thread_create failed\n");
        vc_ring_destroy(&ctx->ring);
        return -1;
    }

    VC_LOG("VideoCommon: GPU thread created\n");
    return 0;
}

void
vc_stop_gpu_thread(vc_ctx_t *ctx)
{
    if (!ctx->gpu_thread)
        return;

    vc_ring_push_and_wake(&ctx->ring, VC_CMD_SHUTDOWN,
                          vc_ring_align(sizeof(vc_ring_cmd_header_t)));

    thread_wait((thread_t *) ctx->gpu_thread);
    ctx->gpu_thread = NULL;

    vc_ring_destroy(&ctx->ring);

    VC_LOG("VideoCommon: GPU thread stopped and ring destroyed\n");
}
