# Texture System Code Audit

**Files**: `src/video/videocommon/vc_texture.h`, `src/video/videocommon/vc_texture.c`
**Date**: 2026-02-28
**Auditor**: vc-debug agent

---

## vc_texture.h -- 263 lines

### Issues Found

- [LOW] Line 108: `current_set` field is dead state. It is written in three places (`vc_texture_bind` line 1105, `vc_texture_reset_descriptors` line 1118, `vc_texture_close` line 576) but never read anywhere in the codebase. The descriptor set returned by `vc_texture_bind()` is used directly by the caller (`vc_thread.c` line 383). This field serves no purpose and could be removed.

- [LOW] Line 94: `entry_count` field is dead bookkeeping. It is incremented in `vc_texture_upload()` when a new slot is first used (line 722) but never decremented when entries are destroyed, and never read outside `vc_texture_close()` (line 526 just zeros it). The field is unreliable (only tracks additions, never removals) and unused.

### Notes

- The header is clean and well-structured. The `vc_sampler_key_t` is packed into 8 bytes with explicit padding, which ensures safe `memcmp()` comparison in the sampler cache lookup.
- `VC_TEX_MAX_DESCRIPTOR_SETS=1024` is generous and resolves the earlier exhaustion issue documented in `texture-upload-black-screen.md`.
- The `VC_TEX_POOL_SIZE=128` matches `2 * TEX_CACHE_MAX(64)` for 2 TMUs, which is the correct maximum.
- `VC_TEX_STAGING_SIZE` (256KB) exactly fits the maximum texture (256x256 x BGRA8 = 256KB) with zero margin. This is tight but correct for the base level. See vc_texture.c issues for the `row_stride` concern.
- Inline accessors `vc_texture_get_view()` and `vc_texture_get_generation()` are correct with proper bounds checking.

---

## vc_texture.c -- 1121 lines

### Issues Found

- [HIGH] Lines 139-148 (`vc_tex_begin_transfer`): **Missing vkWaitForFences before vkResetFences.** The function calls `vkResetFences()` at line 144 without first calling `vkWaitForFences()` to ensure the previous transfer has completed. On the very first call this is safe because the fence is created signaled (line 490). On subsequent calls, `vc_tex_end_transfer()` does call `vkWaitForFences()` at line 187-188 and returns, so in practice the fence is always signaled when `vc_tex_begin_transfer` is next called, assuming no error path skips the wait. However, this pattern is fragile. If any future code path calls `vc_tex_begin_transfer` after a failed `vc_tex_end_transfer` that returned early before the wait, the fence could be in an unsignaled state and `vkResetFences` would not error but would create an undefined state. **Current code is safe in all existing call paths** because every `begin_transfer` is paired with exactly one `end_transfer` and `end_transfer` always reaches `vkWaitForFences` before returning (the VK_CHECK on line 185 would return -1 on submit failure, but the fence was never signaled in that case -- resetting an unsignaled fence is valid per spec). Downgrading to **MODERATE** since the existing paths are safe.

  **Revised assessment: [MODERATE]** -- Defensive `vkWaitForFences()` before `vkResetFences()` would make the function self-contained and robust against future changes. No current bug.

- [MODERATE] Lines 645-646 (`vc_texture_upload`): **Staging buffer overflow possible with row_stride > width.** The copy size is computed as `row_stride * height * 4`. If `row_stride > width`, the staging copy can exceed `VC_TEX_STAGING_SIZE`. The guard at line 646-651 correctly catches this case, so there is no overflow. However, the `VkBufferImageCopy` at line 672-684 sets `bufferRowLength = row_stride` which tells Vulkan the stride in texels. This is correct -- Vulkan will read `width` texels from each row of `row_stride` texels. **No bug here.** The staging buffer capacity check is correct.

- [MODERATE] Line 458: **VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT is unnecessary.** The pool is created with `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` but `vkFreeDescriptorSets()` is never called anywhere in the codebase. The pool is only ever reset whole via `vkResetDescriptorPool()`. This flag has a minor performance cost on some implementations (e.g., NV driver allocates individual tracking metadata per set when this flag is present). Removing it is a micro-optimization, not a correctness issue.

- [MODERATE] Lines 640-641 (`vc_texture_upload`): **Image leaked on create_image success + staging overflow.** If `vc_tex_create_image` succeeds at line 636 but the staging size check at line 646 fails, the error path at line 649 correctly calls `vc_tex_destroy_entry` -- **no leak**. Similarly checked all other error paths: lines 655-657 (begin_transfer fail after image create), 702-704 (end_transfer fail after image create), 710-712 (view create fail). All call `vc_tex_destroy_entry`. **All error paths are correct.**

- [LOW] Lines 716-718 (`vc_texture_upload`): **entry_count bookkeeping is unreliable.** The `entry_count` is incremented when `!was_in_use` but `was_in_use` is captured at line 629 *before* `vc_tex_destroy_entry` is called at line 633. Since `vc_tex_destroy_entry` sets `in_use=0`, the `was_in_use` variable correctly captures the *previous* state. However, `entry_count` is never decremented in any destroy path. Since it is never read, this is dead code.

- [LOW] Line 1105: **`current_set` written but never read.** See header audit above.

- [LOW] Lines 353-365 (`vc_thread_bind_textures` in vc_thread.c, affecting sampler cache): **Sampler key mip_mode is always hardcoded to 2 (no mipmapping).** All sampler keys set `.mip_mode = 2`, which clamps `maxLod = 0.0f` at line 969. This means even if mip levels are uploaded via `vc_texture_upload_mip()`, they will never be sampled because the sampler is always configured for base-level only. This may be intentional (Voodoo mipmapping handled differently, e.g., LOD calculated in software and specific mip uploaded) but it means `vc_texture_upload_mip()` is effectively dead code for rendering purposes. If hardware mipmapping is intended in the future, the sampler key construction in `vc_thread_bind_textures` must be updated to extract the mipmap mode from `textureMode`.

### Verified Correct

1. **Thread safety invariant**: VERIFIED. All `vc_texture_*` functions are called exclusively from the render thread:
   - `vc_texture_init` / `vc_texture_close`: called during context init/destroy (single-threaded).
   - `vc_texture_upload`: called from `vc_thread.c` line 719 inside `VC_CMD_TEXTURE_UPLOAD` dispatch (render thread only).
   - `vc_texture_upload_mip`: no current callers in SPSC dispatch, but would be render-thread-only.
   - `vc_texture_upload_fog`: called from `vc_thread.c` line 752 inside `VC_CMD_FOG_UPLOAD` dispatch (render thread only).
   - `vc_texture_bind`: called from `vc_thread.c` lines 241, 376 (render thread only).
   - `vc_texture_get_sampler`: called from `vc_thread.c` lines 370-372 (render thread only).
   - `vc_texture_reset_descriptors`: called from `vc_thread.c` line 299 inside `vc_begin_frame` (render thread only).
   - `vc_texture_invalidate`: not currently called from any thread. If called from emulation thread, it would be a race on `entries[slot].generation` but since it's only a counter bump, the worst case is a skipped invalidation or double invalidation, both benign.
   - The `vc_texture_upload_async` and `vc_texture_bind_async` wrappers in `vc_core.c` correctly marshal data through the SPSC ring, with the emulation thread doing `malloc`+`memcpy` and the render thread doing `free` after upload.

2. **Image layout transitions**: VERIFIED CORRECT for all paths:
   - `vc_tex_create_placeholder`: UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY. Correct barriers.
   - `vc_tex_create_fog_table`: UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY. Correct barriers.
   - `vc_texture_upload`: UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY for all mip levels. Correct.
   - `vc_texture_upload_mip`: SHADER_READ_ONLY -> TRANSFER_DST -> SHADER_READ_ONLY for single mip. Correct, with per-mip-level subresource range.
   - `vc_texture_upload_fog`: SHADER_READ_ONLY -> TRANSFER_DST -> SHADER_READ_ONLY. Correct.
   - All barriers use correct src/dst access masks and pipeline stages.

3. **Pipeline barrier correctness**: VERIFIED. All transfer operations have proper barriers:
   - Pre-copy barrier: ensures previous reads complete before write.
   - Post-copy barrier: ensures write completes before shader read.
   - Stage masks correctly pair TOP_OF_PIPE/TRANSFER (for initial transitions from UNDEFINED) and FRAGMENT_SHADER/TRANSFER (for re-uploads of existing images).

4. **Queue mutex usage**: VERIFIED CORRECT. `vc_tex_end_transfer` at line 182-184 correctly acquires `ctx->queue_mutex` around `vkQueueSubmit`. This prevents races with the frame submission path on the same graphics queue.

5. **Staging buffer management**: VERIFIED CORRECT. The staging buffer is persistently mapped (`VMA_ALLOCATION_CREATE_MAPPED_BIT`), and `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` ensures host-coherent memory. Data is copied to staging, then a fence-waited submit guarantees GPU has consumed the data before any subsequent `memcpy` to the same staging buffer.

6. **Descriptor set management**: VERIFIED CORRECT. The pool is reset per-frame via `vc_texture_reset_descriptors` in `vc_begin_frame`. All sets allocated during the previous frame are implicitly freed. The pool has capacity for 1024 sets x 3 descriptors = 3072 combined_image_sampler descriptors, which is ample for any frame.

7. **Sampler cache**: VERIFIED CORRECT. Linear scan over max 8 entries. `memcmp` comparison is safe because `vc_sampler_key_t` has explicit padding zeroed at construction. Cache size 8 is generous for Voodoo's limited sampler modes (point/bilinear x clamp/wrap x 2 TMUs = 4 common combos). If cache is full, returns VK_NULL_HANDLE with a log message.

8. **Resource cleanup in `vc_texture_close`**: VERIFIED CORRECT. All resources are destroyed in correct order:
   - Pool entries (image + view + alloc)
   - Placeholder (entry + sampler)
   - Fog table (entry + sampler)
   - Cached samplers
   - Descriptor pool (implicitly frees all remaining sets)
   - Command pool (implicitly frees command buffer)
   - Transfer fence
   - Staging buffer (VMA destroys buffer + frees memory)
   - Final `memset(tex, 0, ...)` zeros the entire struct.

9. **Error path cleanup in `vc_texture_upload`**: VERIFIED CORRECT. Every error after successful image creation calls `vc_tex_destroy_entry` to clean up the partially-created entry.

10. **Descriptor set writes in `vc_texture_bind`**: VERIFIED CORRECT. Three `VkWriteDescriptorSet` entries correctly target bindings 0, 1, 2. Fallback to placeholder view+sampler when slot is invalid (-1 or out of range or not in_use). Fog table always uses its dedicated view and sampler.

### Notes

- The entire texture upload path is synchronous (submit + wait per upload). This is noted in memory as a known performance bottleneck. Future optimization would batch multiple uploads into a single command buffer submission.
- The `vc_texture_upload_mip` function is currently dead code -- no caller in the SPSC command dispatch. The only upload command is `VC_CMD_TEXTURE_UPLOAD` which calls `vc_texture_upload` (base level only).
- The `vc_texture_invalidate` function is also unused -- no callers found in the codebase.
- The fog table format `VK_FORMAT_R8G8_UNORM` with 64x1 dimensions is correct for the Voodoo fog table (64 entries of fog+dfog packed as 2 bytes each).
- The code is clean, well-commented, and follows a consistent pattern throughout. Error handling is thorough.

---

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 0 | -- |
| HIGH | 0 | -- |
| MODERATE | 2 | Defensive fence wait missing; unnecessary descriptor pool flag |
| LOW | 3 | Dead `current_set` field; dead `entry_count` bookkeeping; mip_mode always 2 |

**Overall assessment**: The texture system is well-implemented with no correctness bugs. Thread safety invariant is verified. All Vulkan operations are correctly synchronized. All error paths properly clean up resources. The two MODERATE issues are defensive improvements and a micro-optimization, not bugs. The three LOW issues are dead code that can be cleaned up.
