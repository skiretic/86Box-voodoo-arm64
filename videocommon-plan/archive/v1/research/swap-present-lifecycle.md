# Swap/Present Lifecycle: Emulator Survey and Architecture Analysis

**Date**: 2026-03-01
**Researcher**: vc-arch agent
**Status**: COMPLETE

---

## Executive Summary

This document surveys how Dolphin, PCSX2, and DuckStation handle the swap/present
lifecycle in their Vulkan backends, then maps the findings to our Voodoo emulator's
architecture. The core finding: **all three emulators converge on the same pattern --
the render/GPU thread owns the swapchain, performs Vulkan submit + present, and
handles swap-count/frame-pacing internally.** The FIFO/command thread never
directly touches Vulkan presentation.

Our current architecture has a structural mismatch: we try to bolt Vulkan present
onto the SW renderer's `swap_pending`/`swap_count`/display-callback model, which
creates a circular dependency where the FIFO thread blocks on ring backpressure
before reaching the swap command that would unblock it.

---

## 1. Dolphin Emulator (GameCube/Wii)

### 1.1 Threading Model

Dolphin uses a **two-thread model** (dual-core mode):

| Thread | Role |
|--------|------|
| **CPU Thread** | Runs the PowerPC emulator, feeds FIFO data |
| **GPU Thread** | Runs `RunGpuLoop()`, decodes FIFO opcodes, executes rendering, **performs present** |

There is no separate "render thread" -- the GPU thread IS the render thread. It both
decodes FIFO commands AND submits Vulkan work.

**Source**: [Fifo.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/Fifo.cpp)

### 1.2 Swap/Present Path

The swap lifecycle in Dolphin:

```
1. Game writes EFB copy command to FIFO
2. GPU thread decodes FIFO, encounters EFB copy → XFB address
3. GPU thread copies rendered framebuffer to XFB texture cache entry
4. VideoInterface timer fires on CPU thread at each VI scanline
5. At VBlank (line == v_disp), VideoInterface calls Video_OutputXFB()
6. Video_OutputXFB pushes an async event to the GPU thread
7. GPU thread's PullEvents() dequeues the swap request
8. Presenter::ViSwap() fetches the XFB texture, calls Present()
9. Present() acquires swapchain image, blits XFB → backbuffer, calls PresentBackbuffer()
10. Vulkan backend: SubmitCommandBuffer() does vkQueueSubmit + vkQueuePresentKHR
```

Key insight: **The VideoInterface (emulated display controller) triggers swap via
a timer, NOT via the FIFO.** The FIFO thread never directly initiates a present.
The VI timer fires at the emulated refresh rate (e.g., 60Hz for NTSC) regardless
of how fast the FIFO processes commands.

**Source**: [Present.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/Present.cpp),
[VideoInterface.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/VideoInterface.cpp)

### 1.3 Command Buffer + Present Coupling

Dolphin's `CommandBufferManager` **batches submit and present into one operation**:

```cpp
struct PendingCommandBufferSubmit {
    VkSwapchainKHR present_swap_chain;
    u32 present_image_index;
    u32 command_buffer_index;
};
```

When `SubmitCommandBuffer()` is called with a non-null `present_swap_chain`:
1. `vkQueueSubmit()` submits the command buffer with a render-finished semaphore
2. `vkQueuePresentKHR()` immediately follows, waiting on that semaphore

Both calls happen on the **same thread** (either the GPU thread directly, or
Dolphin's optional submission worker thread). Present is never split across threads.

**Source**: [CommandBufferManager.h](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/Vulkan/CommandBufferManager.h),
[CommandBufferManager.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/Vulkan/CommandBufferManager.cpp)

### 1.4 Backpressure (CPU ↔ GPU)

Dolphin uses an `m_sync_ticks` atomic counter to track CPU-GPU distance:

- CPU increments `sync_ticks` as it feeds FIFO data
- GPU decrements `sync_ticks` as it processes opcodes
- When distance exceeds `m_config_sync_gpu_max_distance`, GPU wakes CPU via event
- When distance drops below `m_config_sync_gpu_min_distance`, CPU blocks

This backpressure operates on the **FIFO level** (opcode processing), not on the
present/swap level. Dolphin does NOT have a swap_count mechanism -- the GameCube
hardware has a single EFB→XFB copy path, and the VideoInterface scans out at a
fixed rate regardless.

**Source**: [Fifo.h](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/Fifo.h)

### 1.5 Frame Pacing

Dolphin's `Presenter::Present()` handles frame pacing:

```cpp
{
    std::lock_guard<std::mutex> guard(m_swap_mutex);
    if (present_info != nullptr) {
        const auto present_time = GetUpdatedPresentationTime(
            present_info->intended_present_time);
        Core::System::GetInstance().GetCoreTiming().SleepUntil(present_time);
        present_info->actual_present_time = Clock::now();
    }
    g_gfx->PresentBackbuffer();
}
```

Frame pacing uses `SleepUntil()` to delay presentation to the intended time,
smoothing frame delivery. The `m_swap_mutex` protects the timing + present
as an atomic operation.

### 1.6 MoltenVK Handling

Dolphin supports macOS via MoltenVK. Key MoltenVK-specific handling:

- Uses 3 swapchain images (triple buffering) for `VK_PRESENT_MODE_FIFO_KHR`
- Handles `VK_SUBOPTIMAL_KHR` by deferring recreation to next frame
- The GPU thread owns swapchain, so no cross-thread races on macOS
- No special `CAMetalDrawable` handling -- MoltenVK manages this internally

---

## 2. PCSX2 (PlayStation 2)

### 2.1 Threading Model

PCSX2 uses the **MTGS (Multi-Threaded Graphics Subsystem)** pattern:

| Thread | Role |
|--------|------|
| **EE Thread** | Runs the PS2's Emotion Engine CPU, writes GS packets to ring buffer |
| **GS Thread** | Processes GS packets from ring buffer, renders, **performs present** |

Like Dolphin, the GS thread IS the render thread. No separate present thread.

**Source**: [MTGS.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MTGS.cpp)

### 2.2 Ring Buffer Architecture

PCSX2 uses a fixed-size ring buffer (`RingBufferSize` entries of `u128`) with:

- **Cache-line aligned** atomic read/write positions to prevent false sharing
- **Commands tagged by type**: VSync, GSPacket, Reset, AsyncCall, etc.
- **VSync command** (`Command::VSync`) carries a snapshot of GS register state

```cpp
// EE thread writes VSync command with full register state
void MTGS::PostVsyncStart(bool registers_written) {
    uint packsize = sizeof(RingCmdPacket_Vsync) / 16;
    PrepDataPacket(Command::VSync, packsize);
    MemCopy_WrappedDest((u128*)PS2MEM_GS, RingBuffer.m_Ring,
                        s_packet_writepos, RingBufferSize, 0xf);
    SendDataPacket();
    s_QueuedFrameCount.fetch_add(1);
}
```

### 2.3 Backpressure (Ring Full)

When the EE thread fills the ring, `GenericStall()` blocks:

```cpp
void MTGS::GenericStall(uint size) {
    if (freeroom <= size) {
        uint somedone = (RingBufferSize - freeroom) / 4;
        s_SignalRingPosition.store(somedone, std::memory_order_release);
        while (true) {
            s_SignalRingEnable.store(true, std::memory_order_release);
            SetEvent();
            s_sem_OnRingReset.Wait();  // Block until GS drains buffer
            if (freeroom > size) break;
        }
    }
}
```

This is essentially the same pattern as our SPSC ring backpressure. The crucial
difference: **the VSync command is just another ring command**, so it can always
be enqueued even when the ring has been busy processing draw commands.

### 2.4 Frame Pacing via VsyncQueueSize

PCSX2 limits how many frames can be queued ahead:

```cpp
if ((s_QueuedFrameCount.fetch_add(1) < EmuConfig.GS.VsyncQueueSize)) {
    return;  // EE thread continues
}
s_VsyncSignalListener.store(true, std::memory_order_release);
s_sem_Vsync.Wait();  // EE thread sleeps until GS processes a frame
```

When `VsyncQueueSize` is exceeded (typically 1 or 2), the EE thread blocks on
a semaphore until the GS thread processes a VSync command and posts back:

```cpp
case Command::VSync:
    GSvsync(field, registers_written);
    s_QueuedFrameCount.fetch_sub(1);
    if (s_VsyncSignalListener.exchange(false))
        s_sem_Vsync.Post();  // Wake EE if throttled
```

**Key lesson**: Frame pacing is controlled by counting VSync commands in the ring,
NOT by stalling on ring capacity. The VSync command is lightweight and always fits.

### 2.5 VSync → Present

When the GS thread processes a VSync command:

1. Calls `GSvsync()` which triggers the renderer's frame presentation
2. The Vulkan backend does `vkQueueSubmit` + `vkQueuePresentKHR` from the GS thread
3. The GS thread owns the VkDevice, VkQueue, and VkSwapchainKHR
4. Present result (`VK_SUCCESS`, `VK_SUBOPTIMAL_KHR`, `VK_ERROR_OUT_OF_DATE_KHR`)
   is handled immediately on the GS thread

---

## 3. DuckStation (PlayStation 1)

### 3.1 Threading Model

DuckStation uses a **VideoThread** (renamed from GPUThread):

| Thread | Role |
|--------|------|
| **CPU Thread** | Runs PS1 CPU emulation, pushes GPU commands to FIFO |
| **Video Thread** | Processes GPU commands, renders, **performs present** |

Same pattern as Dolphin and PCSX2.

**Source**: [video_thread.h](https://github.com/stenzek/duckstation/blob/master/src/core/video_thread.h),
[video_thread.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/video_thread.cpp)

### 3.2 Command FIFO

DuckStation uses a **16 MB circular FIFO** (`COMMAND_QUEUE_SIZE = 16 * 1024 * 1024`)
with atomic read/write pointers:

```cpp
VideoThreadCommand* VideoThread::AllocateCommand(VideoThreadCommandType command, u32 size) {
    for (;;) {
        u32 read_ptr = s_state.command_fifo_read_ptr.load(std::memory_order_acquire);
        u32 write_ptr = s_state.command_fifo_write_ptr.load(std::memory_order_relaxed);
        if (read_ptr > write_ptr) {
            u32 available_size = read_ptr - write_ptr;
            while (available_size < size) {
                WakeThreadIfSleeping();
                read_ptr = s_state.command_fifo_read_ptr.load(std::memory_order_acquire);
                // ... spin until space available
            }
        }
        // Handle wraparound with dummy Wraparound command
    }
}
```

### 3.3 Command Types

```cpp
enum VideoThreadCommandType {
    Wraparound, AsyncCall, AsyncBackendCall, Reconfigure, UpdateSettings,
    UpdateGameInfo, Shutdown, ClearVRAM, ClearDisplay, UpdateDisplay,
    SubmitFrame, BufferSwapped, LoadState, LoadMemoryState, SaveMemoryState,
    ReadVRAM, FillVRAM, UpdateVRAM, CopyVRAM, SetDrawingArea, UpdateCLUT,
    ClearCache, DrawPolygon, DrawPrecisePolygon, DrawRectangle, DrawLine,
    DrawPreciseLine
};
```

Frame boundaries are marked by **`SubmitFrame`** and **`BufferSwapped`** commands.
These are interspersed with draw commands in the same ring buffer.

### 3.4 Present Path

The Video Thread processes commands in a tight loop. When no commands remain,
it enters idle mode and handles presentation:

```cpp
void VideoThread::Internal::DoRunIdle() {
    if (!g_gpu_device->HasMainSwapChain()) {
        Timer::NanoSleep(16 * 1000 * 1000);  // 16ms sleep
        return;
    }
    if (!PresentFrameAndRestoreContext()) return;
    if (g_gpu_device->GetMainSwapChain()->IsVSyncModeBlocking())
        return;
    VideoPresenter::ThrottlePresentation();
}
```

### 3.5 Frame Pacing

DuckStation uses `VideoPresenter` for frame pacing:

```cpp
bool VideoPresenter::ShouldPresentFrame(u64 present_time) {
    const float throttle_period = 1.0f / throttle_rate;
    const double diff = Timer::ConvertValueToSeconds(
        wanted_time - s_locals.last_present_time);
    if (diff >= throttle_period ||
        s_locals.skipped_present_count >= MAX_SKIPPED_PRESENT_COUNT)
    {
        s_locals.skipped_present_count = 0;
        return true;
    }
    s_locals.skipped_present_count++;
    return false;
}
```

Frame skipping caps at 50 consecutive frames, with throttle period based on
display refresh rate. The Video Thread sleeps until the next presentation time
when running ahead.

### 3.6 Timed Present Support

DuckStation supports three present timing modes:

1. **Standard**: Flush + sleep + present (client-side timing)
2. **Explicit present**: Sleep + `SubmitPresent()` (driver-queued)
3. **Timed present**: Let the GPU schedule presentation at a specific time

```cpp
if (scheduled_present && !explicit_present) {
    g_gpu_device->FlushCommands();
    SleepUntilPresentTime(present_time);
}
g_gpu_device->EndPresent(swap_chain, explicit_present,
    timed_present ? present_time : 0);
if (explicit_present) {
    SleepUntilPresentTime(present_time);
    g_gpu_device->SubmitPresent(swap_chain);
}
```

---

## 4. RetroArch (Multi-system)

### 4.1 Threaded Image Acquisition

RetroArch addresses a specific MoltenVK issue: `vkAcquireNextImageKHR()` can
block indefinitely. Their solution is the `vulkan_emulated_mailbox` pattern:

- A **dedicated worker thread** continuously calls `vkAcquireNextImageKHR()`
- The main thread checks for available images without blocking
- Mutex + condition variable coordinate between threads

This prevents the emulation thread from stalling when the GPU hasn't released
a swapchain image yet. This is most relevant on macOS where `[CAMetalLayer
nextDrawable]` can block for up to 1 second when the app is backgrounded.

**Source**: [Vulkan Graphics System - RetroArch](https://deepwiki.com/libretro/RetroArch/5.1-vulkan-graphics-system)

---

## 5. Common Patterns Across All Emulators

### 5.1 Single Thread Owns Everything

| Emulator | "GPU Thread" | Owns VkDevice? | Owns VkQueue? | Owns Swapchain? | Does Present? |
|----------|--------------|-----------------|---------------|-----------------|---------------|
| Dolphin  | GPU Thread   | Yes | Yes | Yes | Yes |
| PCSX2    | GS Thread    | Yes | Yes | Yes | Yes |
| DuckStation | Video Thread | Yes | Yes | Yes | Yes |
| RetroArch | Main/Video Thread | Yes | Yes | Yes | Yes |
| **86Box (current)** | **VK Render Thread** | **Shared** | **Shared** | **GUI Thread** | **Split** |

Every production emulator has **one thread** that owns all Vulkan resources and
performs both rendering and presentation. Our current split (GUI thread owns
swapchain, render thread does vkQueueSubmit, GUI thread does vkQueuePresentKHR
via present channel) is architecturally unique and problematic.

### 5.2 Swap Command = Ring Command, Not Blocking Operation

All emulators treat the guest's "swap buffer" command as **just another command
in the ring buffer**, not as a blocking synchronization point:

- **Dolphin**: EFB copy is a FIFO opcode; VI triggers present via timer
- **PCSX2**: VSync is a ring command (`Command::VSync`) with a queued frame counter
- **DuckStation**: `BufferSwapped` and `SubmitFrame` are ring command types

None of them have the FIFO thread block on a swap_pending flag waiting for
the display callback. The equivalent of our `voodoo_wait_for_swap_complete()`
does not exist in their architectures.

### 5.3 Frame Pacing is Separate from FIFO Processing

Frame pacing (deciding when to present) is decoupled from FIFO processing:

- **Dolphin**: VI timer fires at 60Hz, triggers present independently of FIFO
- **PCSX2**: `VsyncQueueSize` limits how far ahead the EE can get
- **DuckStation**: `ThrottlePresentation()` sleeps to match target refresh rate

The FIFO/command thread can run as fast as it wants. Frame pacing throttles the
**producer** (CPU thread) not the **consumer** (GPU thread).

---

## 6. Analysis: Our Current Architecture vs Proven Patterns

### 6.1 Our Current Swap Lifecycle

```
FIFO Thread (voodoo_reg.c):
1. Encounters SST_swapbufferCMD
2. voodoo->swap_count++ (at register write time, vid_voodoo.c:705)
3. Queues command to FIFO

FIFO Thread (voodoo_fifo.c, processing the queued command):
4. SW path: sets swap_pending=1, calls voodoo_wait_for_swap_complete()
   - Spins waiting for display callback to clear swap_pending
   VK path: calls vc_voodoo_swap_buffers(), immediately decrements swap_count

Display Callback (voodoo_display.c, called from emulated VGA timer):
5. At v_disp (VBlank): checks swap_pending && retrace_count > swap_interval
6. If swap ready: swap_count--, swap_pending=0, wakes FIFO thread
```

### 6.2 The Deadlock Pattern

The deadlock occurs in the VK path when:

```
FIFO Thread                              VK Render Thread
============================================  ==========================================
1. Process triangle commands
   → push to SPSC ring via vc_thread_push()
2. SPSC ring fills up (1024 batch limit)
   → vc_thread_push() blocks (backpressure)
                                              3. Processing ring commands, rendering
                                                 triangles (takes time)
[FIFO Thread is now BLOCKED on ring]
[swap_count remains stuck at 3]
[Guest polls SST_status, sees busy]

4. FIFO Thread cannot reach SST_swapbufferCMD
   because it's stuck on ring backpressure
   from triangle commands BEFORE the swap

5. SW path: display callback would decrement
   swap_count when swap_pending is set...
   but swap_pending is never set because FIFO
   thread never reaches the swap command!

6. VK path: VK path does swap_count-- at
   the swap command itself... which the FIFO
   thread can't reach!
```

**Root cause**: In the VK path, `swap_count--` happens at the swap command in the
FIFO thread. But the FIFO thread blocks on SPSC ring backpressure from the
triangle commands BEFORE it reaches the swap command. The render thread is busy
processing those triangles and doesn't know about the pending swap.

In the SW path, `swap_pending` is set and the display callback decrements
`swap_count` asynchronously. But in the VK path, we bypass `swap_pending` entirely
and do the decrement in-line. So if the FIFO thread can't reach the swap command,
`swap_count` never decrements.

### 6.3 Why Other Emulators Don't Have This Problem

1. **No separate FIFO thread from GPU thread**: In Dolphin/PCSX2/DuckStation,
   the thread that processes FIFO commands IS the thread that does rendering.
   There is no SPSC ring between them. No ring backpressure possible.

2. **Swap is decoupled from FIFO**: In Dolphin, the VI timer triggers present
   independently. In PCSX2, VSync is just another lightweight ring command.
   The swap command never blocks on rendering.

3. **No swap_count / swap_pending at the GPU layer**: These emulators handle
   frame pacing at a higher level (VsyncQueueSize, ThrottlePresentation)
   rather than having the FIFO thread wait for a display callback.

---

## 7. Recommended Architecture for 86Box VideoCommon

### 7.1 Option A: Render Thread Does Swap (Recommended)

Move swap_count decrement to the **render thread**, not the FIFO thread.
Add `VC_CMD_SWAP` as a ring command:

```
FIFO Thread:
1. Encounters SST_swapbufferCMD
2. swap_count++ (already done at register write time)
3. Push VC_CMD_SWAP to SPSC ring (lightweight, always fits)
4. Do NOT wait for swap, do NOT touch swap_count here
5. Continue processing next FIFO command immediately

VK Render Thread:
6. Processes VC_CMD_DRAW, VC_CMD_DRAW, VC_CMD_DRAW...
7. Encounters VC_CMD_SWAP in the ring
8. vc_swap_buffers() -- swap front/back color images
9. swap_count-- (thread-safe via swap_mutex)
10. Optionally: trigger present to display

Display Callback (v_disp timer):
11. No longer involved in swap_count for VK path
12. Still handles VGA passthrough display
```

**Advantages**:
- Ring command ordering guarantees swap happens after all preceding draws
- No FIFO thread blocking -- it pushes swap and continues
- Matches PCSX2's VSync-as-ring-command pattern
- swap_count always decrements when the render thread reaches the swap point
- No deadlock possible: swap command is tiny, always fits in ring

**Risks**:
- swap_count decrement is delayed until render thread processes the command
- If render thread is slow, swap_count might briefly be higher than actual
- Guest polling SST_status might see swap_count=3 slightly longer

**Mitigation**: This is acceptable because:
- The guest already tolerates non-zero swap_count (it polls in a loop)
- The delay is bounded by render thread processing time
- When the render thread catches up, swap_count drops naturally

### 7.2 Option B: Immediate Swap_Count Decrement + Async Swap

Keep the FIFO thread decrementing swap_count immediately (current behavior),
but don't have the FIFO thread block on `voodoo_wait_for_swap_complete()`:

```
FIFO Thread:
1. Encounters SST_swapbufferCMD
2. swap_count++ (register write time)
3. Push VC_CMD_SWAP to SPSC ring
4. swap_count-- (immediately)
5. Continue processing
```

This is essentially what the current VK path does:

```c
vc_voodoo_swap_buffers(voodoo);

thread_wait_mutex(voodoo->swap_mutex);
if (voodoo->swap_count > 0)
    voodoo->swap_count--;
thread_release_mutex(voodoo->swap_mutex);
```

**The problem is NOT swap_count management -- it's that the FIFO thread blocks
on vc_thread_push() backpressure BEFORE reaching the swap command.**

### 7.3 Option C: Ring Size Increase + High-Water Mark (Simplest Fix)

If the deadlock is purely from ring backpressure preventing the FIFO thread
from reaching the swap command, the simplest fix is:

1. **Increase ring size** to hold more batches (e.g., 4096 instead of 1024)
2. **Reserve ring slots for swap commands**: When the FIFO thread encounters
   a swap command, use a separate "priority" push that bypasses backpressure
3. **Non-blocking push**: Add a `vc_thread_try_push()` that returns immediately
   if the ring is full, with the FIFO thread yielding briefly

This doesn't fix the architectural issue but delays the symptom.

### 7.4 Option D: Merge FIFO + Render Thread (Nuclear Option)

Like Dolphin/PCSX2/DuckStation, have the FIFO thread do Vulkan rendering
directly, eliminating the SPSC ring entirely:

```
FIFO Thread (becomes the "GPU Thread"):
1. Process FIFO commands
2. For triangle: immediately record Vulkan draw call
3. For swap: vkQueueSubmit + vkQueuePresentKHR
```

**Advantages**: Eliminates all SPSC ring complexity, matches proven architecture.
**Disadvantages**: Major refactor; FIFO thread would need to own VkDevice;
threading model change affects all of 86Box's Voodoo emulation.

### 7.5 Recommendation

**Option A is the right approach.** It:

- Matches PCSX2's proven pattern (swap-as-ring-command)
- Requires minimal refactoring (add VC_CMD_SWAP, move swap_count-- to render thread)
- Eliminates the deadlock structurally
- Keeps our existing FIFO thread / render thread separation
- Works with both Voodoo 1/2 and Banshee/V3

The key change: **the FIFO thread must NEVER block between triangle commands
and the swap command.** By pushing VC_CMD_SWAP as a ring command and not
decrementing swap_count on the FIFO thread, we decouple swap from FIFO progress.

---

## 8. Detailed Implementation Plan for Option A

### 8.1 New Ring Command: VC_CMD_SWAP

```c
// In vc_thread.h
#define VC_CMD_SWAP 0x08

// In vc_thread.c, render thread loop:
case VC_CMD_SWAP:
    vc_swap_buffers(ctx);
    // Decrement swap_count for the associated voodoo_t
    // The voodoo_t pointer is passed as command payload
    {
        voodoo_t *voodoo = (voodoo_t *)cmd->payload;
        thread_wait_mutex(voodoo->swap_mutex);
        if (voodoo->swap_count > 0)
            voodoo->swap_count--;
        thread_release_mutex(voodoo->swap_mutex);
        voodoo->frame_count++;
    }
    break;
```

### 8.2 FIFO Thread Change (vid_voodoo_reg.c)

Replace current VK swap path:

```c
// CURRENT (problematic):
if (voodoo->use_gpu_renderer && vc_ctx) {
    vc_voodoo_swap_buffers(voodoo);
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_count > 0)
        voodoo->swap_count--;
    thread_release_mutex(voodoo->swap_mutex);
    // ... buffer management ...
    voodoo->frame_count++;
}

// NEW (deferred to render thread):
if (voodoo->use_gpu_renderer && vc_ctx) {
    // Push swap as ring command -- render thread will handle it
    vc_voodoo_push_swap(voodoo);
    // Do buffer rotation immediately (these are CPU-side bookkeeping)
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
    // NOTE: swap_count-- and frame_count++ deferred to render thread
    voodoo->cmd_read++;
    break;
}
```

### 8.3 Present Integration

The render thread's VC_CMD_SWAP handler should also trigger display update:

```c
case VC_CMD_SWAP:
    vc_swap_buffers(ctx);
    // Signal that a new frame is available for display
    // The GUI thread's present() can pick this up
    ctx->frame_ready = 1;
    // ... swap_count management ...
    break;
```

### 8.4 Ring Priority for Swap Commands

To prevent swap commands from being blocked by a full ring, consider:

1. **Reserve 1 slot** in the ring for swap commands (ring capacity = N-1 for
   draws, last slot reserved for swap)
2. Or simply ensure the ring is large enough that it never fills between
   consecutive swap commands (typical Voodoo frame is ~2000-5000 triangles,
   ring capacity of 4096 batches is more than sufficient)

---

## 9. MoltenVK-Specific Findings

### 9.1 vkAcquireNextImageKHR Blocking

MoltenVK's `vkAcquireNextImageKHR` calls `[CAMetalLayer nextDrawable]` which
can block:

- **Normally**: Blocks briefly until GPU releases a drawable (~0-2ms)
- **Backgrounded app**: Blocks up to 1 second (Apple's throttling)
- **GPU overload**: Blocks until a drawable becomes available

**Mitigation**: Like RetroArch, we could use a dedicated acquisition thread.
But since our present happens on the render thread (with Option A), blocking
on acquire only stalls the render thread, not the FIFO thread. This is
acceptable for our use case.

### 9.2 Swapchain Image Count

MoltenVK supports 2 or 3 swapchain images. Recommended: **3 images** for
`VK_PRESENT_MODE_FIFO_KHR` to ensure one image is always available during
rendering while the other two are in the present queue.

### 9.3 No Lazy Drawable Configuration

Unlike some documentation suggests, MoltenVK does NOT have a
`MVK_CONFIG_SWAPCHAIN_IMAGE_AVAILABILITY` setting. Drawable acquisition is
always done at `vkAcquireNextImageKHR` time, which internally calls
`[CAMetalLayer nextDrawable]`.

### 9.4 Swapchain Destruction Safety

Our existing research (swapchain-lifetime.md) covers this extensively.
The key point: with Option A, the render thread owns the swap lifecycle,
so we can do `vkDeviceWaitIdle()` on the render thread before swapchain
destruction without cross-thread races.

---

## 10. References

### Dolphin
- [Fifo.cpp - GPU loop](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/Fifo.cpp)
- [Present.cpp - ViSwap/Present](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/Present.cpp)
- [CommandBufferManager.h](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/Vulkan/CommandBufferManager.h)
- [CommandBufferManager.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/Vulkan/CommandBufferManager.cpp)
- [VideoInterface.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/VideoInterface.cpp)
- [Hybrid XFB blog post](https://dolphin-emu.org/blog/2017/11/19/hybridxfb/)
- [Video Backend Unification blog](https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/)
- [AsyncRequests cleanups PR](https://github.com/dolphin-emu/dolphin/pull/13423)
- [Present semaphore fix PR](https://github.com/dolphin-emu/dolphin/pull/13805)

### PCSX2
- [MTGS.cpp - Ring buffer + VSync](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MTGS.cpp)
- [Vulkan renderer PR](https://github.com/PCSX2/pcsx2/pull/5224)
- [GS thread documentation](https://wiki.pcsx2.net/index.php/PCSX2_Documentation/Graphics_Synthesizer,_GPUs_and_Dual_Cores)

### DuckStation
- [video_thread.h](https://github.com/stenzek/duckstation/blob/master/src/core/video_thread.h)
- [video_thread.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/video_thread.cpp)
- [video_thread_commands.h](https://github.com/stenzek/duckstation/blob/master/src/core/video_thread_commands.h)
- [video_presenter.h](https://github.com/stenzek/duckstation/blob/master/src/core/video_presenter.h)
- [video_presenter.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/video_presenter.cpp)

### RetroArch
- [Vulkan Graphics System](https://deepwiki.com/libretro/RetroArch/5.1-vulkan-graphics-system)

### MoltenVK
- [MoltenVK Whats_New.md](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/Whats_New.md)
- [MoltenVK Runtime User Guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
- [Issue #2031 - Animated resize crash](https://github.com/KhronosGroup/MoltenVK/issues/2031)
- [Apple Developer Forums - nextDrawable blocking](https://developer.apple.com/forums/thread/722434)

### Vulkan Specification
- [Synchronization in Vulkan (KDAB)](https://www.kdab.com/synchronization-in-vulkan/)
- [Vulkan Synchronization Examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
- [Swapchain Semaphore Reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
