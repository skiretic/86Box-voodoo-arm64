# Plan: Move All Vulkan Queue Operations to the Render Thread

## Problem Statement

The current architecture has **three** threads competing for `queue_mutex` to call `vkQueueSubmit`:

1. **Render thread** (`vc_thread.c`) -- submits triangle rendering command buffers
2. **GUI thread** (`qt_vcrenderer.cpp`) -- calls `vkAcquireNextImageKHR`, records blit/post-process commands, calls `vkQueueSubmit` + `vkQueuePresentKHR`
3. **Readback callers** (`vc_readback.c`) -- called from the render thread (async readback at swap boundaries) and potentially from other threads (sync readback for LFB/scanout)

On MoltenVK, `vkQueueSubmit` blocks on `[CAMetalLayer nextDrawable]` when all drawables are in use. If the GUI thread holds `queue_mutex` while blocked on `nextDrawable`, the render thread is starved -- it cannot submit rendering work, which means the GPU never finishes the frame, which means the drawable is never released, creating a priority-inversion-like deadlock.

The present channel (`vc_present_submit`/`vc_present_dispatch`) was introduced to move `vkQueueSubmit` for present off the GUI thread, but the current code (`present()` at line 1470) **reverted to direct GUI-thread submission** because the present channel had its own issues (MoltenVK lazy drawable acquisition blocking the render thread instead of the GUI thread). The present channel dispatch code still exists but is unused by the main present path -- it is only called for legacy/fallback.

Every production emulator (Dolphin, PCSX2, DuckStation, RPCS3) puts ALL Vulkan queue operations on a single thread. We need to do the same.

## Current Architecture (What Exists Today)

### VCRenderer (GUI thread -- `qt_vcrenderer.cpp`)

Owns and manages:
- `VkSurfaceKHR` (platform surface from `QWindow::winId()`)
- `VkSwapchainKHR` (creation, destruction, recreation)
- Swapchain images, views, framebuffers (for post-process)
- Post-processing pipeline (render pass, pipeline, descriptor set/pool, sampler, shader modules)
- Command pool + command buffer (for blit/post-process recording)
- Sync objects: `m_imageAvailable` (semaphore), `m_renderFinished` (semaphore), `m_presentFence` (fence)

Calls (in `present()`):
1. `vkWaitForFences(m_presentFence)` -- wait for previous present to finish
2. `vkAcquireNextImageKHR` -- get next swapchain image
3. Records command buffer (blit or post-process from VC front image to swapchain image)
4. `thread_wait_mutex(queue_mutex)` -- serialize with render thread
5. `vkQueueSubmit` -- submit blit/post-process command buffer
6. `vkQueuePresentKHR` -- present the swapchain image
7. `thread_release_mutex(queue_mutex)`

Triggered by: `onBlit()` slot, connected to `RendererStack::blitToRenderer` signal (queued connection).

### Render thread (`vc_thread.c`)

Owns and manages:
- Frame resources (command pools, command buffers, fences)
- Vertex batches
- SPSC ring consumer

Calls `vkQueueSubmit` in:
- `vc_end_frame()` -- submits triangle rendering command buffer (line 329, takes `queue_mutex`)

Also calls `vc_present_dispatch()` at:
- After each `VC_CMD_SWAP` processing
- After each batch of SPSC ring commands
- (But the main present path no longer uses the present channel)

### Readback (`vc_readback.c`)

Has **7 sites** that take `queue_mutex` for `vkQueueSubmit`:
- Line 253, 255: sync color readback init
- Line 383, 385: sync depth readback init
- Line 655, 657: sync color readback (one-shot)
- Line 789, 791: sync depth readback (one-shot)
- Line 1155, 1157: async readback submit (called from render thread at swap boundaries)
- Line 1599, 1601: LFB write flush color
- Line 1744, 1746: LFB write flush depth

Note: `vc_readback_async_submit` is always called from the render thread (inside `vc_dispatch_command` for `VC_CMD_SWAP`), so it technically does not need the mutex -- it only takes it because the code was written before this invariant was established.

### Present channel (`vc_core.c` lines 1061-1273)

Infrastructure for non-blocking present requests from GUI to render thread:
- `vc_present_channel_init/close` -- lifecycle
- `vc_present_submit` -- GUI thread posts request (atomic flag)
- `vc_present_dispatch` -- render thread picks up and executes
- `vc_present_drain` -- GUI thread waits for pending request to complete

Currently **partially used**: dispatch is called from the render loop, but `present()` does direct GUI-thread submission instead of using `vc_present_submit`.

## Proposed Architecture

### Principle: Single Queue Owner

The render thread becomes the **sole owner** of `VkQueue`. No other thread calls `vkQueueSubmit` or `vkQueuePresentKHR`. The `queue_mutex` is **eliminated entirely**.

### What Moves to the Render Thread

1. **Swapchain creation/destruction/recreation** -- including image enumeration, view creation, framebuffer creation for post-processing
2. **vkAcquireNextImageKHR**
3. **Command buffer recording** for blit/post-process (transition front image, sample/blit to swapchain image, transition to PRESENT_SRC)
4. **vkQueueSubmit** for present command buffer
5. **vkQueuePresentKHR**
6. **Post-processing pipeline creation** (render pass, pipeline, descriptors, shader modules) -- these are device-level operations that can happen on any thread, but keeping them on the render thread simplifies lifetime management
7. **All readback vkQueueSubmit calls** -- they already happen on the render thread for async; sync readback submissions must also move

### What Stays in VCRenderer (GUI Thread)

1. **VkSurfaceKHR creation** -- Qt requires `winId()` to be called from the GUI thread. `vkCreateMetalSurfaceEXT` / `vkCreateWin32SurfaceKHR` / `vkCreateXlibSurfaceKHR` must happen on the GUI thread because they need the native window handle.
2. **Resize detection** -- `resizeEvent()` sets an atomic flag that the render thread checks.
3. **Software fallback rendering** -- `paintFallback()` for BIOS/VGA/pre-Voodoo display.
4. **Signal that a frame should be displayed** -- `onBlit()` sets an atomic "present requested" flag or sends a lightweight notification to the render thread.
5. **Lifecycle management** -- constructor creates the QWindow, destructor signals the render thread to release swapchain resources, finalize waits for completion.

### Surface Handle Passing

The VkSurfaceKHR must be created on the GUI thread. The render thread needs it to create the swapchain. Flow:

1. VCRenderer creates `VkSurfaceKHR` in `tryInitializeVulkan()` (GUI thread) -- this is the only Vulkan call that must stay on the GUI thread.
2. VCRenderer stores the surface handle and signals the render thread (via an atomic field in `vc_context_t` or a new shared struct).
3. The render thread detects the new surface and creates the swapchain.

### New Present Flow

The render thread already has a main loop that processes SPSC ring commands. Present is integrated into this loop as follows:

```
while (running) {
    /* 1. Process SPSC ring commands (triangles, state changes, etc.) */
    while (!ring_empty && batch < limit) {
        dispatch_command(pop());
        batch++;
    }

    /* 2. Check if present is needed (set by VC_CMD_SWAP or a flag) */
    if (present_requested && swapchain_valid) {
        present_frame();  /* acquire + record + submit + present */
    }

    /* 3. Sleep if no work */
    if (!did_work)
        wait_event(wake_event, 1ms);
}
```

`present_frame()` does:
1. `vkAcquireNextImageKHR` -- may block (this is correct: it is GPU backpressure on the correct thread)
2. Record command buffer: transition front image, blit/post-process to swapchain image, transition to PRESENT_SRC
3. `vkQueueSubmit` -- no mutex needed (we are the sole user)
4. `vkQueuePresentKHR`

When to present:
- **After each `VC_CMD_SWAP`** -- the natural frame boundary. The render thread has just finished rendering a frame and swapped front/back. The front buffer is fresh.
- **Not on every blit signal** -- the GUI thread's `onBlit()` is decoupled from the render thread's frame production. The render thread presents at its own pace.
- **Rate limiting**: Present at most once per `VC_CMD_SWAP`. If the GUI is running faster than the emulator (e.g., 120Hz monitor, 30fps game), the render thread just presents the same front image again. If the emulator is faster (uncapped framerate), the render thread can skip presents to avoid blocking on `vkAcquireNextImageKHR` too often.

### Swapchain Resource Ownership

New struct embedded in `vc_context_t` (or a separate `vc_display_t`):

```c
typedef struct vc_display {
    /* Set by GUI thread, read by render thread */
    VkSurfaceKHR surface;          /* Created by GUI thread */
    _Atomic int  surface_valid;    /* 1 = surface is ready for swapchain creation */
    _Atomic int  resize_requested; /* Set by GUI resizeEvent */
    _Atomic int  present_active;   /* Set by GUI to enable/disable presentation */
    _Atomic int  teardown;         /* GUI requests render thread to release resources */

    /* GUI thread writes these when resize_requested is set */
    _Atomic uint32_t requested_width;
    _Atomic uint32_t requested_height;

    /* Owned by render thread (created/destroyed on render thread) */
    VkSwapchainKHR   swapchain;
    VkImage         *swapchain_images;
    VkImageView     *swapchain_views;     /* For post-process */
    VkFramebuffer   *pp_framebuffers;     /* For post-process */
    uint32_t         image_count;
    VkFormat         swapchain_format;
    VkExtent2D       swapchain_extent;

    /* Present sync objects (owned by render thread) */
    VkSemaphore      image_available;
    VkSemaphore      render_finished;
    VkFence          present_fence;

    /* Present command pool/buffer (owned by render thread) */
    VkCommandPool    present_cmd_pool;
    VkCommandBuffer  present_cmd_buf;

    /* Post-processing pipeline (owned by render thread) */
    VkRenderPass          pp_render_pass;
    VkPipelineLayout      pp_pipeline_layout;
    VkPipeline            pp_pipeline;
    VkDescriptorSetLayout pp_desc_layout;
    VkDescriptorPool      pp_desc_pool;
    VkDescriptorSet       pp_desc_set;
    VkSampler             pp_sampler;
    VkShaderModule        pp_vert_module;
    VkShaderModule        pp_frag_module;
    VkImageView           last_front_view;

    /* Post-processing parameters (written by GUI, read by render thread).
     * Could use atomics or just accept occasional stale values. */
    float pp_scanline_intensity;
    float pp_curvature;
    float pp_brightness;

    /* Completion event: signaled by render thread after teardown finishes */
    event_t *teardown_complete_event;
} vc_display_t;
```

### Resize Handling

1. GUI thread `resizeEvent()` sets `resize_requested = 1` and stores the new dimensions in `requested_width/height`.
2. Render thread checks `resize_requested` before each present:
   ```c
   if (atomic_load(&display->resize_requested)) {
       recreate_swapchain(display);
       atomic_store(&display->resize_requested, 0);
   }
   ```
3. `recreate_swapchain` destroys old swapchain resources, creates new ones with the updated dimensions. All on the render thread -- no cross-thread resource lifecycle issues.

### Teardown / Finalize

When VCRenderer is finalized (VM power-off, renderer switch):

1. GUI thread sets `display->teardown = 1` and wakes the render thread.
2. Render thread detects `teardown`, destroys all swapchain and post-process resources, sets `present_active = 0`, signals `teardown_complete_event`.
3. GUI thread waits on `teardown_complete_event` with timeout.
4. GUI thread destroys the `VkSurfaceKHR` (must happen on GUI thread for macOS/MoltenVK, where the CAMetalLayer is attached to the NSView).

### Present Channel Fate: DELETED

The entire present channel infrastructure is eliminated:
- `vc_present_request_t` struct -- deleted from `vc_core.h`
- `vc_present_channel_init/close` -- deleted from `vc_core.c`
- `vc_present_submit` -- deleted
- `vc_present_dispatch` -- deleted (replaced by direct present in the render loop)
- `vc_present_drain` -- deleted (replaced by teardown event)
- `drain_event` -- deleted
- All `vc_present_dispatch()` calls in `vc_thread.c` -- removed

### queue_mutex Fate: ELIMINATED

With all `vkQueueSubmit` calls on the render thread, no mutex is needed:

| Current call site | Thread | Change |
|---|---|---|
| `vc_thread.c:329` (vc_end_frame) | Render | Remove mutex -- sole user |
| `vc_core.c:1219` (present_dispatch) | Render | Deleted entirely |
| `vc_readback.c:253` (sync color init) | Render | Remove mutex -- sole user |
| `vc_readback.c:383` (sync depth init) | Render | Remove mutex -- sole user |
| `vc_readback.c:655` (sync color one-shot) | Render | Remove mutex -- sole user |
| `vc_readback.c:789` (sync depth one-shot) | Render | Remove mutex -- sole user |
| `vc_readback.c:1155` (async submit) | Render | Remove mutex -- sole user |
| `vc_readback.c:1599` (LFB write flush color) | Render | Remove mutex -- sole user |
| `vc_readback.c:1744` (LFB write flush depth) | Render | Remove mutex -- sole user |
| `vc_texture.c:182` (texture init) | Called during vc_init | Runs before render thread starts -- safe |
| `vc_render_pass.c:435` (initial clear) | Called during vc_init | Runs before render thread starts -- safe |
| `qt_vcrenderer.cpp:1503` (present submit) | GUI | **Moved to render thread** |
| `qt_vcrenderer.cpp:1283` (timeout empty submit) | GUI | **Moved to render thread** |

After migration: `queue_mutex` field is removed from `vc_context_t`. The `vc_get_queue_mutex()` accessor is removed. All `thread_wait_mutex/thread_release_mutex` calls around queue submissions are removed.

**Audit note**: Any readback paths currently callable from non-render threads (e.g., sync readback for scanout from the display timer thread) must be refactored to either:
- Route through the SPSC ring (add `VC_CMD_READBACK` command), or
- Be confirmed to only run on the render thread (which is already the case for async readback called from `VC_CMD_SWAP` handler)

The `vc_thread_wait_idle()` function currently calls `vkDeviceWaitIdle()` from the caller's thread. `vkDeviceWaitIdle` is thread-safe per the Vulkan spec (it does not submit to a queue), so this is fine even without the mutex. However, if any readback sync paths are triggered from outside the render thread, they need to be rearchitected. The current code already avoids this -- LFB reads use the async staging buffer (non-blocking), and sync readback (`vc_readback_*_sync`) is not called from the LFB read path (per the deadlock fix documented in MEMORY.md).

## Specific Files and Functions to Change

### `src/qt/qt_vcrenderer.hpp`

**Remove:**
- `VkSwapchainKHR m_swapchain` and all swapchain member variables
- `VkSemaphore m_imageAvailable, m_renderFinished`
- `VkFence m_presentFence`
- `VkCommandPool m_commandPool`, `VkCommandBuffer m_commandBuffer`
- All post-processing members (`m_ppRenderPass`, `m_ppPipeline`, etc.)
- `m_swapchainImages`, `m_swapchainViews`, `m_ppFramebuffers`
- `m_swapchainFormat`, `m_swapchainExtent`, `m_imageCount`
- `m_lastFrontView`
- `m_swapchainValid`, `m_needsRecreate`

**Keep:**
- `m_surface` (created on GUI thread)
- `m_metalLayer` (macOS only)
- `m_vcCtx`, cached Vulkan handles
- `m_initialized`, `m_finalized`, `m_rendererSignalled`
- `m_needsResize` (atomic, set by resizeEvent)
- `m_initTimer` (retry timer for deferred Vulkan init)
- `m_backingStore`, `m_buf[]`, `m_bufFlag[]` (software fallback)

**Simplify:**
- `createSwapchain()` / `destroySwapchain()` / `recreateSwapchain()` -- **deleted** (moved to render thread C code)
- `createPostProcess()` / `destroyPostProcess()` -- **deleted**
- `presentWithPostProcess()` -- **deleted**
- `present()` -- **replaced** with a simple `requestPresent()` that sets an atomic flag
- `onBlit()` -- calls `requestPresent()` instead of `present()`
- `finalize()` -- sets teardown flag, waits for render thread to release resources, then destroys surface

### `src/qt/qt_vcrenderer.cpp`

**Massively simplified.** Current: ~1690 lines. After: ~300-400 lines.

What remains:
- Constructor (QWindow setup, fallback buffers, init timer)
- Destructor (calls finalize)
- `finalize()` -- set teardown flag, wait for completion, destroy surface
- `tryInitializeVulkan()` -- create surface, pass to render thread via `vc_display_t`
- `createSurface()` / `destroySurface()` -- unchanged (must stay on GUI thread)
- `exposeEvent()` / `resizeEvent()` / `event()` -- simplified
- `onBlit()` -- set present-requested flag, wake render thread
- `paintFallback()` -- unchanged
- `getBytesPerRow()` / `getBuffers()` -- unchanged

### `src/video/videocommon/vc_core.h`

**Add:**
- `vc_display_t` struct definition (see above)
- `vc_display_t display;` field in `vc_context_t`

**Remove:**
- `vc_present_request_t` struct
- `vc_present_request_t present_request;` field in `vc_context_t`
- `void *queue_mutex;` field in `vc_context_t`
- All present channel function declarations
- `vc_get_queue_mutex()` declaration

**Add:**
- `vc_display_set_surface(ctx, surface)` -- GUI thread passes surface to context
- `vc_display_request_present(ctx)` -- GUI thread requests a present
- `vc_display_request_resize(ctx, w, h)` -- GUI thread signals resize
- `vc_display_request_teardown(ctx)` -- GUI thread signals teardown
- `int vc_display_wait_teardown(ctx, timeout_ms)` -- GUI thread waits

### `src/video/videocommon/vc_core.c`

**Add:**
- `vc_display_init()` / `vc_display_close()` -- lifecycle for `vc_display_t`
- `vc_display_set_surface()` -- store surface handle, set `surface_valid`
- `vc_display_request_present()` -- set flag, wake render thread
- `vc_display_request_resize()` -- set flag + dimensions
- `vc_display_request_teardown()` -- set flag, wake render thread
- `vc_display_wait_teardown()` -- wait on event

**Remove:**
- `vc_present_channel_init/close/submit/dispatch/drain` functions
- `queue_mutex` creation in `vc_init()` and destruction in `vc_close()`

### `src/video/videocommon/vc_thread.c`

**Major changes to render loop:**

```c
/* In vc_render_thread_func(), after processing SPSC ring commands: */

/* Check if display needs swapchain (re)creation. */
if (display->surface_valid && !display->swapchain)
    vc_display_create_swapchain(display, ctx);

if (display->resize_requested) {
    vc_display_recreate_swapchain(display, ctx);
    atomic_store(&display->resize_requested, 0);
}

/* Present the latest frame if requested. */
if (display->present_requested && display->swapchain) {
    vc_display_present_frame(display, ctx);
    atomic_store(&display->present_requested, 0);
}

/* Handle teardown. */
if (display->teardown) {
    vc_display_destroy_swapchain(display, ctx);
    vc_display_destroy_postprocess(display, ctx);
    atomic_store(&display->teardown, 0);
    thread_set_event(display->teardown_complete_event);
}
```

**Add new functions** (can be in a new `vc_display.c` or in `vc_thread.c`):
- `vc_display_create_swapchain()` -- port from VCRenderer::createSwapchain()
- `vc_display_destroy_swapchain()` -- port from VCRenderer::destroySwapchain()
- `vc_display_recreate_swapchain()` -- port from VCRenderer::recreateSwapchain()
- `vc_display_create_postprocess()` -- port from VCRenderer::createPostProcess()
- `vc_display_destroy_postprocess()` -- port from VCRenderer::destroyPostProcess()
- `vc_display_present_frame()` -- port from VCRenderer::present() + presentWithPostProcess()

**Remove from render loop:**
- All `vc_present_dispatch()` calls

**Remove from `vc_end_frame()`:**
- `thread_wait_mutex(queue_mutex)` / `thread_release_mutex(queue_mutex)` around `vkQueueSubmit`

### `src/video/videocommon/vc_readback.c`

**Remove all `queue_mutex` usage:**
- Remove all `thread_wait_mutex(ctx->queue_mutex)` / `thread_release_mutex(ctx->queue_mutex)` (7 sites)
- Add assertions/comments that these functions must only be called from the render thread

### `src/video/videocommon/vc_texture.c`

**Remove `queue_mutex` usage:**
- Line 182/184: Remove mutex around one-shot submission (happens during `vc_init()` before render thread starts)

### `src/video/videocommon/vc_render_pass.c`

**Remove `queue_mutex` usage:**
- Line 435/437: Remove mutex around initial clear submission (happens during `vc_init()` before render thread starts)

### New file: `src/video/videocommon/vc_display.c` (and `vc_display.h`)

Pure C11 module containing the swapchain and post-processing management code ported from the C++ VCRenderer. This is a significant amount of code (~800-1000 lines), but it is mostly mechanical translation from C++ to C.

The embedded SPIR-V bytecode for post-processing shaders needs to be accessible from C code. Currently it is included via `#include "postprocess_vert_spv.h"` in the C++ file. The same headers can be included in the C file.

## When Does the Render Thread Present?

Two options:

### Option A: Present at VC_CMD_SWAP boundaries (preferred)

The most natural time to present is right after `VC_CMD_SWAP` -- the render thread has just finished rendering a frame and swapped front/back buffers. The front buffer contains the latest completed frame.

```c
case VC_CMD_SWAP:
    vc_end_frame(thread);
    /* async readback... */
    vc_begin_frame(thread);
    /* swap_complete callback... */

    /* Present the just-completed frame to the display. */
    if (thread->ctx->display.present_active && thread->ctx->display.swapchain)
        vc_display_present_frame(&thread->ctx->display, thread->ctx);
    break;
```

This naturally rate-limits presentation to the emulator's frame rate. If the game runs at 30fps, we present 30 times per second. If FIFO mode, the present blocks at `vkAcquireNextImageKHR` until vsync, providing natural frame pacing. With MAILBOX mode, present returns immediately and the compositor handles vsync.

The GUI thread's `onBlit()` becomes a no-op for the Vulkan path -- it only handles the software fallback. The render thread presents independently.

**Concern**: What if the GUI thread needs to force a repaint (e.g., window uncovered after being obscured)? The render thread can re-present the current front image on the next wake cycle. The `present_requested` flag from the GUI thread handles this.

### Option B: Present on demand (GUI-driven)

The GUI thread sets `present_requested` via `onBlit()`. The render thread checks this flag each iteration and presents if set. This more closely matches the current behavior where the GUI thread drives presentation.

**Concern**: If the emulator is running fast (unlocked framerate), the GUI thread's blit signal may arrive faster than the render thread can process SPSC commands. The render thread would need to debounce present requests.

### Recommendation: Hybrid

- Present **unconditionally** after each `VC_CMD_SWAP` (natural frame boundary).
- Also present when `present_requested` is set (GUI thread needs a repaint) and at least one frame has been completed.
- Use a `last_presented_frame_id` counter to avoid redundant presents of the same frame.

## Migration Order

### Phase 1: Add `vc_display_t` infrastructure (no behavior change)

1. Create `vc_display.h` / `vc_display.c` with the `vc_display_t` struct and init/close.
2. Add `vc_display_t display` to `vc_context_t`.
3. Port swapchain creation/destruction from C++ to C.
4. Port post-processing pipeline creation from C++ to C.
5. Port present_frame logic from C++ to C.
6. Build and verify compilation.

### Phase 2: Wire up render thread present (dual path)

1. Add swapchain creation and present_frame calls to the render loop.
2. Add `vc_display_set_surface()` call from VCRenderer after surface creation.
3. The render thread creates the swapchain and presents.
4. Keep the old VCRenderer present path as fallback (controlled by a flag).
5. Test: verify frames are displayed through the new path.

### Phase 3: Simplify VCRenderer

1. Remove swapchain, post-process, sync objects, command pool from VCRenderer.
2. `present()` replaced by `requestPresent()`.
3. `finalize()` uses teardown mechanism.
4. Remove the old present path.

### Phase 4: Eliminate queue_mutex

1. Remove all `queue_mutex` calls from `vc_readback.c`, `vc_thread.c`, `vc_texture.c`, `vc_render_pass.c`.
2. Remove `queue_mutex` from `vc_context_t`.
3. Remove `vc_get_queue_mutex()`.
4. Remove present channel code.

### Phase 5: Cleanup

1. Remove unused code paths in VCRenderer.
2. Update documentation (DESIGN.md, this plan).
3. Run validation (`VC_VALIDATE=1`).

## Risk Assessment

### Low Risk

- **Swapchain/pipeline creation on render thread**: These are standard Vulkan operations. Dolphin, DuckStation, and PCSX2 all create swapchains from non-GUI threads. The Vulkan spec explicitly allows this.
- **queue_mutex elimination**: Removing a lock is always safe when the invariant (single-threaded queue access) is maintained. The key is ensuring no stray `vkQueueSubmit` calls remain on other threads.

### Medium Risk

- **C++ to C port of post-processing pipeline**: ~300 lines of Vulkan pipeline setup code. Mechanical but tedious. Risk of transcription errors. Mitigated by: running validation layers, visual comparison with existing output.
- **Resize handling across threads**: Race between GUI thread setting dimensions and render thread recreating the swapchain. Mitigated by: atomic flags with acquire/release semantics, rechecking dimensions in the swapchain creation path.
- **MoltenVK nextDrawable blocking on render thread**: This is the *correct* place for this backpressure. With MAILBOX present mode, `vkQueuePresentKHR` returns immediately. With FIFO, `vkAcquireNextImageKHR` may block until vsync -- this is acceptable on the render thread because it provides natural frame pacing. However, it does mean the render thread cannot process SPSC ring commands while blocked. If the emulator is producing frames faster than vsync, the ring may fill up. Mitigated by: the ring is 16K entries (plenty of headroom), and MAILBOX mode is preferred on MoltenVK.

### Higher Risk

- **VkSurfaceKHR lifetime vs swapchain**: The GUI thread owns the surface (because it owns the window). The render thread owns the swapchain (which references the surface). If the window is destroyed before the swapchain, we get a crash. Mitigated by: teardown protocol ensures the render thread destroys the swapchain before the GUI thread destroys the surface. The `teardown_complete_event` synchronization makes this safe.
- **Display timing / frame pacing**: Currently, the GUI thread drives presentation at the blit thread's rate. Moving to render-thread-driven presentation changes the timing. The emulator's frame rate (driven by `swap_count` / `retrace_count`) determines present rate. This should be fine for games, but desktop (2D) rendering may have different patterns. Mitigated by: the `present_requested` flag from the GUI thread provides a secondary trigger.
- **Readback paths callable from non-render threads**: The sync readback path (`vc_readback_color_sync` etc.) calls `vkQueueSubmit` under `queue_mutex`. If any code path calls this from outside the render thread after the mutex is removed, it is a data race. Current analysis shows all sync readback callers go through the render thread or `vc_thread_wait_idle()` (which does not submit). But this needs careful audit. Mitigated by: adding `assert(on_render_thread)` guards in debug builds.

## Summary of Benefits

1. **Eliminates queue_mutex contention** -- no more multi-thread queue serialization
2. **Eliminates MoltenVK nextDrawable starvation** -- drawable backpressure on the correct thread
3. **Simplifies VCRenderer by ~1300 lines** -- from ~1690 to ~400 lines
4. **Eliminates present channel complexity** -- ~200 lines of cross-thread present dispatch code deleted
5. **Matches industry practice** -- Dolphin, PCSX2, DuckStation, RPCS3 all use single-thread queue ownership
6. **Cleaner resource lifecycle** -- all swapchain resources created and destroyed on the same thread, no cross-thread lifetime coordination
7. **Enables future optimizations** -- with single-threaded queue access, timeline semaphores and fine-grained command buffer management become simpler
