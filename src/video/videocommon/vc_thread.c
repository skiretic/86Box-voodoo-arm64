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
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/thread.h>

#include "vc_thread.h"

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
    /*
     * Increment wake_counter by 2.  If the old value was negative, the GPU
     * thread is sleeping on the semaphore -- post to wake it.
     *
     * Why +2?  The sleep function decrements by 1.  If the GPU thread
     * decremented from 0 to -1 (sleeping) and we add 1, the counter goes
     * to 0 which is ambiguous.  Adding 2 ensures the counter goes positive
     * (>=1), clearly indicating pending work.
     */
    int32_t old = atomic_fetch_add_explicit(&ring->wake_counter, 2,
                                            memory_order_release);
    if (old < 0)
        vc_sem_post(ring->wake_sem);
}

bool
vc_ring_sleep(vc_ring_t *ring)
{
    /*
     * Decrement wake_counter by 1.  If the old value was > 0, there is
     * pending work -- return immediately without blocking.  If old was 0,
     * the counter transitions to -1 (sleeping state) and we block on the
     * semaphore until a wake posts it.
     */
    int32_t old = atomic_fetch_sub_explicit(&ring->wake_counter, 1,
                                            memory_order_acq_rel);
    if (old > 0)
        return true; /* More work pending, don't actually sleep. */

    /* old == 0 or old == 1 after a racing wake: sleep on semaphore. */
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

    /*
     * We may need up to total_size + VC_RING_ALIGN bytes of contiguous
     * space (the command itself, plus a potential wraparound sentinel).
     * Reserve enough total space for the worst case.
     */
    uint32_t needed = total_size + (uint32_t) sizeof(vc_ring_cmd_header_t);
    if (needed < (uint32_t) total_size + VC_RING_ALIGN)
        needed = (uint32_t) total_size + VC_RING_ALIGN;

    /* Backpressure: block until enough space is available. */
    vc_ring_wait_for_space(ring, needed);

    uint32_t wp = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);

    /* Check if we need a wraparound sentinel. */
    if (wp + total_size > VC_RING_SIZE) {
        /* Write a wraparound sentinel at the current position.
           The sentinel's size covers the remainder of the buffer so the
           consumer skips to position 0. */
        vc_ring_cmd_header_t *wrap = (vc_ring_cmd_header_t *) &ring->buffer[wp];
        wrap->type     = VC_CMD_WRAPAROUND;
        wrap->size     = (uint16_t) (VC_RING_SIZE - wp);
        wrap->reserved = 0;

        /* Publish the sentinel, then wrap write_pos to 0. */
        atomic_store_explicit(&ring->write_pos, 0, memory_order_release);
        wp = 0;

        /*
         * After wrapping, re-check space.  The free space calculation may
         * now see a different geometry (linear from 0 to read_pos).
         */
        while (vc_ring_free_space(ring) < (uint32_t) total_size) {
            vc_ring_wake(ring);
            vc_yield();
        }
    }

    /* Write the command header at write_pos. */
    vc_ring_cmd_header_t *hdr = (vc_ring_cmd_header_t *) &ring->buffer[wp];
    hdr->type     = cmd_type;
    hdr->size     = total_size;
    hdr->reserved = 0;

    /* Advance write_pos past this command.  Since we checked space and the
       ring size is a power of two, the new position is always < VC_RING_SIZE
       when we have no wraparound.  Use mask for safety. */
    uint32_t new_wp = (wp + total_size) & VC_RING_MASK;
    atomic_store_explicit(&ring->write_pos, new_wp, memory_order_release);

    /* Return pointer to payload (immediately after header). */
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
            /* Ring is empty -- sleep until woken by producer. */
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
                continue; /* Re-check without advancing past command. */

            default:
                /* Future commands will be handled here by later phases. */
                VC_LOG("VideoCommon: GPU thread skipping cmd %d (size %d)\n",
                       hdr->type, hdr->size);
                break;
        }

        /* Advance read position past this command. */
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
