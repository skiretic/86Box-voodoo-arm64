# VCRenderer Code Audit

**Date**: 2026-02-28
**Auditor**: vc-debug
**Branch**: videocommon-voodoo
**Commit**: 1636a90ea

**Scope**: Qt display integration for VideoCommon zero-copy GPU presentation.

Files audited:
- `src/qt/qt_vcrenderer.hpp` (214 lines)
- `src/qt/qt_vcrenderer.cpp` (1551 lines)
- `src/qt/qt_vc_metal_layer.mm` (47 lines)

Supporting files referenced:
- `src/video/videocommon/vc_core.c` (vc_get_front_color_image, vc_get_queue_mutex, etc.)
- `src/video/videocommon/vc_core.h` (vc_context_t struct)
- `src/video/videocommon/vc_render_pass.h` (vc_render_pass_front, back_index)
- `src/video/videocommon/vc_thread.c` (render thread queue submit)
- `src/video/vid_voodoo.c` (teardown ordering)
- `src/video/videocommon/shaders/postprocess.vert` (fullscreen triangle)
- `src/video/videocommon/shaders/postprocess.frag` (CRT effects)

---

## qt_vcrenderer.hpp -- 214 lines

### Issues Found

- **[MODERATE]** Line 90: `int direct_present_active` in `vc_core.h` is a plain `int`, not `_Atomic int`. It is written by `vc_set_direct_present()` from the GUI thread and could be read by other threads (e.g., voodoo_callback). While single-word int writes are effectively atomic on x86/ARM64, the C11 memory model technically requires atomic or a mutex for cross-thread access. Should be `_Atomic int` for correctness.

- **[LOW]** Line 165: `m_ppEnabled = true` as the default value means the post-processing pipeline is always attempted. If post-processing creation fails, the fallback to blit is handled (line 312), but the default-true semantic means every fresh VCRenderer will attempt a more complex pipeline even when effects are disabled (scanline=0, curvature=0). This is functionally correct but wasteful -- the shader still runs even when it is a passthrough.

- **[LOW]** Line 207: `kBufSize = 2048 * 2048 * 4` = 16 MB per buffer, 32 MB total for fallback buffers that are only used during BIOS/VGA pre-Voodoo. These are allocated unconditionally in the constructor. Consider lazy allocation.

### Notes

- The class correctly uses `std::atomic<bool>` for `m_needsResize` (line 188), which is set from the resize event thread and read from onBlit/present.
- `m_bufFlag[2]` uses `std::atomic_flag` with `ATOMIC_FLAG_INIT`, which is correct C++11 initialization.
- Forward declarations of VC C API in the header (lines 52-74) properly use `extern "C"` to prevent name mangling.
- The header correctly includes `volk.h` outside the extern "C" block (line 48) since volk includes Vulkan platform headers that must be compiled as C++.

---

## qt_vcrenderer.cpp -- 1551 lines

### Threading Model Summary

Before analyzing issues, establishing which thread calls what:

| Method | Thread | Evidence |
|--------|--------|----------|
| `VCRenderer()` constructor | GUI thread | Qt object creation |
| `~VCRenderer()` destructor | GUI thread | Qt object destruction |
| `finalize()` | GUI thread | Called from destructor |
| `exposeEvent()` | GUI thread | Qt event handler |
| `resizeEvent()` | GUI thread | Qt event handler |
| `onBlit()` | GUI thread | Connected via queued signal from blit thread |
| `present()` | GUI thread | Called from onBlit() |
| `paintFallback()` | GUI thread | Called from onBlit() |
| Timer lambda (line 120) | GUI thread | QTimer fires on owner's thread |
| `tryInitializeVulkan()` | GUI thread | Called from exposeEvent/onBlit/timer |

All VCRenderer methods run on the GUI thread. The render thread only touches:
- `vc_get_queue_mutex()` to serialize vkQueueSubmit
- `vc_framebuffer_swap()` to flip back_index (vc_thread.c:335)
- `vc_get_front_color_image()` called from GUI thread reads `back_index`

### Issues Found

#### Critical

- **[CRITICAL]** Lines 1189-1194: **VK_SUBOPTIMAL_KHR at acquire leaks semaphore signal state.** When `vkAcquireNextImageKHR` returns `VK_SUBOPTIMAL_KHR`, the Vulkan spec says the acquire succeeded and the semaphore WAS signaled (`m_imageAvailable` is now in the signaled state). But the code treats `VK_SUBOPTIMAL_KHR` the same as `VK_ERROR_OUT_OF_DATE_KHR` and calls `recreateSwapchain()`, which destroys `m_imageAvailable` without consuming the signal. This violates the Vulkan spec: "A semaphore that was signaled by a successful acquire must be waited on before the application uses it again as a signal semaphore." On some drivers (especially validation layers), this causes errors. On others it may work by accident. **Fix**: Handle `VK_SUBOPTIMAL_KHR` at acquire by continuing the present (it is a non-error success code), and only recreate the swapchain after the present completes. Alternatively, consume the semaphore via an empty submit before recreating.

  ```cpp
  // Current (wrong):
  if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
      recreateSwapchain();  // destroys semaphore without consuming signal
      return;
  }

  // Correct: only OUT_OF_DATE at acquire triggers immediate recreation.
  // SUBOPTIMAL at acquire means "it works but could be better" -- continue.
  if (res == VK_ERROR_OUT_OF_DATE_KHR) {
      recreateSwapchain();
      return;
  }
  // VK_SUBOPTIMAL_KHR falls through to normal present path.
  // Recreate after present if SUBOPTIMAL was returned.
  ```

- **[CRITICAL]** Lines 1393-1396: **Fence deadlock on vkQueueSubmit failure.** If `vkQueueSubmit` fails (line 1389), the fence `m_presentFence` was already reset at line 1181 and is in the unsignaled state. The function returns at line 1395 without signaling the fence. The next call to `present()` will block forever at `vkWaitForFences` (line 1180). Additionally, `m_imageAvailable` was signaled by the acquire and `m_renderFinished` was specified as a signal semaphore -- on submit failure, these are in an undefined state. **Fix**: On submit failure, the semaphore and fence states are undefined per the Vulkan spec (the application should treat the device as lost). At minimum, recreate the swapchain to get fresh sync objects:

  ```cpp
  if (res != VK_SUCCESS) {
      pclog("VCRenderer: vkQueueSubmit failed: %d\n", res);
      recreateSwapchain();  // destroys and recreates fence (starts signaled)
      return;
  }
  ```

- **[CRITICAL]** Line 54 in `vc_render_pass.h`, referenced from `vc_get_front_color_image()`: **Data race on `back_index`.** The `back_index` field is a plain `int` written by the render thread in `vc_framebuffer_swap()` (vc_thread.c:335) and read by the GUI thread via `vc_render_pass_front()` inside `vc_get_front_color_image()` (vc_core.c:1063). There is no synchronization between these accesses. The render thread writes `back_index` AFTER `vkQueueSubmit` (vc_thread.c:325-327) and after the queue mutex is released (line 327), then calls `vc_framebuffer_swap` at line 335. The GUI thread reads `back_index` without holding any mutex. This is a textbook data race under C11/C++11.

  In practice on ARM64 and x86-64 with a single-word int, the torn-read risk is nil (aligned int reads/writes are atomic at the hardware level). But the compiler is within its rights to optimize assuming no race (e.g., cache the value, reorder loads). **Fix**: Make `back_index` an `_Atomic int` and use `atomic_load`/`atomic_store` with appropriate memory ordering:

  ```c
  // vc_render_pass.h
  _Atomic int back_index;

  // vc_framebuffer_swap:
  atomic_store_explicit(&rp->back_index, 1 - atomic_load_explicit(&rp->back_index, memory_order_relaxed), memory_order_release);

  // vc_render_pass_front:
  return &rp->fb[1 - atomic_load_explicit(&rp->back_index, memory_order_acquire)];
  ```

#### High

- **[HIGH]** Lines 165-167 in `finalize()`: **Use-after-free on m_vcCtx.** The call `vc_set_direct_present(m_vcCtx, 0)` at line 167 uses the cached `m_vcCtx` pointer, which may already be freed. The teardown sequence in `vid_voodoo.c` (lines 1559-1561) is:

  ```c
  vc_set_global_ctx(NULL);   // 1559: clears atomic pointer
  vc_close(vc_ctx);          // 1561: frees the vc_context_t struct
  ```

  If `VCRenderer::finalize()` runs AFTER line 1561 (vc_close has freed the struct), then `m_vcCtx` is a dangling pointer and `vc_set_direct_present(m_vcCtx, 0)` is a use-after-free. The `vc_set_direct_present` function only checks for NULL, not for freed memory. The NULL check at line 173 (`vc_get_global_ctx()`) happens too late -- it should be checked BEFORE the `vc_set_direct_present` call.

  **Fix**: Move the context liveness check before the direct_present call:

  ```cpp
  void VCRenderer::finalize()
  {
      if (m_finalized)
          return;
      m_finalized = true;

      // ... timer and backing store cleanup ...

      vc_context_t *ctx = vc_get_global_ctx();
      if (!ctx || m_device == VK_NULL_HANDLE) {
          m_initialized = false;
          return;
      }

      // Context is still alive -- safe to use m_vcCtx
      vc_set_direct_present(m_vcCtx, 0);

      vkDeviceWaitIdle(m_device);
      // ... rest of cleanup ...
  }
  ```

- **[HIGH]** Lines 1407-1411: **Queue mutex held across vkQueuePresentKHR.** This is the documented freeze bug. When using `VK_PRESENT_MODE_FIFO_KHR` (which is the fallback when MAILBOX is unavailable), `vkQueuePresentKHR` blocks until vsync (~16ms at 60Hz). During this time, the GUI thread holds `queue_mutex`, starving the render thread which needs the same mutex for `vkQueueSubmit` (vc_thread.c:325). At 60fps, the render thread loses up to 16ms per frame waiting for the present to unblock.

  The code mitigates this by preferring `VK_PRESENT_MODE_MAILBOX_KHR` (lines 483-498), which returns immediately. But MAILBOX is not universally available (it is unavailable on some Wayland compositors, some embedded GPUs, and some MoltenVK configurations). When falling back to FIFO, the contention is severe.

  **Analysis of the MAILBOX mitigation**: On macOS with MoltenVK, MAILBOX is typically available via `VK_PRESENT_MODE_MAILBOX_KHR` mapped to Metal's display link. The current code prefers MAILBOX (line 492-494), which is the correct mitigation for macOS. But on platforms where MAILBOX is unavailable, the FIFO path will cause measurable render thread stalls.

  **Root cause**: Using a single VkQueue for both rendering and presentation means they must share a mutex. The proper fix is one of:
  1. Move presentation to the render thread (VC_CMD_PRESENT ring command)
  2. Use a separate presentation queue (if the device supports it)
  3. Accept the FIFO contention as a known limitation on non-MAILBOX platforms

  **Note**: The comment at lines 477-482 documents this issue well, and MAILBOX is the active mitigation. This is a known issue, not an undiscovered bug.

- **[HIGH]** Line 516: **oldSwapchain is always VK_NULL_HANDLE.** When recreating the swapchain (via `recreateSwapchain` -> `destroySwapchain` -> `createSwapchain`), the old swapchain is destroyed before the new one is created. Setting `sci.oldSwapchain = m_swapchain` (the old one) before destroying it would allow the driver to reuse internal resources and potentially avoid a brief visible glitch during recreation. The Vulkan spec says:

  > "If oldSwapchain is not VK_NULL_HANDLE, oldSwapchain is retired even if creation of the new swapchain fails."

  The current approach (destroy first, then create with oldSwapchain=NULL) works correctly but is suboptimal. During the window between destroy and create, the surface has no active swapchain, which can cause a brief flash on some platforms.

  **Fix**: Pass the old swapchain to `createSwapchain()` and destroy it after the new one is created:

  ```cpp
  void VCRenderer::recreateSwapchain()
  {
      VkSwapchainKHR oldSwap = m_swapchain;
      m_swapchain = VK_NULL_HANDLE;
      // ... create new swapchain with sci.oldSwapchain = oldSwap ...
      vkDestroySwapchainKHR(m_device, oldSwap, nullptr);
  }
  ```

- **[HIGH]** Line 513: **compositeAlpha hardcoded to OPAQUE without checking support.** The code sets `sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` without verifying that this bit is set in `caps.supportedCompositeAlpha`. While OPAQUE is virtually universal, the Vulkan spec says the application must use a supported value. On exotic compositors or with XWayland, this could fail swapchain creation.

  **Fix**: Check `caps.supportedCompositeAlpha` and pick the first supported bit:

  ```cpp
  VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
      if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
          compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
      // else try other bits...
  }
  sci.compositeAlpha = compositeAlpha;
  ```

#### Moderate

- **[MODERATE]** Lines 1041: **vkUpdateDescriptorSets called every frame in presentWithPostProcess.** The descriptor set is updated with `vkUpdateDescriptorSets` on every present call (line 1041), even when the front image view hasn't changed. Per the Vulkan spec, `vkUpdateDescriptorSets` must not be called while the descriptor set is in use by a command buffer that has been submitted. The `vkWaitForFences` at line 1180 ensures the previous frame's command buffer has completed, so this is technically safe. But calling it every frame is unnecessary overhead. **Optimization**: Track the last `frontView` and only update when it changes.

- **[MODERATE]** Lines 386-394: **Linux surface creation only supports X11, not Wayland.** The `#else` (non-Apple, non-Windows) path uses `vkCreateXlibSurfaceKHR` unconditionally. Modern Linux desktops increasingly default to Wayland sessions (Fedora, Ubuntu 22.04+). Under Wayland, there is no X11 display, and `QX11Info::display()` may return NULL or cause a crash. `VK_KHR_wayland_surface` would be needed. This is a platform limitation, not a bug per se, but it means VCRenderer will not work on Wayland-native Qt builds.

  **Fix for future**: Detect the Qt platform at runtime and use the appropriate WSI:
  ```cpp
  #if defined(__linux__) || defined(__FreeBSD__)
  if (QGuiApplication::platformName() == "wayland") {
      // Use vkCreateWaylandSurfaceKHR
  } else {
      // Use vkCreateXlibSurfaceKHR
  }
  #endif
  ```

- **[MODERATE]** Line 1180: **vkWaitForFences with UINT64_MAX timeout.** The fence wait uses an infinite timeout. If the GPU hangs (VK_ERROR_DEVICE_LOST), this call will block the GUI thread forever, making the application unresponsive. The user would need to force-kill the process. A finite timeout (e.g., 5 seconds) with error handling would be more robust:

  ```cpp
  VkResult waitRes = vkWaitForFences(m_device, 1, &m_presentFence, VK_TRUE, 5000000000ULL);
  if (waitRes == VK_TIMEOUT) {
      pclog("VCRenderer: Fence wait timed out -- possible GPU hang\n");
      // Try to recover or bail out
      return;
  }
  if (waitRes == VK_ERROR_DEVICE_LOST) {
      pclog("VCRenderer: Device lost during fence wait\n");
      m_swapchainValid = false;
      return;
  }
  ```

- **[MODERATE]** Line 1186: **vkAcquireNextImageKHR with UINT64_MAX timeout.** Same concern as the fence wait -- infinite timeout blocks the GUI thread if the presentation engine hangs. Use a finite timeout.

- **[MODERATE]** Lines 436-441: **No error check on second vkGetPhysicalDeviceSurfaceFormatsKHR call.** The first call (line 436-437) to get the count succeeds, but the second call (line 439-440) with the data pointer does not check the VkResult. If it fails, `formats` will contain uninitialized data, and the format selection loop below will read garbage. The `formatCount` is also not rechecked (the spec allows it to change between calls, though this is rare).

- **[MODERATE]** Lines 485-490: **No error check on vkGetPhysicalDeviceSurfacePresentModesKHR.** Same pattern as the format enumeration -- the VkResult of both calls is unchecked.

- **[MODERATE]** Lines 557-568: **Semaphore and fence creation error handling leaks resources.** If `vkCreateSemaphore` for `m_renderFinished` fails (line 559), the function returns false but `m_imageAvailable` (created at line 556) is not destroyed. Similarly, if `vkCreateFence` fails (line 567), both semaphores leak. The caller (`createSwapchain`) returns false but the partial cleanup in `destroySwapchain` checks for VK_NULL_HANDLE, so the already-created semaphore will be cleaned up there. This is actually safe because `destroySwapchain` is called in `recreateSwapchain` and `finalize`, both of which handle partial initialization. **Verdict**: Not a leak, but the implicit reliance on destroySwapchain for cleanup after partial init is fragile.

#### Low

- **[LOW]** Lines 1262-1266, 1279-1282, 1451-1454: **Static bool logging flags persist across VCRenderer instances.** If VCRenderer is destroyed and recreated (e.g., monitor switch or window recreation), these `static bool` variables remain true and the one-time log messages will not fire for the new instance. Use member variables instead of static locals.

- **[LOW]** Line 109-110: **Fallback buffers zero-initialized via `()`**. The `new uint8_t[kBufSize]()` syntax value-initializes (zeroes) 16 MB per buffer. This is correct but wastes time initializing 32 MB that may never be used (if Vulkan init succeeds quickly). Consider allocating but not zeroing, or deferring allocation.

- **[LOW]** Line 526-528: **Raw `new[]` for swapchain images.** Uses `new VkImage[m_imageCount]` instead of `std::vector<VkImage>`. Manual memory management -- the corresponding `delete[]` at line 609 is correct, but a vector would be safer and more idiomatic C++.

- **[LOW]** Lines 905-906: **Raw `new[]` for swapchain views and framebuffers.** Same concern as above.

- **[LOW]** Line 98: **QWindow() constructor called without surface type.** The comment at lines 104-106 explains this is intentional (using RasterSurface since Qt's Vulkan integration is bypassed). This is correct on macOS where QT_NO_VULKAN is defined. On other platforms, `setSurfaceType(QSurface::VulkanSurface)` would be more semantically correct but not functionally required since we create the surface ourselves.

- **[LOW]** Line 497-498: **Present mode log message uses ternary.** Only distinguishes MAILBOX vs FIFO. If the driver returns another mode (e.g., IMMEDIATE), the log would misleadingly say "FIFO". Minor log accuracy issue.

- **[LOW]** Line 728: **Render pass loadOp is DONT_CARE.** For the post-processing render pass, the load op for the color attachment is `VK_ATTACHMENT_LOAD_OP_DONT_CARE`. This is correct since the fullscreen triangle covers every pixel, so there is no need to clear. But if the triangle doesn't cover the entire framebuffer (e.g., due to a viewport mismatch), undefined content would be visible. The viewport is set to the full swapchain extent, so this is safe in practice.

- **[LOW]** Line 376: **`VkMetalSurfaceCreateInfoEXT` cast discards const.** The cast `(const CAMetalLayer *) m_metalLayer` is correct, but `m_metalLayer` is stored as `void *`, losing type safety. The CAMetalLayer pointer is retained by the NSView (line 42 of qt_vc_metal_layer.mm: `[view setLayer:layer]`), so the NSView owns it. But `m_metalLayer` is never used after surface creation, making it dead storage.

### Notes

- **Queue mutex contention analysis**: The present() function acquires the queue mutex twice: once for `vkQueueSubmit` (line 1388) and once for `vkQueuePresentKHR` (line 1408). With MAILBOX, `vkQueuePresentKHR` returns immediately, so the second lock is brief. With FIFO, the second lock blocks for up to one vsync interval (~16.67ms at 60Hz). During this time, the render thread (vc_thread.c:325-327) cannot submit. At 60fps, this is a worst-case 100% collision rate -- the present blocks the full inter-frame interval.

- **Swapchain lifecycle is sound**: The create/destroy/recreate pattern properly handles all resources. `destroySwapchain` calls `vkDeviceWaitIdle` (line 586) before destroying anything, which is the simplest correct approach (though it stalls the pipeline). `destroyPostProcess` is called from `destroySwapchain`, and handles partial initialization gracefully (null checks on every handle).

- **Error recovery philosophy**: The code generally tries to keep running after errors rather than crashing. The `submit_empty` path (lines 1421-1441) is a thoughtful deadlock-avoidance mechanism. The fence recovery for failed acquires (lines 1196-1208) is also correct. The one gap is the vkQueueSubmit failure path (lines 1393-1396).

- **Post-processing pipeline correctness**: The shader, pipeline, descriptor set, and command buffer recording all look correct. The barrier sequence in `presentWithPostProcess()` correctly transitions the front image to SHADER_READ_ONLY, runs the render pass (which transitions the swapchain image to PRESENT_SRC via finalLayout), then transitions the front image back to COLOR_ATTACHMENT_OPTIMAL. The push constant struct matches the shader layout.

- **QBackingStore fallback is correct**: The `paintFallback()` method properly wraps the raw buffer as a QImage (no copy), uses QPainter for compositing, and flushes through QBackingStore. The resize handling via `onResize()` mirrors SoftwareRenderer behavior.

- **Timer-based deferred init is robust**: The 100ms retry timer (lines 119-131) correctly handles the chicken-and-egg problem where the VCRenderer is created before the Voodoo device initializes its VC context. The timer stops itself on success or finalize.

---

## qt_vc_metal_layer.mm -- 47 lines

### Issues Found

- **[LOW]** Lines 37-40: **contentsScale may be stale if window moves between screens.** The `contentsScale` is set once during layer creation based on the current screen's backing scale factor. If the window is later moved to a screen with a different DPI (e.g., external monitor vs Retina display), the layer scale will not update. This could cause blurriness or incorrect scaling on the destination screen. A proper fix would involve observing `NSWindowDidChangeBackingPropertiesNotification`.

- **[LOW]** Line 44: **Bridging cast does not retain the layer.** The `(__bridge void *)` cast transfers the CAMetalLayer pointer without retaining it. The layer is retained by the NSView (via `setLayer:`), so this is correct as long as the NSView outlives the VCRenderer. Since the QWindow (which wraps the NSView) owns the VCRenderer, this lifetime guarantee holds.

### Notes

- The function correctly uses `[view setWantsLayer:YES]` before setting the layer, which is required on macOS for layer-backed views.
- The Retina handling (lines 35-40) uses `backingScaleFactor` which is the correct macOS API for this purpose.
- The `extern "C"` linkage (line 24) ensures the function is callable from C++ (qt_vcrenderer.cpp line 68).
- The `#ifdef USE_VIDEOCOMMON` guard matches the header, preventing compilation when VideoCommon is disabled.

---

## Cross-Cutting Concerns

### Teardown Race Condition

The most dangerous cross-cutting issue is the teardown race between `voodoo_card_close()` and `VCRenderer::finalize()`. The sequence is:

1. `voodoo_card_close()` calls `vc_set_global_ctx(NULL)` (vid_voodoo.c:1559)
2. `voodoo_card_close()` calls `vc_close(vc_ctx)` (vid_voodoo.c:1561) -- frees the struct
3. At some point, `VCRenderer::finalize()` runs on the GUI thread

If step 3 runs after step 2, then `m_vcCtx` (cached at init time) is a dangling pointer. The code at line 167 (`vc_set_direct_present(m_vcCtx, 0)`) dereferences it before the NULL check at line 173.

Even the "safe" path (step 3 between steps 1 and 2) has a TOCTOU window: `vc_get_global_ctx()` returns NULL at line 173 causing early return, but what if the close happens during `vkDeviceWaitIdle` at line 179? The device could be destroyed while we're waiting.

**Recommended fix**: VCRenderer should not cache `m_vcCtx`. Always go through `vc_get_global_ctx()` for every access. Or better: add a shutdown coordination mechanism (e.g., an atomic flag or a shared_ptr-like ref count).

### Front Image Access Without Synchronization

The `vc_get_front_color_image()` call at line 1162 (and re-fetch at line 1215) reads from the front framebuffer. But the render thread may be in the middle of:
1. Rendering to the back buffer
2. Calling `vc_framebuffer_swap()` (flipping front/back)
3. Starting to render to what was the front buffer

Between steps 2 and 3, the GUI thread might read the "new front" which the render thread is about to start rendering to. The queue mutex only protects `vkQueueSubmit`, not the entire render cycle. However, in practice:
- The render thread renders to the BACK buffer
- After submit + swap, the old back becomes front
- The render thread then starts rendering to the new back
- The GUI thread reads the front (old back) which is no longer being rendered to

So the actual image data is safe (the render thread is done with it). The risk is only the `back_index` read (the data race noted above). If `back_index` is made atomic, this concern is resolved.

### Descriptor Set Update Timing

In `presentWithPostProcess()` (line 1041), `vkUpdateDescriptorSets` is called to point the descriptor at the current front image view. The Vulkan spec says descriptor updates must not happen while the descriptor set is bound to a command buffer that is executing. The `vkWaitForFences` call at line 1180 ensures the previous frame's command buffer has completed, so the descriptor set is not in use. This ordering is correct.

However, the descriptor set is allocated once (line 891) and reused every frame. If the `m_ppDescPool` were reset or the descriptor set were freed between frames (it isn't), this would be a use-after-free. The current code is correct.

---

## Summary Table

| Severity | Count | Key Issues |
|----------|-------|------------|
| CRITICAL | 3 | Semaphore leak on SUBOPTIMAL acquire, fence deadlock on submit failure, data race on back_index |
| HIGH | 4 | Use-after-free in finalize, queue mutex contention (known), missing oldSwapchain, unchecked compositeAlpha |
| MODERATE | 6 | Descriptor update every frame, no Wayland support, infinite timeouts, unchecked VkResult on format/mode queries, partial resource leak on semaphore creation failure |
| LOW | 8 | Static bool logging, zero-init waste, raw new[], missing surface type, log inaccuracy, stale contentsScale, dead m_metalLayer storage, DONT_CARE loadOp |

### Priority Fixes

1. **Fix VK_SUBOPTIMAL_KHR at acquire** (CRITICAL): Do not recreate swapchain -- continue the present. Recreate at the end or on the next frame.
2. **Fix fence deadlock on submit failure** (CRITICAL): Call `recreateSwapchain()` to get fresh sync objects.
3. **Make back_index atomic** (CRITICAL): Prevent data race between render thread and GUI thread.
4. **Fix use-after-free in finalize** (HIGH): Check `vc_get_global_ctx()` before using cached `m_vcCtx`.
5. **Check compositeAlpha support** (HIGH): Query `caps.supportedCompositeAlpha` before hardcoding OPAQUE.
6. **Use finite timeouts** (MODERATE): Prevent GUI thread from hanging forever on GPU issues.
