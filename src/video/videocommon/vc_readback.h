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
#ifndef VIDEOCOMMON_READBACK_H
#define VIDEOCOMMON_READBACK_H

#include "vc_internal.h"
#include "vc_gpu_state.h"

/* Create the readback staging buffer (called once during GPU thread init). */
int vc_readback_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Destroy the readback staging buffer. */
void vc_readback_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Record image-to-buffer copy commands into cmd_buf for the back framebuffer.
   Must be called AFTER vkCmdEndRenderPass and BEFORE vkEndCommandBuffer.
   The image is transitioned TRANSFER_SRC -> COLOR_ATTACHMENT_OPTIMAL. */
void vc_readback_record_copy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                             VkCommandBuffer cmd_buf);

/* After the frame's fence signals, convert RGBA8 staging data to RGB565
   and write into voodoo->fb_mem.  Called from GPU thread after fence wait. */
void vc_readback_copy_to_sw_fb(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

#endif /* VIDEOCOMMON_READBACK_H */
