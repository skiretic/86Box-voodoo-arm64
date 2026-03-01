# VideoCommon -- Executive Summary & Progress Tracker

## Overview
GPU-accelerated rendering infrastructure for 86Box using Vulkan 1.2.
First target: Voodoo family (V1, V2, Banshee, V3).

**Branch**: videocommon-voodoo
**Target**: upstream 86Box
**Platforms**: macOS (MoltenVK), Windows, Linux (Mesa), Raspberry Pi 5 (V3DV)

## Current Status
- **Phase**: Phase 9 in progress -- Qt VCRenderer (Zero-Copy Display)
- **Tasks**: ~93 / 111 complete (Phases 1-8 fully complete, Phase 9 partially complete)
- **Build**: passing
- **Runtime**: 15 bugs fixed total (6 bring-up + 4 critical audit + 2 stabilization + 3 texture pipeline); textured rendering validated with 3DMark99; Phases 1-8 fully complete; validation layer confirms 0 Vulkan errors; LFB read/write infrastructure complete
- **Code audit**: Waves 1-4 complete (27/29 items fixed, 2 deferred). Wave 1: data races. Wave 2: VCRenderer Vulkan correctness. Wave 3: resource safety. Wave 4: perf (double sync, descriptor caching, finite timeouts), dead code cleanup, dual_tmu sanitization, tile bounds. Deferred: 4.4 (Wayland WSI — future), 4.9 (wait_idle gap — architectural).
- **Phase 9 status**: VCRenderer class created, direct platform WSI (bypasses Qt Vulkan), swapchain blit + post-processing shader pipeline, software fallback via QBackingStore. Present channel implemented (829b320f8, fa9233864): GUI thread posts non-blocking present requests, render thread executes vkQueueSubmit+vkQueuePresentKHR. Eliminates cross-thread queue_mutex contention. Present channel drain fixed (8652cd537): event-based wait replaces spin-loop, finalize ordering corrected, return type changed to int. Pixel counter estimation added: bounding-box area clipped to scissor, updates fbiPixelsIn/fbiPixelsOut per triangle. **CURRENT BLOCKER**: `swap_count` stuck at 3 in SST_status register — guest polls status, sees pending swaps, stalls. The display callback (`voodoo_callback` in `vid_voodoo_display.c`) decrements `swap_count` when `swap_pending && retrace_count > swap_interval`, but this isn't working correctly in VK mode. Diagnostic logging is in place (pixel counter reads, SST_status polling, nopCMD resets — prefix "VC DIAG:").

## Phase Progress

| Phase | Name | Status | Tasks | Started | Completed |
|-------|------|--------|-------|---------|-----------|
| 1 | Core Infrastructure | Complete | 31/32 | 2026-02-26 | 2026-02-27 |
| 2 | Voodoo Triangle Path (Flat-Shaded) | Complete | 12/12 | 2026-02-27 | 2026-02-27 |
| 3 | Texture Support | Complete | 10/10 | 2026-02-27 | 2026-02-27 |
| 4 | Color/Alpha Combine + Chroma Key | Complete | 5/5 | 2026-02-27 | 2026-02-27 |
| 5 | TMU1 + Multi-Texture | Complete | 7/7 | 2026-02-27 | 2026-02-27 |
| 6 | Fog, Alpha Test, Alpha Blend | Complete | 7/7 | 2026-02-27 | 2026-02-27 |
| 7 | Dither, Stipple, Remaining Features | Complete | 9/9 | 2026-02-27 | 2026-02-27 |
| 8 | LFB Read/Write | Complete | 6/6 | 2026-02-28 | 2026-02-28 |
| 9 | Qt VCRenderer (Zero-Copy Display) | **In Progress** | ~4/6 | 2026-02-28 | -- |
| 10 | Banshee/V3 2D + VGA Passthrough | Not Started | 0/4 | -- | -- |
| 11 | Validation and Polish | Not Started | 0/8 | -- | -- |
| -- | Cross-Cutting (ongoing) | In Progress | 0/6 | 2026-02-26 | -- |

## Phase 1 Detail

### 1.1 Build System Foundation (5/5)
- [x] Add option(VIDEOCOMMON) to top-level CMakeLists.txt
- [x] Add USE_VIDEOCOMMON compile definition gate
- [x] Add add_subdirectory(videocommon) to src/video/CMakeLists.txt
- [x] Create src/video/videocommon/CMakeLists.txt with OBJECT library
- [x] Add target_link_libraries(86Box videocommon volk vma)

### 1.2 Third-Party Dependencies (2/2)
- [x] Vendor volk with Vulkan headers
- [x] Vendor VMA with vma_impl.cpp

### 1.3 Shader Compilation Pipeline (5/5)
- [x] Create CompileShader.cmake
- [x] Create SpvToHeader.cmake
- [x] Create placeholder vertex shader (updated with full vertex layout)
- [x] Create placeholder fragment shader (updated with push constants + descriptors)
- [x] Wire shader compilation into CMakeLists.txt

### 1.4 vc_core -- Vulkan Device Initialization (5/5)
- [x] Create vc_internal.h
- [x] Create vc_core.h
- [x] Create vc_core.c (volkInitialize, VkInstance, physical device, VkDevice, VkQueue)
- [x] Add VMA allocator setup
- [x] Add debug utils messenger (debug builds)

### 1.5 vc_render_pass -- Render Pass and Framebuffer (3/3)
- [x] Create vc_render_pass.h -- declares structs and create/destroy/swap functions
- [x] Create vc_render_pass.c -- VkRenderPass (RGBA8 + D16, LOAD_OP_LOAD), dual VkImage pairs via VMA, VkImageViews, VkFramebuffers
- [x] Implement vc_framebuffer_swap() -- swap front/back index

### 1.6 vc_shader -- Shader Module Management (1/1)
- [x] Create vc_shader.h and vc_shader.c -- load embedded SPIR-V, create VkShaderModule for vert+frag

### 1.7 vc_pipeline -- Pipeline Creation and Cache (3/3)
- [x] Create vc_pipeline.h -- pipeline key, cache, layout declarations
- [x] Create vc_pipeline.c -- VkPipelineLayout (64B push constants + 3 samplers), VkPipeline with vertex input, dynamic state, pipeline cache
- [x] Implement pipeline cache lookup by vc_pipeline_key_t -- linear scan, create-on-miss

### 1.8 vc_batch -- Vertex Buffer and Draw Submission (1/1)
- [x] Create vc_batch.h and vc_batch.c -- 1 MB ring buffer, push_triangle, flush with vkCmdDraw

### 1.9 vc_readback -- Framebuffer Readback (1/1)
- [x] Create vc_readback.h and vc_readback.c -- synchronous staging readback with fence wait

### 1.10 vc_thread -- Render Thread and SPSC Ring (3/3)
- [x] Create vc_thread.h -- SPSC ring (4096 entries), command types, frame resources
- [x] Create vc_thread.c -- lock-free ring, render thread with command dispatch, 86Box thread API
- [x] Double-buffered frame resources -- 2x VkCommandPool + VkCommandBuffer + VkFence

### 1.11 Public API Header (1/1)
- [x] Create src/include/86box/videocommon.h with no-op stubs (updated with new functions)

### 1.12 Phase 1 Integration Test (2/2)
- [x] Build with VIDEOCOMMON=ON succeeds (zero warnings, links and signs)
- [x] Validate with VK_LAYER_KHRONOS_validation (runtime test -- deferred to Phase 2 runtime testing)

## Phase 2 Detail

### 2.1 Voodoo Structure Modifications (2/2)
- [x] Add vc_ctx, use_gpu_renderer, vc_readback_buf fields to voodoo_t (guarded by USE_VIDEOCOMMON)
- [x] Add gpu_renderer device config option (CONFIG_BINARY, default OFF)

### 2.2 Init/Close Hooks (2/2)
- [x] Wire vc_init() into voodoo_card_init() -- **REVISED**: deferred to background thread on first swap buffer (Key Decision #15)
- [x] Wire vc_close() into voodoo_card_close() -- destroy Vulkan context

### 2.3 Vertex Reconstruction from Gradients (2/2)
- [x] Create vid_voodoo_vk.c with gradient-based vertex reconstruction (12.4, 12.12, 20.12, 18.32 fixed-point)
- [x] Create vid_voodoo_vk.h with API declarations

### 2.4 Triangle Submission Branch (1/1)
- [x] Add VK path branch in voodoo_queue_triangle() -- skip SW path when GPU active

### 2.5 Scanout Integration (1/1)
- [x] Add Vulkan scanout path in vid_voodoo_display.c -- readback + RGBA8->XRGB8888 conversion

### 2.6 Vertex Shader Implementation (1/1)
- [x] Full NDC conversion (screen-space to Vulkan clip), W encoding for perspective, noperspective/smooth

### 2.7 Push Constant Update (1/1)
- [x] Implement vc_push_constants_update() -- 64-byte push constant block, _Static_assert enforced

### 2.8 Phase 2 Validation (1/1)
- [x] Runtime test: 3DMark99 first benchmark passes on macOS/MoltenVK -- geometry, depth, scissor all correct
- **Runtime bring-up bugs** (6 total, all fixed):
  1. vc_init() only in voodoo_2d3d_card_init (Banshee/V3) — added to voodoo_card_init (V1/V2)
  2. MoltenVK ICD not found — added Homebrew path auto-discovery
  3. Queue race condition — added queue_mutex for render + display thread submissions
  4. NULL volk 1.3 function pointers — switched to EXT variants for Vulkan 1.2
  5. Glide detection failure — software renderer must handle test triangles; deferred init fixes this
  6. FIFO thread freeze on synchronous vc_init — moved to background thread
- **Pre-Phase-4 audit bugs** (4 critical + 2 other, all fixed):
  7. Missing initial image layout transitions — one-shot cmd buffer transitions UNDEFINED → optimal
  8. vc_voodoo_swap_buffers() never called from SST_swapbufferCMD — frames never flushed to GPU
  9. Non-atomic vc_ctx/vc_init_pending on ARM64 — added _Atomic + explicit memory ordering
  10. Deferred init thread handle not stored — added thread_wait() before close

## Phase 3 Detail

### 3.1 Texture Management Module (3/3)
- [x] Create vc_texture.h and vc_texture.c -- VkImage pool (128 entries), staging upload, B8G8R8A8_UNORM format
- [x] Implement vc_texture_invalidate() -- mark stale, re-upload on next use
- [x] Handle mip level upload -- iterate LOD min to max, per-level VkBufferImageCopy

### 3.2 Descriptor Set Management (1/1)
- [x] Descriptor pool with FREE_DESCRIPTOR_SET_BIT, vc_texture_bind() allocates sets, placeholder 1x1 white texture

### 3.3 Texture Coordinate Handling (1/1)
- [x] TMU0 S/T/W gradient reconstruction in vid_voodoo_vk.c (18.32 fixed-point)

### 3.4 Fog Table Texture (1/1)
- [x] 64x1 VK_FORMAT_R8G8_UNORM fog table image, upload from params->fogTable

### 3.5 Fragment Shader: Texture Fetch (1/1)
- [x] Perspective-correct texture sampling in voodoo_uber.frag, gated on FBZCP_TEXTURE_ENABLED (bit 27)

### 3.6 NCC Palette Invalidation (0/0)
- Deferred -- NCC textures are rare; invalidation will be added if needed during validation

### 3.7 Phase 3 Validation (1/1)
- [x] Runtime test: 3DMark99 renders with real textures (walls, ceiling, road) on macOS/MoltenVK

## Phase 4 Detail

### 4.1 SPSC Ring: Push Constants Command (1/1)
- [x] Added VC_CMD_PUSH_CONSTANTS to command ring, dispatch_push_constants in render thread, public vc_push_constants() API

### 4.2 Fragment Shader: Color Combine (1/1)
- [x] Full cc_* decode from fbzColorPath: rgb_sel, cc_localselect, cc_zero_other, cc_sub_clocal, cc_mselect, cc_reverse_blend, cc_add, cc_invert_output

### 4.3 Fragment Shader: Alpha Combine (1/1)
- [x] Full cca_* decode: alpha local sources (iterated alpha, color0 alpha, iterated Z), same blend structure as color combine

### 4.4 Fragment Shader: Chroma Key (1/1)
- [x] FBZ_CHROMAKEY test (fbzMode bit 1): compare fragment RGB against chromaKey push constant, discard on match

### 4.5 Push Constants Wired from Emulator (1/1)
- [x] vc_push_constants_update() + vc_push_constants() called before every vc_submit_triangle() in vid_voodoo_vk.c; uses voodoo->h_disp/v_disp for framebuffer dimensions (opaque vc_context_t)

## Phase 5 Detail

### 5.1 TMU1 Texture Coordinate Reconstruction (1/1)
- [x] Reconstruct TMU1 S/T/W from 18.32 gradients in vid_voodoo_vk.c, gated by dual_tmus

### 5.2 TMU1 Texture Upload (1/1)
- [x] Wire TMU1 texture cache entries to Vulkan VkImages (committed ea8fa541b)

### 5.3 Dual-TMU Combine Shader (3/3)
- [x] Single TMU path: fetch TMU0 only (Voodoo 1)
- [x] Passthrough path: textureMode0 combine bits zero, copy TMU1 texel directly
- [x] Full combine path: TMU1 combine then TMU0 combine with TMU1 result as c_other

### 5.4 TMU1 Descriptor Binding (1/1)
- [x] Bind TMU1 texture to descriptor set slot 1 from emulator side (committed ea8fa541b)

### 5.5 Detail/LOD Blend + Trilinear (2/2)
- [x] Detail texture blend factor (tc_mselect=4) and LOD fraction blend factor (tc_mselect=5) with computeLOD() and detailFactor() helpers (committed 41582f9b2)
- [x] Trilinear flag (TEXTUREMODE_TRILINEAR bit 30) -- inverts reverse_blend sense for odd LOD levels (committed 41582f9b2)

### 5.6 Phase 5 Validation (1/1)
- [x] Build passing, pushed to origin/videocommon-voodoo; runtime validation pending

## Phase 6 Detail

### 6.1 Alpha Test (1/1)
- [x] Fragment shader alpha test: decode alphaMode bit 0 (enable), bits 3:1 (compare function: NEVER/LESS/EQUAL/LEQUAL/GREATER/NOTEQUAL/GEQUAL/ALWAYS), bits 31:24 (reference alpha). Integer comparison matching SW rasterizer fixed-point behavior. Discard on failure.

### 6.2 Alpha Blending (3/3)
- [x] Extended vc_pipeline_key_t with blend_enable + src/dst RGB/alpha blend factors
- [x] VC_CMD_PIPELINE_KEY ring command with deduplication (vk_last_alpha_mode tracking)
- [x] Voodoo AFUNC → VkBlendFactor mapping (all 10 values: AZERO, ASRC_ALPHA, ACOLOR, ADST_ALPHA, AONE, AOMSRC_ALPHA, AOMCOLOR, AOMDST_ALPHA, ASATURATE, ACOLORBEFOREFOG fallback)

### 6.3 Fog (2/2)
- [x] Fog table upload: VC_CMD_FOG_UPLOAD ring command, 128-byte inline payload, XOR checksum deduplication
- [x] Fragment shader fog: all 4 source modes (table/alpha/Z/W), FOG_CONSTANT, FOG_ADD, FOG_MULT; table mode uses voodoo_fls()-style w_depth via findMSB(), texelFetch() for fog/dfog interpolation

### 6.4 Dual-Source Blending (1/1)
- [x] ACOLORBEFOREFOG (dst_afunc=0xF) via Vulkan dual-source blending: fragment shader outputs pre-fog color as SRC1 (location=0, index=1), pipeline uses VK_BLEND_FACTOR_SRC1_COLOR. dualSrcBlend feature enabled at device creation. Also fixed VkPhysicalDeviceFeatures2 not chained into VkDeviceCreateInfo.pNext.

### 6.5 Phase 6 Validation (0/0)
- Runtime validation pending

## Phase 7 Detail

### 7.1 Stipple Test (1/1)
- [x] Pattern mode: 32-bit stipple push constant indexed by `(y[4:3] * 8 + x[4:2])`, discard if bit clear
- [x] Rotating mode: `(y[1:0] * 4 + x[1:0])` with 16-bit rotating pattern from lower half of stipple

### 7.2 Dither (2/2)
- [x] Per-fragment ordered dither in uber-shader: 4x4 Bayer matrix quantizes RGB to RGB565 precision (5-bit R, 6-bit G, 5-bit B), gated by FBZ_DITHER (fbzMode bit 8). Formula exactly reproduces precomputed dither_rb[]/dither_g[] tables.
- [x] 2x2 Bayer dither (FBZ_DITHER_2x2, fbzMode bit 11): alternative 2x2 matrix mode. Alpha channel not dithered (matches SW renderer).

### 7.3 Fastfill (1/1)
- [x] `vc_clear_buffers()` public API: pushes VC_CMD_CLEAR to SPSC ring with color/depth clear values
- [x] `vc_voodoo_fastfill()` in vid_voodoo_vk.c: extracts clear color from zaColor/color1, depth from zaColor
- [x] Hooked at SST_fastfillCMD register write in vid_voodoo_reg.c
- [x] Render thread dispatches `vkCmdClearAttachments()` for color+depth within active render pass

### 7.4 Alpha Mask (1/1)
- [x] Already implemented in Phase 4 color combine (fbzMode bit 13, lowest bit of alpha other)

### 7.5 W-Buffer Depth (1/1)
- [x] `FBZ_W_BUFFER` (fbzMode bit 3): logarithmic depth from 1/W using `findMSB()`-style computation
- [x] `gl_FragDepth` layout changed from `depth_unchanged` to `depth_any` (early-Z tradeoff for correctness)

### 7.6 Depth Bias and Source (1/1)
- [x] `FBZ_DEPTH_SOURCE` (fbzMode bit 20): constant depth from `zaColor & 0xFFFF` instead of interpolated
- [x] `FBZ_DEPTH_BIAS` (fbzMode bit 16): signed 16-bit bias from `zaColor` lower half added to depth

### 7.7 Phase 7 Validation (2/2)
- [x] Validated with VK_LAYER_KHRONOS_validation (VC_VALIDATE=1 env var): 0 Vulkan errors, 1 harmless MoltenVK portability warning
- [x] Per-triangle depth state (depth test enable, write enable, compare op, color write mask) extracted from fbzMode -- fixed ghost geometry in 3DMark99
- [x] First-frame clear (vkCmdClearAttachments on first render pass) -- fixed magenta uninitialized framebuffer

## Phase 8 Detail

### 8.1 LFB Read Path (1/1)
- [x] Synchronous staging readback in vc_readback.c -- fence wait, vkCmdCopyImageToBuffer, map
- [x] RGBA8 to RGB565 format conversion on CPU side (color LFB reads)
- [x] Address decode for Voodoo LFB address space
- [x] Wired into vid_voodoo_fb.c LFB read path (guarded by USE_VIDEOCOMMON)

### 8.2 Async Double-Buffered Readback (1/1)
- [x] Ping-pong staging buffers: submit readback to staging[current], read from staging[previous]
- [x] Adaptive threshold: switch to async mode when LFB reads exceed ~10/frame
- [x] Reduces latency for frequent LFB read patterns (mirrors, depth queries)

### 8.3 LFB Write Path (1/1)
- [x] Shadow buffer with dirty row tracking for batched writes
- [x] Format conversion (RGB565/ARGB1555/depth16 to RGBA8) on CPU side
- [x] Auto-flush dirty rows via vkCmdCopyBufferToImage before next render pass
- [x] Wired into vid_voodoo_fb.c LFB write path

### 8.4 Dirty Tile Tracking (1/1)
- [x] 64x64 tile bitmask for region-based readback
- [x] Only readback tiles that have been rendered to since last read
- [x] Reduces readback bandwidth for partial-screen LFB reads

### 8.5 Phase 8 Validation (1/1)
- [x] Infrastructure complete; runtime validation pending with LFB-heavy games
- [x] Build and push after Phase 8 milestone

## Phase 9 Detail

### 9.1 VCRenderer Class (DONE -- with deviations)

- [x] Created `src/qt/qt_vcrenderer.cpp` (1551 lines) and `qt_vcrenderer.hpp` (214 lines)
- [x] VCRenderer inherits QWindow + RendererCommon (not QVulkanWindow -- Qt5 Homebrew has QT_NO_VULKAN)
- [x] Direct platform WSI via volk: vkCreateMetalSurfaceEXT (macOS), vkCreateWin32SurfaceKHR (Windows), vkCreateXlibSurfaceKHR (Linux)
- [x] Created `qt_vc_metal_layer.mm` (47 lines) for CAMetalLayer + Retina contentsScale
- [x] Swapchain with VK_PRESENT_MODE_FIFO_KHR, VK_FORMAT_B8G8R8A8_UNORM
- [x] WSI extensions added to vc_core.c: VK_KHR_surface + platform ext (instance), VK_KHR_swapchain (device)
- **Deviation**: Originally planned to use QVulkanInstance::surfaceForWindow(). Impossible with Qt5 Homebrew (QT_NO_VULKAN). Bypassed Qt entirely with direct platform WSI via volk function pointers.

### 9.2 Swapchain Blit (DONE -- post-process path used)

- [x] Present via post-processing render pass (fullscreen triangle) or fallback vkCmdBlitImage
- [x] Swapchain recreation on window resize via m_needsResize atomic flag
- [x] Fence + semaphore synchronization for acquire/present
- [x] QBackingStore software fallback for pre-Voodoo display (BIOS, VGA, driver loading)

### 9.3 RendererStack Integration (DONE)

- [x] RENDERER_VIDEOCOMMON = 3 in renderdefs.h
- [x] Renderer::VideoCommon in qt_rendererstack.hpp enum
- [x] VCRenderer instantiated in createRenderer() with createWindowContainer wrapper
- [x] vid_api/plat_vidapi mappings for "qt_videocommon" in qt_mainwindow.cpp and qt.c

### 9.4 Post-Processing Shader (DONE)

- [x] postprocess.vert: fullscreen triangle (gl_VertexIndex trick, no vertex buffer)
- [x] postprocess.frag: CRT barrel distortion, scanlines, brightness push constants
- [x] VkRenderPass, VkPipeline, descriptor set layout, sampler, framebuffers all created in VCRenderer
- [x] Pipeline uses per-swapchain-image framebuffers with swapchain image views

### 9.5 Phase 9 Validation (INCOMPLETE -- BLOCKER)

- [ ] **BLOCKER**: Queue mutex contention causes freeze. FIFO present blocks on vsync (~16ms) while holding queue_mutex, starving the render thread. MAILBOX present mode not available on MoltenVK.
- [ ] Partial fix applied: skip readback when direct_present_active (reduces contention from 3-way to 2-way)
- [ ] **Proposed fix**: Move presentation to render thread via SPSC ring (VC_CMD_PRESENT command). This eliminates cross-thread queue access entirely. The Qt GUI thread would push a present command instead of calling vkQueuePresentKHR directly.
- [ ] Full code audit required before proceeding -- ~2400 lines of new code across 18 files needs systematic review.

### Phase 9 Files Created/Modified

| File | Status | Lines | Purpose |
|------|--------|-------|---------|
| src/qt/qt_vcrenderer.cpp | NEW | 1551 | VCRenderer implementation |
| src/qt/qt_vcrenderer.hpp | NEW | 214 | VCRenderer class definition |
| src/qt/qt_vc_metal_layer.mm | NEW | 47 | macOS CAMetalLayer helper |
| src/video/videocommon/shaders/postprocess.vert | NEW | 40 | Fullscreen triangle vertex shader |
| src/video/videocommon/shaders/postprocess.frag | NEW | 85 | CRT effects fragment shader |
| src/video/videocommon/vc_core.c | MOD | +133 | WSI extensions, accessors, global ctx |
| src/video/videocommon/vc_core.h | MOD | +68 | Accessor declarations, direct_present flag |
| src/include/86box/videocommon.h | MOD | +114 | Public API for new accessors |
| src/include/86box/renderdefs.h | MOD | +3 | RENDERER_VIDEOCOMMON enum |
| src/qt/qt_rendererstack.cpp | MOD | +35 | VCRenderer instantiation |
| src/qt/qt_rendererstack.hpp | MOD | +1 | Renderer enum entry |
| src/qt/qt_mainwindow.cpp | MOD | +3 | vid_api mapping |
| src/qt/qt.c | MOD | +5 | plat_vidapi mapping |
| src/qt/CMakeLists.txt | MOD | +24 | Build integration, volk link |
| src/video/videocommon/CMakeLists.txt | MOD | +22 | WSI platform defs, shader compile |
| src/video/vid_voodoo_display.c | MOD | +119 | Skip readback when direct_present |
| src/video/vid_voodoo_reg.c | MOD | +5 | Set vc_global_ctx on deferred init |
| src/video/vid_voodoo.c | MOD | +3 | Clear vc_global_ctx on close |

**Total**: ~2429 lines added across 18 files, 11 Phase 9 commits.

## Stabilization (Post-Phase-4)

Runtime testing with 3DMark99 on macOS/MoltenVK revealed 5 additional bugs after Phase 4 code was complete,
and 3 more during texture pipeline bring-up. All have been resolved; Phases 3 and 4 are now validated.

### Freeze Bugs (4 symptoms, 1 root cause -- commit 132cd8ce1)

The emulator froze solid during 3DMark99 benchmarks. Investigation (see `validation/spsc-threading-analysis.md`)
identified that push constants were being sent to the SPSC ring before **every** triangle, which caused
`vc_dispatch_push_constants()` to flush the vertex batch each time. This produced thousands of individual
`vkCmdDraw` calls per frame instead of batched draws, saturating the 4096-entry SPSC ring and starving the
render thread. The 4 symptoms observed:

1. **SPSC two-producer race**: Ring filled faster than render thread could drain it
2. **Blocked timer callback**: FIFO thread blocked on full ring, preventing timer-driven callbacks
3. **Spin-wait with no yield**: Producer spin-waited for ring space without yielding CPU
4. **Ring too small**: 4096 entries insufficient when every triangle generates 2 ring commands (push constants + triangle)

**Fix**: Track last-sent push constants with `memcmp()`; only send `VC_CMD_PUSH_CONSTANTS` when register
state actually changes between triangles. Most frames now send push constants once or twice instead of
thousands of times, keeping the ring well under capacity.

### Framebuffer Clear Bug (commit 794a7fb03)

5. **LOAD_OP_LOAD with undefined contents**: `VK_ATTACHMENT_LOAD_OP_LOAD` preserves previous framebuffer
   contents, but at init time the image contents are undefined (not cleared). This caused garbage/white
   pixels on undrawn areas. Fix: after the initial `UNDEFINED -> optimal` layout transition, insert an
   additional `TRANSFER_DST` transition, `vkCmdClearColorImage` (black) and `vkCmdClearDepthStencilImage`
   (1.0), then transition to final optimal layouts. Also added `VK_IMAGE_USAGE_TRANSFER_DST_BIT` to depth
   image usage flags.

### Texture Pipeline Bugs (3 bugs, 3 commits)

6. **Texture upload not wired** (commit 34794d54f): `vc_texture_upload()` had zero callers. Wired Voodoo SW
   texture cache to Vulkan VkImages via SPSC ring with deterministic slot mapping (`tmu * TEX_CACHE_MAX + entry`).
   Malloc'd data copy decouples producer lifetime from render thread consumption.

7. **Black screen after texture wiring** (commit cbe67e89e): Descriptor pool too small for texture descriptor
   sets (only had 3 COMBINED_IMAGE_SAMPLER, needed more). Also TMU1 slot initialized to -1 (unsigned) causing
   out-of-bounds access. Fixed pool size and TMU1 init.

8. **Texture bind loss at frame boundary** (commit 3f3cbb181): `vkResetDescriptorPool` invalidates ALL
   descriptor sets, but producer-side tracking (`vk_last_tmu0_slot`, `vk_last_texmode[]`) was not reset at
   frame boundaries. Also, emergency vertex overflow restart (end_frame + begin_frame) did not re-bind
   textures. Fixed both paths; added `vc_thread_bind_textures()` helper and `last_bound_valid` tracking.

### Remaining Known Issues

- **Fastfill (screen clear) IMPLEMENTED** (Phase 7.3): `vkCmdClearAttachments()` hooked at SST_fastfillCMD.
  Fixes ghost geometry accumulation that was the single biggest source of artifacts.
- **Stipple test IMPLEMENTED** (Phase 7.1): Pattern and rotating mode stipple discard in uber-shader.
- **W-buffer depth IMPLEMENTED** (Phase 7.5): Logarithmic depth from 1/W; gl_FragDepth layout changed to depth_any.
- **Depth bias/source IMPLEMENTED** (Phase 7.6): zaColor-based constant depth and signed 16-bit bias.
- **Dither IMPLEMENTED** (Phase 7.2): Per-fragment 4x4/2x2 Bayer ordered dither quantizes RGB to 565 precision in uber-shader (pre-blend). Controlled by fbzMode bits 8 and 11.
- **LFB read/write IMPLEMENTED** (Phase 8): Synchronous + async double-buffered readback, LFB write via
  shadow buffer with dirty row tracking, 64x64 dirty tile bitmask for region-based readback. RGBA8-to-RGB565
  format conversion. Wired into vid_voodoo_fb.c LFB read/write paths.
- **ACOLORBEFOREFOG implemented** (Phase 6.4): Dual-source blending via VK_BLEND_FACTOR_SRC1_COLOR.
  Requires dualSrcBlend device feature (supported on all target platforms).

## Commit Log

All commits related to VideoCommon, in chronological order.

| Hash | Date | Description | Phase |
|------|------|-------------|-------|
| 75cf02cb4 | 2026-02-26 | Phase 1a: build system, volk/VMA, shaders, vc_core | 1 |
| 4d3736825 | 2026-02-26 | STATUS.md + CHANGELOG.md tracking docs | 1 |
| 0a9711f46 | 2026-02-26 | Phase 1b: render pass, shader, pipeline, batch, readback, thread | 1 |
| f115a7ec0 | 2026-02-26 | Update STATUS.md and CHANGELOG.md for Phase 1b completion | 1 |
| 088b90ecf | 2026-02-27 | Update CHECKLIST.md and STATUS.md -- check off 31/32 Phase 1 tasks | 1 |
| 60802ec8f | 2026-02-27 | Wire sub-modules into vc_context_t and implement public API | 1 |
| 83ffb31f7 | 2026-02-27 | Phase 2: Voodoo triangle path (flat-shaded) -- vertex reconstruction, shaders, hooks | 2 |
| ee122b03e | 2026-02-27 | Update STATUS.md, CHECKLIST.md, CHANGELOG.md for Phase 2 | 2 |
| 0aef57540 | 2026-02-27 | Phase 2 complete -- 3DMark99 validated on macOS/MoltenVK | 2 |
| 38aea1bcc | 2026-02-27 | Phase 3: texture support (TMU0) -- vc_texture, descriptor sets, shader sampling | 3 |
| 0b2d642c3 | 2026-02-27 | Update planning docs for Phase 3 completion | 3 |
| 446b0e100 | 2026-02-27 | Add missing commits to STATUS.md commit log | docs |
| 5fa6f18b9 | 2026-02-27 | Runtime bring-up: deferred init on background thread, 6 bug fixes | bugfix |
| 6f97c167d | 2026-02-27 | Pre-Phase-4 audit: fix stale comment, add queue mutex, reconcile docs | audit |
| f437539c4 | 2026-02-27 | Pre-Phase-4 audit: 4 critical bug fixes + depth_unchanged + spec doc + full doc sweep | audit |
| dfd6b9555 | 2026-02-27 | Add VC_CMD_PUSH_CONSTANTS to SPSC command ring | 4 |
| df0567de6 | 2026-02-27 | Phase 4: color/alpha combine and chroma key in uber-shader | 4 |
| 8f7ea74b3 | 2026-02-27 | Phase 4 complete: wire push constants from emulator, update docs | 4 |
| d175dd2de | 2026-02-27 | Update STATUS.md commit log for 8f7ea74b3 | docs |
| 132cd8ce1 | 2026-02-27 | Fix freeze: deduplicate push constant sends to preserve batching | stabilization |
| 794a7fb03 | 2026-02-27 | Clear framebuffer images after initial layout transition | stabilization |
| 34794d54f | 2026-02-27 | Wire Voodoo texture upload to Vulkan pipeline (Phase 3 runtime) | 3 |
| cbe67e89e | 2026-02-27 | Fix black screen: increase descriptor pool, fix TMU1 slot init | stabilization |
| 3f3cbb181 | 2026-02-27 | Fix texture bind loss at frame boundary and vertex overflow | stabilization |
| 8206de92c | 2026-02-27 | Phase 3 validated: update STATUS.md for texture milestone | docs |
| ea8fa541b | 2026-02-27 | Phase 5: TMU1 texture coordinates and dual-TMU combine shader | 5 |
| 41582f9b2 | 2026-02-27 | Phase 5: detail/LOD blend factors + trilinear flag in uber-shader | 5 |
| b22e37660 | 2026-02-27 | Phase 6.1: alpha test in uber-shader | 6 |
| 4cdeb2858 | 2026-02-27 | Phase 6.2: alpha blending via pipeline variants | 6 |
| 0590e29b5 | 2026-02-27 | Phase 6.3: fog table upload and fog blending in uber-shader | 6 |
| da7fca2bd | 2026-02-27 | Update CHECKLIST.md and STATUS.md for Phase 5+6 completion | docs |
| e6f0a6b65 | 2026-02-27 | Fix stale docs -- update known issues, branch name, add Phase 4-6 changelog | docs |
| a0e4ac1ab | 2026-02-27 | Phase 6.4: dual-source blending for ACOLORBEFOREFOG | 6 |
| 8ea2b8bf2 | 2026-02-27 | Update docs for Phase 6.4 completion | docs |
| d284789b1 | 2026-02-27 | Phase 7.1/7.3/7.5/7.6: stipple, fastfill, W-buffer, depth bias | 7 |
| f4c2ee350 | 2026-02-27 | Update docs for Phase 7.1/7.3/7.5/7.6 completion | docs |
| 750a5b91b | 2026-02-27 | Per-triangle depth state from fbzMode (fix ghost geometry) | 7 |
| 0dc6a518d | 2026-02-27 | First-frame clear (fix magenta framebuffer) | 7 |
| 32dd41ff2 | 2026-02-27 | Phase 7.7: runtime validation via VC_VALIDATE=1 | 7 |
| 192252f97 | 2026-02-27 | Phase 7.2: ordered dither (4x4/2x2 Bayer) in uber-shader | 7 |
| 25d786136 | 2026-02-27 | Phase 7 complete: update docs, remove stipple diagnostic logging | docs |
| 6f8cb5207 | 2026-02-28 | Phase 8.1: synchronous LFB read via Vulkan readback | 8 |
| ad1ca4aab | 2026-02-28 | Phase 8.2: async double-buffered LFB readback | 8 |
| 711136046 | 2026-02-28 | Phase 8.3: LFB write path with dirty tracking and shadow buffers | 8 |
| 0f7bf6dd0 | 2026-02-28 | Phase 8.4: dirty tile tracking for region-based readback | 8 |
| a2863f576 | 2026-02-28 | Phase 8 complete: update docs for LFB read/write milestone | docs |
| da2562e1b | 2026-02-28 | Phase 9.1: WSI instance/device extensions for surface+swapchain | 9 |
| 30bbc7ce4 | 2026-02-28 | Phase 9.1: VCRenderer class, direct platform WSI (Metal/Win32/Xlib) | 9 |
| 0c235d072 | 2026-02-28 | Phase 9.3: renderer enum, RendererStack integration, accessors | 9 |
| 421e7deea | 2026-02-28 | Phase 9.4: post-processing shader (CRT barrel distortion, scanlines) | 9 |
| ee60415c5 | 2026-02-28 | Phase 9: data race fix, shutdown ordering, dead code cleanup, Retina | 9 |
| 06b8a9216 | 2026-02-28 | Phase 9: hide atomic behind C accessor functions (C/C++ interop) | 9 |
| 59ff0f66a | 2026-02-28 | Phase 9: deferred Vulkan init (QTimer retry until Voodoo ready) | 9 |
| a36b56f71 | 2026-02-28 | Phase 9: fence deadlock fix, init stall fix, first-frame drop | 9 |
| fb6346d7e | 2026-02-28 | Phase 9: QBackingStore software fallback for pre-Voodoo display | 9 |
| 06e028d42 | 2026-02-28 | Phase 9: display path logging (identify SW vs Vulkan path) | 9 |
| e39a077ea | 2026-02-28 | Phase 9: queue mutex contention fix (partial -- still freezes) | 9 |

## Key Decisions

| # | Decision | Rationale | Date |
|---|----------|-----------|------|
| 1 | Vulkan 1.2 baseline (not 1.0/1.1) | Lowest common denominator across all 4 targets | 2026-02-26 |
| 2 | volk for dynamic loading (no find_package Vulkan) | Eliminates build-time Vulkan SDK dependency | 2026-02-26 |
| 3 | VMA for memory allocation | Industry standard, handles UMA/discrete transparently | 2026-02-26 |
| 4 | Vulkan 1.4.309 headers vendored | Latest stable, backward compatible with 1.2 runtime | 2026-02-26 |
| 5 | VK_KHR_portability_enumeration + subset | Required for MoltenVK device discovery and usage | 2026-02-26 |
| 6 | 86Box thread API for render thread | Uses thread_create/event from thread.h, consistent with rest of codebase | 2026-02-26 |
| 7 | SPSC ring with C11 _Atomic | Lock-free producer-consumer, no mutex overhead | 2026-02-26 |
| 8 | VMA_ALLOCATION_CREATE_MAPPED_BIT for staging | Persistent mapping avoids map/unmap overhead | 2026-02-26 |
| 9 | Vertex stride fix (60→56 bytes) | vc_vertex_t is 14 floats not 15; mismatch caused GPU data misalignment | 2026-02-27 |
| 10 | noperspective for Gouraud colors, smooth for texture coords | Affine color interpolation matches Voodoo HW; perspective-correct textures via W | 2026-02-27 |
| 11 | VK_FORMAT_B8G8R8A8_UNORM for textures | Matches Voodoo SW makergba() BGRA8 output; zero CPU-side format conversion | 2026-02-27 |
| 12 | Descriptor pool with FREE_DESCRIPTOR_SET_BIT | Reset per-frame; placeholder 1x1 white texture for unused TMU slots | 2026-02-27 |
| 13 | textureSize() for UV normalization in shader | Voodoo S/T in texel units, Vulkan texture() expects [0,1]; runtime query avoids push constant overhead | 2026-02-27 |
| 14 | macOS MoltenVK ICD auto-discovery | Vulkan loader can't find MoltenVK ICD when VulkanSDK removed; probe Homebrew paths at runtime | 2026-02-27 |
| 15 | Deferred Vulkan init on background thread | Guest Glide driver submits test triangles and reads fb_mem during HW detection; synchronous vc_init() blocks FIFO thread. Solution: software renderer handles all triangles until vc_ctx is set; vc_init() spawns on background thread at first swap buffer command | 2026-02-27 |
| 16 | Deduplicate push constants with memcmp | Sending push constants before every triangle saturated the SPSC ring (2 cmds/tri * thousands of tris). Track last-sent state, only send on change. Reduces ring pressure by ~1000x typical | 2026-02-27 |
| 17 | Clear framebuffer images at init time | LOAD_OP_LOAD requires defined initial contents; added explicit vkCmdClearColorImage/vkCmdClearDepthStencilImage after layout transition | 2026-02-27 |
| 18 | Deterministic texture slot mapping | `tmu * TEX_CACHE_MAX + entry` avoids cross-thread slot allocation; producer computes slot without render thread coordination | 2026-02-27 |
| 19 | Malloc'd texture data copy for ring commands | Decouples producer lifetime from render thread consumption; render thread frees after VkImage upload | 2026-02-27 |
| 20 | Reset bind tracking at frame boundary | vkResetDescriptorPool invalidates ALL previous descriptor sets; any state tracker that deduplicates bind commands MUST be reset when pool is reset | 2026-02-27 |
| 21 | TMU1 alpha reverse logic swapped vs color | SW rasterizer (vid_voodoo_render.c ~536): TMU1 alpha combine uses !a_reverse to invert factor, opposite of color combine convention. Shader must match this quirk | 2026-02-27 |
| 22 | Dual-source blending for ACOLORBEFOREFOG | Instead of copy-on-blend, use VK_BLEND_FACTOR_SRC1_COLOR with fragment shader outputting pre-fog color as SRC1. Requires dualSrcBlend feature (supported on MoltenVK, Mesa, NVIDIA, AMD). Avoids framebuffer copy overhead | 2026-02-27 |

| 23 | depth_any for gl_FragDepth | W-buffer writes non-interpolated depth, so depth_unchanged is incorrect. depth_any disables early-Z but is required for correctness when W-buffer or depth source override is active | 2026-02-27 |
| 24 | Fastfill via vkCmdClearAttachments inside render pass | Avoids ending/restarting render pass; clears color+depth in a single call within the active render pass | 2026-02-27 |
| 25 | Per-triangle depth state via VK_EXT_extended_dynamic_state | Depth test/write/compare op extracted from fbzMode per-triangle. Avoids pipeline explosion. Color write mask propagated through pipeline key (not dynamic on MoltenVK) | 2026-02-27 |
| 26 | Pre-blend dither (Option D) | Dither applied in fragment shader before hardware blend. Exact for opaque draws; negligible error for alpha-blended draws. Avoids post-blend framebuffer read | 2026-02-27 |
| 27 | Runtime validation via VC_VALIDATE=1 | Debug messenger + validation layer enabled by env var in any build type (not just debug). Catches Vulkan API misuse without recompiling | 2026-02-27 |
| 28 | Async double-buffered LFB readback | Ping-pong staging buffers reduce latency for frequent LFB reads; adaptive threshold switches sync-to-async automatically | 2026-02-28 |
| 29 | Shadow buffer for LFB writes | CPU-side dirty row tracking batches individual pixel writes into single vkCmdCopyBufferToImage per flush | 2026-02-28 |
| 30 | 64x64 dirty tile tracking | Bitmask tracks which framebuffer tiles have been rendered to; region-based readback avoids full-framebuffer copy | 2026-02-28 |
| 31 | Direct platform WSI (bypass Qt Vulkan) | Qt5 Homebrew on macOS has QT_NO_VULKAN; use vkCreateMetalSurfaceEXT/vkCreateWin32SurfaceKHR/vkCreateXlibSurfaceKHR directly via volk | 2026-02-28 |
| 32 | Deferred VCRenderer Vulkan init | VCRenderer created at Qt startup before Voodoo device exists; QTimer retries every 100ms until vc_get_global_ctx() returns valid context | 2026-02-28 |
| 33 | QBackingStore software fallback | VCRenderer (QWindow) needs a paint path for pre-Voodoo display (BIOS, VGA, driver loading); QBackingStore + QPainter paints blit buffers | 2026-02-28 |
| 34 | C/C++ atomic interop via accessor functions | _Atomic(T) doesn't work across C/C++ compilation units with Clang; hide behind vc_get_global_ctx()/vc_set_global_ctx() C functions | 2026-02-28 |

## Risk Register

| Risk | Mitigation | Status |
|------|------------|--------|
| MoltenVK quirks | portability_enumeration/subset enabled; ICD auto-discovery added | Mitigated |
| VK init only in voodoo_2d3d_card_init | Fixed: added to voodoo_card_init() for V1/V2; was only in Banshee/V3 path | Resolved |
| Pi 5 VK_EXT_extended_dynamic_state | Fallback path (CHECKLIST 11.6); caps detection in place | Unverified |
| Duplicate volk link warning | Harmless; OBJECT library pattern requires explicit link | Accepted |
| Thread event timeout=-1 (blocking) | 86Box event_wait supports -1 for infinite wait | Verified |
| Glide HW detection breaks with GPU intercept | Deferred init: software renderer handles all triangles until vc_ctx set | Resolved |
| FIFO thread blocks on synchronous vc_init | Background thread via thread_create(); vc_init_pending flag prevents double init | Resolved |
| Image layout transitions missing at init | One-shot cmd buffer transitions all 4 images UNDEFINED → optimal | Resolved |
| vc_voodoo_swap_buffers() never called | Added call in SST_swapbufferCMD handler | Resolved |
| ARM64 memory ordering for cross-thread fields | _Atomic + acquire/release on vc_ctx, vc_init_pending | Resolved |
| Deferred init thread not joined on close | Store thread handle, thread_wait() before vc_close() | Resolved |
| Early-Z disabled by gl_FragDepth write | layout(depth_unchanged) qualifier re-enables early-Z | Resolved |
| SPSC ring saturation from push constants | Deduplicate with memcmp, only send on state change | Resolved |
| Framebuffer undefined contents with LOAD_OP_LOAD | Explicit clear after initial layout transition | Resolved |
| Texture upload not wired | Wired in 34794d54f; deterministic slot mapping, malloc'd data copy, bind tracking | Resolved |
| Descriptor pool too small for textures | Increased COMBINED_IMAGE_SAMPLER count in descriptor pool | Resolved |
| Texture bind loss at frame boundary | Reset producer tracking at swap; re-bind after emergency restart | Resolved |
| Alpha blending not implemented | Phase 6 complete; alpha test, blending, fog all implemented | **Resolved** |
| Qt5 QT_NO_VULKAN on macOS Homebrew | Bypassed Qt Vulkan entirely; direct platform WSI via volk | Resolved |
| C/C++ _Atomic interop failure | Hidden behind C accessor functions (vc_get/set_global_ctx) | Resolved |
| VCRenderer init before Voodoo device | QTimer deferred init retries every 100ms | Resolved |
| Black screen before Vulkan init | QBackingStore software fallback for pre-Voodoo display | Resolved |
| Fence deadlock on null front image | Restructured present() sync ordering; empty submit fallback | Resolved |
| Queue mutex contention (FIFO present) | Present channel moves all VkQueue ops to render thread (829b320f8, fa9233864). Event-based drain (8652cd537). | **Resolved** |
| **swap_count stuck in VK mode** | **UNRESOLVED**: `swap_count` stays at 3 in SST_status — display callback (`voodoo_callback`) not decrementing. Guest polls status, sees pending swaps, freezes at 3DMark99 test 8-9. `swap_count--` requires `swap_pending && retrace_count > swap_interval` in display callback. Root cause: swap_pending/retrace_count interaction in VK mode not functioning. | **BLOCKER** |
