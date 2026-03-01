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

/* -------------------------------------------------------------------------- */
/*  GPU thread lifecycle                                                       */
/* -------------------------------------------------------------------------- */

int  vc_start_gpu_thread(vc_ctx_t *ctx);
void vc_stop_gpu_thread(vc_ctx_t *ctx);

/* GPU thread main function (passed to thread_create). */
void vc_gpu_thread_func(void *param);

/* -------------------------------------------------------------------------- */
/*  SPSC ring buffer                                                           */
/* -------------------------------------------------------------------------- */

/* Initialise/destroy the ring buffer (allocates 8 MB buffer + event). */
int  vc_ring_init(vc_ring_t *ring);
void vc_ring_destroy(vc_ring_t *ring);

/* Push a command into the ring.  Returns pointer to the payload area
   (immediately after the header) or NULL if the ring is full.
   `total_size` is the size of the entire command including the header,
   and must be a multiple of VC_RING_ALIGN. */
void *vc_ring_push(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size);

/* Same as vc_ring_push but also wakes the GPU thread. */
void *vc_ring_push_and_wake(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size);

/* Wake the GPU thread unconditionally. */
void vc_ring_wake(vc_ring_t *ring);

/* Sleep until woken (called by GPU thread when ring is empty). */
void vc_ring_sleep(vc_ring_t *ring);

#endif /* VIDEOCOMMON_THREAD_H */
