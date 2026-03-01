# Emulator GPU Threading Models -- Research for VideoCommon v2

**Date:** 2026-03-01
**Purpose:** Foundation research for VideoCommon v2 threading and synchronization architecture.
**Context:** V1 failed primarily due to swap/sync/display lifecycle bugs. This document surveys
how proven emulators solve the same problems to inform a correct v2 design.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Dolphin (GameCube/Wii)](#2-dolphin-gamecubewii)
3. [PCSX2 (PlayStation 2)](#3-pcsx2-playstation-2)
4. [DuckStation (PlayStation 1)](#4-duckstation-playstation-1)
5. [MAME (Arcade)](#5-mame-arcade)
6. [MoltenVK (macOS) Specifics](#6-moltenvk-macos-specifics)
7. [Vulkan Spec Constraints](#7-vulkan-spec-constraints)
8. [Cross-Emulator Comparison Table](#8-cross-emulator-comparison-table)
9. [Universal Patterns](#9-universal-patterns)
10. [Implications for VideoCommon v2](#10-implications-for-videocommon-v2)

---

## 1. Executive Summary

**The single most important finding: every major emulator with a Vulkan backend uses ONE
thread that owns the VkDevice, VkQueue, and swapchain, and performs both rendering AND
presentation. No emulator splits VkQueue ownership across threads.**

Key universal patterns:
- **One GPU-owner thread** handles all Vulkan/GPU work (submit, present, swapchain lifecycle)
- **Producer-consumer ring buffer** between CPU/emulation thread and GPU-owner thread
- **Frame pacing throttles the PRODUCER** (CPU thread), not the consumer (GPU thread)
- **Swap/VSync is a ring command** processed by the GPU thread, not a cross-thread signal
- **Backpressure uses atomic counters + semaphores**, not mutexes

---

## 2. Dolphin (GameCube/Wii)

### Sources
- `Source/Core/VideoCommon/Fifo.cpp` -- GPU thread main loop
- `Source/Core/VideoCommon/Present.cpp` -- Presentation/frame pacing
- `Source/Core/VideoBackends/Vulkan/CommandBufferManager.cpp` -- VkQueue submission
- `Source/Core/VideoBackends/Vulkan/VulkanContext.h` -- Device/queue ownership
- Bug [#12961](https://bugs.dolphin-emu.org/issues/12961) -- Threaded submission not parallel
- PR [#13387](https://github.com/dolphin-emu/dolphin/pull/13387) -- Frame pacing fix
- PR [#12035](https://github.com/dolphin-emu/dolphin/pull/12035) -- Closed-loop latency control

### Threading Model

Dolphin has THREE threads relevant to graphics:

| Thread | Role | VkQueue Access |
|--------|------|---------------|
| **CPU Thread** | Emulates CPU, writes to FIFO | None |
| **GPU Thread** | Processes FIFO commands, records command buffers, triggers present | Primary owner |
| **VK Submission Thread** (optional) | Calls vkQueueSubmit + vkQueuePresentKHR | Deferred calls only |

**GPU Thread (`RunGpuLoop` in Fifo.cpp):**
- Continuously processes FIFO commands via `OpcodeDecoder::RunFifo()`
- Also pulls async requests from CPU thread via `AsyncRequests::GetInstance()->PullEvents()`
- Runs in a `BlockingLoop` that sleeps when FIFO is empty (wakes every 100ms)
- Records Vulkan command buffers as it processes FIFO commands

**VK Submission Thread (optional, `CommandBufferManager`):**
- When `use_threaded_submission` is enabled, a `WorkQueueThreadSP` processes submission
- Calls BOTH `vkQueueSubmit()` and `vkQueuePresentKHR()` from the same thread
- Uses atomic `waiting_for_submit` flag with release/acquire memory ordering
- GPU thread can spin-wait for submission completion when needed

**Critical lesson from Bug #12961:** The threaded submission was found to NOT actually run
in parallel. The GPU thread waited on the submission thread after almost every submission,
defeating the purpose. The root cause: `WaitForCommandBufferCompletion` forced a sync with
the submission thread before checking fence status.

### FIFO Processing vs Rendering: Same Thread

The GPU thread both decodes FIFO commands AND records Vulkan command buffers. There is
no separation between "command processing" and "rendering" -- they are interleaved on the
same thread.

```
GPU Thread Loop (simplified):
  while (running) {
    PullEvents()                    // Handle async requests from CPU
    if (fifo_has_data) {
      ReadDataFromFifo()            // Copy from FIFO hardware regs
      OpcodeDecoder::RunFifo()      // Decode + record VK commands
      UpdateFifoPointers()
    } else {
      Sleep(100ms)
    }
  }
```

### Swap/VSync: VI Interrupt-Driven

Dolphin does NOT use a "swap command" in the FIFO. Instead:

1. The GameCube/Wii VI (Video Interface) hardware fires an interrupt at vsync rate
2. The VI interrupt handler calls `Presenter::ViSwap()` with XFB address and dimensions
3. `ViSwap()` calls `Present()` which calls `g_gfx->PresentBackbuffer()`
4. This ultimately calls `SubmitCommandBuffer()` with the swap chain info
5. `SubmitCommandBuffer()` calls `vkQueueSubmit()` then `vkQueuePresentKHR()`

Both vkQueueSubmit and vkQueuePresentKHR happen atomically in the same call, either
on the GPU thread directly or on the submission thread.

### Frame Pacing

**Presentation timing (`Present.cpp`):**
```cpp
TimePoint Presenter::GetUpdatedPresentationTime(TimePoint intended) {
  const auto arrival_offset = std::min(now - intended, DT{});
  // Asymmetric smoothing: slow backward, fast forward
  const auto divisor = (arrival_offset < m_presentation_time_offset) ? 100 : 2;
  m_presentation_time_offset += (arrival_offset - m_presentation_time_offset) / divisor;
  return intended + m_presentation_time_offset;
}
```

The system uses `CoreTiming::SleepUntil(present_time)` to throttle the CPU/GPU threads
before presenting, targeting the intended VI refresh rate.

**Closed-loop latency control (PR #12035):**
- Uses `VK_KHR_present_wait` + `VK_KHR_present_id` extensions
- A dedicated worker thread monitors actual presentation time
- Targets 4ms gap between vkQueueSubmit and actual scanout
- Exponential smoothing with 100ms time constant (~6 frame window)
- Max adjustment: 0.5ms per 16ms interval

**No explicit swap_count:** Dolphin does not have a concept of "swap_count" like 3dfx
Voodoo. The VI interrupt fires at fixed intervals regardless of rendering state. Frame
skip is handled by detecting duplicate XFB addresses.

### Synchronization Between CPU and GPU Threads

- FIFO uses hardware-style head/tail pointers (not a software ring buffer)
- CPU thread writes FIFO data and increments write pointer
- GPU thread reads and processes, incrementing read pointer
- When FIFO is full, CPU stalls (backpressure)
- When FIFO is empty, GPU thread sleeps

### Key Takeaways for VideoCommon v2

1. GPU thread does FIFO processing AND rendering -- no split
2. Submission thread was attempted but proved problematic (bug #12961)
3. Present is triggered by VI interrupt timing, not by FIFO commands
4. vkQueueSubmit and vkQueuePresentKHR are ALWAYS called from the same thread
5. Frame pacing uses wall-clock timing with adaptive smoothing, not GPU fence feedback

---

## 3. PCSX2 (PlayStation 2)

### Sources
- `pcsx2/MTGS.cpp` -- Multi-Threaded Graphics Synthesizer implementation
- `pcsx2/GS/Renderers/Vulkan/GSDeviceVK.h` -- Vulkan device ownership
- `pcsx2/GS/GS.cpp` -- GS API surface

### Threading Model

PCSX2 has TWO threads relevant to graphics:

| Thread | Role | VkQueue Access |
|--------|------|---------------|
| **EE Thread** | Emulates Emotion Engine CPU, writes to ring buffer | None |
| **GS Thread (MTGS)** | Processes ring buffer, renders, presents | Sole owner |

**The GS thread owns everything:** VkDevice, VkQueue, swapchain, all Vulkan resources.
The EE thread has ZERO direct access to any GPU resources.

### Ring Buffer Architecture

```
Ring Buffer Structure:
  struct BufferedData {
    u128 m_Ring[RingBufferSize];   // Main command ring
    u8   Regs[Ps2MemSize::GSregs]; // GS register shadow
  };
```

- Ring stores 128-bit quadwords (matches PS2's 128-bit bus)
- Two atomic pointers: `s_ReadPos` (GS reads) and `s_WritePos` (EE writes)
- When ReadPos == WritePos, ring is empty
- Wraparound via `RingBufferMask` bitmask
- Commands are tagged with a `Command` enum in a header quadword

### GS Thread Main Loop (MTGS::MainLoop)

```
GS Thread Loop (simplified):
  while (open) {
    // Idle presentation when VM not running
    if (idle && !running) {
      GSPresentCurrentFrame();
      GSThrottlePresentation();
    }

    // Wait for work
    s_sem_event.WaitForWork();

    // Process ring buffer
    while (readPos != writePos) {
      cmd = ring[readPos];
      switch (cmd.type) {
        case Command::GIFPath:
          // Process GIF transfer (draw commands)
          GSgifTransfer(data, size);
          break;
        case Command::VSync:
          // Copy GS register state, call GSvsync()
          GSvsync(field, registers_written);
          s_QueuedFrameCount.fetch_sub(1);
          if (s_VsyncSignalListener.exchange(false))
            s_sem_Vsync.Post();
          break;
        // ... other commands
      }
      readPos = (readPos + cmd.size) & RingBufferMask;
    }

    // Signal ring drain waiters
    if (s_SignalRingEnable) {
      s_SignalRingEnable = false;
      s_sem_OnRingReset.Post();
    }
  }
```

### VSync is a Ring Command

**This is a critical design pattern.** The EE thread posts VSync commands through
`MTGS::PostVsyncStart()`:

```cpp
void MTGS::PostVsyncStart(bool registers_written) {
  uint packsize = sizeof(RingCmdPacket_Vsync) / 16;
  PrepDataPacket(Command::VSync, packsize);
  // ... copy register state into ring ...
  SendDataPacket();

  // Frame pacing: block EE if too many frames queued
  if (s_QueuedFrameCount.fetch_add(1) < EmuConfig.GS.VsyncQueueSize)
    return;  // Don't block, queue has room

  // Block EE thread until GS processes a frame
  s_VsyncSignalListener.store(true, std::memory_order_release);
  s_sem_Vsync.Wait();
}
```

When the GS thread processes the VSync command:
1. It copies register state from the ring buffer
2. Calls `GSvsync()` which triggers rendering + presentation
3. Decrements `s_QueuedFrameCount`
4. If EE is waiting, posts `s_sem_Vsync` to unblock it

### Frame Pacing: VsyncQueueSize

**Backpressure mechanism:**
- `s_QueuedFrameCount` (atomic int) tracks frames in the ring but not yet presented
- `EmuConfig.GS.VsyncQueueSize` (typically 2) sets the maximum
- When `s_QueuedFrameCount >= VsyncQueueSize`, EE thread BLOCKS on `s_sem_Vsync`
- GS thread posts `s_sem_Vsync` after processing each VSync command

This throttles the **producer** (EE), not the consumer (GS). The GS thread never
blocks on frame pacing -- it processes as fast as it can.

### Ring Buffer Backpressure

Separate from frame pacing, the ring buffer itself has backpressure:

```cpp
void MTGS::GenericStall(uint size) {
  uint freeroom = calculate_free_space();
  if (freeroom <= size) {
    // For large stalls: semaphore-based wait
    if (stall_amount > 0x80) {
      s_SignalRingPosition.store(target);
      s_SignalRingEnable.store(true);
      SetEvent();  // Wake GS thread
      s_sem_OnRingReset.Wait();  // EE blocks here
    }
    // For small stalls: spin-wait
    else {
      SetEvent();
      while (not_enough_room) { SpinWait(); }
    }
  }
}
```

### Synchronization Primitives Summary

| Primitive | Type | Purpose |
|-----------|------|---------|
| `s_ReadPos` | `atomic<uint>` | GS thread read position |
| `s_WritePos` | `atomic<uint>` | EE thread write position |
| `s_sem_event` | `WorkSema` | Wakes GS thread when work posted |
| `s_sem_OnRingReset` | `UserspaceSemaphore` | Ring buffer backpressure |
| `s_sem_Vsync` | `UserspaceSemaphore` | Frame pacing backpressure |
| `s_QueuedFrameCount` | `atomic<int>` | Pending frames counter |
| `s_VsyncSignalListener` | `atomic<bool>` | EE waiting for VSync ack |
| `s_SignalRingEnable` | `atomic<bool>` | Ring drain signaling active |

Memory ordering: acquire/release on ring pointers, relaxed on flags.

### VkQueue Ownership

The `GSDeviceVK` class owns all Vulkan state:
- `m_device` (VkDevice)
- `m_graphics_queue`, `m_present_queue`, `m_spin_queue` (VkQueue handles)
- `m_swap_chain` (unique_ptr<VKSwapChain>)
- Per-frame `CommandBuffer` resources with fences + fence counters
- `NUM_COMMAND_BUFFERS = 3` (triple-buffered)

All accessed exclusively from the GS thread. No mutex needed.

### Key Takeaways for VideoCommon v2

1. VSync/swap is a ring command -- GS thread processes it, not a cross-thread signal
2. Frame pacing throttles the EE (producer), not the GS thread (consumer)
3. Two distinct backpressure mechanisms: ring-full AND frame-count
4. GS thread does command processing AND rendering AND presentation
5. All Vulkan objects owned by GS thread -- no sharing
6. UserspaceSemaphore for blocking (not mutex), atomic counters for coordination
7. VsyncQueueSize controls latency-throughput tradeoff (default: 2 frames)

---

## 4. DuckStation (PlayStation 1)

### Sources
- `src/core/video_thread.h` / `video_thread.cpp` -- Video thread implementation
- `src/core/video_thread_commands.h` -- Command type definitions
- `src/core/gpu_backend.h` / `gpu_backend.cpp` -- GPU backend interface
- `src/core/video_presenter.h` / `video_presenter.cpp` -- Presentation
- `src/util/vulkan_device.h` -- Vulkan device ownership

### Threading Model

DuckStation has TWO threads:

| Thread | Role | VkQueue Access |
|--------|------|---------------|
| **CPU/Core Thread** | Emulates R3000A CPU, pushes commands | None (static methods only) |
| **Video Thread** | Processes FIFO, renders, presents | Sole owner |

**Strict thread separation enforced at the API level:** The `GPUBackend` class has no
global instance pointer accessible from the CPU thread. Only static factory/push methods
are callable from the CPU side. This makes it impossible to accidentally access GPU
resources from the wrong thread.

### Command FIFO Architecture

```
FIFO Size: 16 MB ring buffer (COMMAND_QUEUE_SIZE = 16 * 1024 * 1024)

Command Header:
  struct VideoThreadCommand {
    u32 size;
    VideoThreadCommandType type;
  };

Commands are variable-sized, aligned to 16 bytes.
Wraparound handled via dummy Wraparound command when space insufficient.
```

**Complete command type enum (26 types):**
```
Wraparound, AsyncCall, AsyncBackendCall, Reconfigure, UpdateSettings,
UpdateGameInfo, Shutdown, ClearVRAM, ClearDisplay, UpdateDisplay,
SubmitFrame, BufferSwapped, LoadState, LoadMemoryState, SaveMemoryState,
ReadVRAM, FillVRAM, UpdateVRAM, CopyVRAM, SetDrawingArea, UpdateCLUT,
ClearCache, DrawPolygon, DrawPrecisePolygon, DrawRectangle, DrawLine,
DrawPreciseLine
```

Notable: `SubmitFrame` and `BufferSwapped` are ring commands, not cross-thread signals.

### Video Thread Main Loop

```cpp
// VideoThread::Internal::VideoThreadEntryPoint()
for (;;) {
  u32 write_ptr = s_state.command_fifo_write_ptr.load(memory_order_acquire);
  u32 read_ptr = s_state.command_fifo_read_ptr.load(memory_order_relaxed);

  if (read_ptr == write_ptr) {
    if (SleepThread(!s_state.run_idle_flag))
      continue;      // Woken up, check for new work
    else
      DoRunIdle();    // Present idle frames, throttle
  }

  // Process commands until FIFO empty
  while (read_ptr != write_ptr) {
    cmd = fifo[read_ptr];
    if (cmd.type == Wraparound) {
      read_ptr = 0;
      continue;
    }
    HandleCommand(cmd);
    read_ptr += cmd.size;
  }
  s_state.command_fifo_read_ptr.store(read_ptr, memory_order_release);
}
```

### Command Push API (Three Variants)

```cpp
// 1. Async (fire-and-forget, no wake)
static void PushCommand(VideoThreadCommand* cmd);

// 2. Wake (signal video thread after push)
static void PushCommandAndWakeThread(VideoThreadCommand* cmd);

// 3. Sync (block until video thread processes command)
static void PushCommandAndSync(VideoThreadCommand* cmd, bool spin);
```

Commands are allocated via factory methods:
```cpp
static GPUBackendDrawPolygonCommand* NewDrawPolygonCommand(u32 num_vertices);
static GPUBackendSubmitFrameCommand* NewSubmitFrameCommand();
```

### Thread Wake Mechanism

Uses an atomic counter with semaphore:
```cpp
void VideoThread::WakeThread() {
  // Increment by 2; if was sleeping (negative), post semaphore
  if (s_state.thread_wake_count.fetch_add(2, memory_order_release) < 0)
    s_state.thread_wake_semaphore.Post();
}
```

- Sleeping state: `thread_wake_count = -1`
- Active state: `thread_wake_count >= 0`
- CPU-waiting flag: `0x40000000` bit OR'd in when CPU blocks for sync
- This is lock-free, no mutex involved

### Frame Pacing and Backpressure

**Frame queue throttling:**
```cpp
bool GPUBackend::BeginQueueFrame() {
  const u32 queued_frames = s_core_thread_state.queued_frames.fetch_add(1) + 1;
  if (queued_frames <= g_settings.gpu_max_queued_frames)
    return false;  // Room available, don't block
  // Else: block CPU thread
}
```

**CPU-side wait (when frame queue full):**
```cpp
void GPUBackend::WaitForOneQueuedFrame() {
  u32 expected = CoreThreadState::WAIT_CORE_THREAD_WAITING;
  if (s_core_thread_state.wait_state.compare_exchange_strong(
    expected, CoreThreadState::WAIT_NONE))
    return;  // Already processed
  // Block on semaphore
  s_core_thread_state.video_thread_wait.Wait();
}
```

**GPU-side release (after presenting):**
```cpp
void GPUBackend::ReleaseQueuedFrame() {
  s_core_thread_state.queued_frames.fetch_sub(1);
  // Signal CPU thread via state machine:
  // WAIT_CORE_THREAD_WAITING -> WAIT_VIDEO_THREAD_SIGNALING -> WAIT_VIDEO_THREAD_POSTED
}
```

### Frame Presentation

Presentation occurs through `HandleSubmitFrameCommand()`:
```cpp
void GPUBackend::HandleSubmitFrameCommand(const GPUBackendFramePresentationParameters* cmd) {
  Host::FrameDoneOnVideoThread(this, cmd->frame_number);
  if (cmd->present_frame) {
    bool result = VideoPresenter::PresentFrame(this, cmd->present_time);
    ReleaseQueuedFrame();
  }
}
```

And during idle mode:
```cpp
void VideoThread::Internal::DoRunIdle() {
  if (!PresentFrameAndRestoreContext())
    return;
  if (g_gpu_device->GetMainSwapChain()->IsVSyncModeBlocking())
    VideoPresenter::ThrottlePresentation();
}
```

### Sync Spin Optimization

```cpp
// Platform-tuned spin times before blocking
static constexpr u32 THREAD_SPIN_TIME_US =
  #ifdef _M_X86  // x86
    50;
  #else          // ARM
    200;
  #endif
```

The CPU thread spin-waits for this duration before falling back to semaphore wait.
ARM gets a longer spin time due to higher context-switch cost.

### VkQueue Ownership

`VulkanDevice` class owns:
- `m_device` (VkDevice)
- `m_graphics_queue`, `m_present_queue` (VkQueue)
- Per-frame `CommandBuffer` structs with VkCommandPool, VkFence, fence counter
- `NUM_COMMAND_BUFFERS = 3` (triple-buffered)
- `m_current_swap_chain` (VulkanSwapChain*)

All accessed exclusively from the Video Thread. No cross-thread access.

### Key Takeaways for VideoCommon v2

1. 16MB ring buffer -- generously sized, avoids stalls
2. Variable-sized commands with explicit Wraparound sentinel
3. SubmitFrame is a ring command, not a cross-thread signal
4. Three push variants: async, wake, sync -- different use cases
5. Lock-free wake mechanism using atomic counter + semaphore
6. Frame pacing: atomic queued_frames counter, CPU blocks when full
7. State machine for CPU-GPU sync (no mutex, just atomics + semaphore)
8. Platform-tuned spin times (50us x86, 200us ARM)
9. Static-only CPU-thread API prevents accidental GPU resource access

---

## 5. MAME (Arcade)

### Sources
- `src/devices/video/voodoo.cpp` -- Voodoo emulation (software renderer)
- [MAME FAQ:Performance](https://wiki.mamedev.org/index.php/FAQ:Performance)
- [BGFX Documentation](https://docs.mamedev.org/advanced/bgfx.html)

### Voodoo Emulation: Pure Software

MAME's Voodoo emulation (voodoo.cpp) is entirely software-rendered:
- Up to 3 threads for triangle rasterization (worker pool)
- No GPU acceleration whatsoever
- The software renderer is well-documented with register specs

MAME does NOT offload Voodoo pixel pipeline work to the host GPU. This is notable
because it means there is no prior art for GPU-accelerated Voodoo emulation in MAME
to learn from.

### BGFX Display Path

MAME uses [bgfx](https://github.com/bkaradzic/bgfx) for display output (applying
CRT shaders, etc.) but this is a post-processing display path, not a hardware
emulation accelerator:

- bgfx is declarative: calls record work, `bgfx::frame()` submits to render thread
- Supports Vulkan, D3D9/11/12, Metal, OpenGL backends
- A dedicated texture upload thread moves textures to GPU
- Known issue: 2 frames of additional latency with bgfx renderer
- Thread model: main thread records, render thread executes, texture thread uploads

### Frame Pacing

MAME uses its own throttling system independent of bgfx:
- Internal timer-based throttle to match emulated machine's refresh rate
- VSync can optionally provide additional pacing
- No swap_count concept -- MAME uses a simple "throttle to target framerate" approach

### Key Takeaways for VideoCommon v2

1. No prior art for GPU-accelerated Voodoo emulation in MAME
2. MAME's software Voodoo uses thread pool for rasterization (similar to 86Box)
3. BGFX's "declare then frame()" pattern is analogous to our ring buffer approach
4. Texture upload thread is a proven pattern (matches our VC_CMD_TEXTURE_UPLOAD)
5. 2-frame latency with bgfx is a cautionary note about excessive pipelining

---

## 6. MoltenVK (macOS) Specifics

### Sources
- `MoltenVK/MoltenVK/GPUObjects/MVKSwapchain.mm` -- Swapchain implementation
- [Issue #486](https://github.com/KhronosGroup/MoltenVK/issues/486) -- Present blocks calling thread
- [Issue #803](https://github.com/KhronosGroup/MoltenVK/issues/803) -- Semaphore signaling bug
- [Issue #234](https://github.com/KhronosGroup/MoltenVK/issues/234) -- Surface creation deadlock
- [Issue #2295](https://github.com/KhronosGroup/MoltenVK/issues/2295) -- vkQueuePresentKHR crashes
- [MoltenVK Configuration Parameters](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md)

### Lazy Drawable Acquisition

MoltenVK implements a **deferred/lazy drawable pattern**:

1. `vkAcquireNextImageKHR()` does NOT call `[CAMetalLayer nextDrawable]` immediately
2. It returns an available image index based on internal availability tracking
3. The actual Metal drawable (`CAMetalDrawable`) is acquired lazily when the image
   is used for rendering
4. The `VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_KHR` flag is acknowledged
   but ignored -- MoltenVK ALWAYS defers, since "swapchain image memory allocation
   is provided by a MTLDrawable, which is retrieved lazily"

**Implication:** `vkAcquireNextImageKHR` is cheap on MoltenVK (no Metal drawable yet).
The expensive `nextDrawable` call happens during command buffer execution or present.
If the drawable pool is exhausted (all 3 being composited), this can block.

### vkQueuePresentKHR Blocks the Calling Thread (Issue #486)

**Critical MoltenVK behavior:** `vkQueuePresentKHR` makes the calling CPU thread wait
on `pWaitSemaphores`, rather than only the GPU waiting:

- On native Vulkan drivers, vkQueuePresentKHR returns quickly and the GPU waits
- On MoltenVK, the calling thread blocks until all wait semaphores are signaled
- This destroys CPU-GPU parallelism in triple-buffered scenarios
- Demonstrated: 12ms GPU + 12ms CPU should be 60fps, but drops to 52fps

**Root cause:** `MVKQueuePresentSurfaceSubmission::execute` synchronously waits on
`_waitSemaphores` in the calling thread context.

**Fix status:** Partially fixed with `MTLFence`/`MTLEvent` support. The configuration
parameter `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS` controls the behavior.

### MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS

**Default: 1 (enabled) -- synchronous processing on calling thread**

| Value | Behavior |
|-------|----------|
| `1` (default) | vkQueueSubmit/vkQueuePresentKHR processed on calling thread |
| `0` | Dispatched to GCD serial queue (respects queue priority) |

**For VideoCommon v2:** Since our GPU thread is the sole VkQueue owner, synchronous
mode (default) is fine -- the GPU thread IS the right thread for these calls. No need
to change this setting.

### Surface Creation Must Use CAMetalLayer (Issue #234)

**Deadlock scenario:**
1. Worker thread calls `vkCreateSurface()` which needs to access `NSView`'s layer
2. MoltenVK dispatches to main thread via `mvkDispatchToMainAndWait()`
3. If main thread is blocking waiting for the worker, **deadlock**

**Fix:** Provide a `CAMetalLayer` directly (not an `NSView`). CALayer is not restricted
to main thread access. This was fixed in MoltenVK PR #258.

**For VideoCommon v2:** When creating the Vulkan surface, ensure we pass a CAMetalLayer
(via Qt's surface abstraction), not an NSView. This avoids the main-thread dispatch.

### Semaphore Signaling Bug (Issue #803)

When an image was not immediately available at `vkAcquireNextImageKHR` time:
- The semaphore was queued for later signaling (during next present)
- But the internal counter was not incremented at acquire time
- This desynchronized signal/wait counters, causing GPU deadlocks

**Fix:** Defer signal operations until images are actually available.

**For VideoCommon v2:** Use `MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE = 1` (default,
Metal events where safe). Avoid unusual acquire-before-present patterns.

### Other MoltenVK Configuration Relevant to Threading

| Parameter | Default | Recommendation |
|-----------|---------|----------------|
| `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS` | 1 | Keep (GPU thread is the right thread) |
| `MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE` | 1 | Keep (Metal events where safe) |
| `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS` | 0 | Keep (encode at submit time) |
| `MVK_CONFIG_MAX_ACTIVE_METAL_COMMAND_BUFFERS_PER_QUEUE` | 64 | Keep (generous) |
| `MVK_CONFIG_SWAPCHAIN_MIN_MAG_FILTER_USE_NEAREST` | 1 | Keep (avoid smearing on Retina) |
| `MVK_CONFIG_USE_COMMAND_POOLING` | 1 | Keep (pools reduce alloc overhead) |

### Multi-Threading on MoltenVK: Summary

1. **Single VkQueue owner thread is the correct pattern** -- avoids all MoltenVK threading issues
2. **Surface creation:** Use CAMetalLayer, not NSView, to avoid main-thread deadlock
3. **vkQueuePresentKHR blocking:** Not a problem when GPU thread is sole caller (it should block there anyway)
4. **Drawable timeout:** Can occur if all drawables are in-flight. Triple-buffer (3 swapchain images) is recommended
5. **No need for GCD dispatch:** MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1 is correct for our model

---

## 7. Vulkan Spec Constraints

### Queue External Synchronization

From the Vulkan 1.2 Specification:

> Host access to `queue` must be externally synchronized.
> -- vkQueueSubmit, vkQueuePresentKHR

This means:
- Only ONE thread can call vkQueueSubmit/vkQueuePresentKHR on a given VkQueue at a time
- The application is responsible for serializing access (mutex or single-thread ownership)
- `VK_KHR_internally_synchronized_queues` (proposed extension) would relax this, but is
  not widely available

**Best practice:** Single-thread ownership of VkQueue eliminates the need for any mutex.
This is exactly what all three emulators do.

### vkQueuePresentKHR Threading Issues

From [SDL Issue #11293](https://github.com/libsdl-org/SDL/issues/11293):

> On some drivers/platforms (Android, Wayland, NVIDIA on Windows) PRESENT_MODE_FIFO
> causes vkQueuePresentKHR to have blocking behavior.

SDL's recommended workaround:
> Punt vkQueuePresentKHR to a background thread. Since vkQueueSubmit and
> vkQueuePresentKHR both need to externally synchronize the VkQueue, they should
> really both be deferred to the same background thread.

This validates the "single GPU-owner thread" pattern. If you want present on a different
thread, you must also move submit there -- so just make one thread do both.

### Present Without Fence

Per Vulkan spec, `vkQueuePresentKHR` does NOT signal any fence or semaphore by default.
`vkDeviceWaitIdle` technically cannot guarantee present completion.

**Extension:** `VK_EXT_swapchain_maintenance1` adds present fences (signal when
presentation engine is done with the image). Supported by MoltenVK.

**For VideoCommon v2:** If we need to know when present is done (e.g., for safe swapchain
destroy), use `VK_EXT_swapchain_maintenance1` present fences. Otherwise, vkDeviceWaitIdle
+ small delay is the pragmatic approach.

---

## 8. Cross-Emulator Comparison Table

| Feature | Dolphin | PCSX2 | DuckStation |
|---------|---------|-------|-------------|
| **GPU-owner thread** | GPU Thread | GS Thread (MTGS) | Video Thread |
| **FIFO/command processing** | Same thread as render | Same thread as render | Same thread as render |
| **Ring buffer size** | HW FIFO registers | u128 array (configurable) | 16 MB |
| **Ring element type** | HW FIFO data | 128-bit quadwords | Variable-size commands |
| **VkQueue owner** | GPU thread (or submission sub-thread) | GS thread exclusively | Video thread exclusively |
| **vkQueueSubmit caller** | GPU/submission thread | GS thread | Video thread |
| **vkQueuePresentKHR caller** | Same as submit | GS thread | Video thread |
| **Swap/VSync trigger** | VI interrupt (external timer) | Ring command (VSync) | Ring command (SubmitFrame) |
| **Frame pacing mechanism** | Wall-clock timing + smoothing | Atomic counter + semaphore | Atomic counter + semaphore |
| **Frame queue limit** | N/A (VI-driven) | VsyncQueueSize (default 2) | gpu_max_queued_frames |
| **Backpressure target** | CPU thread sleeps | EE thread blocks on semaphore | Core thread blocks on semaphore |
| **Wake mechanism** | BlockingLoop (100ms poll) | WorkSema | Atomic counter + semaphore |
| **Spin-wait optimization** | No | Yes (SpinWait for small stalls) | Yes (50us x86 / 200us ARM) |
| **Threaded submission** | Optional (buggy, see #12961) | No | No |
| **Swapchain images** | Triple-buffer | Triple-buffer (3) | Triple-buffer (3) |

---

## 9. Universal Patterns

### Pattern 1: Single GPU-Owner Thread

**Every emulator:** One thread owns VkDevice, VkQueue, swapchain, and all GPU resources.
This thread does:
- Command/FIFO processing
- Vulkan command buffer recording
- vkQueueSubmit
- vkQueuePresentKHR
- Swapchain management (create, resize, destroy)

No emulator splits these responsibilities across threads.

### Pattern 2: Producer-Consumer Ring Buffer

**Every emulator:** CPU thread pushes work into a ring buffer. GPU thread consumes.
Ring buffer details vary but the pattern is universal:

```
CPU Thread                     GPU Thread
    |                              |
    |--- push draw commands ------>|
    |--- push draw commands ------>|  (processes commands,
    |--- push VSync/SubmitFrame -->|   records VK cmd bufs,
    |                              |   submits, presents)
    |<-- frame pacing signal ------|
    |                              |
```

### Pattern 3: VSync/Swap as Ring Command

**PCSX2 and DuckStation:** The swap/present/vsync event is itself a command in the ring
buffer. The GPU thread processes it in-order alongside draw commands.

**Dolphin:** Uses VI interrupt timing instead (hardware-driven), but the effect is similar --
presentation is triggered from the GPU thread's processing loop.

**Anti-pattern (VideoCommon v1):** Triggering swap from a different thread (display callback
or GUI thread) and trying to coordinate with the render thread via shared state. This led
to our swap_count deadlock.

### Pattern 4: Throttle the Producer

**Every emulator:** When too many frames are queued, the CPU/producer thread blocks.
The GPU/consumer thread NEVER blocks for pacing -- it runs as fast as it can.

| Emulator | Producer throttle mechanism |
|----------|---------------------------|
| Dolphin | CoreTiming::SleepUntil(present_time) |
| PCSX2 | s_sem_Vsync.Wait() when QueuedFrameCount >= VsyncQueueSize |
| DuckStation | WaitForOneQueuedFrame() when queued_frames > max |

### Pattern 5: Fence Counter Tracking

**PCSX2 and DuckStation:** Use monotonic fence counters (u64) to track GPU completion:
- Each submitted command buffer gets an incrementing fence counter
- `m_completed_fence_counter` updated when fence signals
- Deferred resource destruction keyed to fence counter values
- No need to wait on individual fences -- just compare counters

### Pattern 6: Triple Buffering

All emulators use 3 command buffer sets / swapchain images:
- Frame N being composited/displayed
- Frame N+1 being GPU-executed
- Frame N+2 being CPU-recorded

This allows maximum CPU-GPU overlap while keeping latency manageable.

---

## 10. Implications for VideoCommon v2

### Architecture Recommendation

Based on universal patterns across all emulators:

```
+--------------------+                    +----------------------------+
|   FIFO Thread      |                    |      Render Thread         |
|  (producer)        |                    |  (sole GPU owner)          |
|                    |    Ring Buffer     |                            |
|  Triangle setup    | =================> |  Process ring commands     |
|  Register writes   |   (lock-free,     |  Record VK cmd bufs        |
|  Swap commands     |    SPSC)          |  vkQueueSubmit             |
|                    |                    |  vkQueuePresentKHR         |
|  Blocks when       |                    |  Swapchain management      |
|  ring full OR      | <================= |  Frame counter decrement   |
|  frame queue       |   (atomic signal)  |  Never blocks for pacing   |
|  saturated         |                    |                            |
+--------------------+                    +----------------------------+
```

### Specific Recommendations

**R1: Make VC_CMD_SWAP a ring command (matches PCSX2/DuckStation pattern)**
- FIFO thread pushes VC_CMD_SWAP into the ring buffer
- Render thread processes it, does vkQueuePresentKHR
- This eliminates the v1 deadlock where FIFO stalled before reaching swap

> **Note (DESIGN.md deviation):** The original R1 text recommended that the render thread
> decrement swap_count after present. DESIGN.md v2 deliberately deviates from this:
> swap_count is left entirely to the existing display callback (timer/CPU thread).
> The PCSX2/DuckStation pattern assumes no independent retrace timing system -- those
> consoles' VSync is synthesized by the emulator, so the render thread is the natural
> place for the decrement. Voodoo is different: it has a hardware-accurate display
> callback driven by a scanline timer that already handles swap_count--, swap_pending,
> and retrace_count with 20+ years of proven behavior. Moving swap_count-- to the
> render thread would fight this existing system (which is exactly what caused the v1
> swap_count-stuck-at-3 bug -- dual ownership, not wrong-thread ownership). See
> DESIGN.md section 2 and section 5.1 for the full rationale.

**R2: Frame pacing via atomic counter + semaphore (matches PCSX2/DuckStation)**
- Atomic `queued_frame_count` tracks frames in ring but not yet presented
- FIFO thread increments when pushing swap command
- Render thread decrements after present completes
- FIFO thread blocks (semaphore wait) when count exceeds threshold (2-3)
- Render thread posts semaphore after decrement

**R3: Single thread owns all Vulkan resources (universal pattern)**
- Render thread owns: VkDevice, VkQueue, swapchain, pipeline cache, all VkImages
- FIFO thread has ZERO direct Vulkan access
- GUI thread has ZERO direct Vulkan access
- Communication is ONLY through the ring buffer and atomic signals

**R4: Triple-buffer command buffers (universal pattern)**
- 3 command buffer sets, each with VkCommandPool + VkFence
- Fence counter tracking for deferred resource destruction
- No per-frame waiting -- just check fence counter for cleanup eligibility

**R5: Lock-free wake mechanism (matches DuckStation)**
- Atomic wake counter + semaphore for thread signaling
- Spin-wait optimization: 200us on ARM before semaphore wait
- Avoid mutex/condvar -- too expensive for per-triangle signaling

**R6: Ring buffer sizing: 8-16 MB (matches DuckStation's 16 MB)**
- Large enough to never stall during normal operation
- Variable-size commands with explicit wraparound sentinel
- Atomic read/write pointers with acquire/release ordering

**R7: MoltenVK configuration**
- Keep MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1 (render thread IS the right thread)
- Use 3 swapchain images (triple buffer) to avoid drawable exhaustion
- Create surface from CAMetalLayer (not NSView) to avoid deadlock
- Keep MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE=1 (Metal events)

**R8: Swapchain lifecycle on render thread (v1 lesson)**
- Render thread creates, recreates, and destroys swapchain
- GUI thread sends resize requests through the ring buffer
- No cross-thread swapchain access under any circumstances
- Use VK_EXT_swapchain_maintenance1 present fences for safe destroy

### What v1 Got Wrong (mapped to universal patterns)

| v1 Bug | Root Cause | Correct Pattern |
|--------|-----------|----------------|
| swap_count stuck at 3 | Dual ownership: both GPU thread AND display callback decremented swap_count, creating a race | Leave swap_count to display callback only (see R1 note); render thread handles present via VC_CMD_SWAP but does NOT touch swap_count |
| Present channel crash | GUI thread accessed swapchain | Single GPU-owner thread (R3) |
| FIFO stall before swap | FIFO blocked on ring backpressure before swap cmd | Frame pacing throttle via atomic counter (R2) |
| Swapchain thrashing | GUI thread recreated swapchain | Swapchain lifecycle on render thread (R8) |

> **Note:** The original table attributed the swap_count bug to "wrong thread" (display
> callback). This was imprecise. The display callback was not the wrong thread -- it is
> the correct and only thread for swap_count-- in the Voodoo architecture. The v1 bug
> was that BOTH the render thread AND the display callback decremented swap_count,
> causing double-decrement and desynchronization. DESIGN.md v2 fixes this by having
> the render thread not touch swap_count at all. See DESIGN.md section 5.1.

---

## Appendix A: Dolphin Frame Pacing Timeline

```
Time ->  0ms     4ms     8ms    12ms    16ms    20ms
        |       |       |       |       |       |
VI:     +--field-+--field-+--field-+--field-+--field-+
GPU:    |rec+sub|  idle |rec+sub|  idle |rec+sub|
Present:|       present |       present |       present
Timing: | <-4ms> |       | <-4ms> |       | <-4ms> |
        +--------+       +--------+       +--------+
```

Dolphin targets 4ms between submit and present (PR #12035).
The SleepUntil mechanism idles the GPU thread between VI events.

## Appendix B: PCSX2 Frame Queue State Machine

```
EE Thread                          GS Thread
    |                                  |
    | PostVsyncStart()                 |
    |   QueuedFrameCount++ -> 1        |
    |   (< VsyncQueueSize=2, continue) |
    |                                  |
    | PostVsyncStart()                 |
    |   QueuedFrameCount++ -> 2        |
    |   (< VsyncQueueSize, continue)   |
    |                                  |
    | PostVsyncStart()                 |  MainLoop()
    |   QueuedFrameCount++ -> 3        |    process VSync cmd
    |   (>= VsyncQueueSize!)           |    GSvsync() -> render + present
    |   VsyncSignalListener = true     |    QueuedFrameCount-- -> 2
    |   sem_Vsync.Wait() --BLOCKED--   |    VsyncSignalListener? -> Post
    |                                  |
    |   <------ sem_Vsync.Post() ------|
    |   (unblocked, continues)         |
```

## Appendix C: DuckStation Wake State Machine

```
States: SLEEPING (-1), ACTIVE (>=0)

CPU pushes command:
  PushCommand(cmd)           -> just writes to FIFO
  PushCommandAndWakeThread() -> writes + WakeThread()

WakeThread():
  old = wake_count.fetch_add(2)
  if (old < 0)              -> was SLEEPING, post semaphore
  else                      -> already ACTIVE, no-op

SleepThread():
  old = wake_count.fetch_sub(1)
  if (old > 0)              -> more work pending, return true (don't sleep)
  if (old == 0)             -> transition to SLEEPING (-1)
     check CPU waiting flag -> if set, post done_semaphore
     thread_wake_semaphore.Wait() -> SLEEP
     return true            -> woken up, check for work
```
