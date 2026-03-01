# VideoCommon Readback & LFB Audit

**Files audited:**
- `src/video/videocommon/vc_readback.h` (597 lines)
- `src/video/videocommon/vc_readback.c` (1744 lines)

**Date:** 2026-02-28

---

## vc_readback.h -- 597 lines

### Issues Found

- **[MODERATE] Lines 296-316: Dirty tile inline functions lack bounds checking.**
  The `vc_dirty_tile_set()`, `vc_dirty_tile_clear()`, and `vc_dirty_tile_test()` functions accept `(tx, ty)` but do NOT validate that they are within `[0, VC_TILE_MAX_X)` x `[0, VC_TILE_MAX_Y)`. If `tx=16` and `ty=0`, then `bit = 0*16+16 = 16`, which accesses `bits[0]` bit 16 -- this is fine. But `ty=16, tx=0` gives `bit = 256`, accessing `bits[8]` -- OUT OF BOUNDS since `bits[]` has only 8 elements (indices 0-7). The callers in `vc_readback_mark_triangle_dirty()` (line 436-437 of .c) DO clamp to `rb->tiles_x` and `rb->tiles_y`, and `vc_readback_mark_pixel_dirty()` (line 366-367 of .h) also clamps. However, these low-level inline helpers are exported in the header and could be called directly without bounds checking. The `vc_dirty_tile_set/clear/test` functions should guard against `bit >= VC_TILE_MAX_COUNT` or at least document the precondition.
  **Risk: Out-of-bounds write if any future caller forgets bounds checking.**

- **[LOW] Line 502: `_Atomic int dirty` field in `vc_lfb_write_t`.**
  Using C11 `_Atomic int` is correct, but the cast pattern `&((vc_lfb_write_t *) lw)->dirty` in line 579-580 to work around `const` qualification is a mild code smell. The `vc_lfb_write_is_dirty` function takes `const vc_lfb_write_t *lw` and then casts away const to call `atomic_load_explicit`. This is technically fine for atomics (reads don't modify the object), but a cleaner approach would be to drop the `const` qualifier from the parameter since atomics have their own memory model semantics.

- **[LOW] Line 490: `void *mutex` type opacity.**
  The mutex is declared as `void *` with a comment `/* mutex_t* from 86box/thread.h */`. This is fine for decoupling, but every call site casts to `(mutex_t *)`. A forward-declared `typedef` or `#include <86box/thread.h>` would be cleaner and catch type errors at compile time.

### Notes

- The dirty tile bitmask design (256 bits = 8 uint32_t words for 16x16 tiles over 1024x1024 framebuffer) is compact and efficient. The `vc_dirty_tiles_any()` loop over 8 words is fast.
- The adaptive threshold design (sync at <10 reads/frame, async at >=10, with 5-frame cooldown) is well thought out.
- The `VC_READBACK_ASYNC_BUFFERS = 2` ping-pong pattern is clean.
- The `vc_readback_mark_pixel_dirty()` inline marks BOTH color and depth dirty on any pixel write, which is correct for LFB writes but slightly conservative (an LFB color-only write also marks depth dirty). This is harmless -- just a minor bandwidth waste if depth is subsequently read back.
- The `vc_lfb_write_t` shadow buffer design (max-dimension allocation, row-granularity dirty tracking, atomic dirty flag for fast-path skip) is sound.

---

## vc_readback.c -- 1744 lines

### Issues Found

#### CRITICAL

- **[CRITICAL] Lines 1348-1371, 1374-1393: Data race on shadow buffer pixel writes.**
  `vc_lfb_write_color_pixel()` writes to `lw->color_shadow[]` at line 1358-1361 BEFORE acquiring the mutex at line 1364. The `vc_lfb_write_flush_color()` function reads from `lw->color_shadow[]` at line 1424-1426 AFTER releasing the mutex at line 1416. This means:
  1. CPU thread writes shadow pixel at offset X (no lock)
  2. Render thread snapshots dirty range under lock (lines 1404-1416)
  3. Render thread copies shadow buffer rows (line 1424-1426, no lock)
  4. CPU thread writes another shadow pixel at a different offset in the same row range (no lock)

  Steps 3 and 4 can execute concurrently -- the CPU thread writes to `color_shadow[]` while the render thread reads from it. This is a textbook data race.

  The code comment (header lines 443-449) claims "Between flushes, only the CPU thread writes the shadow buffer" -- but this is NOT enforced. The mutex only protects the dirty min/max/bitmask, not the shadow buffer itself. The render thread's `memcpy` at line 1424 races with any concurrent pixel writes from the CPU thread.

  **Severity:** In practice, the race window is narrow and the worst case is a torn pixel (half-old, half-new bytes for one pixel) in the staging upload. For LFB writes which are typically full-screen blits or UI overlays, this is unlikely to cause visible artifacts. But it IS undefined behavior per the C standard.

  **Same issue in `vc_lfb_write_depth_pixel()` at lines 1382-1383 vs `vc_lfb_write_flush_depth()` at lines 1563-1565.**

  **Fix:** Either (a) hold the mutex during the `memcpy` in the flush functions (blocking new pixel writes during copy -- latency hit), or (b) use double-buffered shadow buffers (CPU writes to one while render thread reads from the other, swap at flush time), or (c) accept the race as benign and add a comment documenting it.

#### HIGH

- **[HIGH] Lines 730-743, 757-764: Sync readback from CPU thread while render thread may be using the image.**
  `vc_readback_pixels()` (called from display/timer thread, line 581 of `vid_voodoo_display.c`) and `vc_readback_color_sync()` / `vc_readback_depth_sync()` (called from CPU thread after `vc_wait_idle()`, line 1104 of `vid_voodoo_vk.c`) both call `vc_readback_execute()` or `vc_readback_execute_dirty()` which submit their own command buffers to the graphics queue.

  The `vc_readback_execute()` function transitions the image from `COLOR_ATTACHMENT_OPTIMAL` to `TRANSFER_SRC_OPTIMAL` and back (lines 157-218). If the render thread concurrently begins a new render pass that uses the SAME image as a color attachment, the image layout transitions will conflict.

  For the LFB read path (`vc_voodoo_lfb_read_pixel`), `vc_wait_idle()` is called first, which drains the ring and calls `vkDeviceWaitIdle()`. This makes it safe because the render thread is idle.

  For the display path (`vc_readback_pixels` in `vid_voodoo_display.c`), there is NO explicit wait-idle before the readback. The comment says "The front buffer is already finalized by a prior CMD_SWAP, and frames_completed gates entry." This is true -- the front buffer is not being rendered to (the back buffer is), so layout transitions on the front buffer are safe. **VERIFIED CORRECT** -- the display path reads the front buffer, the render thread writes the back buffer.

  **However**, there is a subtle issue: `vc_readback_execute()` submits a command buffer via `vkQueueSubmit` (line 244). If the render thread ALSO submits a command buffer at the same time (via `vc_end_frame()`), these submissions are protected by `queue_mutex`. But the render thread might also do a `vkResetCommandPool` or `vkResetCommandBuffer` on its own pool between frames. Since the readback uses its OWN dedicated `cmd_pool` (line 132 of header), there is no conflict. **VERIFIED CORRECT.**

  **Remaining risk:** `vc_readback_pixels()` reads from the front buffer, but after a swap, the NEW front buffer was the OLD back buffer which may still have in-flight GPU work. The `frames_completed` atomic gate only ensures at least one frame has finished, not that the CURRENT front buffer's render is complete. If the display path calls `vc_readback_pixels()` immediately after a swap, the front image might still be in use by a not-yet-retired GPU command buffer. The `vc_readback_execute()` adds a barrier with `srcStageMask = COLOR_ATTACHMENT_OUTPUT`, which will wait for any prior color attachment writes, so the barrier itself handles this correctly on the GPU side. **VERIFIED CORRECT** -- Vulkan barriers ensure proper ordering.

- **[HIGH] Lines 1190, 1196-1224: `reads_this_frame` and `low_read_frames` are not atomic and accessed from multiple threads.**
  `vc_readback_record_read()` (line 1190: `rb->reads_this_frame++`) is called from the CPU emulation thread (via `vc_readback_track_read()` -> `vc_voodoo_lfb_read_pixel()`).
  `vc_readback_frame_end()` (lines 1196-1224) reads and resets `reads_this_frame` and is called from the render thread (via `vc_thread.c` line 669, in the VC_CMD_SWAP handler).

  This is a data race: the CPU thread increments `reads_this_frame` while the render thread reads and resets it. The worst case is a lost increment (the counter might be slightly wrong for one frame), which would cause a delayed mode transition. This is unlikely to cause functional issues but IS undefined behavior.

  **Fix:** Make `reads_this_frame` an `_Atomic uint32_t` and use `atomic_fetch_add` / `atomic_exchange`.

- **[HIGH] Lines 398-447: `vc_readback_mark_triangle_dirty()` accesses `rb->color_dirty_tiles` and `rb->depth_dirty_tiles` without synchronization.**
  This function is called from the FIFO/CPU thread (via `vc_readback_mark_dirty()` -> `vc_voodoo_submit_triangle()` at `vid_voodoo_vk.c:828`). The dirty tile bitmasks are also read by the render thread in `vc_readback_execute_dirty()` and `vc_readback_execute_depth_dirty()` (via `vc_dirty_tile_test()` in `vc_build_dirty_tile_regions()`).

  However, the sync readback functions (`vc_readback_execute_dirty()`) are only called after `vc_wait_idle()` (which drains the ring and waits for GPU idle), so by the time the render thread reads the dirty tiles, the CPU thread has stopped writing to them. And in the display path, the dirty tiles for the front buffer were finalized before the swap.

  But `vc_readback_mark_pixel_dirty()` (called from `vc_lfb_write_color()` / `vc_lfb_write_depth()` in `vc_core.c` lines 1129, 1143) runs from the CPU thread and modifies the dirty bitmask concurrently with any potential readback. If an LFB write occurs while a sync readback is in progress, the dirty bitmask has a data race.

  **Practical impact:** Low, because `vc_wait_idle()` is called before sync readback, which implies the CPU thread is blocked. But the display-path readback (`vc_readback_pixels`) does NOT call `vc_wait_idle()`, and the CPU thread could be writing LFB pixels while the display thread reads back the front buffer. The display path uses `vc_readback_execute()` (full, NOT dirty-aware), so it does not read the dirty bitmask. **VERIFIED: the data race exists in theory but the current call graph avoids it.**

#### MODERATE

- **[MODERATE] Lines 428-431: Float-to-int truncation in tile coordinate conversion.**
  ```c
  int tx_min = (int) min_x >> VC_TILE_SHIFT;
  ```
  The `(int) min_x` truncation from float to int is correct for positive values, but C's float-to-int conversion for negative values truncates toward zero, not toward negative infinity. After the clamping at lines 419-422, `min_x >= 0.0f`, so this is fine. However, if `min_x` is exactly 0.0f and the float representation is `-0.0f` (which can happen after subtraction), `(int)(-0.0f)` is 0, which is correct. **No bug, but the code relies on the clamping being correct.**

- **[MODERATE] Lines 1419-1426: Shadow buffer copy uses `lw->width` stride but buffer is allocated at `VC_LFB_WRITE_MAX_WIDTH`.**
  The shadow buffer (`color_shadow`) is allocated at `VC_LFB_WRITE_MAX_WIDTH * VC_LFB_WRITE_MAX_HEIGHT * 4` bytes (line 1254), but `vc_lfb_write_color_pixel()` uses `lw->width` for row stride (line 1357: `offset = y * lw->width + x) * 4`). The flush function also uses `lw->width` (line 1419: `row_bytes = lw->width * 4`).

  This means the shadow buffer has "dead" columns between `lw->width` and `VC_LFB_WRITE_MAX_WIDTH` on each row. The `memcpy` in the flush (line 1424-1426) copies `dirty_height * lw->width * 4` bytes starting at offset `min_row * lw->width * 4`, which is correct because the data layout in `color_shadow` IS contiguous with stride `lw->width`, not `VC_LFB_WRITE_MAX_WIDTH`.

  Wait -- this is actually correct! The allocation is oversized but the pixel write function uses `lw->width` stride, so data is packed at the actual width. The max allocation just wastes some memory at the end. The `memcpy` uses `copy_offset = min_row * lw->width * 4` which correctly addresses the packed data. **No bug.**

- **[MODERATE] Lines 1469-1481: `bufferImageHeight` in LFB write flush copy region.**
  ```c
  .bufferRowLength   = lw->width,
  .bufferImageHeight = dirty_height,
  ```
  The Vulkan spec for `VkBufferImageCopy` says `bufferRowLength` is "the number of texels in a buffer row" and `bufferImageHeight` is "the number of rows in a buffer image layer." Setting `bufferImageHeight = dirty_height` tells Vulkan that the buffer contains exactly `dirty_height` rows. Since the staging buffer holds exactly `dirty_height` rows of data (it was filled by `memcpy` from the shadow buffer), this is correct. The `imageOffset.y = min_row` correctly targets the right rows in the destination image. **VERIFIED CORRECT.**

- **[MODERATE] Lines 820-823: Async staging leak on partial creation failure.**
  In `vc_async_staging_create()`, if the color buffer creation succeeds but the depth buffer fails (line 838), the function returns -1 without destroying the color buffer. The cleanup in `vc_readback_async_init()` (lines 908-910) calls `vc_async_staging_destroy()` for indices `[0..i-1]`, which would handle a PRIOR staging pair, but not the CURRENT partially-created one at index `i`.

  Looking more carefully: the `vc_async_staging_create()` function `memset(as, 0, sizeof(*as))` at the start (line 798), and on failure it returns -1. The partially-created resources (color_buf is valid, depth_buf is not) are NOT cleaned up. `vc_async_staging_destroy()` for the FAILED index is never called.

  **Fix:** Call `vc_async_staging_destroy(&rb->async_staging[i], ctx)` after the failure, or clean up within `vc_async_staging_create()` itself on failure.

- **[MODERATE] Lines 499-501, 1469-1471: `bufferRowLength` and `bufferImageHeight` in dirty tile readback.**
  In `vc_build_dirty_tile_regions()`:
  ```c
  .bufferRowLength   = rb->width, /* Row stride in pixels (not bytes). */
  .bufferImageHeight = rb->height,
  ```
  This tells Vulkan that each tile copy's buffer data has a row stride of `rb->width` pixels and an image height of `rb->height` pixels. Combined with the `bufferOffset` that places each tile at its correct position within the full-framebuffer-sized staging buffer, this is correct -- each tile writes into the right place in the full-framebuffer layout. **VERIFIED CORRECT.**

#### LOW

- **[LOW] Lines 190-191, 319-320, 992-993, 1057-1058: Trailing whitespace in `.imageOffset` / `.imageExtent` initializers.**
  The `imageOffset` and `imageExtent` lines have excessive trailing whitespace padding. This is a formatting issue only, not a bug.

- **[LOW] Line 142: Sync readback fence created unsignaled, but vkResetFences is called before every use.**
  In `vc_readback_execute()` line 142, `vkResetFences` is called before the fence is used. The fence was created unsignaled (no `VK_FENCE_CREATE_SIGNALED_BIT`). Calling `vkResetFences` on an already-unsignaled fence is a valid operation per the Vulkan spec (it's a no-op), so this is fine. Just slightly unnecessary on the first call.

- **[LOW] Lines 848-849: Async staging fences created SIGNALED.**
  This is correct -- the comment at line 846 explains: "start signaled so first wait succeeds." The first `vc_readback_async_color()`/`vc_readback_async_depth()` call will check fence status; if no submission was ever made (`as->submitted == 0`), it returns NULL before checking the fence. And in `vc_readback_async_submit()`, the fence is waited on (if submitted) then reset before use. So the initial signaled state is harmless but also unused. **No bug, just slightly redundant.**

- **[LOW] Lines 937-940: Potential infinite wait in async submit.**
  `vc_readback_async_submit()` waits on the fence with `UINT64_MAX` timeout (line 938) if the previous submission to this staging buffer is still in-flight. If the GPU hangs, this will block forever. The sync readback has the same pattern (line 252). This is standard Vulkan practice -- a hung GPU requires driver-level recovery.

- **[LOW] Line 1224: `reads_this_frame` reset at end of frame.**
  The reset `rb->reads_this_frame = 0` in `vc_readback_frame_end()` happens after the threshold check. If a read happens between the threshold check and the reset (from the CPU thread), that read is lost. This is the same data race as the HIGH issue above -- just noting it affects the reset path too.

- **[LOW] Lines 1680-1695: `vc_lfb_write_close()` doesn't wait for pending GPU work.**
  If a flush was submitted but not yet completed (the fence is not signaled), destroying the staging buffer could cause use-after-free on the GPU. However, in practice, the flush functions (`vc_lfb_write_flush_color/depth`) call `vkWaitForFences` synchronously before returning, so the fence is always signaled when the flush function returns. And `vc_lfb_write_close()` is called during shutdown when the device is idle. **No practical bug.**

- **[LOW] Lines 1692-1693: `free(NULL)` is safe in C.**
  The close function calls `free(lw->color_shadow)` and `free(lw->depth_shadow)` without NULL checks. Since `free(NULL)` is defined as a no-op in C, this is fine.

### Notes

#### Vulkan Correctness

1. **Pipeline barriers are correct throughout.** Every image layout transition has proper src/dst access masks and stage masks:
   - Color readback: `COLOR_ATTACHMENT_OUTPUT -> TRANSFER` (pre), `TRANSFER -> COLOR_ATTACHMENT_OUTPUT` (post), `TRANSFER -> HOST` (host visibility)
   - Depth readback: `LATE_FRAGMENT_TESTS -> TRANSFER` (pre), `TRANSFER -> EARLY|LATE_FRAGMENT_TESTS` (post), `TRANSFER -> HOST`
   - LFB write color: `COLOR_ATTACHMENT_OUTPUT -> TRANSFER` (pre), `TRANSFER -> COLOR_ATTACHMENT_OUTPUT` (post)
   - LFB write depth: `LATE_FRAGMENT_TESTS -> TRANSFER` (pre), `TRANSFER -> EARLY|LATE_FRAGMENT_TESTS` (post)

2. **VMA invalidation for non-coherent memory is correctly applied** after every readback fence wait (`vmaInvalidateAllocation` at lines 259, 389, 645, 779, 1145, 1167).

3. **VMA flush for host writes before GPU upload** is correctly applied in LFB write flush (`vmaFlushAllocation` at lines 1429, 1567).

4. **Queue mutex correctly protects all vkQueueSubmit calls** (lines 243-245, 373-375, 629-631, 763-765, 1111-1113, 1521-1523, 1660-1662).

5. **Command pool usage is correct.** Sync readback, async readback, and LFB write each have dedicated command pools with `RESET_COMMAND_BUFFER_BIT`. Async staging command buffers share the sync readback command pool (allocated in `vc_readback_init`, used in `vc_async_staging_create` line 856). This is valid -- a command pool can allocate multiple command buffers, and they are reset independently.

#### Thread Safety Summary

| Resource | Written by | Read by | Synchronization | Status |
|----------|-----------|---------|-----------------|--------|
| `color_shadow[]` | CPU thread (pixel write) | Render thread (flush memcpy) | Mutex on dirty tracking only, NOT on buffer data | **DATA RACE** |
| `depth_shadow[]` | CPU thread (pixel write) | Render thread (flush memcpy) | Same as above | **DATA RACE** |
| `color_dirty_rows[]` | CPU thread (set under mutex) | Render thread (read+clear under mutex) | Mutex | CORRECT |
| `dirty_min/max_row` | CPU thread (update under mutex) | Render thread (snapshot under mutex) | Mutex | CORRECT |
| `dirty` atomic | CPU thread (store release) | Render thread (load acquire) | Atomic | CORRECT |
| `reads_this_frame` | CPU thread (increment) | Render thread (read+reset) | NONE | **DATA RACE** |
| `low_read_frames` | Render thread only | Render thread only | Single-threaded | CORRECT |
| `color_dirty_tiles` | CPU thread (mark dirty) | Render thread (read dirty) | Implicit via wait_idle / swap ordering | Acceptable |
| `depth_dirty_tiles` | CPU thread (mark dirty) | Render thread (read dirty) | Same as above | Acceptable |
| `async_current` | Render thread (swap) | Render thread (submit/read) | Single-threaded | CORRECT |
| `mode` | Render thread (frame_end) | CPU thread (vc_readback_color_sync checks mode) | NONE | **DATA RACE** (benign) |

#### Design Quality

- The code is well-structured with clear phase separation (sync readback -> async readback -> LFB write -> dirty tiles).
- Comments are thorough and accurate (except for the shadow buffer thread safety claim).
- Error handling is consistent: VK_CHECK macro for init, explicit checks + pclog for runtime errors.
- The dirty tile system (`vc_build_dirty_tile_regions()`) correctly handles partial edge tiles by clamping tile width/height to framebuffer bounds.
- The adaptive mode switching is conservative (5-frame cooldown before returning to sync) which avoids ping-ponging.
- Memory management: staging buffers persist across mode switches (async buffers kept when switching back to sync), which avoids repeated allocation.

#### Potential Improvements (not bugs)

1. The sync readback path (`vc_readback_execute`) does a full-framebuffer copy every time. For the display path, this is fine. But for LFB pixel reads where only one pixel is needed, this is extremely wasteful. The dirty-tile variant helps, but still copies 64x64 tile regions. A single-pixel readback optimization (tiny staging buffer + single-pixel copy) would dramatically reduce bandwidth for games that do occasional LFB reads.

2. The LFB write shadow buffer is allocated at max dimensions (1024x1024 = 4MB color + 2MB depth = 6MB total), even for small framebuffers. This is a fixed overhead. For Voodoo 1 (640x480), 2.5MB is wasted.

3. The `vc_readback_execute()` and variants record+submit+wait per call. For the display path (called per scanline), this means a full GPU submission per frame. This is already optimized by caching the readback pointer in `voodoo->vc_readback_buf` (only fetched once per frame in the display callback).

---

## Summary of Issues

| Severity | Count | Key Issues |
|----------|-------|------------|
| CRITICAL | 1 | Shadow buffer data race between CPU pixel write and render thread flush memcpy |
| HIGH | 3 | `reads_this_frame` data race; dirty tile bitmask cross-thread access (acceptable in practice); sync readback image usage (verified correct) |
| MODERATE | 2 | Async staging partial creation leak; float-to-int tile conversion (verified correct after clamp) |
| LOW | 7 | Various minor items (formatting, redundant fence reset, infinite wait on GPU hang, etc.) |

### Priority Fixes

1. **CRITICAL: Shadow buffer race** -- Either add double-buffering, extend mutex scope to cover memcpy, or document as intentionally benign. The practical impact is low (torn pixels during concurrent write+flush), but it is UB.

2. **HIGH: `reads_this_frame` atomicity** -- Make it `_Atomic uint32_t`. Simple one-line fix.

3. **MODERATE: Async staging leak** -- Add cleanup call in the partial-failure path of `vc_readback_async_init()`.
