# Plan: swap_count Ownership Redesign

**Date**: 2026-03-01
**Branch**: videocommon-voodoo
**Status**: PENDING (awaiting doc cleanup, then implementation)

## Problem Statement

3DMark99 freezes because `swap_count` (SST_status bits 28-30) never decrements
to 0 in VK mode. The guest polls SST_status, sees swap_count=3 / busy=1, and
stalls forever.

### Root Cause Chain

1. **Ring backpressure**: The FIFO thread pushes ~10 ring entries per triangle.
   During heavy scenes, the VK render thread blocks in `vkWaitForFences`
   (vc_begin_frame), stops draining the ring, and the ring fills up.

2. **FIFO thread blocks**: `vc_thread_push()` spin-waits when the ring is full.
   The FIFO thread cannot process any more CMDFIFO entries — including swap
   commands that follow the triangles.

3. **swap_count-- unreachable**: With the current fix (commit 78e03c3c4),
   `swap_count--` happens on the FIFO thread when it processes the swap command.
   But the FIFO thread is blocked on ring backpressure before reaching it.

4. **FIFO wake bug**: The FIFO thread sleeps on `wake_fifo_thread` and relies on
   the display callback (vid_voodoo_display.c:727) to wake it after swap
   completion. But in VK mode, `swap_pending` is never set, so that wake event
   never fires. The FIFO thread can go to sleep with pending CMDFIFO entries.

### Why Other Emulators Don't Have This Problem

Dolphin, PCSX2, and DuckStation all use a single thread that both decodes FIFO
commands AND performs Vulkan rendering. There is no ring buffer between the
command processor and the renderer, so there is no backpressure deadlock. PCSX2
specifically treats VSync as a lightweight ring command that always fits.

## Design

### Core Change: Move swap_count-- to the Render Thread

`VC_CMD_SWAP` already exists as a ring command. When the render thread processes
it, it should decrement `swap_count` (under `swap_mutex`). The FIFO thread
should NOT touch swap_count at all.

### Detailed Flow

```
Guest writes SST_swapbufferCMD (PCI/CMDFIFO)
  → vid_voodoo.c: swap_count++ (immediate, on CPU thread)
  → CMDFIFO entry queued

FIFO thread processes CMDFIFO swap entry
  → vid_voodoo_reg.c VK path:
    - Call vc_voodoo_swap_buffers() (pushes VC_CMD_SWAP to ring)
    - Do buffer flip (disp_buffer/draw_buffer)
    - Call voodoo_recalc()
    - Set dirty_line, front_offset, frame_count
    - Increment cmd_read
    - Do NOT touch swap_count
    - break

VK render thread processes VC_CMD_SWAP
  → vc_thread.c:
    - End current frame (flush batch, end render pass, vkQueueSubmit)
    - Begin next frame (wait fence, reset cmd buf)
    - Decrement swap_count (under swap_mutex)
    - Wake FIFO thread (thread_set_event on wake_fifo_thread)
```

### Changes Required

#### 1. `src/video/vid_voodoo_reg.c` — Remove swap_count-- from VK path

Current (commit 78e03c3c4):
```c
thread_wait_mutex(voodoo->swap_mutex);
if (voodoo->swap_count > 0)
    voodoo->swap_count--;
thread_release_mutex(voodoo->swap_mutex);
```

Change to: Remove these 4 lines entirely. The FIFO thread no longer touches
swap_count in VK mode.

#### 2. `src/video/videocommon/vc_thread.c` — Decrement swap_count in VC_CMD_SWAP handler

In the `VC_CMD_SWAP` case (around line 658-711), after `vc_end_frame()` /
`vc_begin_frame()`:

```c
/* Decrement swap_count — the render thread is the authority
 * on when a swap is truly complete in VK mode. */
if (thread->voodoo) {
    voodoo_t *voodoo = thread->voodoo;
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;
    thread_release_mutex(voodoo->swap_mutex);

    /* Wake the FIFO thread — it may be sleeping with pending
     * CMDFIFO entries, waiting for swap_count to drain. */
    thread_set_event(voodoo->wake_fifo_thread);
}
```

This requires passing a `voodoo_t *` pointer to the render thread context.
Options:
- Store it in `vc_thread_t` (set during init)
- Store it in `vc_context_t` (already has the bridge)
- Pass it via the VC_CMD_SWAP ring entry payload

#### 3. `src/video/videocommon/vc_thread.c` or `vc_core.h` — Plumbing

Add a `void *emulator_priv` field to `vc_thread_t` or `vc_context_t` that
points back to the Voodoo state. Set it during `vc_voodoo_init()`. The render
thread uses it to access `swap_mutex`, `swap_count`, and `wake_fifo_thread`.

Alternatively, use a callback: `vc_context_t` gets a `swap_complete_cb` function
pointer + `void *cb_priv` that the render thread calls after each VC_CMD_SWAP.
The Voodoo VK bridge registers a callback that does the decrement + wake. This
is cleaner (no Voodoo-specific knowledge in VideoCommon).

#### 4. FIFO thread wake — Already handled

If we wake the FIFO thread from the render thread after swap_count--, the FIFO
thread will resume processing CMDFIFO entries. No additional changes needed to
the FIFO wake logic itself.

### Callback Approach (Recommended)

```c
/* In vc_core.h */
typedef void (*vc_swap_complete_fn)(void *priv);

typedef struct vc_context_t {
    /* ... existing fields ... */
    vc_swap_complete_fn swap_complete_cb;
    void               *swap_complete_priv;
} vc_context_t;

/* In vid_voodoo_vk.c — during init */
static void voodoo_vk_swap_complete(void *priv) {
    voodoo_t *voodoo = (voodoo_t *)priv;
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;
    thread_release_mutex(voodoo->swap_mutex);
    thread_set_event(voodoo->wake_fifo_thread);
}

/* In vc_thread.c — VC_CMD_SWAP handler */
if (thread->ctx->swap_complete_cb)
    thread->ctx->swap_complete_cb(thread->ctx->swap_complete_priv);
```

## Risk Assessment

- **Low risk**: The change is small and targeted (4 files, ~30 lines)
- **Correctness**: swap_count accurately reflects GPU state (decremented when
  the GPU finishes the frame, not when the FIFO thread queues it)
- **Backpressure**: Ring backpressure still exists but no longer causes
  swap_count deadlock. The FIFO thread may still block pushing triangles, but
  the render thread will eventually process VC_CMD_SWAP and wake it.
- **Timing**: swap_count will decrement slightly later than in SW mode (after
  GPU completes the frame vs after display callback). This is fine — the guest
  only needs it to eventually reach 0.

## Validation

1. Run 3DMark99 through all tests without freezing
2. Verify swap_count oscillates between 0-3 (never stuck)
3. Verify no Vulkan validation errors (VC_VALIDATE=1)
4. Check that the FIFO thread is not permanently sleeping (sample process)

## Files Modified

| File | Change |
|------|--------|
| `src/video/vid_voodoo_reg.c` | Remove swap_count-- from VK path |
| `src/video/videocommon/vc_core.h` | Add swap_complete_cb to vc_context_t |
| `src/video/videocommon/vc_core.c` | Add setter for swap_complete_cb |
| `src/video/videocommon/vc_thread.c` | Call swap_complete_cb in VC_CMD_SWAP handler |
| `src/video/vid_voodoo_vk.c` | Register swap complete callback during init |

## Reference Docs

- `videocommon-plan/validation/swap-display-lifecycle-reference.md` — Full lifecycle map
- `videocommon-plan/research/swap-present-lifecycle.md` — Dolphin/PCSX2/DuckStation patterns
- `videocommon-plan/validation/swap-count-stuck-analysis.md` — Original analysis
- `videocommon-plan/validation/fifo-thread-blocking-analysis.md` — Ring backpressure analysis
