# Swap/Display Lifecycle Reference

Definitive reference for every code path involved in the swap buffer and display lifecycle, for both SW mode and VK mode. All line numbers verified against the current codebase (branch `videocommon-voodoo`, 2026-03-01).

---

## Table of Contents

1. [SW Mode Swap Lifecycle](#sw-mode-swap-lifecycle)
2. [VK Mode Swap Lifecycle](#vk-mode-swap-lifecycle)
3. [Critical State Variables](#critical-state-variables)
4. [The FIFO Thread](#the-fifo-thread)
5. [The Display Callback](#the-display-callback)
6. [The VK Render Thread](#the-vk-render-thread)
7. [Key Questions Answered](#key-questions-answered)

---

## SW Mode Swap Lifecycle

### 1. swap_count is incremented (PCI write path)

**File**: `src/video/vid_voodoo.c`
**Function**: `voodoo_writel()`
**Line**: 703-706

```c
case SST_swapbufferCMD:
    voodoo->cmd_written++;
    thread_wait_mutex(voodoo->swap_mutex);
    voodoo->swap_count++;
    thread_release_mutex(voodoo->swap_mutex);
```

This runs on the **CPU emulation thread** when the guest writes to register 0x114 (SST_swapbufferCMD). The increment happens IMMEDIATELY on write -- before the command is queued to the FIFO thread.

For CMDFIFO-enabled cards (Voodoo 2), the function then returns early (`if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE) return;`) because the CMDFIFO data was already written to VRAM at line 680 (`voodoo->cmdfifo_depth_wr++`). The swap command will be processed later by the FIFO thread when it reads the CMDFIFO.

For non-CMDFIFO cards, it queues via `voodoo_queue_command()` at line 708.

### 2. swap_pending is set to 1

**File**: `src/video/vid_voodoo_reg.c`
**Function**: `voodoo_reg_writel()`
**Lines**: 157, 161, 246, 250

The FIFO thread calls `voodoo_reg_writel()` (via FIFO dispatch or CMDFIFO processing). The swap_pending logic depends on the swap mode:

```c
// Line 140: case SST_swapbufferCMD: (for Banshee+ path, then pre-V2 path below)

// NON-VSYNC swap (val bit 0 == 0):
// Line 144-149: Immediate swap, no swap_pending set
if (!(val & 1)) {
    memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
    voodoo->front_offset = voodoo->params.front_offset;
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;
    thread_release_mutex(voodoo->swap_mutex);
}

// TRIPLE_BUFFER vsync swap:
// Lines 153-158:
else if (TRIPLE_BUFFER) {
    if (voodoo->swap_pending)
        voodoo_wait_for_swap_complete(voodoo);  // blocks FIFO thread!
    voodoo->swap_interval = (val >> 1) & 0xff;
    voodoo->swap_offset   = voodoo->params.front_offset;
    voodoo->swap_pending  = 1;
}

// DOUBLE_BUFFER vsync swap:
// Lines 159-165:
else {
    voodoo->swap_interval = (val >> 1) & 0xff;
    voodoo->swap_offset   = voodoo->params.front_offset;
    voodoo->swap_pending  = 1;
    voodoo_wait_for_swap_complete(voodoo);  // blocks FIFO thread!
}
```

For the SW path (non-VK, non-Banshee), swap_pending is set at lines 246 and 250 (same pattern, different code block starting at line 223).

### 3. Display callback decrements swap_count

**File**: `src/video/vid_voodoo_display.c`
**Function**: `voodoo_callback()`
**Lines**: 696-697 (SLI path), 720-721 (normal path)

The display callback runs per-scanline on the timer thread. At vblank (line == v_disp), it checks swap_pending:

```c
// Line 675-676: vblank detection
if (voodoo->line == voodoo->v_disp) {
    voodoo->retrace_count++;

    // Normal (non-SLI) path, lines 716-729:
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval)) {
        voodoo->front_offset = voodoo->swap_offset;
        if (voodoo->swap_count > 0)
            voodoo->swap_count--;           // LINE 720-721
        voodoo->swap_pending = 0;
        thread_release_mutex(voodoo->swap_mutex);

        memset(voodoo->dirty_line, 1, 1024);
        voodoo->retrace_count = 0;
        thread_set_event(voodoo->wake_fifo_thread);  // Wake FIFO thread!
        voodoo->frame_count++;
    } else
        thread_release_mutex(voodoo->swap_mutex);

    // SLI path, lines 686-710:
    // Similar but waits for BOTH voodoos to have swap_pending
    // Decrements swap_count for both at lines 696-697 and 703-704
}
```

**Key condition**: `swap_pending && (retrace_count > swap_interval)`. If swap_pending is never set to 1, swap_count is NEVER decremented here.

### 4. voodoo_wait_for_swap_complete drains swap_count

**File**: `src/video/vid_voodoo_fifo.c`
**Function**: `voodoo_wait_for_swap_complete()`
**Lines**: 266-291

```c
void voodoo_wait_for_swap_complete(voodoo_t *voodoo) {
    while (voodoo->swap_pending) {
        thread_wait_mutex(voodoo->swap_mutex);
        if ((voodoo->swap_pending && voodoo->flush) || FIFO_FULL) {
            // Emergency drain: skip vsync wait, immediate swap
            memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
            voodoo->front_offset = voodoo->params.front_offset;
            if (voodoo->swap_count > 0)
                voodoo->swap_count--;       // LINE 277-278
            voodoo->swap_pending = 0;
            thread_release_mutex(voodoo->swap_mutex);
            break;
        } else
            thread_release_mutex(voodoo->swap_mutex);
        plat_delay_ms(1);  // poll every 1ms
    }
}
```

This runs on the **FIFO thread**. It spin-waits until the display callback clears swap_pending, OR does an emergency drain if the FIFO is full or flushing.

### 5. swap_pending is cleared

There are exactly 3 places:

| Location | File | Line | Condition |
|----------|------|------|-----------|
| Display callback (normal) | `vid_voodoo_display.c` | 723 | `swap_pending && retrace_count > swap_interval` |
| Display callback (SLI) | `vid_voodoo_display.c` | 698, 706 | Both cards pending + retrace > interval |
| wait_for_swap_complete (emergency) | `vid_voodoo_fifo.c` | 280 | FIFO_FULL or flush active |

### 6. disp_buffer / draw_buffer are flipped

**File**: `src/video/vid_voodoo_reg.c`
**Function**: `voodoo_reg_writel()`
**Lines**: 223-228

```c
// Before the swap_pending logic:
if (TRIPLE_BUFFER) {
    voodoo->disp_buffer = (voodoo->disp_buffer + 1) % 3;
    voodoo->draw_buffer = (voodoo->draw_buffer + 1) % 3;
} else {
    voodoo->disp_buffer = !voodoo->disp_buffer;
    voodoo->draw_buffer = !voodoo->draw_buffer;
}
voodoo_recalc(voodoo);
```

The buffer flip happens on the **FIFO thread** IMMEDIATELY when the swap command is processed, BEFORE swap_pending is set. This is important: the draw_buffer changes right away so subsequent triangles go to the new back buffer.

### 7. front_offset is updated

front_offset is updated in 3 places:

| Location | File | Line | When |
|----------|------|------|------|
| Display callback (normal) | `vid_voodoo_display.c` | 719 | At vblank when swap completes |
| Display callback (SLI) | `vid_voodoo_display.c` | 695, 702 | At vblank when both cards ready |
| Non-vsync swap | `vid_voodoo_reg.c` | 235 | Immediately (no vsync wait) |
| wait_for_swap_complete (emergency) | `vid_voodoo_fifo.c` | 276 | FIFO full emergency |
| fbiInit0 graphics reset | `vid_voodoo.c` | 798 | Hardware reset |

In the normal vsync case, front_offset is NOT updated until the display callback fires at the next vblank. This is how vsync works: the front buffer keeps displaying the old frame until the retrace.

### 8. voodoo_recalc is called during swap

**File**: `src/video/vid_voodoo.c`
**Function**: `voodoo_recalc()`
**Lines**: 139-210

Called from `voodoo_reg_writel()` at line 230 (right after buffer flip). Recalculates:
- `params.front_offset` = `disp_buffer * buffer_offset` (line 146)
- `back_offset` = `draw_buffer * buffer_offset` (line 147)
- `params.aux_offset` (depth buffer offset)
- `fb_write_offset`, `fb_read_offset`, `params.draw_offset` (based on lfbMode/fbzMode)

### 9. FIFO thread wake/sleep mechanism

The FIFO thread sleeps in two places:

**Sleep point 1 -- Main loop idle** (line 387):
```c
while (voodoo->fifo_thread_run) {
    thread_set_event(voodoo->fifo_not_full_event);
    thread_wait_event(voodoo->wake_fifo_thread, -1);  // SLEEP HERE
    thread_reset_event(voodoo->wake_fifo_thread);
    voodoo->voodoo_busy = 1;
    // ... process FIFO ring entries ...
    // ... process CMDFIFO entries ...
    voodoo->voodoo_busy = 0;
    // loops back to sleep
}
```

**Sleep point 2 -- CMDFIFO data wait** (line 299-302):
```c
static uint32_t cmdfifo_get(voodoo_t *voodoo) {
    if (!voodoo->cmdfifo_in_sub) {
        while (voodoo->fifo_thread_run
               && (voodoo->cmdfifo_depth_rd == voodoo->cmdfifo_depth_wr)) {
            thread_wait_event(voodoo->wake_fifo_thread, -1);  // SLEEP HERE
            thread_reset_event(voodoo->wake_fifo_thread);
        }
    }
    // read from fb_mem[cmdfifo_rp]
}
```

**Wake triggers** (what sets `wake_fifo_thread` event):

| Trigger | File | Line | When |
|---------|------|------|------|
| `voodoo_wake_timer()` | `vid_voodoo_fifo.c` | 173-178 | Timer callback fires (delayed wake) |
| `voodoo_wake_fifo_thread_now()` | `vid_voodoo_fifo.c` | 167-170 | Immediate wake |
| `voodoo_wake_fifo_thread()` | `vid_voodoo_fifo.c` | 155-164 | Arms a timer to fire `voodoo_wake_timer` |
| Display callback swap complete | `vid_voodoo_display.c` | 727 | `thread_set_event(voodoo->wake_fifo_thread)` |
| Display callback SLI swap | `vid_voodoo_display.c` | 709-710 | Both cards woken |
| SST_status read (if !busy) | `vid_voodoo.c` | 497-498 | Guest polls status register |
| PCI write command dispatch | `vid_voodoo.c` | 710+ | `voodoo_wake_fifo_threads()` |
| CMDFIFO depth write | `vid_voodoo.c` | 684/689 | When depth < 20 |
| `voodoo_flush()` | `vid_voodoo_fifo.c` | 251 | `voodoo_wake_fifo_thread_now()` in loop |

The **delayed wake** mechanism is important: `voodoo_wake_fifo_thread()` does NOT immediately set the event. It arms a one-shot timer (`wake_timer`) that fires after WAKE_DELAY. This batches multiple CMDFIFO writes into one wake, reducing context-switch overhead. The timer callback (`voodoo_wake_timer`, line 173) then sets the actual event.

### 10. voodoo_busy cleared to 0

**File**: `src/video/vid_voodoo_fifo.c`
**Line**: 1065

```c
// At the end of the main while(fifo_thread_run) loop body:
voodoo->voodoo_busy = 0;
```

This is set to 0 after ALL three processing loops complete:
1. FIFO ring entries (lines 390-479)
2. CMDFIFO entries (lines 483-1060)
3. CMDFIFO_2 entries (lines 774-1063)

It is set to 1 at line 390, BEFORE entering the FIFO ring processing loop. So `voodoo_busy` is 1 for the entire duration the FIFO thread is processing work.

---

## VK Mode Swap Lifecycle

### 1. swap_count++ in vid_voodoo.c (PCI/CMDFIFO write)

**IDENTICAL to SW mode.** Same code path at line 703-706.

The CPU thread writes to SST_swapbufferCMD -> `voodoo_writel()` increments `swap_count` immediately, then for CMDFIFO cards writes the data to VRAM and increments `cmdfifo_depth_wr` at line 680.

### 2. VK path: immediate swap_count-- and buffer flip

**File**: `src/video/vid_voodoo_reg.c`
**Function**: `voodoo_reg_writel()`
**Lines**: 178-212

When the FIFO thread processes the swap command, the VK path is taken:

```c
#ifdef USE_VIDEOCOMMON
    // Line 170-178: Deferred VK init on first swap (if not yet initialized)
    if (voodoo->use_gpu_renderer
        && !atomic_load_explicit(&voodoo->vc_ctx, memory_order_acquire)
        && !atomic_load_explicit(&voodoo->vc_init_pending, memory_order_acquire)) {
        atomic_store_explicit(&voodoo->vc_init_pending, 1, memory_order_release);
        voodoo->vc_init_thread = thread_create(vc_deferred_init_thread, voodoo);
    }

    // Line 192-212: VK swap path
    if (voodoo->use_gpu_renderer
        && atomic_load_explicit(&voodoo->vc_ctx, memory_order_acquire)) {
        vc_voodoo_swap_buffers(voodoo);          // Push VC_CMD_SWAP to ring

        thread_wait_mutex(voodoo->swap_mutex);
        if (voodoo->swap_count > 0)
            voodoo->swap_count--;                 // IMMEDIATE decrement
        thread_release_mutex(voodoo->swap_mutex);

        if (TRIPLE_BUFFER) {
            voodoo->disp_buffer = (voodoo->disp_buffer + 1) % 3;
            voodoo->draw_buffer = (voodoo->draw_buffer + 1) % 3;
        } else {
            voodoo->disp_buffer = !voodoo->disp_buffer;
            voodoo->draw_buffer = !voodoo->draw_buffer;
        }
        voodoo_recalc(voodoo);
        voodoo->params.swapbufferCMD = val;
        memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
        voodoo->front_offset = voodoo->params.front_offset;
        voodoo->frame_count++;
        voodoo->cmd_read++;
        break;
    }
#endif
```

**Key design**: In VK mode, `swap_count--` happens immediately on the FIFO thread, NOT via swap_pending + display callback. The comment at lines 183-191 explains why: the FIFO thread may be delayed by SPSC ring back-pressure (vc_thread_push blocks when the ring is full). If swap_pending were set but never cleared (because the display callback timing is off), swap_count would never decrement.

**swap_pending is NEVER set** in the VK path. This completely bypasses the display callback's swap_count decrement logic.

### 3. vc_voodoo_swap_buffers (vid_voodoo_vk.c)

**File**: `src/video/vid_voodoo_vk.c`
**Function**: `vc_voodoo_swap_buffers()`
**Lines**: 968-1017

```c
void vc_voodoo_swap_buffers(voodoo_t *voodoo) {
    vc_context_t *ctx = (vc_context_t *) atomic_load_explicit(&voodoo->vc_ctx, ...);
    if (ctx) {
        // Flush pending LFB writes
        if (vc_lfb_write_pending(ctx))
            vc_lfb_write_flush(ctx);

        vc_swap_buffers(ctx);       // Push VC_CMD_SWAP to SPSC ring

        // Mark readback tiles dirty
        vc_readback_mark_all_tiles_dirty(ctx);

        // Reset bind tracking (texture, blend, depth, push constants)
        vk_last_tmu0_slot  = -2;
        vk_last_tmu1_slot  = -2;
        vk_last_texmode[0] = 0xFFFFFFFF;
        vk_last_texmode[1] = 0xFFFFFFFF;
        vk_last_alpha_mode = 0xFFFFFFFF;
        vk_last_depth_bits = 0xFFFFFFFF;
        vk_pc_valid = 0;
    }
}
```

`vc_swap_buffers()` (in `vc_core.c`, lines 757-768) simply pushes a `VC_CMD_SWAP` command to the SPSC ring. This is a non-blocking push (unless the ring is full, in which case it blocks the FIFO thread).

### 4. VC_CMD_SWAP processed on render thread

**File**: `src/video/videocommon/vc_thread.c`
**Function**: `vc_dispatch_command()` -> `vc_end_frame()` + `vc_begin_frame()`
**Lines**: 658-711

```c
case VC_CMD_SWAP:
    thread->diag_swap_count++;
    // ... diagnostic logging ...
    vc_end_frame(thread);          // Flush batch, end render pass, submit, swap FBs
    // ... async readback ...
    vc_begin_frame(thread);        // Wait for fence, reset command buffer, reset descriptors
    // ... reset tracking ...
    vc_present_dispatch(thread->ctx);  // Service GUI present if pending
    break;
```

**vc_end_frame()** (lines 303-340):
1. Flushes pending vertex batch (`vc_batch_flush`)
2. Ends render pass (`vc_end_render_pass`)
3. Ends command buffer (`vkEndCommandBuffer`)
4. Submits to GPU (`vkQueueSubmit` with fence)
5. Sets `fr->in_flight = 1`
6. Sets `frames_completed = 1` (atomic)
7. Swaps framebuffers (`vc_framebuffer_swap`)
8. Advances `current_frame` to next slot

**vc_begin_frame()** (lines 269-301):
1. Waits for frame fence if in_flight (`vkWaitForFences` -- **can block!**)
2. Resets fence
3. Resets and begins command buffer
4. Resets vertex batch
5. Resets descriptor pool

### 5. Present path (GUI thread submit + present)

The display pipeline for VK mode has two paths:

**Path A: CPU readback** (legacy, always active as fallback)
- `voodoo_callback()` (timer thread, per-scanline) reads `vc_readback_pixels()` to get the rendered framebuffer
- Converts RGBA8 -> XRGB8888, writes to `monitor->target_buffer`
- At vblank, `svga_doblit()` triggers the blit chain -> `VCRenderer::onBlit()`

**Path B: Zero-copy Vulkan present** (when VCRenderer active)
- `VCRenderer::onBlit()` on the GUI thread calls `present()`
- `present()` acquires swapchain image, records blit command buffer (front VkImage -> swapchain image)
- Calls `vc_present_submit()` (non-blocking, posts atomic request)
- Render thread picks up via `vc_present_dispatch()` -- does `vkQueueSubmit` + `vkQueuePresentKHR`
- Result communicated back via atomic state + VkFence

**Present channel state machine**:
```
GUI thread: vc_present_submit()
    -> atomic_store(&pending, VC_PRESENT_PENDING)
    -> thread_set_event(wake_event)   // wake render thread

Render thread: vc_present_dispatch()
    -> atomic_load(&pending) == PENDING?
    -> vkQueueSubmit(cmd_buf, fence)
    -> vkQueuePresentKHR(swapchain, image)
    -> atomic_store(&pending, VC_PRESENT_IDLE)
    -> thread_set_event(drain_event)  // unblock any waiters
```

---

## Critical State Variables

### swap_count

**Type**: `int` (in `voodoo_t`)
**Protection**: `swap_mutex` (mutex taken for read-modify-write)

| Operation | File | Line | Thread | Mutex |
|-----------|------|------|--------|-------|
| ++ (PCI write) | `vid_voodoo.c` | 705 | CPU | yes |
| -- (non-vsync immediate, SW) | `vid_voodoo_reg.c` | 237-238 | FIFO | yes |
| -- (VK path immediate) | `vid_voodoo_reg.c` | 196-197 | FIFO | yes |
| -- (Banshee non-vsync) | `vid_voodoo_reg.c` | 148-149 | FIFO | yes |
| -- (display callback normal) | `vid_voodoo_display.c` | 720-721 | Timer | yes |
| -- (display callback SLI) | `vid_voodoo_display.c` | 696-697, 703-704 | Timer | yes |
| -- (wait_for_swap emergency) | `vid_voodoo_fifo.c` | 277-278 | FIFO | yes |
| = 0 (fbiInit1 video reset) | `vid_voodoo.c` | 805 | CPU | yes |
| Read (SST_status) | `vid_voodoo.c` | 439 | CPU | **NO** |

**IMPORTANT**: The SST_status read at line 439 reads `swap_count` WITHOUT holding the mutex. This is a data race, but it's benign because:
- The read is a simple int load (atomic on all relevant platforms)
- A stale value just means the guest polls one more time

**Guest visibility**: Bits 28-30 of SST_status register. Capped at 7 (`if (swap_count < 7) temp |= (swap_count << 28); else temp |= (7 << 28);`).

### swap_pending

**Type**: `int` (in `voodoo_t`)
**Protection**: `swap_mutex` for some accesses, but NOT all

| Operation | File | Line | Thread | Mutex |
|-----------|------|------|--------|-------|
| = 1 (SW double-buf) | `vid_voodoo_reg.c` | 250 | FIFO | no |
| = 1 (SW triple-buf) | `vid_voodoo_reg.c` | 246 | FIFO | no |
| = 1 (Banshee) | `vid_voodoo_reg.c` | 157, 161 | FIFO | no |
| = 0 (display callback) | `vid_voodoo_display.c` | 723 | Timer | yes |
| = 0 (display callback SLI) | `vid_voodoo_display.c` | 698, 706 | Timer | yes |
| = 0 (wait_for_swap emergency) | `vid_voodoo_fifo.c` | 280 | FIFO | yes |
| Read (wait_for_swap loop) | `vid_voodoo_fifo.c` | 269 | FIFO | no (loop cond) |
| Read (display callback) | `vid_voodoo_display.c` | 718 | Timer | yes |

**In VK mode**: swap_pending is NEVER set. The VK path at `vid_voodoo_reg.c` line 192-212 does immediate swap_count-- and skips the swap_pending mechanism entirely.

### voodoo_busy

**Type**: `int` (in `voodoo_t`)
**Protection**: None (single-writer: FIFO thread)

| Operation | File | Line | Thread |
|-----------|------|------|--------|
| = 1 | `vid_voodoo_fifo.c` | 390 | FIFO |
| = 0 | `vid_voodoo_fifo.c` | 1065 | FIFO |
| Read (SST_status) | `vid_voodoo.c` | 443 | CPU |
| Read (wake decision) | `vid_voodoo.c` | 497, 710+ | CPU |

**Meaning**: 1 = FIFO thread is actively processing commands. 0 = FIFO thread is idle (sleeping). Used in two ways:
1. SST_status `busy` bit (bit 9, combined with other sources)
2. Wake optimization: if not busy, wake the FIFO thread

**Lifecycle**: Set to 1 immediately after `wake_fifo_thread` event is received. Set to 0 only after ALL queues are drained (FIFO ring + CMDFIFO + CMDFIFO_2).

### retrace_count

**Type**: `int` (in `voodoo_t`)
**Protection**: None (written by timer thread only)

| Operation | File | Line | Thread |
|-----------|------|------|--------|
| ++ | `vid_voodoo_display.c` | 685 | Timer |
| = 0 (on swap complete) | `vid_voodoo_display.c` | 694, 701, 726 | Timer |
| = 0 (fbiInit1 video reset) | `vid_voodoo.c` | 808 | CPU |
| Read (swap check) | `vid_voodoo_display.c` | 692, 718 | Timer |

**Meaning**: Counts vblank intervals since the last swap completed. Compared against `swap_interval` to implement vsync: the swap only completes when `retrace_count > swap_interval`. A swap_interval of 0 means swap at next vblank. A swap_interval of 1 means wait 2 vblanks.

### cmd_written / cmd_read

**cmd_written**: Incremented by the **CPU thread** for PCI register writes that generate GPU commands.
**cmd_written_fifo**: Incremented by the **FIFO thread** for CMDFIFO-decoded commands.
**cmd_written_fifo_2**: Incremented by the **FIFO thread** for CMDFIFO_2-decoded commands.
**cmd_read**: Incremented by the **FIFO thread** when it completes processing a command.

| Variable | File | Lines | Thread |
|----------|------|-------|--------|
| `cmd_written++` | `vid_voodoo.c` | 703, 716, 724, 732, 740 | CPU |
| `cmd_written_fifo++` | `vid_voodoo_fifo.c` | 570, 573, 680, 683 | FIFO |
| `cmd_written_fifo_2++` | `vid_voodoo_fifo.c` | 861, 864, 971, 974 | FIFO |
| `cmd_read++` | `vid_voodoo_reg.c` | 166, 212, 254, 426, 627, 674, 686, 692 | FIFO |

**Guest visibility**: SST_status `busy` bit includes `(written - cmd_read)` where `written = cmd_written + cmd_written_fifo + cmd_written_fifo_2` (line 440-441). When written == cmd_read, no commands are pending.

**Key insight**: cmd_written is incremented by the CPU thread BEFORE the command is queued, so the guest sees "busy" immediately. cmd_read is incremented by the FIFO thread AFTER the command is processed, so busy clears when work is done.

### disp_buffer / draw_buffer

**Type**: `int` (in `voodoo_t`)

Double-buffer: toggles between 0 and 1.
Triple-buffer: cycles through 0, 1, 2.

These directly index into the framebuffer layout:
- `front_offset = disp_buffer * buffer_offset` (display)
- `back_offset = draw_buffer * buffer_offset` (rendering)

Flipped on the **FIFO thread** in `voodoo_reg_writel()` at SST_swapbufferCMD (both SW and VK paths).

### cmdfifo_depth_wr / cmdfifo_depth_rd

**cmdfifo_depth_wr**: Incremented by the **CPU thread** when writing CMDFIFO data.
**cmdfifo_depth_rd**: Incremented by the **FIFO thread** when reading CMDFIFO data.

| Variable | File | Line | Thread |
|----------|------|------|--------|
| `cmdfifo_depth_wr++` | `vid_voodoo.c` | 680 | CPU |
| `cmdfifo_depth_rd++` | `vid_voodoo_fifo.c` | 314 | FIFO |
| Read (SST_cmdFifoDepth) | `vid_voodoo.c` | 628 | CPU |
| Read (busy check) | `vid_voodoo.c` | 442 | CPU |
| Read (cmdfifo_get wait) | `vid_voodoo_fifo.c` | 300 | FIFO |
| Read (FIFO thread loop) | `vid_voodoo_fifo.c` | 483 | FIFO |

**Guest visibility**: SST_cmdFifoDepth register returns `cmdfifo_depth_wr - cmdfifo_depth_rd` (line 628). Also contributes to busy bit in SST_status.

**Protection**: None. Single-writer per variable (CPU for wr, FIFO for rd). Cross-thread reads are plain int loads. On x86 this is safe due to strong memory model. On ARM64 this could theoretically be problematic, but the values are monotonically increasing and a stale read just means one extra poll.

---

## The FIFO Thread

**File**: `src/video/vid_voodoo_fifo.c`
**Function**: `voodoo_fifo_thread()`
**Lines**: 380-1066

### Structure

```
while (fifo_thread_run) {
    set fifo_not_full_event          // notify writers there's room
    SLEEP on wake_fifo_thread        // *** SLEEPS HERE ***
    voodoo_busy = 1

    // Phase 1: Process FIFO ring entries (PCI register writes, FB writes, etc.)
    while (!FIFO_EMPTY) {
        dispatch fifo entry
        if (FIFO_ENTRIES > 0xe000) set fifo_not_full_event
    }

    set fifo_empty_event             // notify flush waiters

    // Phase 2: Process CMDFIFO entries
    while (cmdfifo_enabled && depth_rd != depth_wr) {
        header = cmdfifo_get()       // *** MAY SLEEP IN HERE ***
        dispatch CMDFIFO packet (0-6)
        // Packet type 1/4: iterate registers, call voodoo_reg_writel()
        //   -> SST_swapbufferCMD triggers VK/SW swap path
        // Packet type 3: parse vertices, call voodoo_reg_writel(SST_sDrawTriCMD)
    }

    // Phase 3: Process CMDFIFO_2 entries (same structure as Phase 2)
    while (cmdfifo_enabled_2 && depth_rd_2 != depth_wr_2) { ... }

    voodoo_busy = 0
}
```

### Critical insight: cmdfifo_get() blocking

When the FIFO thread is in Phase 2 (CMDFIFO processing) and reads a partial packet, `cmdfifo_get()` blocks waiting for more data:

```c
while (cmdfifo_depth_rd == cmdfifo_depth_wr) {
    thread_wait_event(wake_fifo_thread, -1);  // BLOCKS
    thread_reset_event(wake_fifo_thread);
}
```

This is the mechanism that caused the original swap_count freeze: if the CPU thread is blocked (e.g., waiting for an LFB read that requires GPU idle), it cannot write CMDFIFO data, so cmdfifo_depth_wr never increments, and the FIFO thread sleeps forever in cmdfifo_get().

---

## The Display Callback

**File**: `src/video/vid_voodoo_display.c`
**Function**: `voodoo_callback()`
**Lines**: 509-767

### Execution context

Runs on the **timer/CPU emulation thread** via the 86Box timer system. Fires once per scanline (configured by `voodoo->line_time`). At the end of each call, `timer_advance_u64()` schedules the next call.

### Per-scanline structure

```
voodoo_callback():
    if (VGA passthrough active) {
        if (line < v_disp) {
            // ACTIVE DISPLAY REGION
            if (VK mode && direct present active) {
                // Just mark dirty lines for blit signal
            } else if (VK mode) {
                // Read from readback buffer, convert RGBA8->XRGB8888
            } else {
                // SW mode: read from fb_mem[], apply CLUT/filter
            }
        }

        // VBLANK
        if (line == v_disp) {
            retrace_count++

            // SWAP LOGIC (see section 3 above)
            if (swap_pending && retrace_count > swap_interval) {
                front_offset = swap_offset
                swap_count--
                swap_pending = 0
                wake FIFO thread
                frame_count++
            }

            v_retrace = 1

            // BLIT
            if (dirty_line_high > dirty_line_low || force_blit)
                svga_doblit()    // triggers blit chain -> VCRenderer::onBlit()
        }
    }

    if (line >= v_total) {
        line = 0
        v_retrace = 0
    }

    line++
    timer_advance_u64()   // schedule next scanline callback
```

### Key timing

- Active display: lines 0 to v_disp-1
- Vblank entry: line == v_disp (retrace_count++ happens here)
- Vblank exit: line >= v_total (line reset to 0)
- `v_retrace`: 1 during vblank, 0 during active display
- Visible in SST_status bit 6: `if (!voodoo->v_retrace) temp |= 0x40;`

---

## The VK Render Thread

**File**: `src/video/videocommon/vc_thread.c`
**Function**: `vc_render_thread_func()`
**Lines**: 812-896

### Structure

```
vc_render_thread_func():
    vc_begin_frame()   // Start first frame

    while (running) {
        batch_count = 0

        // Process ring commands in batches of VC_RING_BATCH_LIMIT (1024)
        while (!ring_empty && batch_count < 1024) {
            cmd = ring_pop()
            if (cmd == CMD_CLOSE) { cleanup; return; }
            dispatch_command(cmd)    // triangle, push constants, swap, etc.
            batch_count++
        }

        // Service present channel after each batch
        vc_present_dispatch(ctx)

        // If no work done, sleep 1ms
        if (!did_work) {
            thread_wait_event(wake_event, 1)
            thread_reset_event(wake_event)
        }
    }
```

### Frame lifecycle on render thread

```
vc_begin_frame():
    vkWaitForFences(fence)           // May block waiting for GPU!
    vkResetFences(fence)
    vkResetCommandBuffer()
    vkBeginCommandBuffer()
    vc_batch_reset()
    vc_texture_reset_descriptors()   // Invalidates ALL descriptor sets

    // ... process triangle commands, push constants, texture binds ...

vc_end_frame():  (triggered by VC_CMD_SWAP)
    vc_batch_flush()                 // Submit remaining vertices
    vc_end_render_pass()
    vkEndCommandBuffer()
    vkQueueSubmit(fence)             // Submit to GPU
    vc_framebuffer_swap()            // Swap front/back VkImages
    current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT

    // Then back to vc_begin_frame() for next frame
```

---

## Key Questions Answered

### After swap_count++, what EXACTLY must happen for the guest to see swap_count==0 and busy==0?

**SW mode**:
1. CPU writes SST_swapbufferCMD -> `swap_count++` (immediate, CPU thread)
2. CMDFIFO data written to VRAM -> `cmdfifo_depth_wr++` (CPU thread)
3. Wake timer fires -> FIFO thread wakes
4. FIFO thread processes CMDFIFO packets until it reaches the swap packet
5. `voodoo_reg_writel(SST_swapbufferCMD)` runs:
   - Flips disp_buffer/draw_buffer
   - Calls `voodoo_recalc()`
   - If vsync: sets `swap_pending = 1`, calls `voodoo_wait_for_swap_complete()` (blocks FIFO thread)
   - If no vsync: immediately decrements swap_count and sets front_offset
6. If vsync: display callback fires at next vblank
   - `retrace_count++`
   - If `retrace_count > swap_interval`: `front_offset = swap_offset`, `swap_count--`, `swap_pending = 0`
   - Wakes FIFO thread (which was blocked in wait_for_swap_complete)
7. FIFO thread continues processing remaining CMDFIFO commands
8. When ALL queues drain: `voodoo_busy = 0`
9. Guest reads SST_status: `busy = (written - cmd_read) | ... | voodoo_busy`, `swap_count` in bits 28-30

**For swap_count to reach 0**: ALL submitted swaps must complete. Each swap either: (a) decrements immediately (non-vsync), or (b) decrements at next vblank via display callback, or (c) decrements via emergency drain in wait_for_swap_complete.

**For busy to reach 0**: `cmd_written == cmd_read` AND `cmdfifo_depth_rd == cmdfifo_depth_wr` AND `voodoo_busy == 0` AND render threads idle.

**VK mode**:
1. CPU writes SST_swapbufferCMD -> `swap_count++` (immediate, CPU thread)
2. Same CMDFIFO path to FIFO thread
3. FIFO thread reaches swap command
4. VK path: `vc_voodoo_swap_buffers()` pushes VC_CMD_SWAP to SPSC ring
5. **IMMEDIATELY**: `swap_count--` (on FIFO thread, before VC_CMD_SWAP is even processed)
6. Buffer flip, voodoo_recalc, cmd_read++ all on FIFO thread
7. `voodoo_busy` clears when FIFO thread drains all queues

In VK mode, swap_count typically goes 1->0 almost instantly (increment and decrement happen close together). The only delay is the time for the FIFO thread to reach the swap command in the CMDFIFO.

### In SW mode, how does the display callback timing relate to FIFO processing?

The display callback and FIFO thread are **independent**. The display callback runs per-scanline on the timer thread. The FIFO thread runs when woken by events.

The coupling point is `swap_pending`: the FIFO thread sets it and blocks (in `voodoo_wait_for_swap_complete`), and the display callback clears it at vblank. This is a producer-consumer handshake:
- FIFO thread produces a swap request (swap_pending=1)
- Display callback consumes it at the right time (next vblank after swap_interval retraces)
- FIFO thread waits for consumption before proceeding

If the FIFO thread never reaches the swap command (because it's blocked on something else), swap_pending is never set, and the display callback just increments retrace_count indefinitely.

### What is the purpose of cmd_written/cmd_read and how does the guest use them?

They implement the **busy bit** in SST_status. The guest polls SST_status to know when the GPU is idle. The busy bit is a composite:

```c
int busy = (written - cmd_read) ||                    // PCI+CMDFIFO commands pending
           (cmdfifo_depth_rd != cmdfifo_depth_wr) ||  // CMDFIFO data unread
           voodoo_busy ||                              // FIFO thread active
           RENDER_VOODOO_BUSY(voodoo, 0) || ...;      // SW render threads active
```

The guest uses this to:
1. **Pace rendering**: Don't submit more work until the GPU is idle
2. **Synchronize before readback**: Wait for busy==0 before reading LFB
3. **Check swap completion**: swap_count > 0 implies work still in flight

`cmd_written` is incremented by the CPU thread when it submits a command. `cmd_read` is incremented by the FIFO thread when it finishes processing that command. The difference gives the number of commands in flight.

### Is there a scenario where the FIFO thread goes to sleep with pending CMDFIFO entries?

**Yes, this is a normal and intended part of the design.**

The FIFO thread sleeps inside `cmdfifo_get()` when it has read a partial CMDFIFO packet and is waiting for more data. For example, a CMDFIFO packet type 3 (triangle strip) may specify 5 vertices, but the CPU may have only written 3 vertices worth of data so far. The FIFO thread calls `cmdfifo_get()` for the 4th vertex, finds `cmdfifo_depth_rd == cmdfifo_depth_wr`, and sleeps.

This is safe as long as the CPU eventually writes more data. The CPU wakes the FIFO thread via `voodoo_wake_fifo_thread()` (delayed timer) whenever it writes to the CMDFIFO.

**Dangerous scenario**: If the CPU thread is blocked waiting for something that requires the FIFO thread to make progress, deadlock occurs. This is exactly the bug we hit with LFB reads: CPU thread blocks on `vc_wait_idle` -> GPU idle requires FIFO thread to push swap -> FIFO thread is blocked in `cmdfifo_get()` waiting for CPU to write more CMDFIFO data -> deadlock.

---

## Appendix: Complete Thread Interaction Diagram

```
CPU EMULATION THREAD                 FIFO THREAD                    RENDER THREAD (VK)              TIMER THREAD
--------------------                 -----------                    ------------------              ------------
Guest writes SST_swapbufferCMD
  -> swap_count++
  -> cmdfifo_depth_wr++
  -> arms wake_timer
                                                                                                    wake_timer fires
                                                                                                      -> set wake_fifo_thread event
                                     Wakes up
                                     voodoo_busy = 1
                                     Processes CMDFIFO packets...
                                     Reaches SST_swapbufferCMD
                                     [VK path:]
                                       vc_voodoo_swap_buffers()
                                         -> push VC_CMD_SWAP to ring
                                       swap_count--
                                       flip buffers
                                       voodoo_recalc()
                                       cmd_read++
                                                                    vc_dispatch_command(CMD_SWAP)
                                                                      vc_end_frame()
                                                                        vkQueueSubmit()
                                                                        vc_framebuffer_swap()
                                                                      vc_begin_frame()
                                                                        vkWaitForFences()
                                                                      vc_present_dispatch()
                                     [SW path:]
                                       flip buffers
                                       voodoo_recalc()
                                       swap_pending = 1
                                       voodoo_wait_for_swap_complete()
                                         -> polls swap_pending
                                                                                                    voodoo_callback()
                                                                                                      if (line == v_disp):
                                                                                                        retrace_count++
                                                                                                        if (swap_pending && retrace_count > interval):
                                                                                                          swap_count--
                                                                                                          swap_pending = 0
                                                                                                          wake FIFO thread
                                       swap_pending cleared!
                                       continues processing...
                                     All queues empty
                                     voodoo_busy = 0
                                     SLEEPS on wake_fifo_thread

Guest reads SST_status
  busy = 0, swap_count = 0
  -> proceeds with next frame
```
