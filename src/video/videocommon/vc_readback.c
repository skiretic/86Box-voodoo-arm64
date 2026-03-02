/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon readback hack -- copies the Vulkan offscreen
 *          framebuffer back to the Voodoo SW framebuffer (fb_mem) so
 *          that LFB reads return real pixel data.  This is a minimal
 *          implementation to unblock Glide detection; the full Phase 7
 *          readback will replace it.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "vc_internal.h"
#include "vc_gpu_state.h"

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>

#include "vc_readback.h"

/* -------------------------------------------------------------------------- */
/*  Logging                                                                    */
/* -------------------------------------------------------------------------- */

#ifdef ENABLE_VIDEOCOMMON_LOG
extern int vc_do_log;

static void
vc_readback_log(const char *fmt, ...)
{
    va_list ap;

    if (vc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vc_readback_log(fmt, ...)
#endif

/* -------------------------------------------------------------------------- */
/*  VMA extern                                                                 */
/* -------------------------------------------------------------------------- */

extern VkResult vc_vma_create_readback_buffer(void                    *allocator,
                                              const VkBufferCreateInfo *buffer_ci,
                                              VkBuffer                *out_buffer,
                                              void                   **out_alloc,
                                              void                   **out_mapped);
extern void     vc_vma_destroy_buffer(void *allocator, VkBuffer buffer,
                                      void *alloc);

/* -------------------------------------------------------------------------- */
/*  Max supported readback resolution                                          */
/* -------------------------------------------------------------------------- */

#define VC_READBACK_MAX_WIDTH  1024
#define VC_READBACK_MAX_HEIGHT 768

/* -------------------------------------------------------------------------- */
/*  Create / destroy                                                           */
/* -------------------------------------------------------------------------- */

int
vc_readback_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    uint32_t w = gpu_st->fb_width;
    uint32_t h = gpu_st->fb_height;
    if (w == 0) w = 640;
    if (h == 0) h = 480;

    /* Over-allocate for max resolution to avoid recreating on resize. */
    uint32_t alloc_w = VC_READBACK_MAX_WIDTH;
    uint32_t alloc_h = VC_READBACK_MAX_HEIGHT;

    VkDeviceSize buf_size = (VkDeviceSize) alloc_w * alloc_h * 4; /* RGBA8 */

    VkBufferCreateInfo buf_ci;
    memset(&buf_ci, 0, sizeof(buf_ci));
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size  = buf_size;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkResult result = vc_vma_create_readback_buffer(
        ctx->allocator, &buf_ci,
        &gpu_st->readback_buffer, &gpu_st->readback_alloc,
        &gpu_st->readback_mapped);

    if (result != VK_SUCCESS) {
        vc_readback_log("VideoCommon: readback buffer creation failed (%d)\n",
                        result);
        return -1;
    }

    gpu_st->readback_width  = w;
    gpu_st->readback_height = h;

    vc_readback_log("VideoCommon: readback buffer created (%ux%u, alloc %ux%u)\n",
                    w, h, alloc_w, alloc_h);
    return 0;
}

void
vc_readback_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    if (gpu_st->readback_buffer != VK_NULL_HANDLE) {
        vc_vma_destroy_buffer(ctx->allocator, gpu_st->readback_buffer,
                              gpu_st->readback_alloc);
        gpu_st->readback_buffer = VK_NULL_HANDLE;
        gpu_st->readback_alloc  = NULL;
        gpu_st->readback_mapped = NULL;
    }
}

/* -------------------------------------------------------------------------- */
/*  Record copy commands                                                       */
/* -------------------------------------------------------------------------- */

void
vc_readback_record_copy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                        VkCommandBuffer cmd_buf)
{
    if (gpu_st->readback_buffer == VK_NULL_HANDLE)
        return;

    int      back_idx = gpu_st->rp.back_index;
    VkImage  src_img  = gpu_st->rp.fb[back_idx].color_image;
    uint32_t w        = gpu_st->rp.fb[back_idx].width;
    uint32_t h        = gpu_st->rp.fb[back_idx].height;

    if (w > VC_READBACK_MAX_WIDTH || h > VC_READBACK_MAX_HEIGHT)
        return;

    /* Update tracked dimensions for the copy-to-sw-fb step. */
    gpu_st->readback_width  = w;
    gpu_st->readback_height = h;

    /* Transition: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL. */
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = src_img;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);

    /* Copy image to buffer. */
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0; /* Tightly packed. */
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent.width  = w;
    region.imageExtent.height = h;
    region.imageExtent.depth  = 1;

    vkCmdCopyImageToBuffer(cmd_buf, src_img,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           gpu_st->readback_buffer, 1, &region);

    /* Transition back: TRANSFER_SRC_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL. */
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
}

/* -------------------------------------------------------------------------- */
/*  Convert RGBA8 to RGB565 and write to SW FB                                 */
/* -------------------------------------------------------------------------- */

void
vc_readback_copy_to_sw_fb(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    if (!ctx->voodoo_ptr || !gpu_st->readback_mapped)
        return;

    voodoo_t       *voodoo = (voodoo_t *) ctx->voodoo_ptr;
    const uint8_t  *src    = (const uint8_t *) gpu_st->readback_mapped;
    uint32_t        w      = gpu_st->readback_width;
    uint32_t        h      = gpu_st->readback_height;

    /* Determine where to write in fb_mem.
       Use params.draw_offset -- this is where the SW rasterizer would have
       written, and where LFB reads (via fb_read_offset) will look. */
    uint32_t dst_offset   = voodoo->params.draw_offset;
    int      row_width    = voodoo->row_width;
    uint32_t fb_mask      = voodoo->fb_mask;

    if (row_width <= 0 || !voodoo->fb_mem)
        return;

    /* Convert RGBA8 -> RGB565 and write row by row. */
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src_row = src + (y * w * 4);
        uint32_t       fb_addr = dst_offset + (y * (uint32_t) row_width);

        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = src_row[x * 4 + 0];
            uint8_t g = src_row[x * 4 + 1];
            uint8_t b = src_row[x * 4 + 2];

            uint16_t rgb565 = ((uint16_t)(r >> 3) << 11)
                             | ((uint16_t)(g >> 2) << 5)
                             | ((uint16_t)(b >> 3));

            uint32_t addr = (fb_addr + x * 2) & fb_mask;
            *(uint16_t *) (&voodoo->fb_mem[addr]) = rgb565;
        }
    }

    /* Mark all lines dirty so the display callback will blit them. */
    memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
    voodoo->dirty_line_low  = 0;
    voodoo->dirty_line_high = (int) h;

    vc_readback_log("VideoCommon: readback copied %ux%u to SW FB (offset 0x%x)\n",
                    w, h, dst_offset);
}
