/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon batch -- vertex buffer management and triangle
 *          batching.  The vertex buffer is host-visible + coherent
 *          for Phase 2 simplicity (no staging buffer).
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>

#include "vc_gpu_state.h"
#include "vc_batch.h"

/* C-callable VMA wrappers (implemented in vc_vma_impl.cpp). */
extern VkResult vc_vma_create_buffer(void *allocator,
                                     const VkBufferCreateInfo *buffer_ci,
                                     int mapped, VkBuffer *out_buffer,
                                     void **out_alloc, void **out_mapped);
extern void     vc_vma_destroy_buffer(void *allocator, VkBuffer buffer,
                                      void *alloc);

int
vc_batch_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    VkDeviceSize buf_size = (VkDeviceSize) VC_BATCH_MAX_TRIANGLES * 3
                          * sizeof(vc_vertex_t);

    VkBufferCreateInfo buf_ci;
    memset(&buf_ci, 0, sizeof(buf_ci));
    buf_ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size        = buf_size;
    buf_ci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vc_vma_create_buffer(
        ctx->allocator, &buf_ci, 1 /* mapped */,
        &gpu_st->batch.vertex_buffer, &gpu_st->batch.vertex_alloc,
        &gpu_st->batch.vertex_mapped);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vertex buffer creation failed (%d)\n", result);
        return -1;
    }

    gpu_st->batch.triangle_count = 0;
    gpu_st->batch.vertex_offset  = 0;

    VC_LOG("VideoCommon: vertex buffer created (%u bytes)\n",
           (uint32_t) buf_size);
    return 0;
}

void
vc_batch_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    if (gpu_st->batch.vertex_buffer != VK_NULL_HANDLE) {
        vc_vma_destroy_buffer(ctx->allocator, gpu_st->batch.vertex_buffer,
                              gpu_st->batch.vertex_alloc);
        gpu_st->batch.vertex_buffer = VK_NULL_HANDLE;
        gpu_st->batch.vertex_alloc  = NULL;
        gpu_st->batch.vertex_mapped = NULL;
    }
}

int
vc_batch_append_triangle(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                         const vc_vertex_t verts[3])
{
    (void) ctx;

    if (gpu_st->batch.triangle_count >= VC_BATCH_MAX_TRIANGLES)
        return -1;

    uint8_t *dst = (uint8_t *) gpu_st->batch.vertex_mapped
                 + gpu_st->batch.vertex_offset;
    memcpy(dst, verts, 3 * sizeof(vc_vertex_t));

    gpu_st->batch.vertex_offset += 3 * (uint32_t) sizeof(vc_vertex_t);
    gpu_st->batch.triangle_count++;

    return 0;
}

void
vc_batch_reset(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    (void) ctx;
    gpu_st->batch.triangle_count = 0;
    gpu_st->batch.vertex_offset  = 0;
}
