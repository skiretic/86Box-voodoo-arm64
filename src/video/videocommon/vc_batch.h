/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon batch -- vertex buffer management and triangle
 *          batching for the GPU thread.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_BATCH_H
#define VIDEOCOMMON_BATCH_H

#include "vc_internal.h"
#include "vc_pipeline.h" /* For vc_vertex_t, vc_push_constants_t. */

/* Forward declaration -- full definition in vc_gpu_state.h. */
typedef struct vc_gpu_state_t vc_gpu_state_t;

/* -------------------------------------------------------------------------- */
/*  Ring command payload for VC_CMD_TRIANGLE.                                  */
/*  Layout: [push_constants (64)] [verts[3] (216)]  = 280 bytes payload.      */
/*  Total with 8-byte header: 288 bytes.                                       */
/* -------------------------------------------------------------------------- */

#define VC_CMD_TRIANGLE_SIZE ((uint16_t)(sizeof(vc_ring_cmd_header_t) \
                            + sizeof(vc_push_constants_t)             \
                            + 3 * sizeof(vc_vertex_t)))

/* Maximum triangles per batch flush (bounded by vertex buffer size). */
#define VC_BATCH_MAX_TRIANGLES 4096

/* Create the vertex buffer (host-visible, coherent, persistently mapped). */
int  vc_batch_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Destroy the vertex buffer. */
void vc_batch_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Append 3 vertices to the batch.  Returns 0 on success, -1 if full. */
int vc_batch_append_triangle(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                             const vc_vertex_t verts[3]);

/* Reset batch counters (after flush or at frame start). */
void vc_batch_reset(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

#endif /* VIDEOCOMMON_BATCH_H */
