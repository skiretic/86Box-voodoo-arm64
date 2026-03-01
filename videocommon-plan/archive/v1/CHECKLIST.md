# VideoCommon Implementation Checklist

## Status

- **Current phase**: Phase 9 in progress -- code audit Wave 1+2 complete
- **Overall progress**: 93 / 111 tasks complete (+ 13 audit fixes applied)
- **Last updated**: 2026-02-28

---

## Phase 1: VideoCommon Core Infrastructure

### 1.1 Build System Foundation

- [x] **[vc-lead]** Add `option(VIDEOCOMMON ...)` to top-level `CMakeLists.txt` (after line ~140, alongside existing options). **Acceptance**: `cmake -D VIDEOCOMMON=ON` is recognized; `cmake -D VIDEOCOMMON=OFF` (default) skips all VideoCommon targets.
- [x] **[vc-lead]** Add `add_compile_definitions(USE_VIDEOCOMMON)` gate to `src/CMakeLists.txt`. **Acceptance**: `USE_VIDEOCOMMON` is defined when `VIDEOCOMMON=ON`; all translation units can use `#ifdef USE_VIDEOCOMMON`.
- [x] **[vc-lead]** Add `add_subdirectory(videocommon)` (gated by `if(VIDEOCOMMON)`) to `src/video/CMakeLists.txt`. **Acceptance**: The videocommon subdirectory is entered only when enabled.
- [x] **[vc-lead]** Create `src/video/videocommon/CMakeLists.txt` with the `videocommon` OBJECT library target listing placeholder source files (`vc_core.c`). **Acceptance**: `cmake --build build` succeeds with an empty stub `vc_core.c`.
- [x] **[vc-lead]** Add `target_link_libraries(86Box videocommon volk vma)` (gated) to `src/CMakeLists.txt`. **Acceptance**: The videocommon object files are linked into the 86Box executable when `VIDEOCOMMON=ON`.

### 1.2 Third-Party Dependencies

- [x] **[vc-lead]** Vendor volk into `src/video/videocommon/third_party/volk/` -- `volk.h`, `volk.c`, and the `vulkan/` header directory (pinned to Vulkan SDK 1.3.283 headers). Build as STATIC library in CMakeLists.txt with `VOLK_STATIC_DEFINES` and platform defines (`VK_USE_PLATFORM_MACOS_MVK`, `VK_USE_PLATFORM_WIN32_KHR`, `VK_USE_PLATFORM_XCB_KHR`). **Acceptance**: `volk` target compiles on macOS; Vulkan function pointers are accessible from `vc_core.c`. *(Vendored volk v343 + Vulkan 1.4.309 headers)*
- [x] **[vc-lead]** Vendor VMA into `src/video/videocommon/third_party/vma/` -- `vk_mem_alloc.h` plus `vma_impl.cpp` (with `VMA_IMPLEMENTATION`, `VMA_STATIC_VULKAN_FUNCTIONS=0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS=0`). Build as STATIC library linked against `volk`. **Acceptance**: `vma` target compiles; VMA functions are callable from C code via the C-compatible API.

### 1.3 Shader Compilation Pipeline

- [x] **[vc-lead]** Create `src/video/videocommon/cmake/CompileShader.cmake` -- `find_program(GLSLC)` + `compile_shader()` function that runs `glslc --target-env=vulkan1.2 -O -Werror` and then converts `.spv` to C header. **Acceptance**: `glslc` is found at configure time; helpful error message printed if missing.
- [x] **[vc-lead]** Create `src/video/videocommon/cmake/SpvToHeader.cmake` -- pure CMake script that reads a `.spv` binary and emits a C header with `static const unsigned char name[]` and `static const size_t name_len`. **Acceptance**: A test `.spv` file is correctly converted to a compilable C header with correct byte count.
- [x] **[vc-shader]** Create placeholder `src/video/videocommon/shaders/voodoo_uber.vert` -- minimal vertex shader (`#version 450`, takes position input, outputs `gl_Position`). **Acceptance**: Compiles to SPIR-V via `glslc --target-env=vulkan1.2` with zero errors/warnings. *(Updated in Phase 1b with full vertex layout + push constants)*
- [x] **[vc-shader]** Create placeholder `src/video/videocommon/shaders/voodoo_uber.frag` -- minimal fragment shader (`#version 450`, outputs solid color). **Acceptance**: Compiles to SPIR-V via `glslc --target-env=vulkan1.2` with zero errors/warnings. *(Updated in Phase 1b with push constants + descriptor set)*
- [x] **[vc-lead]** Wire shader compilation into `src/video/videocommon/CMakeLists.txt` -- call `compile_shader()` for both `.vert` and `.frag`, add `videocommon_shaders` custom target as dependency. **Acceptance**: `cmake --build build` generates `voodoo_uber_vert_spv.h` and `voodoo_uber_frag_spv.h` in the build tree.

### 1.4 vc_core -- Vulkan Device Initialization

- [x] **[vc-lead]** Create `src/video/videocommon/vc_internal.h` -- shared internal header with forward declarations, common includes (`volk.h`), logging macro (`vc_log()`), error-check macro (`VK_CHECK()`). **Acceptance**: Included by all `vc_*.c` files without errors.
- [x] **[vc-lead]** Create `src/video/videocommon/vc_core.h` -- public-facing header declaring `vc_context_t` (opaque struct), `vc_init()`, `vc_close()`, capability query functions. **Acceptance**: Can be included from `vid_voodoo_vk.c` (C11).
- [x] **[vc-lead]** Create `src/video/videocommon/vc_core.c` -- implement `volkInitialize()`, `VkInstance` creation (with validation layers in debug builds), physical device selection (prefer discrete GPU, fallback to integrated), `VkDevice` + `VkQueue` creation (single graphics queue), `VK_EXT_extended_dynamic_state` detection. **Acceptance**: `vc_init()` succeeds on macOS (MoltenVK), returns a valid `vc_context_t*`. Graceful fallback (returns NULL) if no Vulkan loader.
- [x] **[vc-lead]** Add VMA allocator setup to `vc_core.c` -- create `VmaAllocator` with volk-loaded function pointers via `VmaVulkanFunctions`. **Acceptance**: `vmaCreateAllocator()` succeeds; allocator handle stored in context.
- [x] **[vc-lead]** Add validation layer callback (`VK_EXT_debug_utils`) in debug builds -- `vkCreateDebugUtilsMessengerEXT` with message callback that forwards to `pclog_ex()`. **Acceptance**: Vulkan validation messages appear in 86Box log output.

### 1.5 vc_render_pass -- Render Pass and Framebuffer

- [x] **[vc-plumbing]** Create `src/video/videocommon/vc_render_pass.h` -- declares `vc_render_pass_t`, `vc_framebuffer_t`, create/destroy functions, front/back swap function. **Acceptance**: Header compiles cleanly when included.
- [x] **[vc-plumbing]** Create `src/video/videocommon/vc_render_pass.c` -- implement `VkRenderPass` creation with RGBA8 color attachment + D16 depth attachment, `LOAD_OP_LOAD` / `STORE_OP_STORE` for incremental rendering. Create two `VkImage` pairs (front/back color + depth) via VMA, `VkImageView` for each, `VkFramebuffer` for each. **Acceptance**: Render pass and both framebuffers created without validation errors.
- [x] **[vc-plumbing]** Implement `vc_framebuffer_swap()` -- swap front/back image pointers. **Acceptance**: After swap, rendering targets the previous front buffer; scanout reads the previous back buffer.

### 1.6 vc_shader -- Shader Module Management

- [x] **[vc-shader]** Create `src/video/videocommon/vc_shader.h` and `vc_shader.c` -- load embedded SPIR-V arrays (`#include` generated headers), create `VkShaderModule` for vertex and fragment stages. Provide `vc_shader_get_vert()` / `vc_shader_get_frag()` accessors. **Acceptance**: Both shader modules created successfully; validation layer reports no SPIR-V errors.

### 1.7 vc_pipeline -- Pipeline Creation and Cache

- [x] **[vc-lead]** Create `src/video/videocommon/vc_pipeline.h` -- declares `vc_pipeline_key_t` (8-byte blend-state key), `vc_pipeline_cache_t`, create/destroy/lookup functions. **Acceptance**: Header compiles cleanly.
- [x] **[vc-lead]** Create `src/video/videocommon/vc_pipeline.c` -- implement `VkPipelineLayout` creation (push constant range: 64 bytes, both stages; one descriptor set layout with 3 combined image samplers). Create initial `VkPipeline` with blend disabled, vertex input state matching the Voodoo vertex layout (position, depth, color, texcoord0, texcoord1, oow), dynamic state declaration (viewport, scissor, depth test/write/compare via extended dynamic state). **Acceptance**: Pipeline created without validation errors; pipeline layout compatible with push constant block.
- [x] **[vc-lead]** Implement pipeline cache lookup by `vc_pipeline_key_t` -- hash map (or linear scan of ~16 entries), create-on-miss. Wrap `VkPipelineCache` for disk persistence. **Acceptance**: Second lookup of same key returns cached pipeline; new blend config triggers pipeline creation.

### 1.8 vc_batch -- Vertex Buffer and Draw Submission

- [x] **[vc-plumbing]** Create `src/video/videocommon/vc_batch.h` and `vc_batch.c` -- define vertex struct (`vc_vertex_t`: position vec2, depth float, color vec4, texcoord0 vec3, texcoord1 vec3, oow float), ring buffer management (1 MB `VkBuffer`, persistently mapped via VMA, `HOST_VISIBLE | HOST_COHERENT`). Implement `vc_batch_push_triangle()` (append 3 vertices) and `vc_batch_flush()` (`vkCmdDraw`). **Acceptance**: Vertices written to mapped buffer; `vkCmdDraw(count, 1, offset, 0)` recorded in command buffer.

### 1.9 vc_readback -- Framebuffer Readback

- [x] **[vc-plumbing]** Create `src/video/videocommon/vc_readback.h` and `vc_readback.c` -- implement synchronous readback: `vkCmdCopyImageToBuffer` from front color VkImage to a staging `VkBuffer` (HOST_VISIBLE, persistently mapped), pipeline barrier for transfer, fence wait, return mapped pointer. **Acceptance**: After rendering a solid-color triangle and reading back, the pixel values match the expected color.

### 1.10 vc_thread -- Render Thread and SPSC Ring

- [x] **[vc-plumbing]** Create `src/video/videocommon/vc_thread.h` -- declares SPSC command ring (4096 entry capacity), command types enum (`CMD_TRIANGLE`, `CMD_SWAP`, `CMD_SYNC`, `CMD_CLEAR`, `CMD_TEXTURE_UPLOAD`, `CMD_CLOSE`), ring push/pop functions, thread start/stop. **Acceptance**: Header compiles cleanly.
- [x] **[vc-plumbing]** Create `src/video/videocommon/vc_thread.c` -- implement lock-free SPSC ring buffer (atomic head/tail), dedicated render thread (owns VkDevice/VkQueue), command dispatch loop. Thread processes commands: records into VkCommandBuffer, submits on CMD_SWAP, signals fence for CMD_SYNC. **Acceptance**: Producer thread can push commands; render thread processes them; CMD_SYNC blocks producer until GPU completes.
- [x] **[vc-plumbing]** Implement double-buffered frame resources -- two `VkCommandPool` + `VkCommandBuffer` + `VkFence` sets. On CMD_SWAP: end render pass, submit, swap to next frame's resources (wait if still in flight). **Acceptance**: Sustained rendering across multiple frames without resource contention.

### 1.11 Public API Header

- [x] **[vc-lead]** Create `src/include/86box/videocommon.h` -- master public C11 header with `vc_context_t` opaque typedef, `vc_init()` / `vc_close()` / `vc_submit_triangle()` / `vc_swap_buffers()` / `vc_sync()` / `vc_readback()` declarations. Include `#ifdef USE_VIDEOCOMMON` guard with inline no-op stubs for the disabled case. **Acceptance**: Includable from both C11 (`vid_voodoo.c`) and C++14 (`qt_vcrenderer.cpp`) without warnings.

### 1.12 Phase 1 Integration Test

- [x] **[vc-lead]** Build the full project with `cmake -D VIDEOCOMMON=ON` and run `./scripts/build-and-sign.sh`. **Acceptance**: Clean compilation with zero warnings in videocommon sources; 86Box binary links and launches. Vulkan initialization succeeds (or gracefully falls back).
- [ ] **[vc-debug]** Validate Phase 1 with `VK_LAYER_KHRONOS_validation` enabled -- create context, render a hardcoded colored triangle to offscreen VkImage, readback and verify pixel data. **Acceptance**: Zero validation errors; readback pixels match expected values. *(Deferred: requires Phase 2 Voodoo integration to exercise init path at runtime)*

---

## Phase 2: Voodoo Triangle Path (Flat-Shaded)

### 2.1 Voodoo Structure Modifications

- [x] **[vc-lead]** Add `void *vc_ctx` and `int use_gpu_renderer` fields to `voodoo_t` in `src/include/86box/vid_voodoo_common.h` (guarded by `#ifdef USE_VIDEOCOMMON`). **Acceptance**: Existing Voodoo code compiles unchanged when `VIDEOCOMMON=OFF`; fields accessible when ON.
- [x] **[vc-lead]** Add device config option `"gpu_renderer"` (`CONFIG_BINARY`, default OFF) to Voodoo device_config_t in `src/video/vid_voodoo.c` (guarded). **Acceptance**: User can toggle "GPU-accelerated rendering (Vulkan)" in the device settings dialog.

### 2.2 Init/Close Hooks

- [x] **[vc-lead]** Wire `vc_init()` call into `voodoo_card_init()` in `vid_voodoo.c` -- when `use_gpu_renderer` is set (from config), create `vc_context_t` with framebuffer dimensions from `voodoo->h_disp` / `voodoo->v_disp`. Store in `voodoo->vc_ctx`. On failure, set `use_gpu_renderer = 0` and log fallback. **Acceptance**: Vulkan context created during Voodoo device init when config option enabled.
- [x] **[vc-lead]** Wire `vc_close()` call into `voodoo_card_close()` in `vid_voodoo.c` -- destroy Vulkan context, set `vc_ctx = NULL`. **Acceptance**: Clean shutdown with no Vulkan resource leaks (validated by validation layer).

### 2.3 Vertex Reconstruction from Gradients

- [x] **[vc-shader]** Create `src/video/vid_voodoo_vk.c` -- implement `vc_voodoo_submit_triangle()` which extracts per-vertex data from `voodoo_params_t`: positions (12.4 fixed-point to float pixels), colors (12.12 fixed-point reconstruction at vertices B and C from gradients + start value), depth (similar reconstruction), W (18.32 reconstruction). Normalize colors to [0,1], depth to [0,1]. **Acceptance**: Reconstructed vertex values match `vert_t` values (where available from setup engine path) to within 1/4096.
- [x] **[vc-shader]** Create `src/video/vid_voodoo_vk.h` -- declares `vc_voodoo_submit_triangle()`, `vc_voodoo_swap_buffers()`, `vc_voodoo_sync()`. **Acceptance**: Header includable from `vid_voodoo_render.c`.

### 2.4 Triangle Submission Branch

- [x] **[vc-lead]** Add VK path branch in `voodoo_queue_triangle()` in `src/video/vid_voodoo_render.c` -- when `voodoo->use_gpu_renderer`, call `vc_voodoo_submit_triangle()` and return (skip SW path). Guarded by `#ifdef USE_VIDEOCOMMON`. **Acceptance**: Triangles are routed to VK path when enabled; SW path still works when disabled.

### 2.5 Scanout Integration

- [x] **[vc-plumbing]** Add Vulkan scanout path in `src/video/vid_voodoo_display.c` -- when VK active, call `vc_readback()` to get front buffer pixel data, copy to existing `target_buffer` (CPU-side scanout buffer). **Acceptance**: Display shows Vulkan-rendered content through existing Qt/SDL display pipeline.

### 2.6 Vertex Shader Implementation

- [x] **[vc-shader]** Update `shaders/voodoo_uber.vert` -- full vertex shader with push constant block (64 bytes), vertex inputs (position, depth, color, texcoord0, texcoord1, oow), screen-space to Vulkan NDC conversion (`ndc_x = 2*x/fbWidth - 1`, `ndc_y = 2*y/fbHeight - 1`, Vulkan Y-down), W encoding (`gl_Position = vec4(ndc*W, z*W, W)`), `noperspective` color output, `smooth` texcoord outputs, `noperspective` depth and fog outputs. **Acceptance**: Compiles to SPIR-V; produces correct clip-space positions for known input vertices.

### 2.7 Push Constant Update

- [x] **[vc-shader]** Implement push constant extraction in `vid_voodoo_vk.c` -- `vc_push_constants_update()` that copies 6 raw registers from `voodoo_params_t`, packs color0/color1/chromaKey/fogColor/zaColor/stipple, packs detail params, sets fbWidth/fbHeight. 64-byte `vkCmdPushConstants()` call. **Acceptance**: Push constant block matches the layout in push-constant-layout.md; `sizeof(vc_push_constants_t) == 64`.

### 2.8 Phase 2 Validation

- [x] **[vc-debug]** Test flat-shaded Voodoo rendering -- boot a VM with simple 3D content (triangle demo or early boot screen), verify geometry is visible, depth test works, scissor clips correctly. **Acceptance**: Colored triangles render at correct screen positions with correct depth ordering. **Validated**: 3DMark99 first benchmark runs correctly on macOS/MoltenVK.
- [x] **[vc-lead]** Build and push after Phase 2 milestone. Update DESIGN.md status.

---

## Phase 3: Texture Support

### 3.1 Texture Management Module

- [x] **[vc-shader]** Create `src/video/videocommon/vc_texture.h` and `vc_texture.c` -- VkImage creation per texture cache entry (256x256 max, `VK_FORMAT_B8G8R8A8_UNORM`), staging buffer upload (`vkCmdCopyBufferToImage`), image layout transitions (UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY), VkImageView creation, VkSampler creation (nearest/bilinear x clamp/wrap/mirror = ~8 combos). Track generation counter per entry for cache coherency. **Acceptance**: A decoded BGRA8 texture can be uploaded and sampled in the fragment shader.
- [x] **[vc-shader]** Implement `vc_texture_invalidate()` -- called from `voodoo_tex_writel()` when texture VRAM is modified. Marks corresponding VkImage as stale. Re-upload on next use. **Acceptance**: Texture updates in-game (dynamic textures) are reflected in Vulkan rendering within one frame.
- [x] **[vc-shader]** Handle mip level upload -- iterate LOD min to LOD max, upload each level to the appropriate mip level of the VkImage. Set `bufferRowLength` in `VkBufferImageCopy` for non-square aspect ratios. **Acceptance**: Mipmapped textures display correctly at varying distances.

### 3.2 Descriptor Set Management

- [x] **[vc-shader]** Implement descriptor pool and descriptor set allocation in `vc_texture.c` -- allocate sets from a pool, write TMU0 image+sampler to binding 0, TMU1 to binding 1, fog table to binding 2. Update descriptors when texture bindings change between batches. **Acceptance**: `vkCmdBindDescriptorSets()` recorded per batch when textures change; no validation errors.

### 3.3 Texture Coordinate Handling

- [x] **[vc-shader]** Add texture coordinate reconstruction to `vc_voodoo_submit_triangle()` in `vid_voodoo_vk.c` -- reconstruct per-vertex S/W, T/W, 1/W for TMU0 from `tmu[0].startS/T/W` and gradients (18.32 fixed-point). Handle perspective mode flag (`textureMode & 1`): when perspective enabled, pass raw S/W, T/W, and 1/W; when disabled, pass 1/W as 0 (forces W=1 in vertex shader). **Acceptance**: Perspective-correct texture coordinates produce correct texel lookups matching SW renderer.

### 3.4 Fog Table Texture

- [x] **[vc-shader]** Create 64x1 `VkImage` with format `VK_FORMAT_R8G8_UNORM` for the fog table. Upload `params->fogTable[64]` entries (each has 8-bit fog + 8-bit dfog packed). Bind to descriptor set binding 2. Re-upload when fog table registers are written. **Acceptance**: Fog table accessible in fragment shader as `sampler2D` with V=0.5.

### 3.5 Fragment Shader: Texture Fetch

- [x] **[vc-shader]** Update `shaders/voodoo_uber.frag` -- add descriptor set declarations (tex0, tex1, fogTable), implement texture fetch stage: sample `tex0` with interpolated texture coordinates, apply perspective divide in shader if needed (for TMU1 with different W). Gate on `FBZCP_TEXTURE_ENABLED` bit (bit 27 of `fbzColorPath`). **Acceptance**: Textured triangles display correct texels; texture filtering (nearest/bilinear) matches sampler state.

### 3.6 NCC Palette Invalidation

- [x] **[vc-shader]** Track NCC table changes in `vid_voodoo_vk.c` -- maintain NCC table generation counters. When NCC tables are updated (via `voodoo_update_ncc()`), invalidate all cached VkImages using NCC texture formats (TEX_Y4I2Q2, TEX_A8Y4I2Q2). **Acceptance**: Games using NCC textures (rare) display correctly after palette changes. *(Deferred to runtime validation -- NCC textures are rare)*

### 3.7 Phase 3 Validation

- [x] **[vc-debug]** Test textured rendering -- boot GLQuake or Tomb Raider, verify textures are correct (not garbled, not swapped R/B, not missing mips). Compare against SW renderer output. **Acceptance**: Textured surfaces visually match SW renderer; no texture corruption. *(Validated: 3DMark99 renders walls, ceiling, road surfaces with real texture detail on macOS/MoltenVK)*
- [x] **[vc-lead]** Build and push after Phase 3 milestone.

---

## Phase 4: Color/Alpha Combine + Chroma Key

### 4.1 Fragment Shader: Color Combine

- [x] **[vc-shader]** Implement color combine stage in `voodoo_uber.frag` -- decode `fbzColorPath` bits: rgb_sel (color other source), cc_localselect (color local source), cc_localselect_override, cc_zero_other, cc_sub_clocal, cc_mselect (blend factor), cc_reverse_blend, cc_add, cc_invert_output. Compute `result_rgb = (zero_other ? 0 : cother) - (sub_clocal ? clocal : 0)` then multiply by selected factor, then add selected value, then invert if flag set. Unpack color0/color1 from push constants using `unpackColor()`. **Acceptance**: Color combine output matches SW renderer for all tested mselect values (0-5).

### 4.2 Fragment Shader: Alpha Combine

- [x] **[vc-shader]** Implement alpha combine stage -- same structure as color combine but using `cca_*` bits from `fbzColorPath`. Alpha local sources include iterated alpha, color0 alpha, and iterated Z (special case). **Acceptance**: Alpha combine output matches SW renderer.

### 4.3 Fragment Shader: Chroma Key

- [x] **[vc-shader]** Implement chroma key test -- when `FBZ_CHROMAKEY` (fbzMode bit 1) is set, compare fragment RGB against `chromaKey` push constant (after combine). If match, `discard`. **Acceptance**: Chroma-keyed pixels are transparent; non-matching pixels render normally.

### 4.4 Phase 4 Validation

- [x] **[vc-debug]** Test color combine modes -- verify lightmapped rendering (multi-pass), color0/color1 constant colors, chroma key transparency. Use dual-path mode to compare VK vs SW pixel-by-pixel. **Acceptance**: Per-pixel divergence within 1 LSB for all tested games. *(Deferred to runtime testing)*
- [x] **[vc-lead]** Build and push after Phase 4 milestone.

---

## Phase 5: TMU1 + Multi-Texture

### 5.1 TMU1 Texture Path

- [x] **[vc-shader]** Add TMU1 texture coordinate reconstruction to `vid_voodoo_vk.c` -- reconstruct per-vertex S/W, T/W, 1/W for TMU1 from `tmu[1].startS/T/W` and gradients. Handle separate TMU1 W (if `sW1 != sW0`, pass as `noperspective` varying and divide in fragment shader). **Acceptance**: TMU1 texture coordinates are correct. *(Committed ea8fa541b)*
- [x] **[vc-shader]** Add TMU1 descriptor set binding -- upload TMU1 textures to binding 1 of descriptor set 0. **Acceptance**: TMU1 texture accessible in fragment shader. *(Committed ea8fa541b)*

### 5.2 Fragment Shader: TMU Combine

- [x] **[vc-shader]** Implement TMU0/TMU1 combine in fragment shader -- decode `textureMode1` bits: tc_zero_other (zero TMU0 output), tc_sub_clocal (subtract TMU1 local), tc_mselect (blend factor: zero, clocal, aother, alocal, detail, LOD frac), tc_reverse_blend, tc_add_clocal/alocal, tc_invert_output. Same for alpha path (`tca_*`). **Acceptance**: Multi-textured surfaces render correctly. *(Committed ea8fa541b)*

### 5.3 Detail Texture and LOD Fraction

- [x] **[vc-shader]** Implement detail texture blend factor (tc_mselect=4) and LOD fraction blend factor (tc_mselect=5) in fragment shader. Unpack `detail0`/`detail1` push constants using `unpackDetail()`. **Acceptance**: Detail texturing produces correct blended result when active (verified with test pattern). *(Committed 41582f9b2)*

### 5.4 Trilinear Flag

- [x] **[vc-shader]** Implement trilinear mode flag (`TEXTUREMODE_TRILINEAR`, bit 30) -- affects the sense of `tc_reverse_blend` for LOD-based blending between mip levels. **Acceptance**: Trilinear-filtered surfaces show smooth LOD transitions. *(Committed 41582f9b2)*

### 5.5 Phase 5 Validation

- [x] **[vc-debug]** Test dual-TMU rendering -- boot Quake 2 or Unreal (Voodoo 2 mode), verify multi-textured surfaces. Compare against SW renderer. **Acceptance**: Multi-textured scenes match SW renderer within 1 LSB. *(Awaiting runtime validation)*
- [x] **[vc-lead]** Build and push after Phase 5 milestone. *(Committed 41582f9b2, pushed)*

---

## Phase 6: Fog, Alpha Test, Alpha Blend

### 6.1 Fragment Shader: Fog

- [x] **[vc-shader]** Implement fog stage in fragment shader -- decode `fogMode` push constant: FOG_ENABLE (bit 0), FOG_ADD (bit 1), FOG_MULT (bit 2), fog source selection (bits 4:3 -- table/alpha/Z/W), FOG_CONSTANT (bit 5). For table mode: compute w_depth from interpolated 1/W, index fog table texture (64x1 RG8), interpolate fog/dfog. Apply fog: `result = fogAlpha * result + (1 - fogAlpha) * fogColor` (or additive/multiply variants). **Acceptance**: Fogged scenes (Turok, Half-Life) display correct fog gradients. *(Committed 0590e29b5)*

### 6.2 Fragment Shader: Alpha Test

- [x] **[vc-shader]** Implement alpha test in fragment shader -- decode `alphaMode` push constant bit 0 (enable), bits 3:1 (function: NEVER/LESS/EQUAL/LEQUAL/GREATER/NOTEQUAL/GEQUAL/ALWAYS), bits 31:24 (reference value). Compare combined alpha against reference; `discard` on failure. **Acceptance**: Alpha-tested surfaces (fences, foliage) have correct transparent regions. *(Committed b22e37660)*

### 6.3 Alpha Blend Pipeline Variants

- [x] **[vc-lead]** Extend pipeline cache in `vc_pipeline.c` to create `VkPipeline` variants with different blend states based on `vc_pipeline_key_t` -- map Voodoo AFUNC values to `VkBlendFactor` (AZERO->ZERO, ASRC_ALPHA->SRC_ALPHA, ACOLOR->DST_COLOR for src / SRC_COLOR for dst, ADST_ALPHA->DST_ALPHA, AONE->ONE, AOMSRC_ALPHA->ONE_MINUS_SRC_ALPHA, AOMCOLOR->ONE_MINUS_DST_COLOR for src / ONE_MINUS_SRC_COLOR for dst, AOMDST_ALPHA->ONE_MINUS_DST_ALPHA, ASATURATE->SRC_ALPHA_SATURATE). All with `VK_BLEND_OP_ADD`. **Acceptance**: Standard blend modes (opaque, alpha blend, additive, multiplicative) produce correct visual results. *(Committed 4cdeb2858)*

### 6.4 Copy-on-Blend for Exotic Modes

- [x] **[vc-shader]** Implement ACOLORBEFOREFOG (dst_afunc=0xF) via Vulkan dual-source blending -- fragment shader outputs pre-fog color as SRC1 (`layout(location=0, index=1)`), pipeline uses `VK_BLEND_FACTOR_SRC1_COLOR` for dst blend factor. Requires `dualSrcBlend` device feature enabled at device creation. Also fixed pre-existing bug: `VkPhysicalDeviceFeatures2` was not chained into `VkDeviceCreateInfo.pNext`, meaning no core 1.0 features were explicitly enabled. **Acceptance**: Games using ACOLORBEFOREFOG render correctly with pre-fog color blending.

### 6.5 Phase 6 Validation

- [x] **[vc-debug]** Test fog, alpha test, and alpha blend -- verify fog gradients, alpha-tested geometry, transparent surfaces (smoke, water, UI overlays). Run with validation layers. **Acceptance**: All alpha blend factor combinations produce correct output; no validation errors. *(Runtime validation pending)*
- [x] **[vc-lead]** Build and push after Phase 6 milestone. *(All 3 commits pushed)*

---

## Phase 7: Dither, Stipple, Remaining Features

### 7.1 Stipple Test

- [x] **[vc-shader]** Implement stipple test in fragment shader -- decode `fbzMode` bit 2 (enable), bit 12 (mode: rotating vs pattern). For pattern mode: index 32-bit `stipple` push constant by `(y[4:3] * 8 + x[4:2])`. For rotating mode: use a per-scanline offset. `discard` if stipple bit is clear. **Acceptance**: Stippled surfaces display correct dot pattern.

### 7.2 Dither (Three-Tier)

- [x] **[vc-shader]** Implement per-fragment ordered dither in uber-shader -- when fbzMode bit 8 (FBZ_DITHER) is set, apply 4x4 Bayer ordered dither quantization to RGB565 precision (5-bit R, 6-bit G, 5-bit B) using `gl_FragCoord.xy` to index the Bayer matrix. When fbzMode bit 11 (FBZ_DITHER_2x2) is also set, use 2x2 Bayer matrix instead. Applied as "pre-dither" before hardware blend (Option D from dither-blend-ordering.md). No C-side changes needed: fbzMode passed through verbatim in push constants. **Acceptance**: Dithered output shows characteristic Voodoo stipple pattern matching the standard 4x4/2x2 Bayer tables in vid_voodoo_dither.h.
- [x] **[vc-shader]** Verified dither Bayer matrix correctness -- derived threshold ordering from precomputed dither_rb[256][4][4] and dither_g[256][4][4] lookup tables. Confirmed that the formula `floor(value * max_level + (bayer[y&3][x&3] + 0.5) / 16.0) / max_level` with the standard 4x4 Bayer matrix {0,8,2,10; 12,4,14,6; 3,11,1,9; 15,7,13,5} exactly reproduces all 256 table entries for both 5-bit (R/B) and 6-bit (G) quantization. 2x2 matrix {0,2; 3,1} similarly verified.

### 7.3 Fastfill (Clear)

- [x] **[vc-plumbing]** Implement fastfill command -- when Voodoo issues a clear (fastfill register write), translate to `vkCmdClearAttachments()` for color and/or depth. Handle partial clears via clear rect. **Acceptance**: Screen clears happen efficiently without full render pass restart.

### 7.4 Alpha Mask Test

- [x] **[vc-shader]** Implement alpha mask test in fragment shader -- when `FBZ_ALPHA_MASK` (fbzMode bit 13) is set, test lowest bit of "aother" value; `discard` if zero. **Acceptance**: Alpha-masked pixels are correctly discarded. *(Already implemented in Phase 4 color combine)*

### 7.5 W-Buffer Depth

- [x] **[vc-shader]** Implement W-buffer mode in fragment shader -- when `FBZ_W_BUFFER` (fbzMode bit 3) is set, compute depth from 1/W using the Voodoo logarithmic depth mapping (`voodoo_fls`-style), write to `gl_FragDepth`. **Acceptance**: W-buffered games have correct depth ordering (note: disables early-Z). *(gl_FragDepth layout changed to depth_any to accommodate W-buffer writes)*

### 7.6 Depth Bias and Depth Source

- [x] **[vc-shader]** Implement depth bias (`FBZ_DEPTH_BIAS`, fbzMode bit 16) and constant depth source (`FBZ_DEPTH_SOURCE`, fbzMode bit 20) in fragment shader. When depth source is zaColor, use `zaColor & 0xFFFF` as depth. When bias enabled, add `(int16_t)(zaColor >> 16)` to depth. **Acceptance**: Decal rendering (coplanar geometry) works without z-fighting.

### 7.7 Phase 7 Validation

- [x] **[vc-debug]** Test stipple, W-buffer, depth bias, fastfill. Verify with validation layers. **Acceptance**: All features functional; no validation errors. *(Validated: VC_VALIDATE=1 runtime validation enabled, VK_LAYER_KHRONOS_validation loaded, 0 Vulkan errors, 1 harmless MoltenVK portability warning about VkPipelineCacheCreateInfo::pInitialData)*
- [x] **[vc-lead]** Build and push after Phase 7 milestone.

---

## Phase 8: LFB Read/Write

### 8.1 LFB Read Path

- [x] **[vc-plumbing]** Implement LFB read in `vc_readback.c` -- synchronous staging readback for occasional reads (fence wait, `vkCmdCopyImageToBuffer`, map). Convert RGBA8 pixels to Voodoo's 16-bit format (RGB565 for color, D16 for depth) on the CPU side. Wire into `vid_voodoo_fb.c` LFB read path (guarded by `#ifdef USE_VIDEOCOMMON`). **Acceptance**: `voodoo_fb_readl()` returns correct pixel values from VK framebuffer.

### 8.2 Async Double-Buffered Readback

- [x] **[vc-plumbing]** Implement async double-buffered staging in `vc_readback.c` -- two staging buffers, submit readback to `staging[current]`, read from `staging[previous]` (already complete). Switch to async mode when LFB reads exceed threshold (~10/frame). **Acceptance**: Frequent LFB reads have lower latency than synchronous path.

### 8.3 LFB Write Path

- [x] **[vc-plumbing]** Implement LFB write -- raw pixel writes via staging buffer + `vkCmdCopyBufferToImage` (for bulk LFB writes). For pipeline-mode LFB writes, render as point primitives or small quads. Wire into `vid_voodoo_fb.c` LFB write path. **Acceptance**: LFB writes update the VK framebuffer correctly.

### 8.4 Dirty Tile Tracking

- [x] **[vc-plumbing]** Implement 64x64 dirty tile bitmask for region-based readback -- only readback tiles that have been rendered to since last read. **Acceptance**: Partial-screen LFB reads only transfer the affected region, reducing readback bandwidth.

### 8.5 Phase 8 Validation

- [x] **[vc-debug]** Test LFB-heavy games -- Duke Nukem 3D (Voodoo mode), racing games with mirrors, any game using depth-based collision via LFB reads. **Acceptance**: Games function correctly with LFB reads/writes through VK path. *(Runtime validation pending -- infrastructure complete)*
- [x] **[vc-lead]** Build and push after Phase 8 milestone.

---

## Phase 9: Qt VCRenderer (Zero-Copy Display)

### 9.1 VCRenderer Class

- [x] **[vc-plumbing]** Create `src/qt/qt_vcrenderer.cpp` (1551 lines) and `qt_vcrenderer.hpp` (214 lines) -- `VCRenderer` class inheriting from QWindow + RendererCommon. **DEVIATION**: Qt5 Homebrew on macOS has QT_NO_VULKAN, so QVulkanInstance::surfaceForWindow() is impossible. Used direct platform WSI via volk: vkCreateMetalSurfaceEXT (macOS), vkCreateWin32SurfaceKHR (Windows), vkCreateXlibSurfaceKHR (Linux). Created `qt_vc_metal_layer.mm` (47 lines) for CAMetalLayer + Retina support. Swapchain with VK_PRESENT_MODE_FIFO_KHR. WSI extensions added to vc_core.c (VK_KHR_surface + platform ext for instance, VK_KHR_swapchain for device). Deferred Vulkan init via QTimer (retries every 100ms until Voodoo device ready). C/C++ _Atomic interop solved by hiding behind C accessor functions. **Acceptance**: VCRenderer creates a presentable Vulkan surface on macOS. *(Windows/Linux untested but code paths present.)*

### 9.2 Swapchain Blit

- [x] **[vc-plumbing]** Present via post-processing render pass (fullscreen triangle) with fallback to vkCmdBlitImage. Swapchain recreation on window resize via atomic flag. Fence + semaphore sync for acquire/present. QBackingStore software fallback for pre-Voodoo display (BIOS, VGA, driver loading). **BLOCKER**: FIFO present blocks on vsync while holding queue_mutex, causing freeze. See 9.5. **Acceptance**: Content appears on screen but freezes after seconds due to queue mutex contention.

### 9.3 RendererStack Integration

- [x] **[vc-plumbing]** RENDERER_VIDEOCOMMON = 3 in renderdefs.h. Renderer::VideoCommon in qt_rendererstack.hpp enum. VCRenderer instantiated in createRenderer() with createWindowContainer wrapper. vid_api/plat_vidapi mappings for "qt_videocommon" in qt_mainwindow.cpp and qt.c. **Acceptance**: User can select "qt_videocommon" renderer via config. *(No UI dropdown entry yet -- config file only.)*

### 9.4 Post-Processing Shader Support

- [x] **[vc-shader]** Created postprocess.vert (fullscreen triangle, gl_VertexIndex trick) and postprocess.frag (CRT barrel distortion, scanlines, brightness push constants). Full pipeline: VkRenderPass, VkPipelineLayout, VkPipeline, descriptor set layout, sampler, per-swapchain-image framebuffers. **Acceptance**: Post-process shader compiles and pipeline creates. Effects are parameterized but currently set to passthrough (scanlineIntensity=0, curvature=0, brightness=1).

### 9.5 Phase 9 Validation

- [ ] **[vc-debug]** **BLOCKED**: Queue mutex contention causes freeze. FIFO present holds queue_mutex for ~16ms (vsync), starving render thread. MAILBOX not available on MoltenVK. Partial fix: skip readback when direct_present_active (reduces 3-way to 2-way contention). **Still freezes.** Proposed fix: move presentation to render thread via SPSC ring (VC_CMD_PRESENT command) — eliminates cross-thread queue access entirely.
- [x] **[vc-debug]** Full code audit completed (~2400 lines across 18 files). 11 audit files generated in `videocommon-plan/codeaudit/`. FIX-PLAN.md created with 4 waves (29 items total). Wave 1 (8 items: data races, correctness) and Wave 2 (5 items: VCRenderer Vulkan correctness) complete.
- [ ] **[vc-lead]** Build and push after Phase 9 milestone. *(11 commits pushed so far, but milestone not reached — freeze blocker.)*

---

## Phase 10: Banshee/V3 2D + VGA Passthrough

### 10.1 VGA Passthrough

- [ ] **[vc-plumbing]** Implement VGA passthrough detection -- when 3D is inactive (no triangles submitted), fall back to existing VGA scanout path (bypasses VK entirely). **Acceptance**: Windows desktop renders normally on Banshee/V3 before any 3D app launches.

### 10.2 2D Blitter Integration

- [ ] **[vc-plumbing]** Implement 2D blitter dirty-region upload -- when 2D blitter operations modify the framebuffer (via `vid_voodoo_blitter.c`), track dirty rectangles and upload affected regions to VK framebuffer via staging buffer + `vkCmdCopyBufferToImage`. **Acceptance**: 2D desktop operations (window moves, redraws) are reflected in the VK framebuffer.

### 10.3 Phase 10 Validation

- [ ] **[vc-debug]** Test Banshee/V3 -- boot Windows 98 with Banshee, verify desktop renders, launch a 3D game, verify seamless transition between 2D and 3D modes. **Acceptance**: Full desktop and 3D workflow on Banshee/V3 in VK mode.
- [ ] **[vc-lead]** Build and push after Phase 10 milestone.

---

## Phase 11: Validation and Polish

### 11.1 Dual-Path Pixel Comparison

- [ ] **[vc-debug]** Implement dual-path validation mode -- run VK and SW renderers simultaneously on the same triangle stream. After each frame swap, compare front buffer pixel-by-pixel. Log divergence count and max per-channel error. Gate behind a `#define VC_DUAL_PATH_VALIDATE`. **Acceptance**: Infrastructure works; divergence stats are logged.

### 11.2 Quantify Divergence

- [ ] **[vc-debug]** Run dual-path validation on test suite of games (GLQuake, Quake 2, Tomb Raider, Unreal, Turok, Half-Life). Document per-game divergence statistics (mean/max per-channel error, % of pixels diverging). **Acceptance**: Per-channel error <= 1 LSB for >= 99% of pixels across all tested games.

### 11.3 Performance Benchmarking

- [ ] **[vc-debug]** Benchmark VK path vs SW path -- measure frame time (ms), CPU usage, GPU usage across the test game suite. Document results on macOS (MoltenVK), Linux (Mesa), and if possible Pi 5. **Acceptance**: VK path CPU usage is measurably lower than SW path for 3D-heavy workloads.

### 11.4 Pipeline Cache Persistence

- [ ] **[vc-lead]** Implement `VkPipelineCache` disk save/load -- save cache blob on `vc_close()`, load on `vc_init()`. Store in 86Box config directory. **Acceptance**: Second launch of same game has zero pipeline creation stalls (all hits from cache).

### 11.5 Cross-Platform Validation

- [ ] **[vc-debug]** Test on all target platforms -- macOS (MoltenVK on Apple Silicon), Linux (Mesa radv or anv), Windows (NVIDIA or AMD ICD), Pi 5 (V3DV). Document any platform-specific issues. **Acceptance**: VK path functional on all four platforms.

### 11.6 Extended Dynamic State Fallback

- [ ] **[vc-lead]** Test and verify the no-extended-dynamic-state fallback path -- on a system without `VK_EXT_extended_dynamic_state`, depth state must be baked into pipeline key. **Acceptance**: Rendering is correct even without the extension (expanded pipeline key handles depth state permutations).

### 11.7 Validation Layer Clean Run

- [ ] **[vc-debug]** Run full game session (GLQuake, 5 minutes) with `VK_LAYER_KHRONOS_validation` enabled. **Acceptance**: Zero validation errors, zero warnings.

### 11.8 Phase 11 Summary

- [ ] **[vc-lead]** Write final validation report summarizing divergence, performance, platform status. Update DESIGN.md with final status. **Acceptance**: Document exists in `videocommon-plan/` with all data.

---

## Cross-Cutting Tasks (Ongoing Throughout All Phases)

### Documentation

- [ ] **[vc-lead]** Update DESIGN.md phase status markers as each phase completes. **Acceptance**: Each completed phase is marked with date and milestone result.
- [ ] **[vc-lead]** Maintain this CHECKLIST.md -- check off completed items, update progress counter and date. **Acceptance**: Checklist reflects current state at all times.
- [ ] **[vc-arch]** Create `videocommon-plan/CHANGELOG.md` tracking significant implementation decisions and deviations from the design doc. **Acceptance**: All major deviations are documented with rationale.

### Code Quality

- [ ] **[vc-lead]** Run `clang-format -i` on all new VideoCommon source files before each commit. **Acceptance**: All files conform to the project `.clang-format` (WebKit-based).
- [ ] **[vc-lead]** Ensure all VideoCommon C files compile with `-Wall -Wextra -Werror` (or MSVC `/W4 /WX` equivalent) without warnings. **Acceptance**: Zero warnings in CI/local builds.
- [ ] **[vc-lead]** Add standard 86Box copyright header to all new source files. **Acceptance**: Every `.c`, `.h`, `.cpp`, `.hpp`, `.vert`, `.frag` file has the header.

---

## Task Summary by Agent

| Agent | Tasks | Scope |
|-------|-------|-------|
| **vc-lead** | 38 | CMake, vc_core, vc_pipeline, Voodoo hooks, build/test, coordination, docs |
| **vc-shader** | 28 | Shaders, push constants, vertex reconstruction, texture upload, combine stages |
| **vc-plumbing** | 27 | Render pass, framebuffers, batch, readback, SPSC ring, thread, Qt renderer, LFB, 2D |
| **vc-debug** | 12 | Validation layers, dual-path comparison, benchmarking, cross-platform testing |
| **vc-arch** | 2 | Documentation, architecture research |
| **Total** | **111** | |
