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
 *          Phase 1 stubs: only VC_CMD_SHUTDOWN and VC_CMD_WRAPAROUND
 *          are handled.  The ring push/wake/sleep functions are minimal
 *          stubs sufficient for the shutdown round-trip.  vc-plumbing
 *          will replace these with full DuckStation-style atomic
 *          acquire/release, wake counter, and backpressure.
 *
 * Authors: Anthony Campbell
 *
 *          Copyright 2026 Anthony Campbell.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/thread.h>

#include "vc_thread.h"

/* -------------------------------------------------------------------------- */
/*  SPSC ring buffer -- Phase 1 stubs                                          */
/* -------------------------------------------------------------------------- */

int
vc_ring_init(vc_ring_t *ring)
{
    memset(ring, 0, sizeof(vc_ring_t));

    ring->buffer = (uint8_t *) malloc(VC_RING_SIZE);
    if (!ring->buffer)
        return -1;

    memset(ring->buffer, 0, VC_RING_SIZE);
    atomic_store(&ring->write_pos, 0);
    atomic_store(&ring->read_pos, 0);
    atomic_store(&ring->wake_counter, 0);

    ring->wake_event = thread_create_event();
    if (!ring->wake_event) {
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

    if (ring->wake_event) {
        thread_destroy_event((event_t *) ring->wake_event);
        ring->wake_event = NULL;
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

void *
vc_ring_push(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size)
{
    total_size = vc_ring_align(total_size);

    uint32_t wp = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);

    /* Check if we need a wraparound sentinel. */
    if (wp + total_size > VC_RING_SIZE) {
        /* Write a wraparound sentinel at the current position. */
        vc_ring_cmd_header_t *wrap = (vc_ring_cmd_header_t *) &ring->buffer[wp];
        wrap->type     = VC_CMD_WRAPAROUND;
        wrap->size     = (uint16_t) (VC_RING_SIZE - wp);
        wrap->reserved = 0;

        /* Advance write_pos to 0. */
        wp = 0;
        atomic_store_explicit(&ring->write_pos, 0, memory_order_release);
    }

    /* Phase 1 stub: no backpressure check (ring is 8 MB, startup/shutdown
       only pushes a few bytes). */
    vc_ring_cmd_header_t *hdr = (vc_ring_cmd_header_t *) &ring->buffer[wp];
    hdr->type     = cmd_type;
    hdr->size     = total_size;
    hdr->reserved = 0;

    /* Advance write_pos past this command. */
    uint32_t new_wp = (wp + total_size) & VC_RING_MASK;
    atomic_store_explicit(&ring->write_pos, new_wp, memory_order_release);

    /* Return pointer to payload (after header). */
    return (void *) (hdr + 1);
}

void *
vc_ring_push_and_wake(vc_ring_t *ring, uint16_t cmd_type, uint16_t total_size)
{
    void *payload = vc_ring_push(ring, cmd_type, total_size);
    vc_ring_wake(ring);
    return payload;
}

void
vc_ring_wake(vc_ring_t *ring)
{
    /* Increment wake counter and signal the event. */
    atomic_fetch_add_explicit(&ring->wake_counter, 1, memory_order_release);
    thread_set_event((event_t *) ring->wake_event);
}

void
vc_ring_sleep(vc_ring_t *ring)
{
    /* Phase 1 stub: simple event wait with 100ms timeout to allow
       periodic polling.  The full DuckStation pattern will replace this. */
    thread_wait_event((event_t *) ring->wake_event, 100);
    thread_reset_event((event_t *) ring->wake_event);
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

    atomic_store_explicit(&ctx->running, 1, memory_order_release);

    while (atomic_load_explicit(&ctx->running, memory_order_acquire)) {
        uint32_t rp = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
        uint32_t wp = atomic_load_explicit(&ring->write_pos, memory_order_acquire);

        if (rp == wp) {
            /* Ring is empty -- sleep until woken. */
            vc_ring_sleep(ring);
            continue;
        }

        /* Read the command header at the current read position. */
        vc_ring_cmd_header_t *hdr = (vc_ring_cmd_header_t *) &ring->buffer[rp];

        switch (hdr->type) {
            case VC_CMD_SHUTDOWN:
                VC_LOG("VideoCommon: GPU thread received SHUTDOWN\n");
                atomic_store_explicit(&ctx->running, 0, memory_order_release);
                break;

            case VC_CMD_WRAPAROUND:
                /* Jump read position to start of buffer. */
                atomic_store_explicit(&ring->read_pos, 0, memory_order_release);
                continue; /* Re-check without advancing. */

            default:
                /* Phase 1: skip unknown commands by advancing past them. */
                VC_LOG("VideoCommon: GPU thread skipping unknown cmd %d (size %d)\n",
                       hdr->type, hdr->size);
                break;
        }

        /* Advance read position. */
        if (hdr->type != VC_CMD_WRAPAROUND) {
            uint16_t aligned_size = vc_ring_align(hdr->size);
            uint32_t new_rp       = (rp + aligned_size) & VC_RING_MASK;
            atomic_store_explicit(&ring->read_pos, new_rp, memory_order_release);
        }
    }

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

    /* Push shutdown command and wake the GPU thread. */
    vc_ring_push_and_wake(&ctx->ring, VC_CMD_SHUTDOWN,
                          vc_ring_align(sizeof(vc_ring_cmd_header_t)));

    /* Wait for the GPU thread to exit. */
    thread_wait((thread_t *) ctx->gpu_thread);
    ctx->gpu_thread = NULL;

    vc_ring_destroy(&ctx->ring);

    VC_LOG("VideoCommon: GPU thread stopped and ring destroyed\n");
}
