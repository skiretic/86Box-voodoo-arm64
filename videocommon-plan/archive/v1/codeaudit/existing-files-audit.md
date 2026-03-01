# Existing 86Box Files -- VideoCommon Integration Audit

**Date**: 2026-02-28
**Branch**: videocommon-voodoo
**Scope**: All `#ifdef USE_VIDEOCOMMON` blocks and related VideoCommon changes in existing 86Box source files (not the VideoCommon library itself).

---

## Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 1     |
| HIGH     | 2     |
| MODERATE | 3     |
| LOW      | 5     |

---

## 1. `src/include/86box/vid_voodoo_common.h` -- VideoCommon fields in voodoo_t

### Relevant Lines

Lines 776-782: Five new fields added to `voodoo_t` inside `#ifdef USE_VIDEOCOMMON`.

```c
#ifdef USE_VIDEOCOMMON
    _Atomic(void *)  vc_ctx;           /* VideoCommon Vulkan context (vc_context_t*) */
    int              use_gpu_renderer; /* 1 = Vulkan GPU rendering active */
    _Atomic int      vc_init_pending;  /* 1 = background vc_init() in progress */
    const void      *vc_readback_buf;  /* Cached readback pointer (valid until next swap) */
    thread_t        *vc_init_thread;   /* Handle for deferred init thread (for join). */
#endif
```

### Issues Found

- [LOW] Line 778: `use_gpu_renderer` is a plain `int` written from two threads: the deferred init thread (line 93 of vid_voodoo_reg.c, on failure) and implicitly read from the FIFO thread and display thread. Under C11 this is technically a data race, though it is benign in practice -- the value transitions from 1 to 0 on failure, and all readers already check the atomic `vc_ctx` as a secondary guard. The worst case is one extra FIFO cycle seeing `use_gpu_renderer=1` with `vc_ctx=NULL`, which is handled correctly.

- [LOW] Line 780: `vc_readback_buf` is a plain pointer, not atomic. It is written and read exclusively from the timer/display thread (`voodoo_callback`), so this is correct. The NULL write at vsync (line 683 of vid_voodoo_display.c) and the reads during scanline processing (lines 580-584) are on the same thread.

### Notes

- The `_Atomic(void *)` for `vc_ctx` is correct and necessary: it is written from the deferred init thread and read from the FIFO thread, display thread, and close path.
- The `_Atomic int` for `vc_init_pending` is correct: written from both the FIFO thread (line 179 of vid_voodoo_reg.c) and the init thread (line 107).
- Placement at the end of the struct (just before the closing brace) avoids any ABI impact on the non-VideoCommon build -- good practice.
- The `memset(voodoo, 0, sizeof(voodoo_t))` in both init functions zero-initializes all these fields, so `vc_ctx` starts as NULL, `use_gpu_renderer` as 0, etc.

---

## 2. `src/video/vid_voodoo.c` -- Init/close hooks, config

### Relevant Lines

- Line 49-51: Include guard for `<86box/videocommon.h>`
- Line 1329-1332: `voodoo_card_init()` reads `gpu_renderer` config
- Line 1479-1484: `voodoo_2d3d_card_init()` reads `gpu_renderer` config
- Line 1549-1564: `voodoo_card_close()` VideoCommon teardown
- Line 1807-1819: `gpu_renderer` config entry in `voodoo_config[]`

### Issues Found

- [CRITICAL] Lines 1549-1564 vs 1567-1569: **VideoCommon teardown happens before FIFO thread shutdown.** The close sequence is:
  1. Wait for deferred init thread to finish (line 1551-1553)
  2. Load `vc_ctx`, call `vc_close(vc_ctx)`, store NULL (lines 1556-1562)
  3. **Then** shut down FIFO thread (lines 1567-1569)

  This creates a race window: the FIFO thread may still be processing commands between steps 2 and 3. If the FIFO thread loads `vc_ctx` (getting the old non-NULL value) just before `vc_close()` executes, then attempts to use the now-destroyed context (e.g., `vc_voodoo_submit_triangle`, `vc_voodoo_swap_buffers`, or `vc_voodoo_fastfill`), the result is use-after-free.

  **The fix**: Move the VideoCommon teardown block (lines 1549-1564) to AFTER the FIFO thread shutdown (after line 1569). The sequence should be:
  1. Stop FIFO thread (`fifo_thread_run = 0`, wake, wait)
  2. Stop render threads
  3. Wait for deferred init thread
  4. Close VideoCommon context

  The deferred init thread join (line 1551) should remain before FIFO shutdown since the init thread writes `vc_ctx` and the FIFO thread reads it, but `vc_close` MUST come after the FIFO thread is dead.

- [LOW] Lines 1329-1331, 1479-1483: Both `voodoo_card_init()` and `voodoo_2d3d_card_init()` call `device_get_config_int("gpu_renderer")`. This is safe because `device_get_config_int` returns 0 for unknown keys. For Banshee/V3 (which use `voodoo_2d3d_card_init` but have no `gpu_renderer` in their device configs), this correctly defaults to 0 (GPU rendering disabled).

### Notes

- The `gpu_renderer` config entry (line 1807-1819) is properly guarded by `#ifdef USE_VIDEOCOMMON` within the `voodoo_config[]` array. This means only Voodoo 1/2 devices expose the option in the UI.
- The `vc_set_global_ctx(NULL)` call at line 1559 before `vc_close` is correct -- prevents the Qt VCRenderer from accessing a stale pointer.
- The `atomic_store_explicit` at line 1562 (setting `vc_ctx = NULL`) is done after `vc_close` completes, which is the right order for the store -- but the problem is the FIFO thread can still be running and may have cached the old value.

---

## 3. `src/video/vid_voodoo_reg.c` -- Deferred init, swap, fastfill hooks

### Relevant Lines

- Lines 44-47: Include guards for `<86box/videocommon.h>` and `<86box/vid_voodoo_vk.h>`
- Lines 74-109: `vc_deferred_init_thread()` function
- Lines 171-188: Deferred init trigger + swap buffer hook in `SST_swapbufferCMD`
- Lines 652-658: Fastfill hook in `SST_fastfillCMD`

### Issues Found

- [HIGH] Lines 140-169 vs 171-188: **Banshee/V3 early-return skips VideoCommon hooks.** For `voodoo->type >= VOODOO_BANSHEE`, the `SST_swapbufferCMD` handler executes lines 140-168 and then `break`s on line 168. The VideoCommon deferred init trigger (lines 171-181) and swap buffer hook (lines 185-188) are never reached for Banshee/V3. This means:
  - The Vulkan renderer will never initialize for Banshee/V3.
  - Even if `use_gpu_renderer` were somehow set to 1, no triangles would be redirected.

  This is currently **not a bug** because Banshee/V3 configs don't expose the `gpu_renderer` option (returns 0). But it means Banshee/V3 GPU acceleration is structurally impossible without also adding VideoCommon hooks inside the Banshee-specific swap handler (lines 140-168). When Banshee support is added, the VideoCommon hooks need to be placed before the Banshee `break` at line 168.

- [MODERATE] Line 93: `voodoo->use_gpu_renderer = 0` in the failure path of `vc_deferred_init_thread` is a non-atomic write to a field read by other threads. As noted above, this is benign but technically a data race under C11. Could be fixed by making the write happen through an atomic or by accepting the benign race (all readers check `vc_ctx` atomically).

- [LOW] Lines 87-88: Default resolution fallback uses `voodoo->h_disp` and `voodoo->v_disp` which are set by the guest driver. If the guest hasn't set a display mode yet (e.g., during PCI init), these could be 0, triggering the fallback to 640x480. This is correct behavior since the deferred init fires on the first swap (after the guest has set up the display), but it's worth noting that `h_disp`/`v_disp` might not reflect the final resolution if the guest changes modes later. VideoCommon would need to handle resize.

### Notes

- The deferred init fires on `SST_swapbufferCMD`, which is the right trigger point. The comment correctly notes this avoids blocking the FIFO thread during Vulkan startup and ensures Glide hardware detection has already completed.
- The three-way guard pattern (`use_gpu_renderer && !vc_ctx && !vc_init_pending`) prevents double-init. Good.
- The `vc_init_thread` assignment on line 180 is safe because it's only written once (the `vc_init_pending` flag prevents re-entry) and from the FIFO thread only.
- The fastfill hook (lines 652-658) correctly calls `vc_voodoo_fastfill` and then increments `cmd_read` and breaks, skipping the software `voodoo_fastfill`. The early `break` prevents the software path from running. Good.
- The swap buffer hook (lines 185-188) calls `vc_voodoo_swap_buffers` but does NOT skip the software swap logic (lines 191-197 still execute). This means both VK and SW swap state advance simultaneously. This is intentional -- the SW swap state (disp_buffer/draw_buffer) drives the Voodoo register/display timing that the guest depends on, while the VK swap handles the actual frame presentation.

---

## 4. `src/video/vid_voodoo_render.c` -- VK path branch in triangle queue

### Relevant Lines

- Lines 43-46: Include guards
- Lines 1856-1862: VK path branch in `voodoo_queue_triangle()`

```c
void
voodoo_queue_triangle(voodoo_t *voodoo, voodoo_params_t *params)
{
#ifdef USE_VIDEOCOMMON
    if (voodoo->use_gpu_renderer
        && atomic_load_explicit(&voodoo->vc_ctx, memory_order_acquire)) {
        vc_voodoo_submit_triangle(voodoo, params);
        return;
    }
#endif
    /* ... software path follows ... */
```

### Issues Found

None.

### Notes

- The branch is clean: check the guard, call VK submission, return early. Software path is completely skipped.
- This runs on the FIFO thread (called from `voodoo_triangle_setup` -> `voodoo_queue_triangle`), which is the correct thread for SPSC ring submission.
- The `memory_order_acquire` on `vc_ctx` ensures all VideoCommon initialization is visible before any triangle is submitted.
- The `params` pointer passed to `vc_voodoo_submit_triangle` is `&voodoo->params` (the live register state), not a buffered copy. This is fine because the FIFO thread is the only writer at this point (the SW render threads haven't been given this triangle yet).

---

## 5. `src/video/vid_voodoo_display.c` -- VK scanout path

### Relevant Lines

- Lines 40-42: Include guard
- Lines 540-628: Main VK display path (inside `voodoo_callback`, scanline loop)
- Lines 629-631: `vc_sw_fallback` label
- Lines 680-684: Readback cache invalidation at vsync

### Issues Found

- [HIGH] Lines 541-542 vs 554: **Inconsistent voodoo instance for SLI.** The outer guard checks `draw_voodoo->use_gpu_renderer` and `draw_voodoo->vc_ctx`, but line 554 reads `voodoo->vc_ctx` (the master). In SLI mode, `draw_voodoo` alternates between `voodoo` (master) and `voodoo->set->voodoos[1]` (slave) based on scan line. If `draw_voodoo` is the slave (which has `vc_ctx = NULL` and `use_gpu_renderer = 0` since only the master initializes VideoCommon), the outer guard will be false and the code falls through to the software path. So SLI mode with VideoCommon will always take the software path for the slave card's lines.

  However, there's a more subtle issue: the inconsistency between checking `draw_voodoo->vc_ctx` (line 542) and then using `voodoo->vc_ctx` (line 554) means that if somehow both had `vc_ctx` set but to different values, the wrong context would be used for readback. Currently this can't happen because only the master initializes VideoCommon, but it's a latent bug waiting for SLI+GPU support.

  **For correctness now**: Since only the master has `vc_ctx`, SLI + GPU rendering is effectively disabled (slave lines always take SW path). This should be documented.

  **For future SLI+GPU**: Use a single consistent context pointer throughout the block.

- [MODERATE] Lines 607-608: The readback line calculation `rb + draw_line * voodoo->h_disp` uses `draw_line` (which is `voodoo->line >> 1` for SLI) but `voodoo->h_disp`. In the non-SLI case, `draw_line == voodoo->line`, so this is correct. In the SLI case, this code is unreachable (see above), so no actual bug.

- [MODERATE] Lines 596-621: The overscan drawing uses `v_x_add` for left/right borders, matching the software path. The right overscan loop (lines 619-621) writes to `voodoo->h_disp + x + v_x_add`, which is correct. The pixel conversion (RGBA8 -> XRGB8888) swapping R and B channels is correct for the VK_FORMAT_R8G8B8A8_UNORM layout on little-endian.

### Notes

- The `goto vc_sw_fallback` on line 624 (when no GPU frames exist yet) correctly falls through to the software display path. The label placement between two `#ifdef` blocks (lines 629-631) is syntactically valid but slightly unusual.
- The "lazy readback" pattern (fetch pointer on first scanline, cache until vsync) is an efficient design. The comment on lines 572-578 correctly warns against calling `vc_sync()` from the display thread.
- The `vc_readback_buf` invalidation at vsync (line 683) ensures a fresh readback each frame. This is correct.
- The direct-present optimization (lines 555-562) skips readback entirely when VCRenderer is doing zero-copy display. Only dirty-line marking is done to trigger the blit signal chain. This is the intended fast path.

---

## 6. `src/video/vid_voodoo_fb.c` -- LFB read/write VK path

### Relevant Lines

- Lines 40-42: Include guard
- Lines 67-71: `voodoo_fb_readw()` VK path
- Lines 116-120: `voodoo_fb_readl()` VK path
- Lines 201-213: `voodoo_fb_writew()` VK path (with pipeline-mode fallthrough)
- Lines 377-385: `voodoo_fb_writel()` VK path (with pipeline-mode fallthrough)

### Issues Found

None.

### Notes

- All four LFB functions follow the same pattern: check `use_gpu_renderer && vc_ctx`, then dispatch to the VK bridge function.
- The write functions (lines 201-213, 377-385) have a critical design choice: **pipeline-mode LFB writes** (`lfbMode & 0x100`) are NOT handled by the VK path and fall through to the SW renderer. The comment explains this: `vc_voodoo_fb_writew()` returns without action for pipeline-mode writes. This is correct because pipeline-mode LFB writes go through the Voodoo pixel pipeline (blending, depth test, etc.) and are effectively rendered primitives. The VK path handles raw LFB writes (direct framebuffer access).
- The read functions unconditionally dispatch to VK when the context is active. The VK bridge reads from the readback staging buffer.
- The `memory_order_acquire` on `vc_ctx` ensures the VK context is fully initialized before any LFB operation uses it.
- LFB reads/writes can come from the CPU thread (direct MMIO access) or the FIFO thread (FIFO-enqueued LFB commands). The VK bridge functions (`vc_voodoo_fb_read/write*`) must be thread-safe with respect to the render thread. This is handled by the readback staging buffer (for reads) and the LFB shadow buffer with dirty tracking (for writes).

---

## 7. `src/qt/qt_rendererstack.cpp` -- VCRenderer instantiation

### Relevant Lines

- Lines 25-27: Include guard for `qt_vcrenderer.hpp`
- Lines 454-481: `Renderer::VideoCommon` case in `createRenderer()`
- Lines 500-501: Buffer-fetch skip for async renderers

### Issues Found

None.

### Notes

- The VCRenderer creation follows the exact same pattern as the Vulkan renderer case above it: `createWinId()`, create renderer object, connect blit/init/error signals, `createWindowContainer()`.
- Error handling (lines 467-478) falls back to Software renderer on initialization failure, with a user-visible message box. Good.
- Line 500-501 correctly includes `Renderer::VideoCommon` in the set of renderers that delay `getBuffers()` until after initialization (same as OpenGL3 and Vulkan). This prevents use of uninitialized buffers.
- The `Qt::QueuedConnection` for `blitToRenderer` ensures the blit happens on the GUI thread, not the emulation thread.

---

## 8. `src/qt/qt_rendererstack.hpp` -- Renderer enum

### Relevant Lines

- Line 87: `VideoCommon` entry in `Renderer` enum

### Issues Found

- [LOW] Line 87: The `VideoCommon` enum value is NOT guarded by `#ifdef USE_VIDEOCOMMON`. It always exists regardless of build configuration. This is consistent with how `Vulkan` is handled (also always present in the enum), and the `createRenderer()` switch handles the missing case via the `default:` fallback to Software. But it means the C++ enum has an entry that may have no corresponding implementation.

### Notes

- The enum ordering is: Software=0, OpenGL3=1, Vulkan=2, VideoCommon=3, None=-1. The integer values match `RENDERER_SOFTWARE=0`, `RENDERER_OPENGL3=1`, `RENDERER_VULKAN=2`, `RENDERER_VIDEOCOMMON=3` from `renderdefs.h` by coincidence of ordering (they're not explicitly assigned in the enum class). This implicit mapping works because `qt_mainwindow.cpp` uses an explicit switch rather than a cast.

---

## 9. `src/qt/qt_mainwindow.cpp` -- vid_api mapping

### Relevant Lines

- Lines 536-538: `RENDERER_VIDEOCOMMON` -> `Renderer::VideoCommon` mapping

### Issues Found

None.

### Notes

- The switch case is simple and correct: maps the C-side integer constant to the C++ enum.
- Not guarded by `#ifdef USE_VIDEOCOMMON`, but this is harmless: even if the renderer isn't available, the switch mapping just sets `newVidApi`, and `createRenderer()` handles the unknown case gracefully.

---

## 10. `src/qt/qt.c` -- plat_vidapi mapping

### Relevant Lines

- Lines 48-49: String-to-int mapping (`"qt_videocommon"` -> `RENDERER_VIDEOCOMMON`)
- Lines 71-72: Int-to-string mapping (`RENDERER_VIDEOCOMMON` -> `"qt_videocommon"`)

### Issues Found

None.

### Notes

- Both mappings are unguarded (no `#ifdef`), which is consistent with the approach throughout the renderer infrastructure. The constants come from `renderdefs.h` which is always available.
- If a user manually sets `vid_api = qt_videocommon` in their config and the build doesn't have VideoCommon, the renderer stack will fall back to Software (via the `default:` case in `createRenderer`).

---

## 11. `src/include/86box/renderdefs.h` -- Renderer constants

### Relevant Lines

- Line 25: `RENDERER_NAME_QT_VIDEOCOMMON "qt_videocommon"`
- Line 31: `RENDERER_VIDEOCOMMON 3`

### Issues Found

None.

### Notes

- The constants are always defined (not conditionally compiled). This is the correct approach -- it allows the config file to reference VideoCommon regardless of build configuration, with graceful fallback.
- `RENDERER_VIDEOCOMMON = 3` sits between `RENDERER_VULKAN = 2` and `RENDERER_VNC = 4`. No collisions.

---

## Cross-Cutting Analysis

### Thread Model

The VideoCommon integration touches four threads:

| Thread | VideoCommon Actions |
|--------|-------------------|
| **FIFO thread** | Deferred init trigger (swap cmd), triangle submission, swap buffer, fastfill, LFB read/write (FIFO-enqueued) |
| **Deferred init thread** | `vc_init()`, writes `vc_ctx` and `vc_init_pending` |
| **Timer/display thread** | Readback for scanout, `vc_readback_buf` cache, dirty-line marking |
| **Qt GUI thread** | VCRenderer creation, `direct_present_active` flag, `vc_get_global_ctx()` |

Synchronization correctness:
- `vc_ctx` (_Atomic): Written by init thread, read by all others. Correct.
- `vc_init_pending` (_Atomic): Written by FIFO and init threads. Correct.
- `use_gpu_renderer` (plain int): Written by init thread on failure. **Technically a data race** but benign.
- `vc_readback_buf` (plain pointer): Single-thread access (display thread only). Correct.
- `vc_init_thread` (thread_t*): Written by FIFO thread, read by close path. Safe due to lifetime (close waits for FIFO to finish, but see CRITICAL issue above -- the current ordering doesn't actually ensure this for VideoCommon teardown).
- `direct_present_active` (plain int): Written by Qt thread, read by display thread. **Technically a data race** but benign on target architectures.

### Deferred Init Design

The deferred init fires on the first `SST_swapbufferCMD` for Voodoo 1/2 (pre-Banshee). This is a good trigger point because:
1. The guest driver has completed PCI probing and register init.
2. The display mode is set (`h_disp`/`v_disp` are valid).
3. The FIFO thread is the only thread that writes to the SPSC ring, so init from a background thread doesn't conflict.

During init, the software renderer handles all triangles (because `vc_ctx` is still NULL). Once init completes, subsequent triangles are redirected to the VK path. Triangles submitted during init are rendered in software and will be overwritten by subsequent VK frames -- this is acceptable.

### Fallback Behavior

When `USE_VIDEOCOMMON` is not defined:
- All `#ifdef USE_VIDEOCOMMON` blocks are compiled out.
- The `voodoo_t` struct has no VC fields (no size change).
- No VideoCommon headers are included.
- The `gpu_renderer` config option is absent from `voodoo_config[]`.
- The renderer enum/constants in `renderdefs.h` and `qt_rendererstack.hpp` still exist but have no implementation -- `createRenderer()` falls through to Software.
- All code paths compile and work as before. **Verified correct.**

### Config Handling

- `gpu_renderer` is a `CONFIG_BINARY` (checkbox) with `default_int = 0` (disabled by default).
- Only present in `voodoo_config[]` (Voodoo 1/2). Banshee/V3 configs don't include it.
- `device_get_config_int("gpu_renderer")` returns 0 for devices without the config key.
- The config value is stored per-device in the 86Box config file (e.g., `[Creative Labs 3D Blaster Voodoo 2]` section).

---

## Priority Fix List

1. **[CRITICAL] Close ordering in `voodoo_card_close()`** (`src/video/vid_voodoo.c` lines 1549-1569): Move `vc_close()` call to after FIFO thread shutdown to prevent use-after-free. The deferred init thread join can remain early (it's safe and necessary to prevent the init from racing with close), but the actual context destruction must happen after all consumer threads are dead.

2. **[HIGH] SLI inconsistency in display path** (`src/video/vid_voodoo_display.c` lines 541-554): The outer guard checks `draw_voodoo->vc_ctx` but inner code uses `voodoo->vc_ctx`. Should consistently use the master's context, or document that SLI + GPU rendering is not supported.

3. **[HIGH] Banshee/V3 swap handler skips VideoCommon** (`src/video/vid_voodoo_reg.c` lines 140-188): The Banshee early-return at line 168 means VideoCommon hooks are unreachable for Banshee/V3. This is currently harmless (no config option exposed), but blocks future Banshee GPU acceleration without refactoring.
