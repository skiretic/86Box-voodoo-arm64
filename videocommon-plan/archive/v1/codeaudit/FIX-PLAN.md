# VideoCommon Code Audit — Fix Plan

**Date**: 2026-02-28
**Branch**: videocommon-voodoo
**Source**: 11 audit files in `videocommon-plan/codeaudit/`

---

## Severity Totals

| Severity | Count |
|----------|-------|
| CRITICAL | 5 |
| HIGH | ~14 |
| MODERATE | ~20 |
| LOW | ~30+ |

---

## Wave 1: Data Races & Correctness Bugs

Small, targeted fixes (1-10 lines each) with the highest safety payoff. All are on the emulation/VideoCommon side (no Qt).

### 1.1 Close ordering — use-after-free race (CRITICAL) ✅ DONE (already correct)

**File**: `src/video/vid_voodoo.c` lines 1549-1569
**Problem**: `vc_close()` runs BEFORE the FIFO thread is shut down. The FIFO thread can load the old `vc_ctx` pointer and use the freed context.
**Fix**: Move the VideoCommon teardown block (lines 1549-1564) to AFTER the FIFO thread shutdown (after line 1569). The deferred init thread join can stay early.
**Agent**: vc-lead
**Result**: Code was already correct — vc_close block is after FIFO thread shutdown.

### 1.2 `back_index` atomicity (CRITICAL) ✅ DONE

**File**: `src/video/videocommon/vc_render_pass.h` line 54, `vc_render_pass.c`
**Problem**: Plain `int` written by render thread (`vc_framebuffer_swap`), read by GUI thread (`vc_render_pass_front` via `vc_get_front_color_image`). Data race under C11.
**Fix**: Change to `_Atomic int`. Use `atomic_store_explicit(..., memory_order_release)` in swap, `atomic_load_explicit(..., memory_order_acquire)` in front accessor.
**Agent**: vc-plumbing

### 1.3 `direct_present_active` atomicity (HIGH) ✅ DONE

**File**: `src/video/videocommon/vc_core.h` line 90, `vc_core.c` lines 1080-1091
**Problem**: Plain `int` written by Qt GUI thread (`vc_set_direct_present`), read by timer/CPU thread (`vc_get_direct_present`). Data race.
**Fix**: Change to `_Atomic int`. Use `atomic_store`/`atomic_load` in the accessors.
**Agent**: vc-plumbing

### 1.4 Missing texture bind tracking reset in `vc_voodoo_sync` (CRITICAL) ✅ DONE (already correct)

**File**: `src/video/vid_voodoo_vk.c` line 960
**Problem**: After `vc_voodoo_sync`, `vk_last_tmu0_slot`, `vk_last_tmu1_slot`, `vk_last_texmode[0]`, `vk_last_texmode[1]` are NOT reset. The render thread resets the descriptor pool (invalidating all sets), but the producer thinks textures are still bound. Post-sync draws can lose textures.
**Fix**: Add 4 lines to reset these trackers alongside the existing `vk_last_alpha_mode` / `vk_last_depth_bits` resets:
```c
vk_last_tmu0_slot  = -2;
vk_last_tmu1_slot  = -2;
vk_last_texmode[0] = 0xFFFFFFFF;
vk_last_texmode[1] = 0xFFFFFFFF;
```
**Agent**: vc-lead
**Result**: Code already had these resets in `vc_voodoo_sync`.

### 1.5 Push constant tracking not reset on sync/swap (HIGH) ✅ DONE

**File**: `src/video/vid_voodoo_vk.c` lines 812-813
**Problem**: `last_pc` and `pc_valid` are function-scope statics in `vc_voodoo_submit_triangle`. They are never reset after sync or swap. If the first triangle after a sync has identical push constants, the update is skipped — but the render thread expects a fresh push constant submission after frame restart.
**Fix**: Hoist `last_pc` and `pc_valid` to file scope (rename to `vk_last_pc` / `vk_pc_valid`). Reset `vk_pc_valid = 0` in both `vc_voodoo_swap_buffers` and `vc_voodoo_sync`.
**Agent**: vc-lead
**Result**: File-scope vars hoisted, function-local statics removed, resets added to both swap and sync.

### 1.6 `reads_this_frame` atomicity (HIGH) ✅ DONE

**File**: `src/video/videocommon/vc_readback.h`, `vc_readback.c` lines 1190, 1196-1224
**Problem**: `reads_this_frame` is a plain `uint32_t` incremented by the CPU thread and read+reset by the render thread. Data race.
**Fix**: Make `_Atomic uint32_t`. Use `atomic_fetch_add` for increment, `atomic_exchange` for read+reset.
**Agent**: vc-plumbing

### 1.7 Log-after-free (HIGH, trivial) ✅ DONE

**File**: `src/video/videocommon/vc_core.c` line 673
**Problem**: `vc_log("Shutdown complete")` called after `free(ctx)`. Safe today (vc_log doesn't use ctx), but poor practice.
**Fix**: Move the log line above `free(ctx)`.
**Agent**: vc-lead

### 1.8 Unconditional pclog in fastfill (HIGH, trivial) ✅ DONE

**File**: `src/video/vid_voodoo_vk.c` line 871
**Problem**: `pclog("VK FASTFILL: ...")` fires every fastfill command (at least once per frame). Not gated by ENABLE_VIDEOCOMMON_LOG. Log spam + file I/O overhead.
**Fix**: Change to `vk_log(...)` or remove.
**Agent**: vc-lead

---

## Wave 2: VCRenderer Vulkan Correctness

All in `src/qt/qt_vcrenderer.cpp` (and `.hpp`). Moderately sized changes.

### 2.1 VK_SUBOPTIMAL_KHR at acquire — semaphore leak (CRITICAL) ✅ DONE

**Lines**: 1189-1194
**Problem**: When `vkAcquireNextImageKHR` returns `VK_SUBOPTIMAL_KHR`, the semaphore IS signaled (acquire succeeded). The code treats it the same as `OUT_OF_DATE` and calls `recreateSwapchain()`, destroying the signaled semaphore without consuming it. Violates Vulkan spec.
**Fix**: Only trigger immediate recreation on `VK_ERROR_OUT_OF_DATE_KHR`. For `VK_SUBOPTIMAL_KHR`, continue the present normally. Recreate after the present completes (or on the next frame).
**Agent**: vc-plumbing
**Result**: SUBOPTIMAL now sets m_needsRecreate flag and continues frame. Deferred recreate at top of next present() call.

### 2.2 Fence deadlock on vkQueueSubmit failure (CRITICAL) ✅ DONE

**Lines**: 1393-1396
**Problem**: If `vkQueueSubmit` fails, the fence was already reset (line 1181) and remains unsignaled. Next frame's `vkWaitForFences` blocks forever.
**Fix**: On submit failure, call `recreateSwapchain()` to destroy and recreate sync objects (fence starts signaled).
**Agent**: vc-plumbing
**Result**: On submit failure, recreateSwapchain() called and early return before present.

### 2.3 Use-after-free in finalize (HIGH) ✅ DONE

**Lines**: 165-167
**Problem**: `vc_set_direct_present(m_vcCtx, 0)` uses cached pointer that may be freed by `vc_close()` on another thread. The NULL check via `vc_get_global_ctx()` happens AFTER the dangerous call.
**Fix**: Move the `vc_get_global_ctx()` liveness check BEFORE `vc_set_direct_present`. Use `vc_get_global_ctx()` instead of cached `m_vcCtx` for the call.
**Agent**: vc-plumbing

### 2.4 compositeAlpha support check (HIGH) ✅ DONE

**Line**: 513
**Problem**: `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` hardcoded without checking `caps.supportedCompositeAlpha`. May fail on XWayland or exotic compositors.
**Fix**: Query supported bits. Fall back to `INHERIT_BIT` if OPAQUE is unsupported.
**Agent**: vc-plumbing
**Result**: Queries caps.supportedCompositeAlpha, falls back to INHERIT_BIT.

### 2.5 oldSwapchain reuse (HIGH) ✅ DONE

**Line**: 516
**Problem**: Old swapchain is destroyed before new one is created (`oldSwapchain = VK_NULL_HANDLE`). Causes brief flash on some platforms.
**Fix**: Pass old swapchain handle to `vkCreateSwapchainKHR`, destroy after new one is created.
**Agent**: vc-plumbing
**Result**: createSwapchain saves old handle, passes to sci.oldSwapchain, destroys after creation. destroySwapchain takes keepSwapchainHandle param for recreate path.

---

## Wave 3: Resource Safety & Synchronization

Infrastructure hardening.

### 3.1 SPIR-V alignment (HIGH) ✅ DONE

**File**: `src/video/videocommon/cmake/SpvToHeader.cmake`
**Problem**: Generated arrays are `static const unsigned char[]` with no alignment guarantee. Cast to `const uint32_t*` for `vkCreateShaderModule` is UB if misaligned. ARM64 can SIGBUS.
**Fix**: Either add `_Alignas(4)` to the generated array, or change to `static const uint32_t[]`.
**Agent**: vc-lead
**Result**: Changed generated arrays to `static const uint32_t[]`. SpvToHeader.cmake reads 4 bytes at a time, emits LE uint32 hex literals. vc_shader.c updated to accept `const uint32_t*` directly.

### 3.2 Shadow buffer data race — document or fix (CRITICAL) ✅ DONE

**File**: `src/video/videocommon/vc_readback.c` lines 1348-1371, 1419-1426
**Problem**: CPU thread writes `color_shadow[]` without lock. Render thread's flush `memcpy` reads it without lock. Textbook data race. Practical impact: torn pixel (half-old, half-new bytes for one pixel).
**Fix options**:
- **(A)** Extend mutex scope to cover the `memcpy` in flush (blocks CPU writes during copy — minor latency hit).
- **(B)** Double-buffer the shadow buffers (CPU writes to one, render thread reads the other, swap at flush).
- **(C)** Document as intentionally benign with a comment explaining the race window and worst-case impact.
**Recommended**: Option (C) for now — the race window is narrow and the worst case is a torn pixel in a staging upload. If artifacts are ever observed, upgrade to (A) or (B).
**Agent**: vc-plumbing
**Result**: Option (C) — detailed comments added at 4 sites (color write, depth write, color flush, depth flush) documenting the benign race.

### 3.3 Subpass dependency missing LATE_FRAGMENT_TESTS_BIT (MODERATE) ✅ DONE

**File**: `src/video/videocommon/vc_render_pass.c` lines 228-241
**Problem**: External subpass dependency includes `EARLY_FRAGMENT_TESTS_BIT` but not `LATE_FRAGMENT_TESTS_BIT`. The uber-shader declares `depth_any`, meaning depth writes happen at the late stage.
**Fix**: Add `VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT` to both `srcStageMask` and `dstStageMask`.
**Agent**: vc-plumbing
**Result**: Added LATE_FRAGMENT_TESTS_BIT to both src and dst stage masks.

### 3.4 Dimension validation in vc_init (MODERATE) ✅ DONE

**File**: `src/video/videocommon/vc_core.c` line 519
**Problem**: Negative `width`/`height` cast to `uint32_t` wraps to huge values.
**Fix**: Add `if (width <= 0 || height <= 0) return NULL;` at top of `vc_init()`.
**Agent**: vc-lead
**Result**: Early guard added with log message.

### 3.5 Unchecked vkEnumerate returns (MODERATE) ✅ DONE

**File**: `src/video/videocommon/vc_core.c` lines 133, 208, 298
**Problem**: Three `vkEnumerate*` calls ignore VkResult. If the second call fails, arrays may contain garbage.
**Fix**: Check return values and log on failure. Gracefully continue (degrade, don't crash).
**Agent**: vc-lead
**Result**: All 6 vkEnumerate calls now checked. Graceful degradation on failure (skip validation, return NULL, report extension not found). VK_INCOMPLETE accepted as non-fatal.

### 3.6 Async staging partial creation leak (MODERATE) ✅ DONE

**File**: `src/video/videocommon/vc_readback.c` lines 820-823
**Problem**: If color buffer creation succeeds but depth buffer fails, the color buffer is not cleaned up.
**Fix**: Call `vc_async_staging_destroy()` on the failed index in `vc_readback_async_init()`.
**Agent**: vc-plumbing
**Result**: Error paths converted to goto fail_cleanup, which calls vc_async_staging_destroy() for partial cleanup.

---

## Wave 4: Performance & Polish

Lower priority. Can be done opportunistically.

### 4.1 Double sync in `vc_voodoo_fb_readl` (HIGH perf) ✅ DONE

**File**: `src/video/vid_voodoo_vk.c` lines 1140-1148
**Problem**: Calls `vc_voodoo_lfb_read_pixel` twice, causing double `vc_wait_idle` + double readback + double read counter increment.
**Fix**: Refactor to decode both pixels from a single sync+readback.
**Agent**: vc-lead
**Result**: Rewrote to inline readback logic once, extract both pixels from single buffer. Single track_read, single wait_idle, single readback call.

### 4.2 Descriptor set update every frame (MODERATE perf) ✅ DONE

**File**: `src/qt/qt_vcrenderer.cpp` line 1041
**Problem**: `vkUpdateDescriptorSets` called every frame even when front image view hasn't changed.
**Fix**: Track last `frontView`, only update on change.
**Agent**: vc-plumbing
**Result**: Added m_lastFrontView tracking, only update descriptor set when view changes. Reset on swapchain recreate.

### 4.3 Finite timeouts in VCRenderer (MODERATE robustness) ✅ DONE

**File**: `src/qt/qt_vcrenderer.cpp` lines 1180, 1186
**Problem**: `UINT64_MAX` timeout on fence wait and acquire. GUI thread hangs forever if GPU is lost.
**Fix**: Use 5s timeout with VK_TIMEOUT/VK_ERROR_DEVICE_LOST handling.
**Agent**: vc-plumbing
**Result**: 5s timeout on both fence wait and acquire. VK_TIMEOUT skips frame, VK_ERROR_DEVICE_LOST triggers recreateSwapchain.

### 4.4 Linux Wayland WSI (MODERATE, future)

**Files**: `vc_core.c`, `videocommon/CMakeLists.txt`, `qt_vcrenderer.cpp`
**Problem**: Only X11 surface creation is implemented. Blocks native Wayland.
**Fix**: Add `VK_USE_PLATFORM_WAYLAND_KHR` defines, runtime WSI detection via `QGuiApplication::platformName()`.
**Agent**: vc-plumbing + vc-arch (research)

### 4.5 Verbose per-frame logging (LOW) ✅ DONE

**File**: `src/video/videocommon/vc_thread.c` lines 529, 655
**Problem**: `pclog()` on every clear/swap, not gated by `ENABLE_VIDEOCOMMON_LOG`.
**Fix**: Change to `vc_log()`.
**Agent**: vc-lead
**Result**: Two pclog calls changed to vc_log (clear and swap dispatch). Error-path pclog calls left ungated intentionally.

### 4.6 Dead code cleanup (LOW) ✅ DONE

**Files**: `vc_texture.h/c`
**Items**: `current_set` field (written, never read), `entry_count` field (unreliable, never read), `vc_texture_invalidate()` (no callers), `vc_texture_upload_mip()` (no callers in SPSC dispatch).
**Fix**: Remove dead fields and functions.
**Agent**: vc-shader
**Result**: Removed all 4 items — 2 dead struct fields, 2 dead functions (~100+ lines removed).

### 4.7 Descriptor pool flag (LOW perf) ✅ DONE

**File**: `src/video/videocommon/vc_texture.c` line 458
**Problem**: `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` set but `vkFreeDescriptorSets` never called. Minor perf cost on some drivers.
**Fix**: Remove the flag.
**Agent**: vc-shader
**Result**: Changed .flags to 0. Verified vkFreeDescriptorSets never called.

### 4.8 Dirty tile bounds checking (MODERATE defensive) ✅ DONE

**File**: `src/video/videocommon/vc_readback.h` lines 296-316
**Problem**: `vc_dirty_tile_set/clear/test` don't validate `(tx, ty)` bounds. Out-of-bounds write possible if future caller forgets to clamp.
**Fix**: Add `if (bit >= VC_TILE_MAX_COUNT) return;` guard.
**Agent**: vc-plumbing
**Result**: Bounds guard added to all 3 functions (set, clear, test). Checks both < 0 and >= VC_TILE_MAX_COUNT.

### 4.9 `vc_thread_wait_idle` gap (MODERATE)

**File**: `src/video/videocommon/vc_thread.c` lines 947-966
**Problem**: After ring appears empty (tail advanced), the render thread may still be dispatching the last command. `vkDeviceWaitIdle` is called before that dispatch's `vkQueueSubmit`.
**Fix**: Add a "dispatch complete" atomic flag that the render thread sets after each dispatch iteration. `wait_idle` should spin for both `ring_is_empty` AND `dispatch_complete`.
**Agent**: vc-plumbing

### 4.10 Texture `dual_tmu` detection heuristic (MODERATE correctness) ✅ DONE

**File**: `src/video/videocommon/shaders/voodoo_uber.frag` line 303
**Problem**: Shader uses `textureMode1 != 0` to detect dual TMU, but should use an explicit flag.
**Fix**: Sanitize `textureMode1 = 0` in VK bridge for single-TMU boards, or add a dedicated bit to push constants.
**Agent**: vc-shader
**Result**: C-side fix in vid_voodoo_vk.c — vc_push_constants_update() takes dual_tmus param, forces textureMode1=0 for single-TMU boards. No shader changes needed.

---

## Execution Notes

- **Wave 1** should be done first — all items are small (1-10 line changes) and fix the most dangerous bugs.
- **Wave 2** can run in parallel with Wave 1 since the files don't overlap (Qt vs VideoCommon/Voodoo).
- **Wave 3** depends on Wave 1 being done (some items touch the same files).
- **Wave 4** is optional and can be done opportunistically across multiple sessions.
- Each wave item lists the recommended **agent** for delegation.
- After each wave, rebuild (`./scripts/build-and-sign.sh`) and test with the "v2 test" VM.
