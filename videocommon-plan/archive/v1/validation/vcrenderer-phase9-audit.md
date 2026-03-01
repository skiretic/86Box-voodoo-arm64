# VCRenderer Phase 9.5 Validation Report

**Date**: 2026-02-28
**Scope**: Zero-copy GPU-to-screen display (Phase 9 commits)
**Files reviewed**:
- `src/qt/qt_vcrenderer.cpp` (693 lines)
- `src/qt/qt_vcrenderer.hpp` (152 lines)
- `src/qt/qt_vc_metal_layer.mm` (37 lines)
- `src/qt/qt_rendererstack.cpp` (lines 454-506)
- `src/video/videocommon/vc_core.c` (1135 lines)
- `src/video/videocommon/vc_core.h` (159 lines)
- `src/video/videocommon/vc_render_pass.h` (95 lines)
- `src/video/videocommon/vc_render_pass.c` (lines 180-258)
- `src/video/videocommon/vc_thread.c` (1005 lines)
- `src/video/vid_voodoo_reg.c` (lines 83-110)
- `src/video/vid_voodoo.c` (lines 1546-1565)

---

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| HIGH     | 2 | vc_global_ctx race; shutdown ordering (VCRenderer vs vc_close) |
| MODERATE | 3 | NSView lifetime, post-blit barrier dstStageMask, suboptimal swapchain drops frame |
| LOW      | 4 | Minor robustness issues (contentsScale, composite alpha, usage flags, fallback buffers) |
| CORRECT  | 10 | Verified correct aspects (see list at end) |

---

## 1. Vulkan Correctness

### 1.1 Image Layout Transitions

**Front VkImage: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC -> COLOR_ATTACHMENT_OPTIMAL**
- `qt_vcrenderer.cpp` lines 536-545: pre-blit barrier transitions from COLOR_ATTACHMENT_OPTIMAL to TRANSFER_SRC_OPTIMAL.
- `qt_vcrenderer.cpp` lines 584-593: post-blit barrier transitions back to COLOR_ATTACHMENT_OPTIMAL.

**ANALYSIS: Front image synchronization between render thread submission and VCRenderer blit.**

The front image is the one *not* being actively rendered to (it was the back buffer during the last frame). After `vc_end_frame()` in `vc_thread.c` line 337, the framebuffers are swapped via `vc_framebuffer_swap()`. The render pass's `finalLayout` is `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` (confirmed at `vc_render_pass.c` line 195), so when the render pass ends, the back buffer (which then becomes the front) is transitioned to COLOR_ATTACHMENT_OPTIMAL. This is correct.

The timing concern: `present()` on the Qt GUI thread reads from the front image, while the render thread may be simultaneously rendering to the back image. The render thread submits its command buffer via `vkQueueSubmit` (with the queue mutex) before the VCRenderer's `vkQueueSubmit`. Both use the same VkQueue, serialized by the queue_mutex.

Per the Vulkan 1.2 specification, Chapter 7.2 "Implicit Synchronization Guarantees":

> "When a batch is submitted to a queue via a queue submission command, it defines a memory dependency with prior queue operations... The first synchronization scope includes every command previously submitted to the same queue... The first access scope includes all memory access performed by the device. The second synchronization scope includes every command subsequently submitted to the same queue... The second access scope includes all memory access performed by the device."

This means that on a **single queue**, there is an **implicit full memory dependency** between consecutive submissions. ALL writes from the render thread's prior `vkQueueSubmit` are guaranteed to be visible to ALL reads in the VCRenderer's subsequent `vkQueueSubmit`.

The VCRenderer's barrier provides the **image layout transition** (COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL), which is necessary even with the implicit memory dependency. The barrier's `srcStageMask = COLOR_ATTACHMENT_OUTPUT_BIT` and `srcAccessMask = COLOR_ATTACHMENT_WRITE_BIT` are correct -- the implicit memory dependency from submission ordering ensures these writes have completed and are visible before the layout transition executes.

**Verdict**: CORRECT. The implicit single-queue submission ordering guarantee provides the needed synchronization. No semaphore is required.

**Note**: This relies on both submissions going to the same VkQueue. The code correctly ensures this -- the render thread uses `ctx->graphics_queue` (`vc_thread.c` line 326) and the VCRenderer uses `m_queue` (line 636), which was set from `vc_get_graphics_queue()` (line 174). Both resolve to `ctx->graphics_queue`. The queue_mutex ensures the submissions happen in order on the CPU side.


**Swapchain image: UNDEFINED -> TRANSFER_DST -> PRESENT_SRC**
- Lines 548-557: pre-blit transitions from UNDEFINED to TRANSFER_DST_OPTIMAL.
- Lines 596-605: post-blit transitions from TRANSFER_DST_OPTIMAL to PRESENT_SRC_KHR.

VERIFIED CORRECT. Starting from UNDEFINED is correct for swapchain images after acquire (contents are undefined). The final layout PRESENT_SRC_KHR is required for vkQueuePresentKHR.

### 1.2 Barrier Stage Masks

**Pre-blit barriers** (line 560):
```c
vkCmdPipelineBarrier(m_commandBuffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_TRANSFER_BIT,                  // dstStageMask
    ...);
```

- srcStageMask = COLOR_ATTACHMENT_OUTPUT_BIT: correct for front image (was a color attachment).
- dstStageMask = TRANSFER_BIT: correct (blit is a transfer operation).
- srcAccessMask on front image = COLOR_ATTACHMENT_WRITE_BIT: correct.
- dstAccessMask on front image = TRANSFER_READ_BIT: correct.
- srcAccessMask on swapchain = 0: correct (UNDEFINED layout discards contents).
- dstAccessMask on swapchain = TRANSFER_WRITE_BIT: correct.

VERIFIED CORRECT (assuming the cross-submission sync issue from 1.1 is resolved).

**Post-blit barriers** (lines 608-612):
```c
vkCmdPipelineBarrier(m_commandBuffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,                  // srcStageMask
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT     // dstStageMask
        | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    ...);
```

MODERATE ISSUE: The `dstStageMask` includes `COLOR_ATTACHMENT_OUTPUT_BIT` for the front image transition back to `COLOR_ATTACHMENT_OPTIMAL`. This is correct for the front image (it will be used as a color attachment when it becomes the back buffer again). However, for the swapchain image (transitioning to PRESENT_SRC_KHR), the `dstStageMask` should only need `BOTTOM_OF_PIPE_BIT` (since presentation is not a pipeline stage). The combined mask is technically correct but over-specifies the dependency.

The `dstAccessMask` on the swapchain image is 0, which is correct for PRESENT_SRC_KHR (no pipeline access needed -- presentation reads are handled by the presentation engine).

The `srcBack` barrier for the front image has `dstAccessMask = COLOR_ATTACHMENT_WRITE_BIT`, which is correct.

**Verdict**: CORRECT (minor over-specification in dstStageMask, harmless).

### 1.3 Semaphore/Fence Usage

```
m_imageAvailable: signaled by vkAcquireNextImageKHR
m_renderFinished: signaled by vkQueueSubmit, waited on by vkQueuePresentKHR
m_presentFence:   signaled by vkQueueSubmit, waited on at top of present()
```

- Line 494: `vkWaitForFences(m_presentFence)` -- gates previous frame's present completion. CORRECT.
- Line 501: `vkAcquireNextImageKHR` signals `m_imageAvailable`. CORRECT.
- Line 621-630: `vkQueueSubmit` waits on `m_imageAvailable` (at TRANSFER stage), signals `m_renderFinished`, signals `m_presentFence`. CORRECT.
- Line 646-658: `vkQueuePresentKHR` waits on `m_renderFinished`. CORRECT.

VERIFIED CORRECT. The fence/semaphore dance follows the canonical present loop pattern.

### 1.4 vkCmdBlitImage Format Compatibility

Source: `VK_FORMAT_R8G8B8A8_UNORM` (VC framebuffer color image, confirmed at `vc_render_pass.c` line 103)
Dest: `VK_FORMAT_B8G8R8A8_UNORM` (swapchain, preferred at `qt_vcrenderer.cpp` line 313)

Per Vulkan 1.2 spec Table 45, both RGBA8_UNORM and BGRA8_UNORM are in the same "compatibility class" (32-bit). `vkCmdBlitImage` requires that source and destination formats be "compatible" for the blit filter. Per the spec, `VK_FORMAT_R8G8B8A8_UNORM` and `VK_FORMAT_B8G8R8A8_UNORM` are both UNORM and same size, but they are NOT in the same compatibility class for image copy operations.

However, `vkCmdBlitImage` is NOT a copy -- it's a scaled image transfer with format conversion. The spec says (Section 19.5):
> "Each of the source and destination regions must be a valid region in the source and destination images, respectively... The formats of the source and destination images must be compatible. Specifically, both formats must be in the same format compatibility class."

Checking: `VK_FORMAT_R8G8B8A8_UNORM` compatibility class = "32-bit", `VK_FORMAT_B8G8R8A8_UNORM` compatibility class = "32-bit". They ARE in the same class.

Additionally, `vkCmdBlitImage` supports format conversion between the src and dst formats when they differ -- it uses the component mapping to interpret color channels correctly.

The `VK_FILTER_LINEAR` filter is used. Per the spec, linear filter support for UNORM formats is widely guaranteed by `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT` on the source format and `VK_FORMAT_FEATURE_BLIT_DST_BIT` on the destination format. Both RGBA8_UNORM and BGRA8_UNORM universally support these features.

IMPORTANT: The source image must have `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`. Confirmed at `vc_render_pass.c` line 105: the color image includes `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`.

VERIFIED CORRECT. The RGBA8->BGRA8 blit with linear filter is valid and universally supported.

### 1.5 Queue Sharing and Mutex

`present()` runs on the Qt main thread but uses the same VkQueue as the render thread.

Lines 633-638: Queue mutex acquired for `vkQueueSubmit`.
Lines 654-658: Queue mutex acquired again for `vkQueuePresentKHR`.

**HIGH ISSUE: Queue mutex is released between vkQueueSubmit and vkQueuePresentKHR.**

Between lines 638 and 654, the mutex is released. The render thread could submit work to the queue in this window. This is technically safe because vkQueuePresentKHR will wait on the `m_renderFinished` semaphore (which was signaled by the submit), so the present won't execute until the blit is done. However, the render thread's submissions could interleave between the VCRenderer's submit and present, potentially causing:
- No correctness issue (semaphore ordering is fine)
- But the interleaved submission could delay the present

This is acceptable but suboptimal. A tighter approach would hold the mutex across both calls. However, this increases mutex hold time and could delay the render thread.

**Verdict**: ACCEPTABLE but noted. The split mutex is safe due to semaphore synchronization.

### 1.6 Swapchain Recreation

Line 503-506: On `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR` from acquire, recreate and return (no present this frame).

**MODERATE ISSUE: After vkAcquireNextImageKHR succeeds but returns VK_SUBOPTIMAL_KHR, the code calls recreateSwapchain() which destroys the swapchain, but the image was already acquired and the imageAvailable semaphore was signaled. This leaves the semaphore in an unsignaled-but-waited-on state for the next frame's present() call.**

Actually, `VK_SUBOPTIMAL_KHR` is a success code, so `vkAcquireNextImageKHR` succeeded and the `m_imageAvailable` semaphore was signaled. But then `recreateSwapchain()` destroys the swapchain (and all sync objects via `destroySwapchain()`), so the old semaphore is destroyed. The new `createSwapchain()` creates new semaphores. So the signaled semaphore is just destroyed -- no leak, no issue.

BUT: the `VK_SUBOPTIMAL_KHR` case should probably still present the acquired image before recreating, for smoothness. Currently it drops a frame. Minor UX issue.

**Verdict**: MODERATE (frame drop on suboptimal, not a correctness bug).

---

## 2. Threading Safety

### 2.1 vc_global_ctx Race Condition

**HIGH ISSUE #1: vc_global_ctx is a plain pointer, not atomic.**

At `vc_core.h` line 157:
```c
extern vc_context_t *vc_global_ctx;
```
At `vc_core.c` line 1078:
```c
vc_context_t *vc_global_ctx = NULL;
```

Written in `vc_deferred_init_thread()` (`vid_voodoo_reg.c` line 104):
```c
vc_global_ctx = (vc_context_t *) ctx;
```

Read in `VCRenderer::exposeEvent()` (`qt_vcrenderer.cpp` line 162):
```c
m_vcCtx = vc_global_ctx;
```

The write happens on the deferred init thread. The read happens on the Qt GUI thread. There is no memory barrier, atomic operation, or synchronization primitive between the write and read. This is a data race in the C and C++ memory models.

In practice, on ARM64 (macOS), this is likely safe because:
1. `voodoo->vc_ctx` is written with `memory_order_release` immediately before `vc_global_ctx` is written.
2. The deferred init thread writes `vc_global_ctx` and then clears `vc_init_pending`.
3. The Qt expose event happens much later (user-driven), so the value has propagated.

However, this is technically undefined behavior. The fix is trivial: make `vc_global_ctx` atomic, or use a mutex/flag.

**Written at `vid_voodoo.c` line 1560** (cleanup):
```c
vc_global_ctx = NULL;
```
This runs on the main emulation thread during device close. If VCRenderer's present() is running concurrently and just read `vc_global_ctx` into `m_vcCtx`, the cached pointer becomes dangling. But `m_vcCtx` is cached at init time (line 162), not re-read each frame, so this is safe as long as VCRenderer is destroyed before vc_close() runs.

**Verdict**: HIGH. Data race on `vc_global_ctx`. Practically safe on ARM64 due to timing, but should be fixed for correctness.

**Suggested fix**: Change to `_Atomic(vc_context_t *)` in C / `std::atomic<vc_context_t*>` in C++, or declare with `volatile` and use explicit fences.

### 2.2 Queue Mutex in Render Thread

The render thread holds the queue mutex during `vkQueueSubmit` at `vc_thread.c` line 325-327:
```c
thread_wait_mutex(thread->ctx->queue_mutex);
VkResult res = vkQueueSubmit(thread->ctx->graphics_queue, 1, &submit, fr->fence);
thread_release_mutex(thread->ctx->queue_mutex);
```

The VCRenderer holds the same mutex during its `vkQueueSubmit` (line 635-637) and `vkQueuePresentKHR` (line 655-657). This is correct mutual exclusion.

VERIFIED CORRECT.

### 2.3 m_needsResize Atomicity

`std::atomic<bool>` with `memory_order_relaxed` for both store (line 221) and exchange (line 487).

This is sufficient. The resize flag is a hint -- it does not need to be ordered with respect to any other variable. Missing a resize for one frame is harmless (it will be picked up next frame).

VERIFIED CORRECT.

### 2.4 Potential Deadlock Analysis

Could present() deadlock against the render thread?

- present() takes `queue_mutex` -> vkQueueSubmit -> release -> take `queue_mutex` -> vkQueuePresentKHR -> release.
- render thread takes `queue_mutex` -> vkQueueSubmit -> release.

Neither thread holds two locks simultaneously. No nested locking. No deadlock possible.

However, `vkWaitForFences` at line 494 blocks the Qt thread. If the `m_presentFence` is never signaled (e.g., device lost), the Qt GUI thread hangs forever. Should add a timeout.

**LOW ISSUE**: `vkWaitForFences` with `UINT64_MAX` timeout can hang the Qt GUI thread on device loss. Consider a finite timeout with error recovery.

---

## 3. Resource Lifecycle

### 3.1 finalize() and In-Flight Frames

`finalize()` at line 100-114:
```c
if (m_device != VK_NULL_HANDLE)
    vkDeviceWaitIdle(m_device);
destroySwapchain();
destroySurface();
```

`destroySwapchain()` at line 427-462 also calls `vkDeviceWaitIdle()` (line 434).

This is correct: `vkDeviceWaitIdle` ensures all GPU work (including pending presents) has completed before destroying resources.

VERIFIED CORRECT.

### 3.2 Surface Destruction

`destroySurface()` at lines 277-283:
```c
if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
}
```

The VCRenderer creates the surface itself (line 246), so it must destroy it. This is handled correctly.

VERIFIED CORRECT.

### 3.3 Double-Free Potential with vc_close()

**Checked: Potential double-free of display surface and swapchain -- NOT A BUG.**

`vc_core.h` lines 89-94 declare `display_surface` and `display_swapchain` fields in `vc_context_t`. These are destroyed in `vc_close()` at `vc_core.c` lines 657-663:
```c
if (ctx->display_swapchain != VK_NULL_HANDLE && ctx->device != VK_NULL_HANDLE)
    vkDestroySwapchainKHR(ctx->device, ctx->display_swapchain, NULL);
free(ctx->swapchain_images);
if (ctx->display_surface != VK_NULL_HANDLE && ctx->instance != VK_NULL_HANDLE)
    vkDestroySurfaceKHR(ctx->instance, ctx->display_surface, NULL);
```

However, these fields are **never populated** by the current VCRenderer code. The VCRenderer creates and manages its own `m_surface` and `m_swapchain` -- it does NOT store them in `vc_context_t`. So `ctx->display_surface` and `ctx->display_swapchain` remain `VK_NULL_HANDLE` (zero-initialized by calloc), and the cleanup in `vc_close()` is a no-op.

There is NO double-free in the current code. But the dead fields in `vc_context_t` are confusing and should be removed since they serve no purpose.

**Revised verdict**: NO BUG -- but dead code. The `display_surface`, `display_swapchain`, `swapchain_images`, `swapchain_image_count`, `swapchain_format`, `swapchain_extent` fields in `vc_context_t` (lines 89-94) are unused by the current VCRenderer. They appear to be remnants of a planned centralized swapchain approach that was replaced by the current decoupled design.

**Suggested fix**: Remove the dead fields and the cleanup code in `vc_close()` to avoid confusion.

### 3.4 Destruction Order

The shutdown path is:
1. `voodoo_card_close()` sets `vc_global_ctx = NULL`.
2. `vc_close()` calls `vc_thread_close()` (stops render thread).
3. `vc_close()` calls `vkDeviceWaitIdle()`.
4. `vc_close()` destroys all Vulkan resources.
5. `vc_close()` calls `vkDestroyDevice()`.
6. Eventually, Qt destroys the VCRenderer (during widget teardown).

**ISSUE**: The VCRenderer holds cached Vulkan handles (`m_device`, `m_instance`, etc.). If `vc_close()` destroys the device before VCRenderer::finalize() runs, then `finalize()` will call `vkDeviceWaitIdle` and `vkDestroySwapchainKHR` on a destroyed device.

The key question is: does the VCRenderer get destroyed before or after `vc_close()`?

Looking at the shutdown:
- `voodoo_card_close()` is called from the device model cleanup.
- Qt widget cleanup happens when the RendererStack is destroyed.
- These may happen in different order depending on shutdown sequence.

If `vc_close()` runs first: VCRenderer's cached device handle is dangling. `finalize()` will crash or produce undefined behavior.

If VCRenderer::finalize() runs first: it calls `vkDeviceWaitIdle(m_device)` which is still valid, destroys its swapchain, and the surface. Then `vc_close()` runs fine (the device is still alive).

**Verdict**: HIGH (Issue #2). Depends on shutdown order. The VCRenderer should detect that the device is gone (e.g., check `vc_global_ctx == NULL` in finalize()) and skip Vulkan cleanup if the context was already destroyed. Alternatively, ensure the VCRenderer is always destroyed before vc_close() runs by coordinating shutdown order in the main close path.

---

## 4. macOS-Specific (qt_vc_metal_layer.mm)

### 4.1 CAMetalLayer Setup

```objc
NSView *view = (__bridge NSView *) ns_view;
[view setWantsLayer:YES];
CAMetalLayer *layer = [CAMetalLayer layer];
[view setLayer:layer];
return (__bridge void *) layer;
```

This is the standard pattern for attaching a Metal layer to an NSView for Vulkan rendering via MoltenVK.

- `setWantsLayer:YES` enables layer-backed mode. CORRECT.
- `[CAMetalLayer layer]` creates an autoreleased CAMetalLayer. CORRECT.
- `[view setLayer:layer]` sets the view's backing layer. CORRECT.
- `__bridge` cast does not transfer ownership. The layer is retained by the view. CORRECT.

One issue: the `CAMetalLayer` should ideally have its `contentsScale` set to match the window's `backingScaleFactor` for Retina displays. Without this, the layer may render at 1x scale on a 2x Retina display, producing a blurry image. However, MoltenVK typically handles this automatically when creating the `VkSurfaceKHR` from the Metal layer.

**LOW ISSUE**: Missing `layer.contentsScale = view.window.backingScaleFactor` for explicit Retina support. MoltenVK may handle this, but explicit setting is more robust.

### 4.2 NSView Lifetime

The `ns_view` comes from `winId()` on a QWindow. Qt retains ownership of the NSView. When the QWindow is destroyed, the NSView is destroyed, which destroys the CAMetalLayer (since the layer is owned by the view).

If the `VkSurfaceKHR` still references the CAMetalLayer after the view is destroyed, Vulkan calls on that surface would fail. This is guarded by `finalize()` being called from the destructor (`~VCRenderer()`), which destroys the surface before the QWindow base class destroys the NSView.

**MODERATE ISSUE**: The destruction order depends on C++ destructor ordering. `~VCRenderer()` calls `finalize()` first (line 93), then `~QWindow()` runs (destroying the NSView). This is correct -- the VkSurfaceKHR is destroyed before the NSView. However, if any code path triggers Qt to destroy the underlying NSView before `~VCRenderer()` runs (e.g., reparenting), the surface would reference freed memory.

In practice, `createWindowContainer()` at `qt_rendererstack.cpp` line 479 wraps the VCRenderer QWindow. When the container is destroyed, it destroys the VCRenderer. The destruction sequence is safe for the normal case.

**Verdict**: CORRECT for normal shutdown; edge case risk during reparenting (unlikely in practice).

---

## 5. Missing Pieces Check

### 5.1 vc_get_fb_dimensions()
Defined at `vc_core.c` lines 794-804. Returns `render_pass->width` and `render_pass->height`.
VERIFIED EXISTS.

### 5.2 vc_render_pass_front()
Defined as `static inline` at `vc_render_pass.h` lines 89-93. Returns `&rp->fb[1 - rp->back_index]`.
VERIFIED EXISTS. Accessible from `vc_core.c` (which includes `vc_render_pass.h`).

### 5.3 queue_mutex in vc_context_t
Declared at `vc_core.h` line 54: `void *queue_mutex;`.
Created at `vc_core.c` line 549: `ctx->queue_mutex = thread_create_mutex();`.
Destroyed at `vc_core.c` line 671: `thread_close_mutex(ctx->queue_mutex);`.
Exposed via `vc_get_queue_mutex()` at `vc_core.c` lines 1061-1066.
VERIFIED EXISTS and correctly managed.

### 5.4 vc_get_front_color_image()
Defined at `vc_core.c` lines 1068-1075. Returns `vc_render_pass_front(ctx->render_pass)->color_image`.
Declared in `qt_vcrenderer.hpp` line 61 as `extern "C"`.
VERIFIED EXISTS. Note: returns `VkImage` directly (not `void*`), which requires `volk.h` to be included in the header. This is handled correctly at `qt_vcrenderer.hpp` line 45.

### 5.5 Instance-Level WSI Extensions
`vc_core.c` lines 113-124 enable `VK_KHR_SURFACE_EXTENSION_NAME` plus platform-specific extensions (`VK_EXT_METAL_SURFACE_EXTENSION_NAME`, `VK_KHR_WIN32_SURFACE_EXTENSION_NAME`, `VK_KHR_XLIB_SURFACE_EXTENSION_NAME`).
VERIFIED CORRECT.

### 5.6 VK_KHR_swapchain Device Extension
`vc_core.c` lines 337-339 enable `VK_KHR_SWAPCHAIN_EXTENSION_NAME`.
VERIFIED CORRECT.

---

## 6. Additional Observations

### 6.1 Swapchain Image Usage Flags

`qt_vcrenderer.cpp` line 357:
```c
sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
```

Only `VK_IMAGE_USAGE_TRANSFER_DST_BIT` is needed (since we blit to it, not render to it). The `COLOR_ATTACHMENT_BIT` is unnecessary. Not a bug, but adds an unnecessary capability requirement.

**LOW**: Remove `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` from swapchain image usage.

### 6.2 Composite Alpha

Line 361: `sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;`

This should be verified against `caps.supportedCompositeAlpha`. While `OPAQUE_BIT_KHR` is almost universally supported, the spec does not guarantee it.

**LOW**: Should check `caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` and fall back to the first supported mode.

### 6.3 Buffer Size

`qt_vcrenderer.hpp` line 144:
```cpp
static constexpr int kBufSize = 2048 * 2048 * 4;
```

This allocates 16MB per buffer (32MB total) for fallback buffers that are never actually used for rendering (since VCRenderer does zero-copy blit). These buffers exist only to satisfy the `getBuffers()` interface. Consider making them smaller or lazy-allocated.

**LOW**: Wasteful allocation. Could be 1x1 or lazy.

### 6.4 onBlit() Signal Connection

`qt_rendererstack.cpp` line 460:
```cpp
connect(this, &RendererStack::blitToRenderer, vcr, &VCRenderer::onBlit, Qt::QueuedConnection);
```

Uses `Qt::QueuedConnection`, meaning `onBlit()` is invoked in the VCRenderer's (QWindow's) thread, which is the Qt main/GUI thread. This is correct -- all Vulkan presentation should happen on the same thread.

VERIFIED CORRECT.

---

## 7. Verdict Summary

### HIGH
1. **vc_global_ctx data race** (vc_core.c line 1078 / vid_voodoo_reg.c line 104 / qt_vcrenderer.cpp line 162): Plain pointer written from deferred init thread, read from Qt GUI thread without synchronization. Fix: use `_Atomic` or `std::atomic`.
2. **Shutdown ordering risk** (vc_core.c / qt_vcrenderer.cpp): If vc_close() destroys the VkDevice before VCRenderer::finalize() runs, the cached device handle in VCRenderer becomes dangling. VCRenderer should check vc_global_ctx==NULL in finalize() to skip Vulkan cleanup if context was already destroyed.

### MODERATE
3. **NSView lifetime during reparenting** (qt_vc_metal_layer.mm): If Qt reparents or destroys the underlying NSView before VCRenderer::finalize(), the VkSurface references freed memory. Unlikely in practice.
4. **Post-blit dstStageMask over-specification** (qt_vcrenderer.cpp line 610): `COLOR_ATTACHMENT_OUTPUT_BIT` is unnecessary for the swapchain image's transition to PRESENT_SRC. Harmless.
5. **Suboptimal swapchain drops a frame** (qt_vcrenderer.cpp line 503-506): Could present the acquired image before recreating for smoother resize.

### LOW
6. Missing `contentsScale` on CAMetalLayer for Retina (qt_vc_metal_layer.mm line 32).
7. Unnecessary `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` on swapchain (qt_vcrenderer.cpp line 357).
8. 32MB unused fallback buffers (qt_vcrenderer.hpp line 144).
9. `vkWaitForFences` with `UINT64_MAX` can hang GUI on device lost (qt_vcrenderer.cpp line 494). Consider finite timeout.

### CORRECT
- **Cross-submission sync** on front image: implicit single-queue memory dependency guarantees visibility (Vulkan 1.2 spec Chapter 7.2). Barrier provides required layout transition.
- Image layout transition sequence (UNDEFINED->DST->PRESENT for swapchain)
- Front image transition (COLOR_ATTACHMENT_OPTIMAL->TRANSFER_SRC->COLOR_ATTACHMENT_OPTIMAL)
- Semaphore/fence present loop pattern
- RGBA8->BGRA8 blit format compatibility with VK_FILTER_LINEAR
- Queue mutex serialization (no deadlock potential)
- std::atomic<bool> for m_needsResize (relaxed ordering sufficient)
- finalize() calls vkDeviceWaitIdle before destroy
- Surface creation and destruction (platform-specific WSI via volk)
- All required accessors, extensions, and declarations exist
- No double-free (vc_context_t display fields are unused/dead, VCRenderer manages its own resources)
