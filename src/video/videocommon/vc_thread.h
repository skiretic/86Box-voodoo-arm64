/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon GPU thread and SPSC ring buffer declarations.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_THREAD_H
#define VIDEOCOMMON_THREAD_H

#include "vc_internal.h"

/* Forward declaration (defined in vc_gpu_state.h). */
typedef struct vc_gpu_state_t vc_gpu_state_t;

/* -------------------------------------------------------------------------- */
/*  GPU thread lifecycle                                                       */
/* -------------------------------------------------------------------------- */

int  vc_start_gpu_thread(vc_ctx_t *ctx);
void vc_stop_gpu_thread(vc_ctx_t *ctx);

/* GPU thread main function (passed to thread_create). */
void vc_gpu_thread_func(void *param);

/* End the current frame: flush remaining triangles, end render pass,
   submit command buffer, advance frame index.  No-op if no render pass
   is active.  Called by the GPU thread (and vc_display_tick). */
void vc_gpu_end_frame(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* -------------------------------------------------------------------------- */
/*  SPSC ring buffer                                                           */
/* -------------------------------------------------------------------------- */

/* Initialise/destroy the ring buffer (allocates 8 MB buffer + semaphore). */
int  vc_ring_init(vc_ring_t *ring);
void vc_ring_destroy(vc_ring_t *ring);

/* Push a command into the ring.  Returns pointer to the payload area
   (immediately after the header).  Blocks (with backpressure) if ring
   is full.  `total_size` is the size of the entire command including
   the header; it will be aligned up to VC_RING_ALIGN internally. */
void *vc_ring_push(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size);

/* Same as vc_ring_push but also wakes the GPU thread. */
void *vc_ring_push_and_wake(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size);

/* Wake the GPU thread (DuckStation-style: atomic counter + semaphore). */
void vc_ring_wake(vc_ring_t *ring);

/* Sleep until woken.  Returns true always; if work was already pending
   (wake_counter > 0) the call returns without blocking.  Called by the
   GPU thread when the ring is empty. */
bool vc_ring_sleep(vc_ring_t *ring);

/* Spin-yield until at least `needed` bytes are free in the ring.
   Wakes the GPU thread on each iteration to ensure forward progress. */
void vc_ring_wait_for_space(vc_ring_t *ring, uint32_t needed);

#endif /* VIDEOCOMMON_THREAD_H */
