/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon render pass -- offscreen framebuffer management,
 *          render pass creation, image layout transitions.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_RENDER_PASS_H
#define VIDEOCOMMON_RENDER_PASS_H

#include "vc_internal.h"

/* Forward declaration -- full definition in vc_gpu_state.h. */
typedef struct vc_gpu_state_t vc_gpu_state_t;

/* Create both render passes (clear + load). */
int vc_render_pass_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Destroy both render passes. */
void vc_render_pass_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Create or recreate double-buffered framebuffers at the given resolution. */
int vc_render_pass_create_framebuffers(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                                       uint32_t width, uint32_t height);

/* Destroy both framebuffers. */
void vc_render_pass_destroy_framebuffers(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Swap front and back framebuffer indices. */
void vc_render_pass_swap(vc_gpu_state_t *gpu_st);

#endif /* VIDEOCOMMON_RENDER_PASS_H */
