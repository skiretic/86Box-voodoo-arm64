# Render Pass + Shader Module Audit

Date: 2026-02-28
Files audited:
- `src/video/videocommon/vc_render_pass.h` (95 lines)
- `src/video/videocommon/vc_render_pass.c` (513 lines)
- `src/video/videocommon/vc_shader.h` (59 lines)
- `src/video/videocommon/vc_shader.c` (95 lines)

---

## vc_render_pass.h -- 95 lines

### Issues Found

- None.

### Notes

- Clean header with proper include guard and forward declaration.
- `vc_framebuffer_t` aggregates color+depth image/view/alloc plus the VkFramebuffer object -- well-structured.
- `vc_render_pass_t` holds dual framebuffers with a `back_index` toggle (0 or 1). Simple and correct.
- Inline accessors `vc_render_pass_back()` and `vc_render_pass_front()` use `1 - back_index` pattern, which is correct for a binary toggle but has no bounds validation. Since `back_index` is only ever set by `vc_framebuffer_swap()` (which toggles 0<->1) and initialized to 0 by `calloc`, this is safe in practice.
- No NULL checks in inline accessors -- callers must ensure `rp` is non-NULL. This is consistent with how they are used (always guarded by `if (!ctx->render_pass) return`).

---

## vc_render_pass.c -- 513 lines

### Issues Found

- **[MODERATE] Line 43: SPIR-V-style alignment concern in pCode cast** -- Wait, wrong file. This is render_pass.c. No issue here.

- **[MODERATE] Lines 228-241: Subpass dependency missing LATE_FRAGMENT_TESTS_BIT stage.**
  The external->subpass dependency specifies `srcStageMask` and `dstStageMask` that include `VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT` but NOT `VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT`. The uber-shader declares `layout(depth_any) out float gl_FragDepth` (line 83 of `voodoo_uber.frag`), which means depth test/write occurs at the late fragment test stage. The subpass dependency should include `LATE_FRAGMENT_TESTS_BIT` in both `srcStageMask` and `dstStageMask` to correctly synchronize depth attachment access from previous render pass instances that also use late fragment tests.

  In practice, this is masked by two factors: (1) Vulkan 1.2 implicit single-queue submission ordering provides full memory dependency between consecutive `vkQueueSubmit` calls, and (2) `LOAD_OP_LOAD` with `initialLayout == finalLayout` means the render pass performs no layout transition, so the dependency only governs access synchronization. However, the Vulkan validation layers SHOULD flag this as a missing dependency if depth is written at the late stage.

  **Fix:** Add `VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT` to both `srcStageMask` and `dstStageMask`:
  ```c
  .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
  .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
  ```

- **[LOW] Line 438: Unchecked `vkWaitForFences` return value.**
  `vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX)` return value is discarded. While `UINT64_MAX` timeout makes `VK_TIMEOUT` impossible, `VK_ERROR_DEVICE_LOST` is still a possible return. This is a one-shot initialization path (not hot path), so the practical risk is low -- if the device is lost, subsequent Vulkan calls will also fail. But for completeness, the return should be checked and logged.

  **Fix:**
  ```c
  res = vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX);
  if (res != VK_SUCCESS)
      pclog("VideoCommon: vkWaitForFences (layout transition) failed: %d\n", res);
  ```

- **[LOW] Lines 95-152: `vc_framebuffer_create` returns -1 on partial failure without cleaning up already-created resources.**
  For example, if `depth_image` creation succeeds but `depth_view` creation fails (line 132), the function returns -1 with `depth_image` and `depth_alloc` still allocated. However, this is NOT a leak because the caller (`vc_render_pass_create`) always calls `vc_render_pass_destroy(rp, ctx)` on failure, which calls `vc_framebuffer_destroy_single` on each framebuffer, checking each handle individually. Since the struct was `calloc`'d and only successfully-created handles are non-NULL, the cleanup correctly destroys only what was created. **No actual bug**, but the code relies on the caller for cleanup -- a defensive local cleanup would be more robust if this function were ever reused elsewhere.

### Notes

- **Image creation**: VMA with `VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT` is appropriate for framebuffer-sized images. Format choices (RGBA8_UNORM for color, D16_UNORM for depth) match Voodoo's native capabilities.
- **Usage flags**: `TRANSFER_SRC | TRANSFER_DST` on both color and depth images enables readback (LFB reads) and clear operations. Correct.
- **Initial layout transitions**: Three-step approach (UNDEFINED -> TRANSFER_DST -> clear -> attachment-optimal) is correct and well-structured. The barrier stages and access masks are appropriate for each transition.
- **Queue mutex**: Properly used around `vkQueueSubmit` (line 428-430). The one-shot command buffer pattern with fence wait is standard.
- **Temporary resource cleanup**: Command pool and fence are always destroyed, even on error paths. Correct.
- **Render pass definition**: `LOAD_OP_LOAD` with `initialLayout == finalLayout` for both color and depth is correct for incremental rendering (Voodoo draws triangles across multiple batches per frame). `STORE_OP_STORE` preserves results. Stencil ops are `DONT_CARE` since no stencil is used. All correct.
- **Thread safety of `back_index`**: `vc_framebuffer_swap()` is called only from the render thread (`vc_thread.c:335`). `vc_render_pass_front()` is called from the render thread (line 673 of vc_thread.c) and from the timer/CPU thread via `vc_readback_color_sync` / `vc_get_front_color_image`. The `back_index` field is a plain `int` with no atomic or lock protection. On ARM64, a non-atomic read of an `int` being written by another thread is a data race (undefined behavior per C11). However, in practice: (1) the swap happens once per frame after `vkQueueSubmit`, (2) readback only occurs after `frames_completed` is set (which is an atomic store that also acts as a release fence), and (3) the value only toggles between 0 and 1 (both valid indices). The race is benign in practice but is technically UB per C11. Making `back_index` `_Atomic int` would be the correct fix.
- **Destroy order**: `vc_render_pass_destroy` destroys framebuffers before the render pass, which is correct (VkFramebuffer references the VkRenderPass, so the framebuffer must be destroyed first by Vulkan spec... actually, Vulkan does not require a specific destruction order for render passes vs framebuffers; the render pass is used at framebuffer creation time but not retained. Either order is fine.)
- **`vc_framebuffer_destroy_single` memset**: After destroying all Vulkan objects, the struct is zeroed. This prevents double-free if called again, which is defensive but good.

---

## vc_shader.h -- 59 lines

### Issues Found

- None.

### Notes

- Minimal, clean header. `vc_shader_t` just holds two `VkShaderModule` handles.
- Inline accessors are trivial and correct.
- `vc_shader_init` takes a pointer to caller-owned `vc_shader_t` (embedded in `vc_context_t`), avoiding heap allocation. Good.

---

## vc_shader.c -- 95 lines

### Issues Found

- **[HIGH] Line 43: Potential undefined behavior from misaligned pointer cast.**
  `(const uint32_t *) code` casts `const unsigned char *` to `const uint32_t *`. The source array (`voodoo_uber_vert_spv` / `voodoo_uber_frag_spv`) is declared in the generated header as `static const unsigned char []` with no alignment attribute. On most platforms, static arrays of `unsigned char` are aligned to 1 byte. Casting to `uint32_t *` requires 4-byte alignment.

  In practice, most compilers (Clang, GCC, MSVC) align static arrays of any type to at least the natural alignment of the type or some minimum (often 4 or 8 bytes), but this is NOT guaranteed by the C standard. On ARM64 specifically, an unaligned `uint32_t` load from a misaligned address causes a hardware alignment fault (SIGBUS) unless the compiler generates byte-by-byte loads. Vulkan spec requires `pCode` to be a valid pointer to `uint32_t[]`.

  **Fix:** Add `_Alignas(4)` (or `__attribute__((aligned(4)))`) to the generated array in `SpvToHeader.cmake`:
  ```c
  // Change from:
  static const unsigned char ${ARRAY_NAME}[] = {
  // To:
  _Alignas(4) static const unsigned char ${ARRAY_NAME}[] = {
  ```

  Or better yet, generate the array as `static const uint32_t[]` instead of `unsigned char[]`, which automatically has correct alignment and avoids the cast entirely.

  **Risk assessment:** On macOS ARM64 (Apple Silicon), Clang typically aligns static arrays to 16 bytes for SIMD optimization, so this likely works in practice. But it is still technically undefined behavior and will break on platforms with stricter alignment enforcement or less generous default alignment.

- **[LOW] Line 35: Size validation is incomplete.**
  The check `code_size == 0 || (code_size % 4) != 0` validates size but does not validate the SPIR-V magic number (0x07230203). While the Vulkan driver's `vkCreateShaderModule` will validate the magic, a pre-check here would give a more informative error message (e.g., "Not valid SPIR-V" vs "vkCreateShaderModule failed: -1000012000").

  This is a quality-of-life improvement, not a correctness issue.

### Notes

- **Error handling**: `vc_shader_init` correctly cleans up the vertex module (via `vc_shader_close`) if the fragment module fails to create. No resource leaks.
- **`vc_shader_close` idempotency**: Sets handles to `VK_NULL_HANDLE` after destruction, so it is safe to call multiple times. The NULL checks on both `shader` and `ctx` prevent crashes. Good.
- **No hot-path concerns**: Shader modules are created once at init and destroyed once at shutdown. No performance sensitivity.

---

## Summary

| Severity | Count | Files |
|----------|-------|-------|
| CRITICAL | 0 | -- |
| HIGH     | 1 | vc_shader.c (misaligned pCode cast) |
| MODERATE | 1 | vc_render_pass.c (missing LATE_FRAGMENT_TESTS_BIT in subpass dependency) |
| LOW      | 2 | vc_render_pass.c (unchecked vkWaitForFences), vc_shader.c (no magic check) |

### Priority Fixes

1. **vc_shader.c / SpvToHeader.cmake -- alignment**: Change the CMake script to emit `_Alignas(4) static const unsigned char` or use `static const uint32_t[]`. This is a latent UB that will cause a hard crash on platforms with strict alignment.

2. **vc_render_pass.c -- subpass dependency stages**: Add `VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT` to the external subpass dependency. This ensures correct synchronization when the shader writes `gl_FragDepth`.

3. **vc_render_pass.c -- vkWaitForFences return**: Check and log the return value. Trivial fix.
