/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon GPU thread state -- private struct that holds all
 *          render resources owned by the GPU thread.  Not visible to the
 *          FIFO thread.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_GPU_STATE_H
#define VIDEOCOMMON_GPU_STATE_H

#include "vc_internal.h"
#include "vc_pipeline.h" /* vc_pipeline_t, vc_vertex_t, vc_push_constants_t */
#include "vc_shader.h"   /* vc_shaders_t */

/* All GPU-thread-local rendering state.  Allocated at thread init,
   freed at thread cleanup.  Accessed ONLY by the GPU thread. */
typedef struct vc_gpu_state_t {
    vc_render_pass_state_t rp;
    vc_batch_state_t       batch;
    vc_shaders_t           shaders;
    vc_pipeline_t          pipe;

    /* Per-frame resources (triple-buffered). */
    vc_frame_t frame[VC_NUM_FRAMES];
    uint32_t   frame_index;

    /* Render state tracking. */
    int      render_pass_active;
    uint32_t fb_width;
    uint32_t fb_height;
} vc_gpu_state_t;

#endif /* VIDEOCOMMON_GPU_STATE_H */
