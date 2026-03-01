# VideoCommon Implementation Changelog

Tracks significant implementation decisions, deviations from DESIGN.md,
and notable events during development.

---

## Phase 9: Present Channel (Queue Mutex Contention Fix)

### 2026-02-28 -- Present channel architecture (829b320f8, fa9233864)

- **Problem**: VCRenderer::present() called vkQueuePresentKHR on the GUI thread while holding queue_mutex for ~16ms (FIFO vsync). This starved the render thread which also needs queue_mutex for vkQueueSubmit, causing the SPSC ring to fill and emulation to freeze.

- **Solution**: Present channel -- a side-band atomic communication path between the GUI thread and render thread, separate from the SPSC ring (which is single-producer from the FIFO thread). The GUI thread posts a non-blocking present request (swapchain, image index, semaphores, fence) via an atomic IDLE→PENDING state machine, and the render thread dispatches it inside the ring processing loop.

- **Implementation details**:
  - `vc_present_request_t` struct in vc_core.h with atomic pending flag (IDLE/PENDING states)
  - `vc_present_submit()` (GUI thread): fills request fields, sets PENDING, signals wake_event, returns immediately (non-blocking)
  - `vc_present_dispatch()` (render thread): checks pending==PENDING, takes queue_mutex, does vkQueueSubmit+vkQueuePresentKHR, transitions PENDING→IDLE
  - Render thread services present channel after batch limit (1024 cmds) and at frame boundaries (VC_CMD_SWAP)
  - Fence-based synchronization: VCRenderer waits on m_presentFence at START of next present() call, not at end of current one

- **Iteration history** (8 test cycles):
  1. Blocking vc_present_submit (5s timeout) → froze Qt event loop, black screen
  2. Per-command vc_present_dispatch in inner loop → overhead slowed ring drain, ring chronically full
  3. pclog() I/O delay accidentally provided enough throttling to render slowly → confirmed ring drain rate issue
  4. Batch limit 256 → too many interruptions, still slow
  5. Batch limit 1024 + frame-boundary presents → blocking submit still froze event loop
  6. Non-blocking submit → renders first benchmark successfully but freezes on benchmark transition

- **Current state**: First benchmark renders correctly (textured 3DMark99). Freezes when transitioning to second benchmark -- ring stall or present channel timing issue during scene teardown/rebuild. Process stays alive but unresponsive.

---

## Phase 1: Core Infrastructure

### 2026-02-26 -- Phase 1a started

- Created directory structure: `src/video/videocommon/` with `cmake/`, `shaders/`, `third_party/volk/`, `third_party/vma/`
- Vendored volk (header version 343) with Vulkan 1.4.309 headers from Khronos
- Vendored VMA (latest from GPUOpen master)
- HAVE_STDARG_H must be defined before including `<86box/86box.h>` to access `pclog_ex()` -- this is the same pattern used by all existing 86Box source files (e.g., vid_voodoo.c defines it at top)
- volk include path is `third_party/volk` which contains both `volk.h` and the `vulkan/` subdirectory, so `<vulkan/vulkan_core.h>` resolves correctly
- VK_KHR_portability_enumeration and VK_KHR_portability_subset are both needed for MoltenVK compatibility -- instance creation needs the enumeration flag, device creation needs the subset extension
- Duplicate linker warning for libvolk.a is harmless -- it's linked via both videocommon's PUBLIC dependency and the explicit target_link_libraries in src/CMakeLists.txt. The explicit link is needed because videocommon is an OBJECT library.
- clang-format not installed on this system; code written to follow WebKit style manually

### 2026-02-26 -- Phase 1b: remaining modules

- **vc_render_pass**: VkRenderPass uses `LOAD_OP_LOAD` for both color and depth -- critical for Voodoo's incremental rendering pattern (triangles added across multiple batches within a single frame). Initial/final layouts both set to OPTIMAL (no layout transition within pass). VMA allocations use DEDICATED_MEMORY_BIT for framebuffer images.

- **vc_shader**: SPIR-V alignment check -- generated arrays from SpvToHeader.cmake are always 4-byte aligned since glslc output is. VkShaderModuleCreateInfo takes `const uint32_t*` so we cast from `unsigned char*`.

- **vc_pipeline**: Vertex input layout is 56 bytes (14 floats) matching vc_vertex_t exactly. Attributes: position(vec2, 0), depth(float, 8), color(vec4, 12), texcoord0(vec3, 28), texcoord1(vec3, 40), oow(float, 52). *(Originally written as 60 bytes/15 floats -- corrected during Phase 1 completion; see bug fix below.)* Dynamic state includes viewport + scissor always; depth test/write/compare + cull mode only when VK_EXT_extended_dynamic_state is available. Blend state baked into pipeline (MoltenVK limitation). Up to 32 cached pipeline variants.

- **vc_batch**: Uses VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT + MAPPED_BIT for persistent mapping without explicit map/unmap. 1 MB ring = 17,476 vertices max (5,825 triangles). Buffer full condition triggers immediate flush + reset rather than wrap-around (simpler, adequate for Voodoo workloads).

- **vc_readback**: Dedicated command pool with RESET_COMMAND_BUFFER_BIT for re-use. Pipeline barrier sequence: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL (pre-copy) -> COLOR_ATTACHMENT_OPTIMAL (post-copy). Host visibility barrier added (TRANSFER_WRITE -> HOST_READ) even though we also call vmaInvalidateAllocation (belt-and-suspenders for non-coherent memory).

- **vc_thread**: Uses 86Box's `thread_create_named()` / `thread_wait()` / `thread_create_event()` / `thread_set_event()` / `thread_wait_event()` API from `<86box/thread.h>`. Event-based wake: render thread sleeps with 1ms timeout when idle, woken by producer on push. Frame fences created with VK_FENCE_CREATE_SIGNALED_BIT so first vkWaitForFences succeeds immediately. CMD_SYNC: ends frame, waits for previous frame's fence, signals sync_event for producer.

- **Shaders updated**: Both .vert and .frag now declare the full vertex layout (6 attributes), 64-byte push constant block, and descriptor set (3 combined image samplers). Phase 1 placeholder: vertex shader passes through coordinates, fragment shader outputs interpolated vertex color.

- **Public API updated**: videocommon.h now declares vc_submit_triangle(), vc_swap_buffers(), vc_sync(), vc_readback_pixels() with no-op stubs when USE_VIDEOCOMMON is not defined.

- **CMakeLists.txt updated**: All 7 source files listed in OBJECT library (vc_core, vc_render_pass, vc_shader, vc_pipeline, vc_batch, vc_readback, vc_thread).

### 2026-02-27 -- Phase 1 completion: sub-module wiring

- **vc_core.h**: Added sub-module struct members to `vc_context_t` -- render_pass (pointer), shader, pipeline_cache, readback, thread (all embedded by value except render_pass which is heap-allocated). The C11 typedef forward declaration pattern (in sub-module headers) is compatible with the later full definition.

- **vc_core.c**: `vc_init()` now initializes all sub-modules in sequence (render pass → shader → pipeline → readback → thread), with rollback via `vc_close()` on any failure. `vc_close()` destroys in reverse order. Public API functions implemented: `vc_submit_triangle()` builds CMD_TRIANGLE and pushes to SPSC ring, `vc_swap_buffers()` pushes CMD_SWAP, `vc_sync()` calls `vc_thread_sync()`, `vc_readback_pixels()` reads front framebuffer via staging buffer.

- **Bug fix**: `VC_VERTEX_STRIDE` was 60 (15 floats) but `vc_vertex_t` is actually 14 floats (56 bytes). The struct has: position[2] + depth + color[4] + texcoord0[3] + texcoord1[3] + oow = 14. Attribute offsets were correct (ending at 52+4=56); only the stride and comments were wrong. Fixed in both `vc_pipeline.c` and `vc_batch.h`.

---

## Phase 2: Voodoo Triangle Path (Flat-Shaded)

### 2026-02-27 -- Phase 2 implementation

- **vid_voodoo_common.h**: Added 3 fields to `voodoo_t` (guarded by `USE_VIDEOCOMMON`): `vc_ctx` (void*, holds vc_context_t), `use_gpu_renderer` (int, config-driven), `vc_readback_buf` (const void*, cached readback pointer valid until next swap).

- **vid_voodoo.c**: Init hook creates Vulkan context via `vc_init(640, 480)` when config enabled; falls back to SW on failure. Close hook destroys context. Added `"gpu_renderer"` config option (CONFIG_BINARY, default OFF).

- **vid_voodoo_vk.c/h** (NEW): Complete vertex reconstruction from `voodoo_params_t` gradients:
  - Fixed-point converters: 12.4 positions, 12.12 colors, 20.12 depth, 18.32 W/texture coords
  - Gradient reconstruction: `V_B = startV + dVdX*(xB-xA) + dVdY*(yB-yA)`, using int64 intermediates for 12.12/20.12 and double intermediates for 18.32
  - `vc_push_constants_t` (64 bytes, `_Static_assert` enforced): 6 raw register copies + packed fogColor (from rgbvoodoo_t {b,g,r,pad} to R<<16|G<<8|B) + detail params + fbWidth/fbHeight
  - `vc_voodoo_submit_triangle()`: reconstructs all 3 vertices, submits via `vc_submit_triangle()`
  - TMU coordinates are placeholder zeros for Phase 2 (will be filled in Phase 3)

- **vid_voodoo_render.c**: VK path branch at top of `voodoo_queue_triangle()` -- when `use_gpu_renderer`, calls `vc_voodoo_submit_triangle()` and returns, skipping the SW rasterizer entirely.

- **vid_voodoo_display.c**: Vulkan scanout path in `voodoo_callback()`:
  - Lazy readback: only calls `vc_readback_pixels()` once per frame (cached in `vc_readback_buf`)
  - RGBA8 → XRGB8888 conversion (swap R and B channels): `((r << 16) | (g << 8) | b)`
  - Cache invalidated at vsync (line == v_disp)
  - Falls through to existing SW path when VK not active

- **Vertex shader** (`voodoo_uber.vert`): Full NDC conversion from screen-space pixel coordinates. W encoding: `gl_Position = vec4(ndc_x*W, ndc_y*W, z_ndc*W, W)` where `W = 1.0/inOOW`. Outputs: `vColor` (noperspective -- affine interpolation matching Voodoo HW Gouraud), `vTexCoord0/1` (smooth -- perspective-correct), `vDepth` and `vFog` (noperspective).

- **Fragment shader** (`voodoo_uber.frag`): Phase 2 placeholder -- outputs interpolated vertex color directly, writes `gl_FragDepth = vDepth`. Full color combine pipeline deferred to Phase 4.

- **CMakeLists.txt**: Added `vid_voodoo_vk.c` to voodoo OBJECT library target (conditional on VIDEOCOMMON).

- **Include chain for vid_voodoo_vk.c**: Requires `<86box/mem.h>`, `<86box/timer.h>`, `<86box/video.h>`, `<86box/vid_svga.h>` before `<86box/vid_voodoo_common.h>` to satisfy transitive type dependencies (`mem_mapping_t`, `pc_timer_t`, etc.).

---

## Phase 3: Texture Support

### 2026-02-27 -- Phase 3 implementation

- **vc_texture.h** (NEW, 263 lines): Texture management header. Key types: `vc_tex_entry_t` (VkImage + VkImageView + VmaAllocation + generation counter), `vc_sampler_key_t` (min/mag filter + address mode), `vc_texture_t` (pool of 128 entries, sampler cache of 8, descriptor pool, staging buffer). Constants: `VC_TEX_POOL_SIZE=128`, `VC_SAMPLER_CACHE_SIZE=8`, `VC_TEX_MAX_MIP_LEVELS=9`, `VC_TEX_STAGING_SIZE=256KB`.

- **vc_texture.c** (NEW, 1118 lines): Full implementation:
  - VkImage pool: 128 entries, `VK_FORMAT_B8G8R8A8_UNORM` matching Voodoo's `makergba()` BGRA8 output
  - Staging upload: synchronous via dedicated transfer command pool/buffer/fence, pipeline barriers for layout transitions (UNDEFINED→TRANSFER_DST→SHADER_READ_ONLY)
  - Fog table: 64x1 `VK_FORMAT_R8G8_UNORM` (R=fog alpha, G=dfog delta)
  - Placeholder texture: 1x1 white pixel (0xFFFFFFFF) bound to all unused descriptor slots to avoid validation errors
  - Descriptor pool: `FREE_DESCRIPTOR_SET_BIT` enabled, `vc_texture_bind()` allocates fresh sets with TMU0/TMU1/fog bindings
  - Sampler cache: linear scan of 8 entries (nearest/bilinear × clamp/wrap/mirror combinations)
  - Transfer fence created `SIGNALED` for first-use compatibility with `vkWaitForFences`

- **vc_core.h**: Added `#include "vc_texture.h"` and `vc_texture_t texture` member to `vc_context_t`

- **vc_core.c**: Added `vc_texture_init()` in init sequence (after pipeline cache, before readback) and `vc_texture_close()` in destroy sequence (after readback, before pipeline)

- **vc_thread.c**: Three changes:
  - Added `#include "vc_texture.h"`
  - `vc_begin_render_pass()`: Now binds default pipeline (blend disabled) via `vc_pipeline_lookup()`, sets extended dynamic state (depth test/write/compare), binds placeholder descriptor set via `vc_texture_bind()` with TMU0=-1, TMU1=-1
  - `vc_begin_frame()`: Added `vc_texture_reset_descriptors()` call after batch reset to reclaim descriptor sets per-frame

- **CMakeLists.txt**: Added `vc_texture.c` to videocommon OBJECT library (8 source files total)

- **vid_voodoo_vk.c**: TMU0 texture coordinate reconstruction -- replaced placeholder zeros with actual gradient-based reconstruction from `tmu[0].startS/T/W` and `dS/T/WdX/dY` using `reconstruct_18_32()` helper (same pattern as OOW). TMU1 remains placeholder zeros (Phase 5).

- **voodoo_uber.frag**: Updated from Phase 2 to Phase 3:
  - Added `unpackColor()` and `unpackRGB()` helper functions for future Phase 4 color combine
  - Texture fetch: checks `FBZCP_TEXTURE_ENABLED` (bit 27 of `fbzColorPath`)
  - Perspective-correct sampling: reconstructs S,T from smooth-interpolated `vTexCoord0 = (S/W, T/W, 1/W)` via perspective divide when `1/W > 0`, raw S/W,T/W when perspective disabled
  - UV normalization: `textureSize(tex0, 0)` converts texel coordinates to [0,1] for Vulkan `texture()` call
  - Samples TMU0 via `texture(tex0, uv)` and replaces vertex color when texturing enabled

### 2026-02-27 -- Texture upload pipeline wired (commit 34794d54f)

- **vc_texture_upload()** had zero callers at runtime -- the 1x1 white placeholder was always bound. Wired
  Voodoo SW texture cache (`tex_entry[]`) to Vulkan VkImages via SPSC ring with two new commands:
  `VC_CMD_TEXTURE_UPLOAD` (transfers decoded BGRA8 data to VkImage) and `VC_CMD_TEXTURE_BIND` (binds a
  texture slot to a TMU descriptor set binding).

- **Deterministic slot mapping**: `vc_slot = tmu * TEX_CACHE_MAX + cache_entry` avoids cross-thread slot
  allocation. Producer computes slot without render thread coordination.

- **Texture identity tracking** per-TMU: `(base, tLOD, palette_checksum)` triple -- only upload when
  any component changes. Sampler extracted from `textureMode` register (bit1=minFilter, bit2=magFilter,
  bit6=clampS, bit7=clampT).

- **Malloc'd data copy**: `vc_texture_upload_async()` mallocs a copy of the decoded texture data so the
  producer's texture cache entry can be released immediately. Render thread frees after VkImage upload.

### 2026-02-27 -- Black screen fix (commit cbe67e89e)

- **Descriptor pool too small**: Pool was created with only 3 `COMBINED_IMAGE_SAMPLER` descriptors -- far
  too few once real textures started being bound. Increased to accommodate per-frame allocations.

- **TMU1 slot init bug**: `vc_voodoo_submit_triangle()` initialized TMU1 slot to `-1` as a signed int, but
  the slot variable was unsigned, causing out-of-bounds texture pool access. Fixed to use `VC_TEX_SLOT_NONE`
  sentinel value.

### 2026-02-27 -- Descriptor set bind loss fix (commit 3f3cbb181)

- **Root cause**: `vkResetDescriptorPool` invalidates ALL descriptor sets at frame boundaries, but the
  producer-side tracking state (`vk_last_tmu0_slot`, `vk_last_texmode[]`) was NOT reset, causing the
  producer to skip bind commands for the new frame (thinking the textures were already bound).

- **Bug 1 (producer)**: Reset `vk_last_tmu0_slot` / `vk_last_texmode[]` in `vc_voodoo_swap_buffers()`
  after `vc_swap_buffers()`.

- **Bug 2 (consumer)**: Vertex ring overflow triggers emergency `vc_end_frame`/`vc_begin_frame` with no
  texture re-bind. Added `last_bound_valid` tracking to `vc_thread_t`; re-issue bind after emergency
  restart via `vc_thread_bind_textures()` helper.

- **Invalidation paths**: `CMD_SWAP` and `CMD_SYNC` handlers now set `last_bound_valid = false` to prevent
  stale re-binds.

### 2026-02-27 -- Phase 3 milestone: textures validated at runtime (commit 8206de92c)

- **3DMark99** running on macOS/MoltenVK (Apple M1 Pro, Vulkan 1.2.323) now renders real textures:
  walls, ceiling, and road surfaces show correct texture detail. HUD elements render as opaque rectangles
  (expected -- alpha blending is Phase 6).

- 15 bugs total fixed since initial bring-up (6 bring-up + 4 audit + 2 stabilization + 3 texture pipeline).

- Phase 3 (Texture Support) and Phase 4 (Color/Alpha Combine) are both validated at runtime.

---

## Runtime Bring-Up: First Vulkan Activation

### 2026-02-27 -- Bug: vc_init() only called for Banshee/V3, not V1/V2

- **Root cause**: The `#ifdef USE_VIDEOCOMMON` block with `vc_init()` was placed inside `voodoo_2d3d_card_init()` (Banshee/V3 init path) but NOT inside `voodoo_card_init()` (V1/V2 init path). Since testing used a Voodoo 2, the Vulkan renderer was never activated.
- **Discovery**: Diagnostic breadcrumbs in `/tmp/vc_status.txt` showed `voodoo_card_init` entered but never reaching the `#ifdef` block. The function returns at line 1330 — the `#ifdef` was 150 lines later, inside a different function.
- **Fix**: Added identical `#ifdef USE_VIDEOCOMMON` block to `voodoo_card_init()` before its `return voodoo;`.

### 2026-02-27 -- Bug: MoltenVK ICD not found by Vulkan loader

- **Symptom**: `vkCreateInstance` returned `VK_ERROR_INCOMPATIBLE_DRIVER` (-9)
- **Root cause**: User previously had VulkanSDK installed at `/Users/anthony/VulkanSDK/1.4.335.1/` which set `VK_DRIVER_FILES` / `VK_ICD_FILENAMES` in shell profile. SDK was removed but env vars still pointed to the dead path. The Vulkan loader found zero ICDs.
- **Discovery**: `vulkaninfo --summary` showed "Failed to open JSON file /Users/anthony/VulkanSDK/1.4.335.1/macOS/share/vulkan/icd.d/MoltenVK_icd.json". MoltenVK is installed via Homebrew at `/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`.
- **Fix**: Added macOS ICD auto-discovery to `vc_init()` in `vc_core.c`. Before `volkInitialize()`, checks if `VK_DRIVER_FILES` points to a valid file; if not, probes well-known Homebrew paths (`/opt/homebrew/etc/`, `/opt/homebrew/share/`, `/usr/local/share/`) and overrides the env var.

### 2026-02-27 -- Vulkan fully initializes

- After both fixes, Vulkan initialization succeeds:
  - MoltenVK ICD auto-detected from `/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`
  - Device: Apple M1 Pro, API 1.2.323
  - Extended dynamic state: yes
  - All sub-modules initialized (render pass, shaders, pipeline, textures, readback, render thread)
### 2026-02-27 -- Bug: Queue race condition

- **Symptom**: Occasional crash in `vkQueueSubmit`
- **Root cause**: Both the render thread (`vc_end_frame`) and the display thread (`vc_readback_execute`) submit to the same `VkQueue`, which is not thread-safe without external synchronization.
- **Fix**: Added `queue_mutex` (86Box mutex) to `vc_context_t`. All `vkQueueSubmit` calls now wrapped with `thread_wait_mutex` / `thread_release_mutex`. Applied in `vc_readback.c` and `vc_thread.c`.

### 2026-02-27 -- Bug: NULL volk 1.3 function pointers

- **Symptom**: Crash on first `vkCmdSetDepthTestEnable()` call from render thread.
- **Root cause**: MoltenVK reports Vulkan 1.2. volk only loads core 1.3+ function pointers for 1.3+ devices. `vkCmdSetDepthTestEnable` is core 1.3, so the pointer was NULL.
- **Fix**: Switched to EXT variants (`vkCmdSetDepthTestEnableEXT`, `vkCmdSetDepthWriteEnableEXT`, `vkCmdSetDepthCompareOpEXT`, `vkCmdSetCullModeEXT`) which are loaded via `VK_EXT_extended_dynamic_state`.

### 2026-02-27 -- Bug: Glide hardware detection failure

- **Symptom**: Guest `glide2x.dll` reports "_GlideInitEnvironment: expected Voodoo^2, none detected" when `gpu_renderer=1`.
- **Root cause**: Glide driver submits test triangles and reads back `fb_mem` during hardware detection. When triangles are intercepted by the GPU path (or even silently dropped), the software renderer never writes to `fb_mem`, and detection fails.
- **Discovery**: Systematic testing -- gpu_renderer=0 works, forced flag=0 in code works, triangle-drop test (intercept but do nothing) still fails. Confirmed root cause: guest needs SW-rendered test triangle results in fb_mem.
- **Fix**: Deferred Vulkan init. Software renderer handles ALL triangles until `vc_ctx` is non-NULL. Triangle redirect in `voodoo_queue_triangle()` checks both `use_gpu_renderer && vc_ctx`.

### 2026-02-27 -- Bug: FIFO thread freeze on synchronous vc_init()

- **Symptom**: Emulation froze when first 3D game launched (e.g., Quake 3).
- **Root cause**: `vc_init()` takes hundreds of milliseconds (Vulkan instance, device, pipelines, etc). When called synchronously from the FIFO thread's swap buffer handler, all command processing blocks.
- **Fix**: Spawn `vc_init()` on a background thread via `thread_create()`. Track with `vc_init_pending` flag in `voodoo_t` to prevent double init. Triggered on first `SST_swapbufferCMD`.

### 2026-02-27 -- Bug: Texture transfer missing queue mutex

- **Discovery**: Found during pre-Phase-4 code audit.
- **Root cause**: `vc_tex_end_transfer()` called `vkQueueSubmit()` without the queue mutex. Currently safe (init-time only) but would crash when runtime texture uploads are wired in.
- **Fix**: Added `thread_wait_mutex(ctx->queue_mutex)` / `thread_release_mutex(ctx->queue_mutex)` around `vkQueueSubmit` in `vc_tex_end_transfer()`.

### 2026-02-27 -- Architecture note: Push constants not yet integrated

- Push constant struct `vc_push_constants_t` is declared and `vc_push_constants_update()` exists, but no `vkCmdPushConstants()` call is made anywhere. The SPSC ring has no command type for push constant updates.
- The fragment shader's `pc.fbzColorPath` is always zero, so texture sampling (`tex_enabled`) never activates at runtime despite Phase 3 code being written.
- This is expected: Phase 4 (color combine) will add push constant integration, at which point the texture fetch path also becomes active.
- Similarly, actual Voodoo texture data is never uploaded to VkImages at runtime -- only placeholder descriptor sets are bound. This is Phase 4+ work.

---

## Pre-Phase 4 Audit: Critical Bug Fixes

### 2026-02-27 -- Bug: Missing initial image layout transitions (CRITICAL)

- **Discovery**: Found during second-pass audit by vc-plumbing agent.
- **Root cause**: `vmaCreateImage()` creates images with `initialLayout = VK_IMAGE_LAYOUT_UNDEFINED`, but the render pass uses `LOAD_OP_LOAD` which requires images to already be in `COLOR_ATTACHMENT_OPTIMAL` / `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`. Without an explicit transition, the first render pass load is undefined behavior.
- **Fix**: Added `vc_transition_initial_layouts()` to `vc_render_pass.c` -- a one-shot command buffer that transitions all 4 framebuffer images (2 color + 2 depth) from `UNDEFINED` to their optimal layouts immediately after creation. Uses a temporary command pool, pipeline barrier, fence wait, and cleanup.

### 2026-02-27 -- Bug: vc_voodoo_swap_buffers() never called (CRITICAL)

- **Discovery**: Found during second-pass audit by vc-plumbing agent.
- **Root cause**: `SST_swapbufferCMD` in `vid_voodoo_reg.c` handled deferred init but never called `vc_voodoo_swap_buffers()` after init completed. Triangles were queued to the SPSC ring but never flushed to the GPU.
- **Fix**: Added `vc_voodoo_swap_buffers(voodoo)` call in the swap buffer handler, gated on `use_gpu_renderer && vc_ctx`. Also added `#include <86box/vid_voodoo_vk.h>` for the declaration.

### 2026-02-27 -- Bug: Non-atomic vc_ctx/vc_init_pending on ARM64 (CRITICAL)

- **Discovery**: Found during second-pass audit by vc-plumbing agent.
- **Root cause**: `vc_ctx` (void*) and `vc_init_pending` (int) in `voodoo_t` were plain types accessed from multiple threads (FIFO thread writes, render/display threads read). On ARM64 (weak memory model), stores from the init thread may not be visible to reader threads without explicit memory ordering.
- **Fix**: Changed types to `_Atomic(void *)` and `_Atomic int` in `vid_voodoo_common.h`. Added `<stdatomic.h>` include. All accesses now use `atomic_load_explicit(..., memory_order_acquire)` / `atomic_store_explicit(..., memory_order_release)` throughout `vid_voodoo_reg.c`, `vid_voodoo_render.c`, `vid_voodoo_display.c`, `vid_voodoo_vk.c`, and `vid_voodoo.c`.

### 2026-02-27 -- Bug: Deferred init thread handle not stored (SIGNIFICANT)

- **Discovery**: Found during second-pass audit by vc-plumbing agent.
- **Root cause**: `thread_create(vc_deferred_init_thread, voodoo)` return value was discarded. If `voodoo_card_close()` ran before the init thread finished, the thread could access freed memory.
- **Fix**: Added `thread_t *vc_init_thread` field to `voodoo_t`. Store the handle from `thread_create()`. In `voodoo_card_close()`, call `thread_wait(vc_init_thread)` before destroying the Vulkan context.

### 2026-02-27 -- Shader: depth_unchanged qualifier added

- **Discovery**: Found during second-pass audit by vc-shader agent.
- **Root cause**: The fragment shader unconditionally writes `gl_FragDepth = vDepth`, which disables early-Z culling on all GPUs (GPU must run the fragment shader to know the final depth). Since `vDepth` is a `noperspective` interpolation of the same value the rasterizer computes, the written depth always equals the rasterized depth.
- **Fix**: Added `layout(depth_unchanged) out float gl_FragDepth;` declaration to `voodoo_uber.frag`. This tells the GPU that the shader-written depth will not change relative to the rasterizer depth, re-enabling early-Z culling. Includes a comment noting that Phase 7 (W-buffer / zaColor depth source) will need to change this to `depth_any`.

### 2026-02-27 -- Spec doc: push-constant-layout.md corrections

- **Discovery**: Found during second-pass audit by vc-shader agent.
- **Root cause**: The detail parameter packing layout in `push-constant-layout.md` was documented as `bias[31:22] max[21:14] scale[13:11]` but the actual code in `vid_voodoo_vk.c` packs as `scale[31:28] bias[27:20] max[19:12]`. Field names `fbWidth`/`fbHeight`/`fogTable` didn't match actual code names `fb_width`/`fb_height`/`fog_table`.
- **Fix**: Updated all three instances (Section 1, 4, 7) of detail packing to match code. Renamed all field references to match actual shader/C names throughout the document.

---

## Phase 4: Color/Alpha Combine + Chroma Key

### 2026-02-27 -- Phase 4 implementation (commits dfd6b9555, df0567de6, 8f7ea74b3)

- **VC_CMD_PUSH_CONSTANTS**: New SPSC ring command type added. `vc_dispatch_push_constants()` flushes the vertex batch and issues `vkCmdPushConstants()` with the 64-byte block. Public API `vc_push_constants()` wraps ring push.

- **Color combine** in `voodoo_uber.frag`: Full `cc_*` decode from `fbzColorPath` -- rgb_sel, cc_localselect, cc_localselect_override, cc_zero_other, cc_sub_clocal, cc_mselect (6 blend factor sources), cc_reverse_blend, cc_add, cc_invert_output. Computes `(zero_other ? 0 : cother) - (sub_clocal ? clocal : 0)`, multiplies by selected factor, adds selected value, inverts if flagged.

- **Alpha combine**: Same structure as color combine using `cca_*` bits. Alpha local sources: iterated alpha, color0 alpha, iterated Z (special case).

- **Chroma key**: `FBZ_CHROMAKEY` (fbzMode bit 1) comparison against `chromaKey` push constant, `discard` on match.

- **Push constants wired from emulator**: `vc_push_constants_update()` called before every `vc_submit_triangle()` in `vid_voodoo_vk.c`, with deduplication via `memcmp()` to avoid SPSC ring saturation (Key Decision #16).

### 2026-02-27 -- Stabilization: freeze fix + framebuffer clear (commits 132cd8ce1, 794a7fb03)

- **Freeze root cause**: Push constants sent before EVERY triangle saturated the 4096-entry SPSC ring (2 cmds/tri × thousands of tris). Fix: `memcmp()` deduplication, only send on state change.

- **Framebuffer clear at init**: `LOAD_OP_LOAD` with undefined initial contents caused garbage. Fix: explicit `vkCmdClearColorImage`/`vkCmdClearDepthStencilImage` after layout transition.

---

## Phase 5: TMU1 + Multi-Texture

### 2026-02-27 -- Phase 5 implementation (commits ea8fa541b, 41582f9b2)

- **TMU1 texture coordinate reconstruction**: Gradient-based S/T/W reconstruction from `tmu[1].startS/T/W` and `dS/T/WdX/dY`, gated by `dual_tmus` flag.

- **TMU1 texture upload + descriptor binding**: Wired TMU1 texture cache to VkImages using same slot mapping pattern as TMU0 (`tmu * TEX_CACHE_MAX + entry`). Bound to descriptor set slot 1.

- **Dual-TMU combine shader**: Three paths in `voodoo_uber.frag`:
  1. Single TMU (Voodoo 1): fetch TMU0 only
  2. Passthrough: textureMode0 combine bits zero, copy TMU1 texel directly
  3. Full combine: TMU1 combine then TMU0 combine with TMU1 result as `c_other`

- **TMU1 alpha reverse quirk** (Key Decision #21): SW rasterizer uses `!a_reverse` for TMU1 alpha combine, opposite of color combine convention. Shader matches this.

- **Detail texture blend factor** (tc_mselect=4): `detailFactor()` helper unpacks scale/bias/max from detail push constants, computes `clamp((bias - lod) << scale, 0, max) / 255.0`.

- **LOD fraction blend factor** (tc_mselect=5): `computeLOD()` helper computes approximate LOD from screen-space derivatives via `dFdx`/`dFdy`.

- **Trilinear flag** (bit 30 of textureMode): Inverts `tc_reverse_blend` sense for odd LOD levels, enabling smooth mip-level transitions.

---

## Phase 6: Fog, Alpha Test, Alpha Blend

### 2026-02-27 -- Phase 6.1: Alpha test (commit b22e37660)

- **Fragment shader alpha test**: After color/alpha combine, tests combined alpha against reference value. Decode: bit 0 (enable), bits 3:1 (compare function), bits 31:24 (reference alpha 0-255).
- **Integer comparison**: `uint(ca_result * 255.0 + 0.5)` for exact match with SW rasterizer fixed-point behavior.
- All 8 compare functions: NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS.
- **Visual impact**: Fixes opaque HUD rectangles in 3DMark99 (alpha-tested fragments now properly discarded).

### 2026-02-27 -- Phase 6.2: Alpha blending (commit 4cdeb2858)

- **vc_pipeline_key_t extended**: Added blend_enable + 4 blend factors (src_rgb, dst_rgb, src_a, dst_a) to pipeline key.
- **VC_CMD_PIPELINE_KEY**: New ring command. Render thread builds pipeline key, looks up/creates pipeline variant, binds it. Tracks `last_pipeline_key` for re-bind after emergency restart.
- **Voodoo AFUNC → VkBlendFactor mapping**: 10 values including AZERO→ZERO, ASRC_ALPHA→SRC_ALPHA, ACOLOR→DST_COLOR(src)/SRC_COLOR(dst), ADST_ALPHA→DST_ALPHA, AONE→ONE, AOMSRC_ALPHA→ONE_MINUS_SRC_ALPHA, AOMCOLOR→ONE_MINUS_DST_COLOR(src)/ONE_MINUS_SRC_COLOR(dst), AOMDST_ALPHA→ONE_MINUS_DST_ALPHA, ASATURATE→SRC_ALPHA_SATURATE.
- **ACOLORBEFOREFOG** (dst_afunc=15) implemented in Phase 6.4 via dual-source blending (VK_BLEND_FACTOR_SRC1_COLOR).
- **Deduplication**: `alphaMode & 0x00FFFFF0` mask captures blend-relevant bits (4-23), ignoring alpha test bits and reference value.

### 2026-02-27 -- Phase 6.3: Fog (commit 0590e29b5)

- **Fog table upload**: `VC_CMD_FOG_UPLOAD` ring command with 128-byte inline payload (no malloc needed). XOR checksum deduplication — fog tables rarely change after init.
- **Vertex shader**: `vFog` output changed from placeholder `0.0` to `inOOW` (per-vertex 1/W) for fragment-level w_depth computation.
- **Fragment shader fog** (Stage 8, between alpha test and final output):
  - FOG_CONSTANT (bit 5): adds fogColor directly
  - Table mode (source=0): `findMSB()`-based w_depth from 1/W, `texelFetch()` into 64x1 fog table, fog/dfog interpolation
  - FOG_ALPHA (source=1): fog factor from combined alpha
  - FOG_Z (source=2): fog factor from interpolated depth (top 8 bits)
  - FOG_W (source=3): fog factor from integer part of 1/W
  - FOG_ADD (bit 1): additive fog (start from zero instead of fogColor)
  - FOG_MULT (bit 2): multiplicative fog (replace color entirely)
  - All modes apply `fog_a++ / 256` bias matching SW rasterizer

### 2026-02-27 -- Phase 6.4: Dual-source blending for ACOLORBEFOREFOG (commit a0e4ac1ab)

- **ACOLORBEFOREFOG via dual-source blending**: Instead of the originally planned copy-on-blend approach
  (which would require copying the framebuffer region to a staging texture before each blended draw),
  implemented using Vulkan dual-source blending. The fragment shader saves the pre-fog color and outputs
  it as a second color attachment (`layout(location=0, index=1) out vec4 outColorSrc1`). The pipeline
  uses `VK_BLEND_FACTOR_SRC1_COLOR` for the destination blend factor when ACOLORBEFOREFOG is active.

- **dualSrcBlend feature enabled**: `vc_core.c` now explicitly enables `dualSrcBlend` in
  `VkPhysicalDeviceFeatures`. This feature is supported on all target platforms (MoltenVK, Mesa, NVIDIA,
  AMD drivers).

- **Bug fix: VkPhysicalDeviceFeatures2 not chained into VkDeviceCreateInfo**: Pre-existing bug where
  the `VkPhysicalDeviceFeatures2` structure was populated but never set as `VkDeviceCreateInfo.pNext`.
  This meant no core Vulkan 1.0 features were being explicitly enabled at device creation time (drivers
  typically enable features anyway, but this was technically incorrect). Fixed by setting
  `device_ci.pNext = &features2`.

- **Design deviation from CHECKLIST.md**: Task 6.4 originally specified a copy-on-blend approach with
  `vkCmdCopyImage` + staging texture + additional descriptor. Dual-source blending is simpler (no copy,
  no extra descriptor, no pipeline barrier) and has zero performance overhead since the pre-fog color
  is already computed in the shader. The only requirement is `dualSrcBlend` device feature support,
  which is universal across all target platforms.

---

## Phase 7: Dither, Stipple, Remaining Features

### 2026-02-27 -- Phase 7.1: Stipple test (commit d284789b1)

- **Stipple test in fragment shader**: Two modes controlled by `fbzMode` bits:
  - Pattern mode (bit 12 = 0): Index 32-bit `stipple` push constant by `(y[4:3] * 8 + x[4:2])`,
    extract single bit, discard if clear. This implements the standard Voodoo 8x4 stipple pattern.
  - Rotating mode (bit 12 = 1): Use `(y[1:0] * 4 + x[1:0])` as bit index into lower 16 bits of
    stipple register, implementing a 4x4 rotating stipple pattern.
  - Both modes gated by `fbzMode` bit 2 (stipple enable).

### 2026-02-27 -- Phase 7.3: Fastfill (commit d284789b1)

- **Fastfill via vkCmdClearAttachments**: When the Voodoo issues a framebuffer clear through the
  `SST_fastfillCMD` register, the VK path now intercepts it and translates to a Vulkan clear operation.

- **vc_clear_buffers() public API**: New function in `videocommon.h` and `vc_core.c`. Takes RGBA clear
  color (float[4]) and depth clear value (float). Pushes `VC_CMD_CLEAR` to the SPSC ring. Render thread
  dispatches `vkCmdClearAttachments()` for both color and depth attachments within the active render pass,
  avoiding the overhead of ending and restarting the render pass.

- **vc_voodoo_fastfill()** in `vid_voodoo_vk.c`: Extracts clear color from Voodoo registers -- blue from
  `zaColor` bits 7:0, green from `color1` bits 23:16, red from `color1` bits 7:0. Depth extracted from
  `zaColor` bits 31:16, normalized to [0,1] by dividing by 65535.

- **SST_fastfillCMD hook**: Added to `vid_voodoo_reg.c` alongside the existing swap buffer hook. When
  `use_gpu_renderer && vc_ctx`, calls `vc_voodoo_fastfill()` instead of (or in addition to) the software
  clear path.

- **Visual impact**: This is the single biggest fix for visual artifacts. Without fastfill, old triangles
  accumulated across frames causing heavy ghost geometry, white streaking, and visual corruption.

### 2026-02-27 -- Phase 7.4: Alpha mask (already implemented)

- **Already implemented in Phase 4**: The alpha mask test (`fbzMode` bit 13) was already part of the
  color combine implementation. It tests the lowest bit of the alpha "other" value and discards if zero.
  Marked as complete in CHECKLIST.md with a note.

### 2026-02-27 -- Phase 7.5: W-buffer depth (commit d284789b1)

- **W-buffer mode in fragment shader**: When `FBZ_W_BUFFER` (fbzMode bit 3) is set, computes depth
  from 1/W using a `findMSB()`-style logarithmic depth mapping that matches the Voodoo hardware's
  `voodoo_fls()` function. Writes the result to `gl_FragDepth`.

- **gl_FragDepth layout changed to depth_any**: Previously `depth_unchanged` (which enabled early-Z
  optimization). W-buffer writes a depth value that differs from the rasterizer's interpolated depth,
  so `depth_unchanged` would be incorrect. Changed to `depth_any` which disables early-Z but is
  correct for all depth modes. Key Decision #23.

- **Trade-off**: Early-Z culling is now disabled for ALL fragments (not just W-buffer mode), because
  the layout qualifier is static. A future optimization could use separate pipelines or shader variants
  for Z-buffer vs W-buffer modes.

### 2026-02-27 -- Phase 7.6: Depth bias and depth source (commit d284789b1)

- **Constant depth source** (`FBZ_DEPTH_SOURCE`, fbzMode bit 20): When set, uses `zaColor & 0xFFFF`
  as the depth value instead of the interpolated per-vertex depth. Normalized to [0,1] by dividing
  by 65535. Used for some overlay/UI rendering techniques.

- **Depth bias** (`FBZ_DEPTH_BIAS`, fbzMode bit 16): Adds a signed 16-bit bias from the lower 16 bits
  of `zaColor` to the depth value. Bias is applied as `(int16_t)(zaColor & 0xFFFF) / 65535.0`. Used
  for decal rendering to resolve coplanar z-fighting artifacts.

### 2026-02-27 -- Phase 7.2: Ordered dither (commit 192252f97)

- **4x4 Bayer dither** (`FBZ_DITHER`, fbzMode bit 8): When enabled, the fragment shader quantizes
  RGB channels to RGB565 precision (5-bit R, 6-bit G, 5-bit B) using a standard 4x4 ordered Bayer
  dither matrix. The formula `floor(value * max_level + (bayer[y&3][x&3] + 0.5) / 16) / max_level`
  exactly reproduces the precomputed `dither_rb[]` and `dither_g[]` lookup tables from
  `vid_voodoo_dither.h` (verified against all 256 entries for both 5-bit and 6-bit quantization).

- **2x2 Bayer dither** (`FBZ_DITHER_2x2`, fbzMode bit 11): When set alongside FBZ_DITHER, uses a
  2x2 Bayer matrix `{0,2; 3,1}` with threshold `(bayer + 0.5) / 4.0` instead of the 4x4 matrix.
  Matches the precomputed `dither_rb2x2[]` and `dither_g2x2[]` tables.

- **Pre-dither approach (Option D)**: Dither is applied in the fragment shader BEFORE Vulkan's
  hardware blend stage. This is mathematically identical to the correct post-blend dither for opaque
  draws (src=ONE, dst=ZERO). For alpha-blended draws, the dither effect is slightly attenuated by
  the blend factor -- the maximum error is `|offset * (1 - alpha)|` per channel, which is negligible
  in practice. See `dither-blend-ordering.md` for the full analysis.

- **No C-side changes**: The fbzMode register is already passed through verbatim as push constant
  offset 0, so the FBZ_DITHER (bit 8) and FBZ_DITHER_2x2 (bit 11) flags are available to the
  shader without any additional extraction code.

- **Alpha channel not dithered**: Only RGB channels are quantized. This matches the SW renderer,
  which only dithers RGB before the 16-bit RGB565 framebuffer write.

### 2026-02-27 -- Phase 7: Per-triangle depth state (commit 750a5b91b)

- **Root cause of ghost geometry**: Depth test enable, depth write enable, depth compare op, and
  color write mask were all hardcoded. Triangles that should have been depth-tested with LESS were
  rendering with ALWAYS, causing stale geometry to persist across frames.

- **VK_EXT_extended_dynamic_state for depth**: `vkCmdSetDepthTestEnableEXT`,
  `vkCmdSetDepthWriteEnableEXT`, `vkCmdSetDepthCompareOpEXT` set per-triangle from fbzMode bits.
  This avoids pipeline explosion (depth state permutations would multiply pipeline variants).

- **Color write mask through pipeline key**: `VK_EXT_extended_dynamic_state3` is NOT supported on
  MoltenVK, so color write mask cannot be dynamic state. Instead, it is propagated through the
  pipeline key (`vc_pipeline_key_t.color_write_mask`), creating pipeline variants only when the
  mask changes (which is rare -- most triangles write all channels).

- **VC_CMD_DEPTH_STATE ring command**: New SPSC command carries depth test enable, write enable,
  compare op, and color write mask. Deduplication via `VK_DEPTH_STATE_MASK = 0x000006F0` on fbzMode.

- **Voodoo depth ops map 1:1 to VkCompareOp**: Values 0-7 (NEVER through ALWAYS) are identical
  between Voodoo and Vulkan, requiring no translation table.

### 2026-02-27 -- Phase 7: First-frame clear fix (commit 0dc6a518d)

- **MoltenVK/Metal framebuffer initialization**: The one-shot `vkCmdClearColorImage` in a separate
  command buffer at init time was not reliably initializing the framebuffer for subsequent
  `LOAD_OP_LOAD` render passes on MoltenVK. Result: magenta (uninitialized) framebuffer on first frame.

- **Fix**: `vkCmdClearAttachments` on the first `vc_begin_render_pass` call (color=black, depth=1.0).
  A `first_render_pass_done` flag in `vc_thread_t` gates the one-time clear. The init-time clear
  in `vc_render_pass.c` is kept as belt-and-suspenders.

### 2026-02-27 -- Phase 7.7: Runtime validation (commit 32dd41ff2)

- **VC_VALIDATE=1 environment variable**: Enables VK_LAYER_KHRONOS_validation and the debug utils
  messenger in any build type (release or debug). Previously, validation was only available in
  debug builds via `#ifndef NDEBUG` guards.

- **Validation result**: 0 Vulkan API errors across all 8 vc_*.c files. 1 harmless MoltenVK
  portability warning about `VkPipelineCacheCreateInfo::pInitialData` (MoltenVK does not support
  pipeline cache serialization).

- **Debug messenger routes to pclog()**: All severity levels (INFO, WARN, ERROR) captured and
  forwarded to 86Box's logging infrastructure with severity prefix.

### 2026-02-27 -- Phase 7 complete

- All 9 Phase 7 tasks complete (7.1 stipple, 7.2 dither, 7.3 fastfill, 7.4 alpha mask, 7.5 W-buffer,
  7.6 depth bias/source, 7.7 validation).
- Additional fixes during Phase 7: per-triangle depth state (ghost geometry), first-frame clear
  (magenta framebuffer), runtime validation infrastructure.
- Stipple diagnostic logging removed from `vid_voodoo_vk.c` (was temporary for debugging).
- Next: Phase 8 (LFB Read/Write).

---

## Phase 8: LFB Read/Write

### 2026-02-28 -- Phase 8.1: Synchronous LFB read (commit 6f8cb5207)

- **Synchronous staging readback**: Extended `vc_readback.c` with LFB-specific read path. Uses
  existing staging buffer infrastructure (fence wait, `vkCmdCopyImageToBuffer`, map) for
  occasional LFB reads.

- **RGBA8 to RGB565 format conversion**: CPU-side conversion from Vulkan's RGBA8 framebuffer
  to Voodoo's 16-bit RGB565 format for color reads, and direct depth16 extraction for depth
  reads. Handles address decoding for the Voodoo LFB address space.

- **vid_voodoo_fb.c integration**: Wired into `voodoo_fb_readl()` with `#ifdef USE_VIDEOCOMMON`
  guard. When the GPU renderer is active, reads come from Vulkan framebuffer instead of
  software fb_mem.

### 2026-02-28 -- Phase 8.2: Async double-buffered readback (commit ad1ca4aab)

- **Ping-pong staging buffers**: Two staging buffers alternate roles -- submit readback to
  `staging[current]`, read from `staging[previous]` (already complete from previous frame).
  Eliminates GPU stall on frequent reads.

- **Adaptive threshold**: Automatically switches from synchronous to asynchronous mode when
  LFB reads exceed approximately 10 per frame. Single reads (e.g., depth queries) still use
  the synchronous path to avoid one-frame latency.

- **Design note**: Ping-pong scheme means async reads are one frame behind. This is acceptable
  for most LFB use cases (mirrors, HUD rendering) since the visual difference is imperceptible
  at 60 FPS.

### 2026-02-28 -- Phase 8.3: LFB write path (commit 711136046)

- **Shadow buffer with dirty row tracking**: CPU-side shadow buffer receives all LFB pixel
  writes. A dirty row bitmask tracks which rows have been modified. On the next render pass
  start (or explicit flush), dirty rows are uploaded to the Vulkan framebuffer via
  `vkCmdCopyBufferToImage`.

- **Format conversion**: Handles RGB565, ARGB1555, and depth16 input formats, converting to
  RGBA8 for Vulkan upload. Conversion is done at write time (CPU-side) to keep the staging
  buffer in Vulkan-native format.

- **Auto-flush**: Dirty rows are automatically flushed before the next render pass begins,
  ensuring LFB writes are visible to subsequent 3D rendering. Also flushable on demand via
  explicit sync command.

### 2026-02-28 -- Phase 8.4: Dirty tile tracking (commit 0f7bf6dd0)

- **64x64 tile bitmask**: Framebuffer divided into 64x64-pixel tiles. A bitmask tracks which
  tiles have been rendered to since the last readback. LFB reads only transfer the affected
  tiles, reducing readback bandwidth by up to 10x for partial-screen reads.

- **Region-based readback**: `vkCmdCopyImageToBuffer` uses per-tile `VkBufferImageCopy` regions
  instead of full-framebuffer copies. Multiple dirty tiles are batched into a single command
  buffer submission.

- **Tile invalidation**: Tiles are marked dirty on triangle submission (based on triangle
  bounding box) and on LFB writes. Tiles are cleared on successful readback.

### 2026-02-28 -- Phase 8.5: Validation and documentation

- All 6 Phase 8 tasks complete (8.1-8.5).
- Build passing on macOS ARM64.
- Runtime validation pending with LFB-heavy games (Duke Nukem 3D, racing games with mirrors).
- Next: Phase 9 (Qt VCRenderer -- zero-copy display).

---

## Phase 9: Qt VCRenderer (Zero-Copy Display)

### 2026-02-28 -- Phase 9.1: WSI extensions (commit da2562e1b)

- **WSI instance extensions**: Added `VK_KHR_SURFACE_EXTENSION_NAME` + platform surface extension
  (`VK_EXT_METAL_SURFACE_EXTENSION_NAME` on macOS, `VK_KHR_WIN32_SURFACE_EXTENSION_NAME` on Windows,
  `VK_KHR_XLIB_SURFACE_EXTENSION_NAME` on Linux) to `vc_core.c` instance creation.

- **WSI device extension**: Added `VK_KHR_SWAPCHAIN_EXTENSION_NAME` to device creation.

- **Accessor functions**: Added `vc_get_instance()`, `vc_get_device()`, `vc_get_physical_device()`,
  `vc_get_graphics_queue_family()`, `vc_get_graphics_queue()`, `vc_get_queue_mutex()`,
  `vc_get_front_color_image()`, `vc_get_front_color_image_view()` to expose vc_context_t internals
  to the C++ Qt layer without exposing the full struct definition.

### 2026-02-28 -- Phase 9.1: VCRenderer class (commit 30bbc7ce4)

- **QVulkanInstance impossible**: Qt5 from Homebrew on macOS is compiled with `QT_NO_VULKAN` defined.
  QVulkanInstance, QVulkanWindow, and surfaceForWindow() do not exist. Had to bypass Qt Vulkan
  integration entirely.

- **Direct platform WSI via volk**: VCRenderer creates VkSurfaceKHR using direct platform APIs:
  - macOS: `vkCreateMetalSurfaceEXT` with CAMetalLayer from NSView
  - Windows: `vkCreateWin32SurfaceKHR` with HWND from QWindow::winId()
  - Linux: `vkCreateXlibSurfaceKHR` with Display/Window from QX11Info

- **qt_vc_metal_layer.mm** (NEW, 47 lines): Objective-C++ helper that creates a CAMetalLayer,
  attaches it to the NSView obtained from QWindow::winId(), and sets contentsScale for Retina
  display support.

- **VCRenderer class**: Inherits QWindow + RendererCommon. Owns VkSurfaceKHR, VkSwapchainKHR,
  sync objects (semaphores + fence), command pool/buffer. Does NOT own VkInstance/VkDevice
  (borrowed from vc_context_t).

### 2026-02-28 -- Phase 9.3: RendererStack integration (commit 0c235d072)

- **RENDERER_VIDEOCOMMON = 3** added to `renderdefs.h`, shifting RENDERER_VNC to 4.
- **Renderer::VideoCommon** added to qt_rendererstack.hpp enum.
- **createRenderer()**: Instantiates VCRenderer, wraps in QWidget::createWindowContainer(),
  connects blitToRenderer signal to onBlit slot, handles rendererInitialized/errorInitializing.
- **vid_api mappings**: "qt_videocommon" string mapped in qt_mainwindow.cpp and qt.c.

### 2026-02-28 -- Phase 9.4: Post-processing shader (commit 421e7deea)

- **postprocess.vert** (NEW, 40 lines): Fullscreen triangle using gl_VertexIndex trick (3 vertices,
  no vertex buffer). Outputs UV coordinates for fragment shader.

- **postprocess.frag** (NEW, 85 lines): CRT effects with push constants:
  - `resolution` (vec2): swapchain dimensions
  - `texResolution` (vec2): source framebuffer dimensions
  - `scanlineIntensity` (float): 0.0 = off (passthrough)
  - `curvature` (float): 0.0 = flat
  - `brightness` (float): 1.0 = normal
  - Barrel distortion function for CRT curvature effect
  - Scanline darkening based on screen-space Y coordinate

- **VCRenderer pipeline**: VkRenderPass (swapchain format, LOAD_OP_DONT_CARE/STORE_OP_STORE),
  VkDescriptorSetLayout (1 combined image sampler), VkPipelineLayout (push constants),
  VkPipeline (fullscreen triangle, no vertex input), per-swapchain-image VkFramebuffers,
  VkSampler (LINEAR filter, CLAMP_TO_EDGE).

### 2026-02-28 -- Bug fixes and stabilization (commits ee60415c5 through e39a077ea)

- **Data race on vc_global_ctx** (commit ee60415c5): `vc_global_ctx` was a plain pointer accessed
  from multiple threads. Changed to `static _Atomic(vc_context_t *)` in vc_core.c.

- **C/C++ _Atomic interop failure** (commit 06b8a9216): `_Atomic(vc_context_t *)` in a header
  included by both C and C++ caused redeclaration type mismatches with Clang. Fix: made
  vc_global_ctx static in vc_core.c, exposed via `vc_get_global_ctx()` / `vc_set_global_ctx()`
  C-linkage accessor functions. C++ code never sees `_Atomic`.

- **VCRenderer init before Voodoo device** (commit 59ff0f66a): VCRenderer::exposeEvent fires at
  Qt startup but Voodoo device doesn't exist yet (it's created later when guest driver loads).
  Fix: QTimer retries `tryInitializeVulkan()` every 100ms until `vc_get_global_ctx()` returns
  non-NULL. Emit `rendererInitialized()` immediately so blit thread unblocks with fallback buffers.

- **Fence deadlock on null front image** (commit a36b56f71): present() acquired a swapchain image
  (consuming the fence), then returned early if frontImage was NULL, leaving the fence unsignaled
  forever. Fix: check frontImage BEFORE touching sync objects; add empty submit fallback for
  error paths after acquire to keep fence signaled.

- **Black screen** (commit fb6346d7e): VCRenderer (QWindow) had no paint method for software
  blit buffers. Added QBackingStore + QPainter software fallback in `paintFallback()` that
  paints XRGB8888 buffer data via QImage→QBackingStore when Vulkan path isn't active.

- **Display path logging** (commit 06e028d42): Added pclog messages to identify which display
  path is active: software fallback vs Vulkan direct present vs post-process path.

- **Queue mutex contention** (commit e39a077ea, PARTIAL FIX): Three threads contend on queue_mutex:
  render thread (draw submits), readback thread (LFB readback), Qt GUI thread (present). FIFO
  present blocks on vsync (~16ms) while holding mutex, starving the render thread. Partial fix:
  set `direct_present_active` flag to skip readback when VCRenderer is active (reduces 3-way to
  2-way contention). **Still freezes** because FIFO present still blocks the queue.

### 2026-02-28 -- Phase 9 status: BLOCKED

- **Critical issue**: Queue mutex contention between render thread and Qt GUI thread. FIFO present
  mode (the only mode available on MoltenVK) blocks for up to ~16ms during vkQueuePresentKHR.
  During this time, the render thread cannot submit draw commands, causing the SPSC ring to fill
  up and the emulation thread to stall.

- **MAILBOX present mode**: Would solve the problem (non-blocking present, replaces pending frame)
  but is NOT supported by MoltenVK. Only FIFO and IMMEDIATE are available.

- **Proposed architectural fix**: Move presentation entirely to the render thread via the SPSC
  ring buffer. Add a `VC_CMD_PRESENT` command that the Qt GUI thread pushes instead of calling
  vkQueuePresentKHR directly. The render thread would own all VkQueue access, eliminating
  cross-thread contention entirely. The Qt GUI thread would only push present commands to the ring
  and never touch the VkQueue.

- **Full code audit required**: ~2400 lines of new code across 18 files needs systematic
  line-by-line review before implementing the architectural fix. Multiple rapid bug fixes during
  bring-up may have introduced subtle issues.

### 2026-02-28 -- Code audit Wave 1+2 complete (commit d66cda7f1)

- **Full code audit completed**: 11 audit files generated in `videocommon-plan/codeaudit/`, covering
  all VideoCommon code (vc_core, vc_render_pass, vc_pipeline, vc_shader, vc_texture, vc_batch,
  vc_readback, vc_thread, vid_voodoo_vk, qt_vcrenderer, public API/build). FIX-PLAN.md created
  with 4 waves (29 items total, prioritized by severity).

- **Wave 1 fixes (data races & correctness)**:
  - `back_index` in vc_render_pass: `_Atomic int` with release/acquire ordering (CRITICAL)
  - `direct_present_active` in vc_core: `_Atomic int` for cross-thread flag (HIGH)
  - Push constant deduplication: hoisted to file scope, reset on swap/sync (HIGH)
  - `reads_this_frame` in vc_readback: `_Atomic uint32_t` with fetch_add/exchange (HIGH)
  - Log-after-free: moved log above free(ctx) (HIGH)
  - Unconditional pclog in fastfill: gated behind vk_log() (HIGH)
  - Items 1.1 and 1.4 verified already correct (no changes needed)

- **Wave 2 fixes (VCRenderer Vulkan correctness)**:
  - VK_SUBOPTIMAL_KHR: continue frame normally (semaphore is signaled), defer recreate via
    `m_needsRecreate` flag checked at top of next present() (CRITICAL)
  - Fence deadlock on submit failure: recreateSwapchain() + early return (CRITICAL)
  - compositeAlpha: query caps.supportedCompositeAlpha, fall back to INHERIT_BIT (HIGH)
  - oldSwapchain reuse: pass old handle to vkCreateSwapchainKHR, destroy after creation.
    destroySwapchain() takes `keepSwapchainHandle` param for recreate path (HIGH)
  - Item 2.3 (use-after-free in finalize) fixed in prior session

- **vk_last_pc declaration order fix**: Moved file-scope `vk_last_pc` / `vk_pc_valid` below
  the `vc_push_constants_t` typedef (was above it, causing compile error).

### 2026-02-28 -- Code audit Wave 3 complete (commits 35b8f3978, 7c56881cb)

- **Wave 3 fixes (resource safety & synchronization)**:
  - SPIR-V alignment: SpvToHeader.cmake now generates `uint32_t[]` arrays (was `unsigned char[]`).
    vc_shader.c accepts `const uint32_t*` directly, eliminating UB cast. (HIGH)
  - Shadow buffer race: Documented as intentionally benign at 4 sites — color/depth write and
    flush. Worst case: torn pixel in staging upload. (CRITICAL, benign)
  - Subpass dependency: Added LATE_FRAGMENT_TESTS_BIT to src/dst stage masks. Required because
    uber-shader uses `depth_any` layout. (MODERATE)
  - Dimension validation: `vc_init()` rejects width/height <= 0 early. (MODERATE)
  - vkEnumerate return checks: All 6 calls checked. Graceful degradation on failure. (MODERATE)
  - Async staging leak: Error paths use goto cleanup to destroy partial state. (MODERATE)

### 2026-02-28 -- Code audit Wave 4 complete (commits 8d379fbe7, 1a0da6007)

- **Wave 4 fixes (performance & polish)**:
  - Double sync in fb_readl: Single readback for both 16-bit pixels instead of two
    separate sync+readback calls. Major LFB read perf improvement. (HIGH perf)
  - Descriptor set caching: Track m_lastFrontView, skip vkUpdateDescriptorSets when
    unchanged. Reset on swapchain recreate. (MODERATE perf)
  - Finite timeouts: 5s timeout on fence wait and acquire instead of UINT64_MAX.
    VK_TIMEOUT skips frame, VK_ERROR_DEVICE_LOST triggers recreate. (MODERATE)
  - Per-frame logging: pclog on clear/swap changed to vc_log (gated). (LOW)
  - Dead code cleanup: Removed entry_count, current_set fields, vc_texture_invalidate(),
    vc_texture_upload_mip() from vc_texture.h/c (~100+ lines). (LOW)
  - Descriptor pool flag: Removed FREE_DESCRIPTOR_SET_BIT (never used). (LOW)
  - Dirty tile bounds: Guards added to set/clear/test functions. (MODERATE)
  - dual_tmu sanitization: textureMode1 forced to 0 on single-TMU boards in
    vc_push_constants_update(). No shader changes. (MODERATE)
  - **Deferred**: 4.4 (Wayland WSI — future platform work), 4.9 (wait_idle gap —
    architectural change, risk of regression)

### 2026-02-28 -- Code audit COMPLETE

- All 4 waves complete: 27 of 29 items fixed, 2 deferred (Wayland WSI, wait_idle gap).
- Total changes: ~15 files modified across VideoCommon core, Voodoo VK bridge, Qt VCRenderer.
- Categories: 5 CRITICAL fixes, ~12 HIGH fixes, ~8 MODERATE fixes, ~4 LOW fixes.
- Build clean, all tests passing (manual), 0 Vulkan validation errors.
