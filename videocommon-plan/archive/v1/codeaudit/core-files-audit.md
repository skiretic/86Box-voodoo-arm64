# VideoCommon Core Files Audit

**Date**: 2026-02-28
**Files audited**: `vc_internal.h`, `vc_core.h`, `vc_core.c`
**Auditor**: vc-debug agent

---

## vc_internal.h -- 78 lines

### Issues Found

- **[MODERATE] Line 32: `#define HAVE_STDARG_H` before `86box.h` is fragile.**
  This macro is defined unconditionally to expose `pclog_ex()` in `86box.h`. If `86box.h` ever changes or if `HAVE_STDARG_H` is already defined by the build system (e.g., via CMake configure checks), this could silently change behavior. The correct approach would be to ensure the build system always defines it, or to check `#ifndef HAVE_STDARG_H` before defining it.

  **Severity justification**: Moderate because it works today and the macro is simple (just a guard), but it's a maintenance trap if the codebase evolves.

- **[LOW] Lines 44-53: `vc_log()` is defined as a `static inline` function in a header.**
  Every translation unit that includes `vc_internal.h` with `ENABLE_VIDEOCOMMON_LOG` defined will get its own copy of this function. This is fine for an inline function but means the `videocommon_do_log` extern variable must be defined in exactly one TU. If multiple `.c` files define `ENABLE_VIDEOCOMMON_LOG 1`, they all reference `videocommon_do_log` -- which is defined in `vc_core.c` line 26. This is correct, but fragile: if `vc_core.c` ever stops defining `ENABLE_VIDEOCOMMON_LOG`, the extern would be unresolved.

  **Status**: Works correctly today. The pattern matches standard 86Box convention.

- **[LOW] Lines 69-76: `VK_CHECK` macro uses `pclog()` directly instead of `vc_log()`.**
  This means VK_CHECK always logs errors regardless of whether `ENABLE_VIDEOCOMMON_LOG` is defined. This is actually **correct behavior** -- Vulkan errors should always be logged -- but the inconsistency with `vc_log()` for debug messages could confuse maintainers.

  **Status**: Intentional design, not a bug.

### Notes
- Header guard is correct (`VC_INTERNAL_H`).
- Include order is correct: `stdarg.h`, `stddef.h`, `stdint.h`, `string.h`, then `volk.h` (before Vulkan headers), then `vk_mem_alloc.h`, then `86box.h`.
- The `VK_CHECK` macro is well-designed: evaluates `result` once (if used correctly), logs with context, and returns the specified value. The double-evaluation risk is minimal since `result` is always a VkResult variable in practice.
- No thread safety issues in this header (it only defines logging and macros).

---

## vc_core.h -- 179 lines

### Issues Found

- **[HIGH] Line 90: `direct_present_active` is a plain `int`, not `_Atomic int`.**
  This field is written by the Qt GUI thread (via `vc_set_direct_present()`) and read by the CPU/timer thread (via `vc_get_direct_present()` in `voodoo_callback()`). On ARM64 (Apple Silicon), plain int reads/writes are naturally atomic (aligned 32-bit), but the C11 standard does not guarantee this -- it is a data race and technically undefined behavior. The compiler could optimize away re-reads, cache the value in a register, or reorder across it.

  Compare with `frames_completed` on line 83, which is correctly declared `_Atomic int` for the same cross-thread access pattern.

  **Fix**: Change to `_Atomic int direct_present_active;` in `vc_core.h`, and use `atomic_store`/`atomic_load` in the accessors in `vc_core.c` (lines 1081-1091).

- **[MODERATE] Lines 131-136: Dispatchable handle accessors return `void*` -- correct for dispatchable handles only.**
  The comment on line 128-129 correctly notes that dispatchable handles (VkInstance, VkDevice, VkPhysicalDevice, VkQueue) are pointer-sized. This is correct per the Vulkan spec. However, `vc_get_front_color_image()` on line 144 returns `VkImage` directly (not `void*`). `VkImage` is a **non-dispatchable handle** which is `uint64_t` on 64-bit platforms but could be a different size on 32-bit. Since this project targets 64-bit only (ARM64, x86-64), this is fine in practice, but the inconsistency between the `void*` accessors and the `VkImage` accessor is worth noting.

  **Status**: Not a bug on target platforms. The `VkImage` return is actually cleaner since it avoids the cast.

- **[LOW] Line 114-117: `vc_has_cap()` does not check for NULL ctx.**
  If called with a NULL context (which shouldn't happen in normal operation), it will dereference NULL. All other accessors check for NULL. This is an inline function likely only called after init, so the risk is low.

  **Status**: Minor inconsistency. All callers should have a valid ctx.

### Notes
- The `vc_context_t` struct layout is clean: Vulkan core objects first, allocator, caps, debug, dimensions, sub-modules, then frame tracking.
- Forward declarations via repeated compatible typedefs (C11 feature) is documented and correct.
- The global context pattern (`vc_get_global_ctx`/`vc_set_global_ctx`) with acquire/release atomics is well-designed for C/C++ interop.
- The `vc_capability_flags_t` enum uses bit flags correctly.
- Sub-module ownership is clear: `render_pass` is heap-allocated (nullable pointer), others are embedded structs.

---

## vc_core.c -- 1169 lines

### Issues Found

- **[HIGH] Line 673: `vc_log()` called after `free(ctx)` -- use-after-free of logging context.**
  ```c
  free(ctx);
  vc_log("VideoCommon: Shutdown complete\n");
  ```
  While `vc_log()` doesn't actually dereference `ctx` (it just calls `pclog_ex()` through the `videocommon_do_log` global), this is not a use-after-free bug in practice. However, if `vc_log` ever changes to access context state, this would become one. More importantly, the `vc_log` macro expands to a function call that references only the global `videocommon_do_log`, so this is **safe but poor practice**.

  **Fix**: Move the log before `free(ctx)`, or change to `pclog()` directly.

- **[HIGH] Lines 1080-1091: `direct_present_active` accessed without atomics across threads.**
  ```c
  void vc_set_direct_present(vc_context_t *ctx, int active)
  {
      if (ctx)
          ctx->direct_present_active = active;
  }
  int vc_get_direct_present(vc_context_t *ctx)
  {
      return ctx ? ctx->direct_present_active : 0;
  }
  ```
  As noted in the `vc_core.h` audit above, `direct_present_active` is a plain `int` accessed from multiple threads (GUI writer, timer/CPU reader). This is a C11 data race.

  **Fix**: Declare as `_Atomic int` in the struct, use `atomic_store_explicit(&ctx->direct_present_active, active, memory_order_release)` and `atomic_load_explicit(&ctx->direct_present_active, memory_order_acquire)`.

- **[MODERATE] Lines 121-123: Linux WSI assumes X11 unconditionally.**
  ```c
  #else
      extensions[ext_count++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
      vc_log("VideoCommon: Enabling VK_KHR_xlib_surface (Linux/X11)\n");
  #endif
  ```
  The `#else` clause catches all non-macOS, non-Windows platforms and assumes X11. This fails on:
  1. **Wayland-only systems** (increasingly common on modern Linux, e.g., Fedora, Ubuntu 22.04+)
  2. **Pi 5** (one of the explicit target platforms in the design doc) which may use either X11 or Wayland
  3. **FreeBSD** or other Unix-like systems

  The Qt 5 backend knows which windowing system it's using. A more robust approach would be to query `QGuiApplication::platformName()` at runtime, or to add compile-time detection for `WAYLAND_DISPLAY` / `DISPLAY` environment variables, or to enable both X11 and Wayland extensions when available.

  **Fix**: Add Wayland support:
  ```c
  #elif defined(WAYLAND_DISPLAY) || defined(VK_USE_PLATFORM_WAYLAND_KHR)
      extensions[ext_count++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
  #else
      extensions[ext_count++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
  #endif
  ```
  Or better: enable both at instance level and let surface creation pick the right one at runtime.

- **[MODERATE] Line 133: Return value of `vkEnumerateInstanceLayerProperties` not checked.**
  ```c
  vkEnumerateInstanceLayerProperties(&available_count, NULL);
  ```
  This call can fail (e.g., `VK_ERROR_OUT_OF_HOST_MEMORY`). The result is ignored. If it fails, `available_count` remains 0, so the code gracefully skips validation -- not a crash, but a silent failure with no log message.

  Similarly on line 137:
  ```c
  vkEnumerateInstanceLayerProperties(&available_count, available);
  ```
  Also unchecked. If this fails, the `available` array may contain garbage and the strcmp loop could read uninitialized memory.

  **Fix**: Check the return value and log on failure.

- **[MODERATE] Line 208: Return value of `vkEnumeratePhysicalDevices` not checked.**
  ```c
  vkEnumeratePhysicalDevices(instance, &count, NULL);
  ```
  Same pattern as above. Failure could leave `count` uninitialized or zero; the zero case is handled (line 209), but `VK_INCOMPLETE` or other errors would be missed.

  Similarly line 217:
  ```c
  vkEnumeratePhysicalDevices(instance, &count, devices);
  ```
  Unchecked. `VK_INCOMPLETE` here means some devices were enumerated but not all -- would work but miss devices.

- **[MODERATE] Lines 298-306: Return value of `vkEnumerateDeviceExtensionProperties` not checked.**
  Same pattern: lines 299 and 306 both call `vkEnumerateDeviceExtensionProperties` without checking the result.

- **[MODERATE] Line 519-520: Negative width/height cast to uint32_t without validation.**
  ```c
  ctx->fb_width  = (uint32_t) width;
  ctx->fb_height = (uint32_t) height;
  ```
  The function signature is `vc_init(int width, int height)`. If negative values are passed, the cast to `uint32_t` wraps to huge values. The function should validate width > 0 and height > 0.

  **Fix**: Add at the top of `vc_init()`:
  ```c
  if (width <= 0 || height <= 0)
      return NULL;
  ```

- **[MODERATE] Line 743: `vc_readback_pixels` reads front buffer without sync -- potential race.**
  ```c
  vc_framebuffer_t *front = vc_render_pass_front(ctx->render_pass);
  return vc_readback_execute(&ctx->readback, ctx, front->color_image);
  ```
  The `front` pointer is obtained by reading `render_pass->back_index` which is modified by the render thread during swap. If the display thread calls `vc_readback_pixels()` while the render thread is executing `vc_framebuffer_swap()`, the `back_index` could change between the pointer dereference and the readback execution, causing a readback from the wrong buffer or even an image in the wrong layout.

  The `frames_completed` atomic check on line 738 provides a weak ordering guarantee (acquire), but `back_index` is a plain `int` with no synchronization.

  **Severity justification**: Moderate because in practice the readback path is protected by higher-level sync (vc_sync() is called before LFB reads), but the function itself provides no guarantees.

- **[LOW] Line 524-526: Instance creation failure path leaks memory but not ctx fields.**
  ```c
  ctx->instance = vc_create_instance(&ctx->debug_messenger);
  if (ctx->instance == VK_NULL_HANDLE) {
      free(ctx);
      return NULL;
  }
  ```
  If `vc_create_instance` fails, `ctx` is freed directly instead of calling `vc_close(ctx)`. This is safe because no sub-modules are initialized yet, but it's inconsistent with subsequent failure paths (lines 531-534) which call `vc_close(ctx)`. The first failure path (line 525) could also use `vc_close(ctx)` safely since `vc_close` checks for NULL handles.

  **Status**: Not a bug (all fields are zero from calloc), but inconsistent.

- **[LOW] Line 172: `volkLoadInstance` return value not checked.**
  ```c
  volkLoadInstance(instance);
  ```
  `volkLoadInstance()` returns void in the standard volk API, so this is fine. No action needed.

- **[LOW] Lines 1010-1056: Accessor functions use `(void *)(uintptr_t)` cast for dispatchable handles.**
  ```c
  return (void *) (uintptr_t) ctx->instance;
  ```
  The `(uintptr_t)` intermediate cast is unnecessary -- `VkInstance` is already a pointer type (dispatchable handle), so `(void *) ctx->instance` would suffice. The extra cast does no harm but adds visual noise.

  **Status**: Harmless. May have been added for compiler warning suppression.

- **[LOW] Lines 816-830: `vc_texture_upload_async` performs a malloc+memcpy per texture upload.**
  ```c
  void *copy = malloc(data_size);
  if (!copy)
      return;
  memcpy(copy, pixels, data_size);
  ```
  For large textures (256x256 BGRA8 = 256KB), this is a per-upload heap allocation. Under heavy texture switching (100+ per frame in 3DMark99), this creates significant allocation pressure. A pooled staging approach would be more efficient.

  **Status**: Functional but suboptimal. Already noted as a perf bottleneck in agent memory.

- **[LOW] Line 700: Push constant size capped at 64 bytes.**
  ```c
  if (!ctx || !data || size == 0 || size > 64)
      return;
  ```
  The 64-byte limit is correct for the current push constant layout (per `push-constant-layout.md`), but this is a magic number. It would be better to use a named constant like `VC_PUSH_CONSTANT_MAX_SIZE`.

### Notes

- **Initialization order is correct and well-documented** (Steps 1-10 in comments). Each step checks for failure and calls `vc_close()` which handles partial cleanup.

- **Shutdown order is correct**: thread first (stops GPU work submission), then `vkDeviceWaitIdle` (drains GPU), then sub-modules in reverse order, then Vulkan core objects in reverse creation order.

- **The global context atomic pattern** (lines 1100-1112) with `memory_order_acquire`/`memory_order_release` is textbook correct for publishing a pointer from one thread to be consumed by another.

- **The `VK_CHECK` macro usage** is consistent throughout: all `vkCreate*` calls check results. The enumeration functions (lines 133, 208, 298) are the exception -- they don't use VK_CHECK because they need special handling (continue-on-failure for enumerations).

- **macOS ICD path auto-detection** (lines 472-503) is a practical workaround for a real-world problem (stale VK_DRIVER_FILES from removed Vulkan SDK installs). The fallback chain covers Homebrew ARM64, Homebrew x86, and legacy paths. Good defensive code.

- **Queue mutex** is created right after device creation (line 549) and destroyed before device destruction (line 661). This ensures the mutex lifetime encompasses all possible queue submissions. Correct.

- **Memory management pattern**: `calloc()` for context means all fields start as zero/NULL, so `vc_close()` can safely check each handle before destroying it. This is a good defensive pattern.

- **VMA function pointer setup** (lines 420-444) is comprehensive and correctly uses the Vulkan 1.2 core function names (not KHR suffixes) for `vkGetBufferMemoryRequirements2`, etc. The KHR-suffixed VMA fields are filled with core 1.2 functions, which is correct because these are the same functions promoted to core.

---

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 0 | -- |
| HIGH | 2 | `direct_present_active` data race (vc_core.h line 90, vc_core.c lines 1080-1091); log-after-free pattern (vc_core.c line 673, safe in practice) |
| MODERATE | 6 | Linux WSI X11-only assumption; unchecked enumerate returns (3 instances); negative dimension cast; front buffer race in readback |
| LOW | 6 | `HAVE_STDARG_H` fragility; inline logging pattern; NULL check gap in `vc_has_cap`; inconsistent early-return cleanup; unnecessary uintptr_t casts; push constant magic number; per-upload malloc |

### Priority Fixes

1. **`direct_present_active` atomicity** -- Simple fix, prevents UB on all platforms. Change to `_Atomic int` and use atomic accessors.

2. **Linux Wayland WSI support** -- Blocks Pi 5 and modern Linux. Need compile-time or runtime WSI selection.

3. **Enumerate return value checks** -- Add VkResult checks to `vkEnumerateInstanceLayerProperties`, `vkEnumeratePhysicalDevices`, `vkEnumerateDeviceExtensionProperties`. Log failures. Not crash-prone (graceful degradation) but hides real driver issues.

4. **Dimension validation** -- Add `width > 0 && height > 0` check in `vc_init()`.
