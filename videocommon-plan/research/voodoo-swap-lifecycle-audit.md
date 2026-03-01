# Voodoo Swap/Display Lifecycle -- Definitive Audit

**Date**: 2026-03-01
**Branch**: videocommon-voodoo (= master, clean baseline for VideoCommon v2)
**Purpose**: Exhaustive map of every code path involved in swap buffer management, display callback, and retrace synchronization in the existing software Voodoo renderer. This document is the authoritative reference for VideoCommon v2 design.

---

## Table of Contents

1. [Structural Overview](#1-structural-overview)
2. [swap_count Lifecycle](#2-swap_count-lifecycle)
3. [swap_pending Lifecycle](#3-swap_pending-lifecycle)
4. [Display Callback (voodoo_callback)](#4-display-callback)
5. [FIFO Thread Wake/Sleep](#5-fifo-thread-wakesleep)
6. [CMDFIFO vs Non-CMDFIFO Paths](#6-cmdfifo-vs-non-cmdfifo-paths)
7. [SST_status Register](#7-sst_status-register)
8. [Buffer Management](#8-buffer-management)
9. [Retrace / VSync](#9-retrace--vsync)
10. [Banshee / Voodoo 3 Differences](#10-banshee--voodoo-3-differences)
11. [Thread Safety Analysis](#11-thread-safety-analysis)
12. [State Machine Diagram](#12-state-machine-diagram)
13. [Design Implications for VideoCommon v2](#13-design-implications-for-videocommon-v2)

---

## 1. Structural Overview

### Threads

| Thread | Identity | Role |
|--------|----------|------|
| **CPU thread** | The main emulation thread; also runs PCI register writes | Writes to Voodoo MMIO registers, reads SST_status, enqueues FIFO commands |
| **Timer thread** | CPU thread via 86Box timer subsystem (NOT a separate thread) | Fires `voodoo_callback` once per scanline; runs on CPU thread context |
| **FIFO thread** | `voodoo_fifo_thread` (dedicated per Voodoo card) | Drains FIFO queue and CMDFIFO; calls `voodoo_reg_writel` for each command |
| **Render threads** | `voodoo_render_thread_1..4` (1-4 per card) | Execute triangle rasterization from params buffer |

**Key insight**: The timer callback (`voodoo_callback`) runs on the CPU emulation thread, not a separate thread. It fires once per emulated scanline via `timer_advance_u64`.

### Synchronization Primitives

| Primitive | Type | Purpose |
|-----------|------|---------|
| `swap_mutex` | `mutex_t*` | Protects `swap_count`, `swap_pending`, `front_offset` during swap operations |
| `wake_fifo_thread` | `event_t*` | Wakes FIFO thread from sleep (set by timer, CPU writes, display callback) |
| `fifo_not_full_event` | `event_t*` | Signals CPU thread that FIFO has room |
| `fifo_empty_event` | `event_t*` | Signals that FIFO is fully drained |
| `wake_render_thread[4]` | `event_t*` | Wakes render threads |
| `render_not_full_event[4]` | `event_t*` | Signals render threads have capacity |
| `force_blit_mutex` | `mutex_t*` | Protects `force_blit_count` and `can_blit` |

### Key Files

| File | Path | Relevance |
|------|------|-----------|
| vid_voodoo.c | `src/video/vid_voodoo.c` | Device init, PCI register writes (SST_swapbufferCMD on CPU side), SST_status reads, voodoo_recalc |
| vid_voodoo_reg.c | `src/video/vid_voodoo_reg.c` | Register write handler (called by FIFO thread), SST_swapbufferCMD processing |
| vid_voodoo_fifo.c | `src/video/vid_voodoo_fifo.c` | FIFO thread main loop, CMDFIFO processing, `voodoo_wait_for_swap_complete` |
| vid_voodoo_display.c | `src/video/vid_voodoo_display.c` | Display callback (timer-driven per-scanline rendering + swap completion) |
| vid_voodoo_common.h | `src/include/86box/vid_voodoo_common.h` | Struct definitions for `voodoo_t` |
| vid_voodoo_regs.h | `src/include/86box/vid_voodoo_regs.h` | Register addresses, flag definitions |

---

## 2. swap_count Lifecycle

`swap_count` tracks the number of pending swap buffer commands that have been submitted by the guest but not yet completed (displayed). The guest reads this from SST_status bits [30:28] and uses it to throttle rendering.

### 2.1 Struct Definition

```c
// src/include/86box/vid_voodoo_common.h:344
int swap_count;
```

Protected by `swap_mutex` (mutex_t*, line 343).

### 2.2 All Increment Points

#### (A) CPU thread -- `voodoo_writel()` SST_swapbufferCMD handler

**File**: `src/video/vid_voodoo.c`, lines 681-684
**Thread**: CPU emulation thread
**Mutex**: `swap_mutex` held

```c
case SST_swapbufferCMD:
    voodoo->cmd_written++;
    thread_wait_mutex(voodoo->swap_mutex);
    voodoo->swap_count++;
    thread_release_mutex(voodoo->swap_mutex);
    if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE)
        return;  // CMDFIFO path: don't enqueue to FIFO, data already in CMDFIFO
    voodoo_queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
    ...
    break;
```

**This is the ONLY increment for Voodoo 1/2** (non-Banshee). It happens IMMEDIATELY when the guest writes to register 0x128, BEFORE the command is processed by the FIFO thread. This is by design -- the guest sees the increased swap_count right away via SST_status, allowing it to stop submitting if swap_count reaches the limit.

**CRITICAL**: For CMDFIFO-enabled cards, after incrementing swap_count and cmd_written, the function RETURNS EARLY. The swap command value has already been written to CMDFIFO memory (in the `addr & 0x200000` branch at line 647). The FIFO thread will later read it from CMDFIFO and call `voodoo_reg_writel(SST_swapbufferCMD, val)`.

For non-CMDFIFO cards, the swap command is enqueued via `voodoo_queue_command()` into the FIFO ring for the FIFO thread to process later.

#### (B) Banshee CPU thread -- SST_swapPending register

**File**: `src/video/vid_voodoo_banshee.c`, lines 1932-1935
**Thread**: CPU emulation thread
**Mutex**: `swap_mutex` held

```c
case SST_swapPending:
    thread_wait_mutex(voodoo->swap_mutex);
    voodoo->swap_count++;
    thread_release_mutex(voodoo->swap_mutex);
    break;
```

Banshee/V3 uses a separate register (0x24C) for swap count tracking, written by the driver.

### 2.3 All Decrement Points

#### (A) Display callback -- normal vsync swap completion

**File**: `src/video/vid_voodoo_display.c`

**Non-SLI path** (lines 616-628):
```c
thread_wait_mutex(voodoo->swap_mutex);
if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval)) {
    voodoo->front_offset = voodoo->swap_offset;
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;           // <-- DECREMENT
    voodoo->swap_pending = 0;
    thread_release_mutex(voodoo->swap_mutex);
    memset(voodoo->dirty_line, 1, 1024);
    voodoo->retrace_count = 0;
    thread_set_event(voodoo->wake_fifo_thread);
    voodoo->frame_count++;
} else
    thread_release_mutex(voodoo->swap_mutex);
```

**Thread**: CPU/timer thread (voodoo_callback runs on CPU thread via timer subsystem)
**Mutex**: `swap_mutex` held
**Condition**: `swap_pending != 0` AND `retrace_count > swap_interval`

**SLI path** (lines 589-613):
```c
if (voodoo->swap_pending && ... && voodoo_1->swap_pending && ...) {
    ...
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;           // <-- DECREMENT card 0
    voodoo->swap_pending = 0;
    ...
    if (voodoo_1->swap_count > 0)
        voodoo_1->swap_count--;         // <-- DECREMENT card 1
    voodoo_1->swap_pending = 0;
    ...
}
```

**Thread**: CPU/timer thread
**Mutex**: `swap_mutex` of card 0 held (note: only card 0's mutex is used for both!)
**Condition**: Both cards have `swap_pending` and both have `retrace_count > swap_interval`

#### (B) FIFO thread -- emergency swap in `voodoo_wait_for_swap_complete()`

**File**: `src/video/vid_voodoo_fifo.c`, lines 270-284
```c
void voodoo_wait_for_swap_complete(voodoo_t *voodoo) {
    while (voodoo->swap_pending) {
        thread_wait_mutex(voodoo->swap_mutex);
        if ((voodoo->swap_pending && voodoo->flush) || FIFO_FULL) {
            memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
            voodoo->front_offset = voodoo->params.front_offset;
            if (voodoo->swap_count > 0)
                voodoo->swap_count--;   // <-- DECREMENT (emergency)
            voodoo->swap_pending = 0;
            thread_release_mutex(voodoo->swap_mutex);
            break;
        } else
            thread_release_mutex(voodoo->swap_mutex);
        plat_delay_ms(1);  // yield
    }
}
```

**Thread**: FIFO thread
**Mutex**: `swap_mutex` held
**Condition**: `swap_pending` is true AND (`flush` is set OR FIFO is full). This is a pressure-relief valve: if the CPU thread is blocked waiting for the FIFO to drain (flush=1), or the FIFO is full, the FIFO thread forces the swap without waiting for vsync.

#### (C) FIFO thread -- SST_swapbufferCMD register handler, no-vsync swap

**File**: `src/video/vid_voodoo_reg.c`, lines 147-152 (non-Banshee, val bit 0 = 0):
```c
if (!(val & 1)) {
    memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
    voodoo->front_offset = voodoo->params.front_offset;
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;           // <-- DECREMENT (immediate swap)
    thread_release_mutex(voodoo->swap_mutex);
}
```

**Thread**: FIFO thread (this is inside `voodoo_reg_writel`, called by FIFO thread)
**Mutex**: `swap_mutex` held
**Condition**: `val & 1 == 0` (swap without vsync wait)

**Banshee path** (same file, lines 105-110):
```c
if (!(val & 1)) {
    banshee_set_overlay_addr(voodoo->priv, voodoo->leftOverlayBuf);
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;           // <-- DECREMENT (immediate swap, Banshee)
    thread_release_mutex(voodoo->swap_mutex);
    voodoo->frame_count++;
}
```

#### (D) Banshee display callback -- vsync swap completion

**File**: `src/video/vid_voodoo_banshee.c`, lines 3048-3061
```c
static void banshee_vsync_callback(svga_t *svga) {
    ...
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval)) {
        if (voodoo->swap_count > 0)
            voodoo->swap_count--;       // <-- DECREMENT
        voodoo->swap_pending = 0;
        thread_release_mutex(voodoo->swap_mutex);
        ...
    }
}
```

**Thread**: Timer/CPU thread (svga vsync callback)
**Mutex**: `swap_mutex` held

### 2.4 Reset Points

#### (A) fbiInit1 VIDEO_RESET de-assertion

**File**: `src/video/vid_voodoo.c`, lines 780-785
```c
case SST_fbiInit1:
    if (voodoo->initEnable & 0x01) {
        if ((voodoo->fbiInit1 & FBIINIT1_VIDEO_RESET) && !(val & FBIINIT1_VIDEO_RESET)) {
            voodoo->line = 0;
            thread_wait_mutex(voodoo->swap_mutex);
            voodoo->swap_count = 0;     // <-- RESET
            thread_release_mutex(voodoo->swap_mutex);
            voodoo->retrace_count = 0;
        }
        ...
    }
```

**Thread**: CPU emulation thread
**Mutex**: `swap_mutex` held
**Condition**: VIDEO_RESET transitioning from 1 to 0 (falling edge)

### 2.5 Read Points

#### (A) SST_status register (Voodoo 1/2)

**File**: `src/video/vid_voodoo.c`, lines 436, 449-450, 470-473
```c
case SST_status: {
    int swap_count = voodoo->swap_count;    // read without mutex!
    ...
    if (SLI_ENABLED && ...) {
        if (voodoo_other->swap_count > swap_count)
            swap_count = voodoo_other->swap_count;  // take max of both cards
    }
    ...
    if (swap_count < 7)
        temp |= (swap_count << 28);
    else
        temp |= (7 << 28);                 // capped at 7
}
```

**Thread**: CPU emulation thread
**Mutex**: NOT held (benign -- swap_count is int, reads are atomic on all platforms)

#### (B) banshee_status (Banshee/V3)

**File**: `src/video/vid_voodoo_banshee.c`, lines 1129, 1140-1141
```c
int swap_count = voodoo->swap_count;
...
if (swap_count < 7)
    ret |= (swap_count << 28);
else
    ret |= (7 << 28);
```

Same pattern, same thread (CPU), no mutex.

---

## 3. swap_pending Lifecycle

`swap_pending` is a flag (0 or 1) indicating that a vsync-synchronized swap has been requested but not yet completed by the display callback.

### 3.1 Struct Definition

```c
// src/include/86box/vid_voodoo_common.h:413
int swap_pending;
```

No dedicated lock -- protected by `swap_mutex` in most places.

### 3.2 Set Points (swap_pending = 1)

#### (A) FIFO thread -- SST_swapbufferCMD with vsync (val & 1 == 1)

**File**: `src/video/vid_voodoo_reg.c`

**Banshee, triple buffer** (lines 113-118):
```c
} else if (TRIPLE_BUFFER) {
    if (voodoo->swap_pending)
        voodoo_wait_for_swap_complete(voodoo);
    voodoo->swap_interval = (val >> 1) & 0xff;
    voodoo->swap_offset   = voodoo->leftOverlayBuf;
    voodoo->swap_pending  = 1;
```

**Banshee, double buffer** (lines 119-122):
```c
} else {
    voodoo->swap_interval = (val >> 1) & 0xff;
    voodoo->swap_offset   = voodoo->leftOverlayBuf;
    voodoo->swap_pending  = 1;
    voodoo_wait_for_swap_complete(voodoo);
}
```

**Non-Banshee, triple buffer** (lines 153-159):
```c
} else if (TRIPLE_BUFFER) {
    if (voodoo->swap_pending)
        voodoo_wait_for_swap_complete(voodoo);
    voodoo->swap_interval = (val >> 1) & 0xff;
    voodoo->swap_offset   = voodoo->params.front_offset;
    voodoo->swap_pending  = 1;
}
```

**Non-Banshee, double buffer** (lines 160-165):
```c
} else {
    voodoo->swap_interval = (val >> 1) & 0xff;
    voodoo->swap_offset   = voodoo->params.front_offset;
    voodoo->swap_pending  = 1;
    voodoo_wait_for_swap_complete(voodoo);
}
```

**Thread**: FIFO thread (all of the above are inside `voodoo_reg_writel`)
**Mutex**: NOT held when setting swap_pending (but swap_mutex is held by readers/clearers)

**Key difference between triple and double buffer**:
- **Triple buffer**: If a previous swap is still pending, wait for it first. Then set pending and continue (non-blocking -- FIFO thread keeps processing).
- **Double buffer**: Set pending and BLOCK until the display callback completes the swap via `voodoo_wait_for_swap_complete()`. This provides vsync throttling.

### 3.3 Clear Points (swap_pending = 0)

#### (A) Display callback -- normal vsync completion

**File**: `src/video/vid_voodoo_display.c`
- Line 597: `voodoo->swap_pending = 0;` (SLI path, card 0)
- Line 604: `voodoo_1->swap_pending = 0;` (SLI path, card 1)
- Line 621: `voodoo->swap_pending = 0;` (non-SLI path)

**Thread**: CPU/timer thread
**Mutex**: `swap_mutex` held

#### (B) FIFO thread -- emergency swap

**File**: `src/video/vid_voodoo_fifo.c`, line 279
```c
voodoo->swap_pending = 0;
```

**Thread**: FIFO thread
**Mutex**: `swap_mutex` held

#### (C) Banshee vsync callback

**File**: `src/video/vid_voodoo_banshee.c`, line 3052
```c
voodoo->swap_pending = 0;
```

**Thread**: Timer thread (svga vsync callback context)
**Mutex**: `swap_mutex` held

### 3.4 Test Points

- `voodoo_wait_for_swap_complete()` polls `swap_pending` in a while loop (FIFO thread)
- Display callback tests `swap_pending` under `swap_mutex` (timer thread)
- SST_swapbufferCMD handler checks `swap_pending` before `voodoo_wait_for_swap_complete()` for triple buffer path (FIFO thread)

---

## 4. Display Callback

### 4.1 Registration

```c
// src/video/vid_voodoo.c:1228
timer_add(&voodoo->timer, voodoo_callback, voodoo, 1);
```

The timer fires once per scanline. After each invocation, it re-arms itself:
```c
// src/video/vid_voodoo_display.c:663-666
if (voodoo->line_time)
    timer_advance_u64(&voodoo->timer, voodoo->line_time);
else
    timer_advance_u64(&voodoo->timer, TIMER_USEC * 32);
```

`line_time` is calculated in `voodoo_pixelclock_update()` based on the DAC PLL and hSync timing.

### 4.2 Thread

The callback runs on the **CPU emulation thread** via the 86Box timer subsystem. Timers are dispatched as part of the CPU execution loop. This means the callback can be delayed if the CPU is busy.

### 4.3 Full Callback Flow

```
voodoo_callback(voodoo):
  1. If VGA pass-through enabled (FBIINIT0_VGA_PASS):
     If line < v_disp:
       - Select draw source (SLI: alternate cards per line)
       - If dirty_line[draw_line] set:
         - Read RGB565 pixels from fb_mem[front_offset + draw_line * row_width]
         - Convert to 32-bit via video_16to32[] LUT (or apply screen filter)
         - Write to monitor->target_buffer->line[]
         - Update dirty_line_low/dirty_line_high

  2. skip_draw:
     If line == v_disp:  // START OF VERTICAL BLANKING
       a. retrace_count++
       b. [SLI_SYNC path]:
          - Lock swap_mutex
          - If BOTH cards have swap_pending AND retrace_count > swap_interval:
            - front_offset = swap_offset (for both cards)
            - swap_count-- (for both cards)
            - swap_pending = 0 (for both cards)
            - dirty_line[] = all 1s (for both cards)
            - retrace_count = 0 (for both cards)
            - Wake FIFO threads for both cards
            - frame_count++ (for both cards)
          - Unlock swap_mutex
       c. [Non-SLI path]:
          - Lock swap_mutex
          - If swap_pending AND retrace_count > swap_interval:
            - front_offset = swap_offset
            - swap_count--
            - swap_pending = 0
            - Unlock swap_mutex
            - dirty_line[] = all 1s
            - retrace_count = 0
            - Wake FIFO thread
            - frame_count++
          - Else: unlock swap_mutex
       d. v_retrace = 1

  3. line++

  4. If VGA pass-through AND line == v_disp:  // BLIT TO HOST DISPLAY
     - Check force_blit_count (under force_blit_mutex)
     - If dirty_line_high > dirty_line_low OR force_blit:
       - svga_doblit(h_disp, v_disp-1, svga)
     - Reset dirty_line_high/low

  5. If line >= v_total:
     - line = 0
     - v_retrace = 0

  6. Re-arm timer (timer_advance_u64)
```

### 4.4 Key Observations

1. **Swap completion happens at line == v_disp** (start of vblank), not at line 0.
2. **retrace_count increments once per frame** (at line == v_disp), not once per scanline.
3. **swap_interval** specifies the MINIMUM number of retrace cycles before the swap is executed. Setting swap_interval=0 means "swap on the next retrace", swap_interval=1 means "wait at least 1 full retrace" (i.e., next-next frame).
4. **The condition is `retrace_count > swap_interval`** (strictly greater), NOT `>=`. So swap_interval=0 fires on the first retrace after pending is set (retrace_count goes from 0 to 1, 1 > 0 is true). Wait, actually: retrace_count starts at 0 and is immediately incremented to 1 on the same line, so with swap_interval=0, the condition `1 > 0` is true and the swap fires immediately on the next vblank.
5. **dirty_line[] is set to all-1** when a swap completes. This forces the entire framebuffer to be re-scanned for display on the next frame.
6. **wake_fifo_thread** is signaled after swap completion. This wakes the FIFO thread if it's blocked in `voodoo_wait_for_swap_complete()`.
7. **svga_doblit** triggers the actual blit to the host display (Qt window). This is the bridge between the Voodoo scanout and the 86Box video output system.

---

## 5. FIFO Thread Wake/Sleep

### 5.1 Thread Main Loop

**File**: `src/video/vid_voodoo_fifo.c`, `voodoo_fifo_thread()`

```
while (fifo_thread_run):
    1. Signal fifo_not_full_event (allow CPU to queue more)
    2. Wait on wake_fifo_thread event (BLOCKING)
    3. Reset wake_fifo_thread event
    4. voodoo_busy = 1

    5. Process FIFO ring:
       while (!FIFO_EMPTY):
         - Read next FIFO entry
         - Dispatch based on type:
           FIFO_WRITEL_REG -> voodoo_reg_writel()
           FIFO_WRITEW_FB  -> voodoo_fb_writew()
           FIFO_WRITEL_FB  -> voodoo_fb_writel()
           FIFO_WRITEL_TEX -> voodoo_tex_writel()
           FIFO_WRITEL_2DREG -> voodoo_2d_reg_writel()
         - If FIFO_ENTRIES > 0xe000: signal fifo_not_full_event

    6. Mark FIFO complete:
       - cmd_status |= (1 << 24)   // set FIFO empty bit
       - Signal fifo_empty_event
       - Set fifo_empty_signaled = 1

    7. Process CMDFIFO (primary):
       while (cmdfifo_enabled && depth_rd != depth_wr):
         - Read header from CMDFIFO memory
         - Dispatch based on packet type (0-6)
         - Packet type 1: sequential register writes including SST_swapbufferCMD
         - Packet type 3: vertex data + draw commands
         - Packet type 5: bulk data (framebuffer, texture)

    8. Process CMDFIFO (secondary, for dual CMDFIFO):
       Same as (7) for cmdfifo_2

    9. voodoo_busy = 0

    (Loop back to step 1)
```

### 5.2 Wake Events

The FIFO thread is woken by signaling `wake_fifo_thread`:

| Source | Function | File:Line | Condition |
|--------|----------|-----------|-----------|
| CPU write | `voodoo_wake_fifo_thread()` | vid_voodoo_fifo.c:166 | Sets delayed wake timer (100us default) |
| CPU write | `voodoo_wake_fifo_thread_now()` | vid_voodoo_fifo.c:174 | Immediate wake (when FIFO is full) |
| Timer expiry | `voodoo_wake_timer()` | vid_voodoo_fifo.c:180 | Delayed wake timer fires |
| Display callback | `thread_set_event(wake_fifo_thread)` | vid_voodoo_display.c:607/608/626 | After swap completed |
| CPU status read | `voodoo_wake_fifo_thread()` | vid_voodoo.c:461/479 | Wakes idle FIFO thread on status poll |
| FIFO queue | `voodoo_queue_command()` | vid_voodoo_fifo.c:220 | When FIFO_ENTRIES > 0xe000 |

### 5.3 Sleep Conditions

The FIFO thread sleeps in two places:

1. **Main loop top** (line after step 2): `thread_wait_event(wake_fifo_thread, -1)` -- sleeps when both FIFO ring and CMDFIFO are empty. Woken by any wake event.

2. **Inside `cmdfifo_get()`**: `thread_wait_event(wake_fifo_thread, -1)` -- sleeps when CMDFIFO read pointer catches up to write pointer. Woken when CPU writes more data to CMDFIFO.

3. **Inside `voodoo_wait_for_swap_complete()`**: `plat_delay_ms(1)` polling loop -- sleeps while waiting for display callback to clear `swap_pending`. NOT event-driven -- busy-polls with 1ms yield.

### 5.4 Wake Timer Mechanism

The `voodoo_wake_fifo_thread()` function does NOT immediately signal the event. Instead, it arms a one-shot timer (`wake_timer`) with a 100us delay:

```c
void voodoo_wake_fifo_thread(voodoo_t *voodoo) {
    if (!timer_is_enabled(&voodoo->wake_timer)) {
        timer_set_delay_u64(&voodoo->wake_timer, WAKE_DELAY_OF(voodoo));
    }
}
```

When the timer fires, `voodoo_wake_timer()` signals `wake_fifo_thread`. This batching prevents excessive wake/sleep cycles when the CPU writes many registers in quick succession.

---

## 6. CMDFIFO vs Non-CMDFIFO Paths

### 6.1 Non-CMDFIFO Path (Voodoo 1, Voodoo 2 without CMDFIFO)

Guest writes to register addresses directly via PCI MMIO. The CPU-thread `voodoo_writel()` handler:

1. For SST_swapbufferCMD:
   - Increments `cmd_written++`
   - Increments `swap_count++` (under `swap_mutex`)
   - Enqueues `addr | FIFO_WRITEL_REG` + `val` into FIFO ring via `voodoo_queue_command()`
   - Wakes FIFO thread

2. FIFO thread dequeues and calls `voodoo_reg_writel(SST_swapbufferCMD, val)` which:
   - Rotates `disp_buffer`/`draw_buffer` (see section 8)
   - Calls `voodoo_recalc()` to update buffer offsets
   - If `val & 1 == 0`: immediate swap (decrement swap_count, set front_offset)
   - If `val & 1 == 1` + triple buffer: set swap_pending, wait if previous pending
   - If `val & 1 == 1` + double buffer: set swap_pending, block until completed

### 6.2 CMDFIFO Path (Voodoo 2 with CMDFIFO, Banshee, V3)

Guest writes command data to the CMDFIFO address range (0x200000+ offset). The CPU-thread `voodoo_writel()`:

1. Writes data to `fb_mem[cmdfifo_base + offset]`
2. Increments `cmdfifo_depth_wr`
3. Wakes FIFO thread (if depth < 20 words)

**For SST_swapbufferCMD specifically**: Even on CMDFIFO path, the guest also writes directly to register 0x128. The CPU handler:
1. Increments `cmd_written++` and `swap_count++`
2. Checks `FBIINIT7_CMDFIFO_ENABLE` -- if set, **returns without enqueueing** to FIFO ring
3. The swap command value is part of the CMDFIFO data stream (written to fb_mem)

The FIFO thread, after draining the FIFO ring, enters the CMDFIFO processing loop. When it encounters a register write to SST_swapbufferCMD in the CMDFIFO data (via packet type 1 or 4), it calls `voodoo_cmdfifo_reg_writel()` which:
- Calls `voodoo_reg_writel(addr, val)` -- same handler as non-CMDFIFO
- Calls `voodoo_queue_apply_reg(addr, val)` -- updates queued buffer tracking

**CMDFIFO also tracks cmd counts**: In packet types 1 and 4, if the register address is SST_swapbufferCMD (and type >= VOODOO_BANSHEE), `cmd_written_fifo` is incremented. This feeds into the "busy" calculation.

### 6.3 Key Difference: Timing of swap_count Increment

| Path | When swap_count++ happens | When swap is processed |
|------|---------------------------|----------------------|
| Non-CMDFIFO | CPU write to 0x128 | FIFO thread dequeues and calls voodoo_reg_writel |
| CMDFIFO | CPU write to 0x128 (same!) | FIFO thread reads CMDFIFO packet and calls voodoo_reg_writel |

In BOTH cases, `swap_count` is incremented immediately on the CPU thread. The swap PROCESSING happens later on the FIFO thread. This creates a window where swap_count may be >0 but swap_pending is not yet set. The guest sees the elevated swap_count via SST_status and throttles accordingly.

---

## 7. SST_status Register

### 7.1 Voodoo 1/2 Status (vid_voodoo.c:433-478)

```c
case SST_status: {
    int fifo_entries = FIFO_ENTRIES;
    int swap_count   = voodoo->swap_count;
    int written      = voodoo->cmd_written + voodoo->cmd_written_fifo
                       + voodoo->cmd_written_fifo_2;
    int busy         = (written - voodoo->cmd_read) ||
                       (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr) ||
                       voodoo->voodoo_busy ||
                       RENDER_VOODOO_BUSY(voodoo, 0) ||
                       (render_threads >= 2 && RENDER_VOODOO_BUSY(voodoo, 1)) ||
                       (render_threads == 4 && (...));
    // SLI: take max swap_count, max FIFO entries, OR busy flags
    ...
}
```

### 7.2 Bit Layout

| Bits | Name | Value Source |
|------|------|-------------|
| [3:0] | FIFO free entries (low) | `min(0x3f, 0xffff - FIFO_ENTRIES)` |
| [6] | Vertical retrace (active low) | `!v_retrace` -- 0x40 = NOT in retrace |
| [9:7] | Busy flags | 0x380 if any work pending |
| [27:12] | FIFO free entries (high) | `(0xffff - FIFO_ENTRIES) << 12` |
| [30:28] | swap_count | `min(7, swap_count) << 28` |

### 7.3 Busy Determination

The "busy" flag (bits 9:7) is set if ANY of:
1. `cmd_written != cmd_read` -- unprocessed commands
2. `cmdfifo_depth_rd != cmdfifo_depth_wr` -- CMDFIFO has data
3. `voodoo_busy` -- FIFO thread is actively processing
4. `RENDER_VOODOO_BUSY(n)` -- any render thread is active
5. (SLI) Any of the above on the other card

### 7.4 Side Effects

Reading SST_status also:
- Wakes the FIFO thread if idle: `if (!voodoo->voodoo_busy) voodoo_wake_fifo_thread(voodoo);`
- For SLI: also wakes the other card's FIFO thread

This prevents a deadlock where the guest polls status waiting for busy=0, but the FIFO thread is asleep.

### 7.5 Banshee Status (vid_voodoo_banshee.c:1122-1151)

Very similar layout. Differences:
- Uses SVGA `cgastat` for retrace instead of `v_retrace`
- Checks both CMDFIFO depths (`cmdfifo_depth_rd_2`)
- Bit layout has minor differences in FIFO entry encoding (5-bit low field)

---

## 8. Buffer Management

### 8.1 Key Fields

```c
// In voodoo_t:
uint32_t front_offset;   // BYTE offset into fb_mem for scanout
uint32_t back_offset;    // BYTE offset into fb_mem for drawing
int      disp_buffer;    // Buffer index for display (0, 1, or 2 for triple)
int      draw_buffer;    // Buffer index for drawing (0, 1, or 2 for triple)

// In voodoo_params_t (per-triangle):
uint32_t front_offset;   // front buffer offset (used for swap_offset calculation)
uint32_t draw_offset;    // where triangles are drawn
uint32_t aux_offset;     // depth/stencil buffer offset
```

### 8.2 Buffer Offset Calculation

**File**: `src/video/vid_voodoo.c`, `voodoo_recalc()` (lines 139-197)

```c
uint32_t buffer_offset = ((voodoo->fbiInit2 >> 11) & 511) * 4096;
// buffer_offset = stride between buffers in bytes

voodoo->params.front_offset = voodoo->disp_buffer * buffer_offset;
voodoo->back_offset         = voodoo->draw_buffer * buffer_offset;

// Triple buffer: aux at 3*offset, cutoff at 4*offset
// Double buffer: aux at 2*offset, cutoff at 3*offset
if (TRIPLE_BUFFER)
    voodoo->params.aux_offset = buffer_offset * 3;
else
    voodoo->params.aux_offset = buffer_offset * 2;
```

Memory layout (double buffer):
```
fb_mem[0]                          = buffer 0
fb_mem[buffer_offset]              = buffer 1
fb_mem[2 * buffer_offset]          = aux (depth) buffer
```

Memory layout (triple buffer):
```
fb_mem[0]                          = buffer 0
fb_mem[buffer_offset]              = buffer 1
fb_mem[2 * buffer_offset]          = buffer 2
fb_mem[3 * buffer_offset]          = aux (depth) buffer
```

### 8.3 Buffer Rotation on Swap

**File**: `src/video/vid_voodoo_reg.c`, SST_swapbufferCMD handler (lines 130-137)

```c
if (TRIPLE_BUFFER) {
    voodoo->disp_buffer = (voodoo->disp_buffer + 1) % 3;
    voodoo->draw_buffer = (voodoo->draw_buffer + 1) % 3;
} else {
    voodoo->disp_buffer = !voodoo->disp_buffer;
    voodoo->draw_buffer = !voodoo->draw_buffer;
}
voodoo_recalc(voodoo);
```

**Double buffer**: disp and draw swap between 0 and 1.
**Triple buffer**: disp and draw rotate through 0, 1, 2.

This rotation happens BEFORE the vsync wait. It updates `params.front_offset` and `back_offset` immediately. The actual scanout doesn't change yet because `voodoo->front_offset` (the field used by the display callback for reading pixels) is only updated when the swap completes.

### 8.4 The Two `front_offset` Fields

This is a critical subtlety:

1. **`voodoo->params.front_offset`**: Recalculated by `voodoo_recalc()` after buffer rotation. Represents where the NEXT displayed frame will come from. Updated on FIFO thread.

2. **`voodoo->front_offset`**: The ACTIVE scanout offset, used by `voodoo_callback()` to read pixels for display. Updated when swap completes (display callback or emergency swap).

The flow is:
```
CPU writes SST_swapbufferCMD
  -> swap_count++ (CPU thread)
  -> FIFO thread processes:
     -> rotate disp_buffer/draw_buffer
     -> voodoo_recalc(): params.front_offset = new value
     -> if vsync: swap_offset = params.front_offset, swap_pending = 1
       -> display callback: front_offset = swap_offset
     -> if no vsync: front_offset = params.front_offset (immediate)
```

### 8.5 Queued Buffer Tracking

There is a parallel "queued" buffer tracking system used for FIFO hazard detection:

```c
int queued_disp_buffer;
int queued_draw_buffer;
int queued_fb_write_buffer;
int queued_fb_draw_buffer;
```

These are updated on the CPU thread (in `voodoo_queue_command()` via `voodoo_queue_apply_reg()`) to predict which buffer each queued command will affect. This allows the relaxed LFB read system to skip synchronization when reading from a buffer that has no pending writes.

The queued tracking also rotates on SST_swapbufferCMD:
```c
// src/video/vid_voodoo_fifo.c, voodoo_queue_apply_reg(), lines 117-126
case SST_swapbufferCMD:
    if (TRIPLE_BUFFER) {
        voodoo->queued_disp_buffer = (voodoo->queued_disp_buffer + 1) % 3;
        voodoo->queued_draw_buffer = (voodoo->queued_draw_buffer + 1) % 3;
    } else {
        voodoo->queued_disp_buffer = !voodoo->queued_disp_buffer;
        voodoo->queued_draw_buffer = !voodoo->queued_draw_buffer;
    }
    voodoo_queue_recalc_buffers(voodoo);
    break;
```

### 8.6 FBIINIT0_GRAPHICS_RESET

When `fbiInit0` bit 1 is set:
```c
// src/video/vid_voodoo.c:763-774
voodoo->disp_buffer = 0;
voodoo->draw_buffer = 1;
voodoo->queued_disp_buffer = voodoo->disp_buffer;
voodoo->queued_draw_buffer = voodoo->draw_buffer;
...
voodoo_recalc(voodoo);
voodoo->front_offset = voodoo->params.front_offset;
```

Resets all buffer state to defaults.

---

## 9. Retrace / VSync

### 9.1 Retrace Signal

The Voodoo does NOT use the VGA retrace signal. It has its own timing chain driven by the DAC PLL:

```c
// src/include/86box/vid_voodoo_common.h
int h_total;    // total scanlines including blanking
int v_total;    // total lines per frame
int v_disp;     // visible display lines
int h_disp;     // visible columns
int v_retrace;  // 1 when in vertical blanking interval
int line;       // current scanline (0 to v_total-1)
```

### 9.2 Timing Chain

The display callback (`voodoo_callback`) fires once per scanline via a timer. At the end of each invocation, it re-arms:

```c
if (voodoo->line_time)
    timer_advance_u64(&voodoo->timer, voodoo->line_time);
else
    timer_advance_u64(&voodoo->timer, TIMER_USEC * 32);  // fallback: 32us per line
```

`line_time` is calculated from the pixel clock and hSync timing:
```c
// src/video/vid_voodoo_display.c, voodoo_pixelclock_update()
clock_const       = cpuclock / t;  // t = pixel clock in Hz
voodoo->line_time = (uint64_t)((double)line_length * clock_const * (double)(1ULL << 32));
```

### 9.3 retrace_count and swap_interval Interaction

- `retrace_count` is set to 0 when:
  - A swap completes (display callback)
  - fbiInit1 VIDEO_RESET de-asserts (CPU thread)

- `retrace_count` is incremented once per frame at `line == v_disp` (start of vblank)

- Swap fires when `retrace_count > swap_interval`

**Timing implications**:

| swap_interval | Behavior |
|--------------|----------|
| 0 | Swap on the next vblank after pending is set |
| 1 | Skip one vblank, swap on the second |
| N | Skip N vblanks, swap on the (N+1)th |

Since `retrace_count` resets to 0 after each swap, and the condition is `> swap_interval`:
- retrace_count starts at 0
- At vblank, incremented to 1
- If swap_interval=0: 1 > 0 = true, swap fires
- If swap_interval=1: 1 > 1 = false, wait. Next vblank: 2 > 1 = true, swap fires.

### 9.4 v_retrace Flag

```c
// Set at line == v_disp (start of vblank):
voodoo->v_retrace = 1;

// Cleared at line >= v_total (start of new frame):
voodoo->v_retrace = 0;
```

The guest reads this via SST_status bit 6 (inverted: bit 6 = 0 means in retrace, bit 6 = 1 means active display).

### 9.5 Banshee Retrace

Banshee uses the standard SVGA timing chain instead of its own. The vsync callback is:
```c
// src/video/vid_voodoo_banshee.c:3040
static void banshee_vsync_callback(svga_t *svga) { ... }
```

This is called by the SVGA code at the start of vertical blanking. The retrace bit in Banshee status comes from `svga->cgastat & 8` instead of `voodoo->v_retrace`.

---

## 10. Banshee / Voodoo 3 Differences

### 10.1 Swap Source

- **Voodoo 1/2**: `swap_offset = voodoo->params.front_offset` (from disp_buffer * buffer_offset)
- **Banshee/V3**: `swap_offset = voodoo->leftOverlayBuf` (register 0x250, set by driver directly)

### 10.2 Display Path

- **Voodoo 1/2**: `voodoo_callback()` reads from `fb_mem[front_offset]`, converts RGB565 to RGB888 via CLUT
- **Banshee/V3**: Uses overlay hardware via `banshee_set_overlay_addr()`, integrated with SVGA display

### 10.3 Swap Count Source

- **Voodoo 1/2**: `swap_count++` in SST_swapbufferCMD handler
- **Banshee/V3**: `swap_count++` in SST_swapPending (0x24C) handler -- separate register

### 10.4 VSync Source

- **Voodoo 1/2**: Own timer-driven scanline counter
- **Banshee/V3**: SVGA vsync callback

---

## 11. Thread Safety Analysis

### 11.1 swap_count Access

| Operation | Thread | Protected? |
|-----------|--------|-----------|
| swap_count++ | CPU thread | YES (swap_mutex) |
| swap_count-- (display callback) | CPU/timer thread | YES (swap_mutex) |
| swap_count-- (emergency) | FIFO thread | YES (swap_mutex) |
| swap_count-- (no-vsync) | FIFO thread | YES (swap_mutex) |
| swap_count read (SST_status) | CPU thread | NO (benign -- int read) |
| swap_count = 0 (VIDEO_RESET) | CPU thread | YES (swap_mutex) |

**Verdict**: Properly synchronized. The mutex-free read in SST_status is safe because `int` reads are atomic on all platforms, and a slightly stale value is acceptable for a polling register.

### 11.2 swap_pending Access

| Operation | Thread | Protected? |
|-----------|--------|-----------|
| swap_pending = 1 | FIFO thread | NO (not under mutex) |
| swap_pending = 0 (display callback) | CPU/timer thread | YES (swap_mutex) |
| swap_pending = 0 (emergency) | FIFO thread | YES (swap_mutex) |
| swap_pending read (wait loop) | FIFO thread | NO (polled) |
| swap_pending read (display callback) | CPU/timer thread | YES (swap_mutex) |

**Verdict**: The write of `swap_pending = 1` (FIFO thread, vid_voodoo_reg.c:117/121/159/163) is NOT protected by swap_mutex. However, this is the ONLY writer of 1, and the readers either:
- Hold swap_mutex (display callback) -- will see the write eventually
- Poll without mutex (wait_for_swap_complete) -- will see the write eventually

Since `int` writes are atomic, and the only concern is ordering (the display callback must see `swap_offset` before `swap_pending`), this works on x86 (TSO). On ARM64, this is technically a data race, but in practice the FIFO thread writes swap_interval, swap_offset, then swap_pending in sequence. The display callback reads swap_pending under mutex, which provides a barrier. The potential issue is if the display callback sees swap_pending=1 before swap_offset is written, but this would require extreme reordering that is unlikely given the intervening mutex operations.

### 11.3 front_offset Access

| Operation | Thread | Protected? |
|-----------|--------|-----------|
| front_offset = swap_offset (display callback) | CPU/timer | swap_mutex held |
| front_offset = params.front_offset (emergency) | FIFO thread | swap_mutex held |
| front_offset = params.front_offset (no-vsync) | FIFO thread | NOT protected |
| front_offset read (display callback) | CPU/timer | NOT protected (same thread) |

**Verdict**: The no-vsync write (vid_voodoo_reg.c:148) is not under swap_mutex. However, the display callback reads it on the same CPU/timer thread that holds the timer dispatch, and the FIFO thread write is followed by setting dirty_line which forces re-read. In practice, this works because the display callback will see the new front_offset on the next scanline callback.

### 11.4 dirty_line Access

Written by:
- Display callback (clear individual lines after display)
- FIFO thread (set all to 1 via memset after swap)
- Render thread (set individual lines after drawing)

Read by:
- Display callback (check before displaying)

**Verdict**: Race condition exists between render thread setting dirty_line and display callback reading it, but this is benign -- worst case a line is displayed one frame late.

---

## 12. State Machine Diagram

```
                     CPU THREAD                                  FIFO THREAD                              TIMER/DISPLAY CALLBACK
                     ==========                                  ===========                              ======================

Guest writes 0x128:  swap_count++ (mutex)
                     cmd_written++
                     enqueue to FIFO/CMDFIFO
                            |
                            v
                                                    dequeue SST_swapbufferCMD
                                                    rotate disp_buffer/draw_buffer
                                                    voodoo_recalc()
                                                    params.front_offset = new
                                                           |
                                                    +------+------+
                                                    |             |
                                              val&1=0?      val&1=1?
                                                    |             |
                                              (immediate)   (vsync wait)
                                                    |             |
                                              front_offset=     swap_interval=(val>>1)&0xff
                                              params.front_offset  swap_offset=params.front_offset
                                              swap_count-- (mutex)  swap_pending=1
                                              dirty_line=all 1s       |
                                              cmd_read++        +-----+-----+
                                                               |           |
                                                         triple?      double?
                                                               |           |
                                                         if prev pending:  voodoo_wait_for_swap_complete()
                                                         wait first, then  BLOCKS until display callback
                                                         set pending       clears swap_pending
                                                               |           |
                                                               v           v
                                                                                          line == v_disp:
                                                                                            retrace_count++
                                                                                            lock swap_mutex
                                                                                            if swap_pending &&
                                                                                               retrace_count > swap_interval:
                                                                                              front_offset = swap_offset
                                                                                              swap_count--
                                                                                              swap_pending = 0
                                                                                              unlock swap_mutex
                                                                                              dirty_line = all 1s
                                                                                              retrace_count = 0
                                                                                              wake_fifo_thread!
                                                                                              frame_count++
```

### Emergency Swap Path

If the FIFO thread is in `voodoo_wait_for_swap_complete()` and either:
- `voodoo->flush` is set (CPU called `voodoo_flush()`)
- FIFO ring is full

Then the FIFO thread forces the swap without waiting for vsync:
```
FIFO thread:
  front_offset = params.front_offset
  swap_count-- (mutex)
  swap_pending = 0 (mutex)
  dirty_line = all 1s
  break (stop waiting)
```

This prevents deadlocks where the CPU is blocked waiting for FIFO space, the FIFO thread is blocked waiting for vsync, and the display callback can't fire because the CPU is blocked.

---

## 13. Design Implications for VideoCommon v2

### 13.1 What Must Be Preserved

1. **swap_count semantics**: Guest polls SST_status bits [30:28] and stalls rendering when swap_count >= threshold (typically 2-3). VideoCommon must maintain accurate swap_count.

2. **Immediate increment on CPU thread**: swap_count MUST be incremented in `voodoo_writel()` on the CPU thread, not deferred to the render thread. The guest reads SST_status immediately after writing SST_swapbufferCMD and expects to see the incremented count.

3. **Decrement timing**: swap_count must be decremented when the swap actually completes (buffer becomes visible), which is tied to vsync timing. For Vulkan, this maps to successful `vkQueuePresentKHR` or equivalent.

4. **FIFO blocking on double-buffer**: With double buffering, `voodoo_wait_for_swap_complete()` blocks the FIFO thread until the swap completes. VideoCommon must replicate this throttling behavior. Without it, the FIFO thread will race ahead, queueing unlimited frames.

5. **Emergency swap**: The flush/FIFO-full escape hatch must exist to prevent deadlocks.

### 13.2 What Can Be Simplified

1. **Buffer rotation**: Voodoo uses buffer indices (0, 1, 2) and recalculates offsets. VideoCommon can use Vulkan swapchain images directly.

2. **dirty_line tracking**: Not needed for GPU rendering. Vulkan handles its own display.

3. **front_offset vs params.front_offset distinction**: With Vulkan, "which image is displayed" is managed by the swapchain, not by memory offsets.

### 13.3 Critical Design Decisions

1. **Where does swap_count-- happen?** Options:
   - In the Vulkan present completion callback (ideal but async)
   - In the display callback (matches SW behavior but requires plumbing)
   - At vkQueuePresentKHR return (synchronous but may stall)

2. **How to throttle the FIFO thread?** The FIFO thread must not get too far ahead of the display. Options:
   - Use Vulkan fence wait (like vkWaitForFences on present)
   - Use the existing swap_pending mechanism
   - Both (fence for GPU sync, swap_pending for guest-visible throttling)

3. **CMDFIFO swap handling**: The swap_count++ still happens on CPU thread. The actual swap processing happens when the FIFO thread reaches the SST_swapbufferCMD in the CMDFIFO data. VideoCommon must handle both paths.

4. **Timer-driven retrace simulation**: The display callback's retrace_count / swap_interval system provides frame pacing. VideoCommon should either:
   - Hook into this existing system (simplest, reuses retrace timing)
   - Replace it with Vulkan present timing (more complex, better quality)

### 13.4 Failure Modes to Avoid (Lessons from v1)

1. **swap_count stuck**: v1 had swap_count stuck because the FIFO thread blocked on ring-full before reaching the swap command. Fix: ensure the render thread drains work promptly, or use the emergency swap path.

2. **Three-way mutex contention**: v1 had queue_mutex contention between CPU (readback), FIFO (submit), and GUI (present). Fix: minimize shared locks, use async operations.

3. **Present channel starvation**: v1's SPSC ring loop processed all commands before dispatching present. Fix: interleave present dispatch.

4. **Descriptor pool exhaustion**: v1 ran out of descriptor sets mid-frame. Fix: sufficient pool size or dynamic descriptor management.

---

## Appendix A: Register Addresses

| Register | Address | Purpose |
|----------|---------|---------|
| SST_status | 0x000 | Status read (swap_count in bits 30:28) |
| SST_swapbufferCMD | 0x128 | Swap buffer command |
| SST_swapPending | 0x24C | Banshee swap count register |
| SST_leftOverlayBuf | 0x250 | Banshee overlay buffer address |
| SST_fbiInit0 | 0x210 | VGA passthrough, graphics reset |
| SST_fbiInit1 | 0x214 | Video reset, SLI enable |
| SST_fbiInit2 | 0x218 | Buffer offset, swap algorithm |

## Appendix B: SST_swapbufferCMD Value Encoding

```
Bit 0: Swap trigger
  0 = Immediate swap (no vsync wait)
  1 = Synchronized swap (wait for vsync)

Bits [8:1]: swap_interval
  Number of retrace cycles to wait before swapping
  0 = swap on next retrace
  1 = skip 1 retrace, swap on 2nd
  etc.
```

## Appendix C: Key Macro Definitions

```c
#define FIFO_SIZE       65536
#define FIFO_MASK       (FIFO_SIZE - 1)
#define FIFO_ENTRIES    (voodoo->fifo_write_idx - voodoo->fifo_read_idx)
#define FIFO_FULL       ((voodoo->fifo_write_idx - voodoo->fifo_read_idx) >= FIFO_SIZE - 4)
#define FIFO_EMPTY      (voodoo->fifo_read_idx == voodoo->fifo_write_idx)

#define TRIPLE_BUFFER   ((voodoo->fbiInit2 & 0x10) || (voodoo->fbiInit5 & 0x600) == 0x400)
#define SLI_ENABLED     (voodoo->fbiInit1 & FBIINIT1_SLI_ENABLE)

// FBIINIT0_GRAPHICS_RESET  = (1 << 1)
// FBIINIT1_VIDEO_RESET     = (1 << 8)
// FBIINIT7_CMDFIFO_ENABLE  = (1 << 8)
```
