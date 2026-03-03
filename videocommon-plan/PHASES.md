# VideoCommon v2 -- Implementation Phases

**Date**: 2026-03-01
**Companion**: `DESIGN.md` (architecture), `LESSONS.md` (post-mortem)

Each phase is independently testable. Do not skip phases or combine them.
After each phase, the emulator must build, run, and boot to Windows desktop
with the SW fallback still working.

---

## Phase 1: Infrastructure ✅ COMPLETE

**Goal**: Vulkan instance/device creation, GPU thread, SPSC ring, CMake integration.
No rendering -- just proof that the GPU thread starts, communicates, and shuts down.

### Creates

| File | Description |
|------|-------------|
| `src/video/videocommon/CMakeLists.txt` | OBJECT library, volk/VMA integration |
| `src/video/videocommon/vc_core.c/h` | Vulkan instance, device selection, VMA init, logging |
| `src/video/videocommon/vc_thread.c/h` | GPU thread, SPSC ring buffer, wake mechanism |
| `src/video/videocommon/vc_internal.h` | Shared internal defines, struct forward declarations |
| `src/include/86box/videocommon.h` | Public C11 API header with no-op stubs |
| `extra/volk/` | Vendored volk (dynamic Vulkan loader) |
| `extra/VMA/` | Vendored Vulkan Memory Allocator |

### Modifies

| File | Change |
|------|--------|
| `src/video/CMakeLists.txt` | `if(VIDEOCOMMON) add_subdirectory(videocommon)` |
| `CMakeLists.txt` | `option(VIDEOCOMMON ...)`, link videocommon to 86Box |
| `src/include/86box/vid_voodoo_common.h` | Add `void *vc_ctx; int use_gpu_renderer;` to voodoo_t |
| `src/video/vid_voodoo.c` | Add `vc_voodoo_init()`/`vc_voodoo_close()` calls, gated by config |

### Implementation Details

**SPSC ring buffer** (`vc_thread.c`):
- 8 MB buffer, `mmap` or `malloc` + page-aligned
- Atomic `read_pos` / `write_pos` (uint32_t, acquire/release)
- DuckStation-style wake counter + semaphore
- `vc_ring_push()`, `vc_ring_push_and_wake()` (only two variants -- no sync push, see DESIGN.md section 4.4)
- `vc_ring_wait_for_space()` with spin-yield backpressure
- Wraparound sentinel command

**GPU thread** (`vc_thread.c`):
- `vc_gpu_thread_main()`: init Vulkan, enter main loop, cleanup on exit
- Main loop: check ring, process commands, sleep when empty
- Only command recognized in Phase 1: `VC_CMD_SHUTDOWN`
- Thread started in `vc_voodoo_init()`, joined in `vc_voodoo_close()`

**Vulkan init** (`vc_core.c`):
- `volkInitialize()` to load Vulkan loader
- `vkCreateInstance()` with app info, validation layers (if VC_VALIDATE=1)
- Physical device enumeration and selection (prefer discrete)
- `vkCreateDevice()` with single graphics queue
- `vmaCreateAllocator()` for memory management
- Capability detection: extended_dynamic_state, push_descriptor, etc.

### Test

1. Build with `cmake -D VIDEOCOMMON=ON`
2. Boot to Windows desktop with Voodoo 2 card (SW renderer)
3. Check log for "VideoCommon: Vulkan 1.2 initialized" message
4. Check log for "VideoCommon: GPU thread started" message
5. Close emulator, check log for "VideoCommon: GPU thread exited" (clean shutdown)
6. Verify no Vulkan validation errors (VC_VALIDATE=1)

### Success Criteria

- [ ] Clean compile on macOS ARM64
- [ ] volk loads MoltenVK successfully
- [ ] VkInstance and VkDevice created
- [ ] GPU thread starts and enters sleep loop
- [ ] VC_CMD_SHUTDOWN causes clean thread exit
- [ ] SW fallback works (no visual regression)

---

## Phase 2: Basic Rendering ✅ COMPLETE

**Goal**: Flat-shaded triangles rendered on the GPU. No textures, no blending,
no depth test. Just colored triangles on a black background.

### Creates

| File | Description |
|------|-------------|
| `src/video/videocommon/vc_render_pass.c/h` | Offscreen framebuffer (color + depth), render pass |
| `src/video/videocommon/vc_pipeline.c/h` | Graphics pipeline, push constants, dynamic state |
| `src/video/videocommon/vc_shader.c/h` | SPIR-V loading, shader module creation |
| `src/video/videocommon/vc_batch.c/h` | Vertex buffer, triangle batching, draw submission |
| `src/video/vid_voodoo_vk.c` | Bridge: voodoo_params_t -> vertices + ring commands |
| `shaders/voodoo_uber.vert` | Vertex shader (pass-through screen-space) |
| `shaders/voodoo_uber.frag` | Fragment shader (iterated color only, Phase 2) |

### Modifies

| File | Change |
|------|--------|
| `src/video/vid_voodoo_render.c` | If `use_gpu_renderer`: call `voodoo_vk_push_triangle()` instead of SW dispatch |
| `src/video/vid_voodoo_reg.c` | If `use_gpu_renderer`: push `VC_CMD_SWAP` after SST_swapbufferCMD processing |

### Implementation Details

**Vertex extraction** (`vid_voodoo_vk.c`):
- Read `voodoo_params_t` fields: `vA`, `vB`, `vC` (screen-space XY)
- Read iterated color: `startR/G/B/A`, `dRdX/dY`, etc.
- Convert to `vc_vertex_t` format
- Push `VC_CMD_TRIANGLE` to ring

**Offscreen framebuffer** (`vc_render_pass.c`):
- VkImage (R8G8B8A8_UNORM, 640x480 initially)
- VkImage (D32_SFLOAT)
- VkFramebuffer, VkRenderPass
- First-frame clear via vkCmdClearAttachments

**Graphics pipeline** (`vc_pipeline.c`):
- Load SPIR-V from embedded data
- Vertex input: position (float4), color (float4)
- No blending, no depth test (Phase 2 only)
- Push constants: fbzMode, fbzColorPath (just enough for color selection)

**GPU thread additions** (`vc_thread.c`):
- Handle VC_CMD_TRIANGLE: write vertices to per-frame buffer, record vkCmdDraw
- Handle VC_CMD_SWAP: end render pass, submit, present (stub -- no swapchain yet)
- Frame resource management: triple-buffered cmd pool + fence

### Research

Phase 2 research docs (authoritative, replaces v1 archive):
- `research/phase2-implementation.md` -- Vulkan architecture for Phase 2
- `research/phase2-shader-design.md` -- Uber-shader design for Phase 2

### Test

**Primary test card**: Voodoo 2 (gpu_renderer=1). Once passing, also test on Voodoo 3/Banshee.
All Voodoo cards (V1, V2, Banshee, V3) are in scope but V2 is the development target.

Test VMs:
- **Voodoo 2**: "v2 test" (`gpu_renderer=1`, config section `[3Dfx Voodoo Graphics]`)
- **Voodoo 3**: "Windows 98 Low End copy" (needs `gpu_renderer = 1` added to `[3Dfx Voodoo3]` section in 86box.cfg before Phase 4 testing)

1. Run 3DMark99 on Voodoo 2 (gpu_renderer=1)
2. Flat-colored triangles should appear (rendered to offscreen FB)
3. No display output yet (Phase 3 adds swapchain) -- verify via RenderDoc capture
   or Vulkan validation that draws are submitted
4. Verify SW renderer still works (gpu_renderer=0)
5. Boot with Voodoo 3 (gpu_renderer=1) -- verify no crash

### Success Criteria

- [ ] VC_CMD_TRIANGLE flows from FIFO thread through ring to GPU thread
- [ ] Vertices correctly extracted from voodoo_params_t
- [ ] vkCmdDraw records successfully (validation clean)
- [ ] Render pass begins and ends without errors
- [ ] No crash, no hang, clean shutdown
- [ ] swap_count operates correctly (display callback unchanged)
- [ ] Works on both Voodoo 2 and Voodoo 3 (same code path)

---

## Phase 3: Display ✅ COMPLETE

**Goal**: Rendered frames visible in the 86Box window. Swapchain creation,
post-processing blit, present, display callback skip.

### Creates

| File | Description |
|------|-------------|
| `src/video/videocommon/vc_display.c/h` | Swapchain, post-process pipeline, present |
| `src/qt/qt_vcrenderer.cpp/hpp` | VCRenderer: surface creation, atomic handoff to GPU thread |
| `shaders/postprocess.vert` | Fullscreen triangle vertex shader |
| `shaders/postprocess.frag` | Sample offscreen FB, output to swapchain |

### Modifies

| File | Change |
|------|--------|
| `src/video/vid_voodoo_display.c` | Skip scanout section when `use_gpu_renderer` |
| `src/qt/qt_rendererstack.cpp` | Add VCRenderer option to renderer selection |

### Implementation Details

**Swapchain** (`vc_display.c`):
- Created by GPU thread when surface handle is set
- 3 images, B8G8R8A8_SRGB, FIFO present mode
- Recreated on VK_ERROR_OUT_OF_DATE (resize)
- Destroyed by GPU thread on teardown request

**Post-process blit** (`vc_display.c`):
- Separate render pass: swapchain image as color attachment
- Fullscreen triangle (no vertex buffer, use gl_VertexIndex)
- Fragment shader samples offscreen color image
- Nearest-neighbor filter (pixel-perfect)

**VCRenderer** (`qt_vcrenderer.cpp`):
- ~300 lines, surface creation only
- `initialize()`: create VkSurfaceKHR, pass to `vc_display_set_surface()`
- `finalize()`: request teardown, wait for GPU thread, destroy surface
- No Vulkan drawing code (GPU thread does everything)

**Display callback skip** (`vid_voodoo_display.c`):
- In `voodoo_callback()`, add `!voodoo->use_gpu_renderer` to BOTH `VGA_PASS`
  conditional blocks. A single `goto skip_scanout` will NOT work because
  the per-scanline pixel drawing and `svga_doblit` are in separate conditional
  blocks with swap/retrace code between them (see DESIGN.md section 5.6).
- Insertion point 1: the per-scanline pixel output block (line < v_disp).
  Change `if (fbiInit0 & FBIINIT0_VGA_PASS)` to
  `if ((fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->use_gpu_renderer)`.
- Insertion point 2: the `svga_doblit` trigger block (line == v_disp).
  Same guard: `if ((fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->use_gpu_renderer)`.
- Swap completion, retrace timing, swap_pending, and wake_fifo_thread code
  between these blocks is UNCHANGED and always runs.

### Test

1. Run 3DMark99, Voodoo 2, gpu_renderer=1
2. Flat-colored triangles visible in the 86Box window
3. Frames update on swap (not frozen)
4. Resize window -- swapchain recreates, no crash
5. Close emulator -- clean shutdown (no Vulkan object leaks)
6. Verify swap_count decrements correctly (guest does not stall)

### Success Criteria

- [ ] Swapchain created from Qt surface
- [ ] Post-process blit renders offscreen to window
- [ ] Present succeeds (frames visible)
- [ ] Display callback retrace timing unchanged
- [ ] swap_count lifecycle correct (guest-visible)
- [ ] Window resize works
- [ ] Clean shutdown (no leaks, no device lost)
- [ ] SW renderer still works as fallback

---

## Phase 4: Textures ✅ COMPLETE

**Goal**: Textured triangles. TMU0 texture upload, sampling, descriptor sets.

### Creates

| File | Description |
|------|-------------|
| `src/video/videocommon/vc_texture.c/h` | VkImage per texture slot, staging upload, sampler cache |

### Modifies

| File | Change |
|------|--------|
| `src/video/vid_voodoo_vk.c` | `voodoo_vk_push_texture()`: detect texture change, copy pixels, push upload/bind |
| `shaders/voodoo_uber.frag` | Add texture sampling (TMU0), basic texture combine |
| `src/video/videocommon/vc_pipeline.c` | Add descriptor set layout (sampler binding), update pipeline layout |
| `src/video/videocommon/vc_thread.c` | Handle VC_CMD_TEXTURE_UPLOAD, VC_CMD_TEXTURE_BIND |

### Implementation Details

**Texture upload** (`vc_texture.c`):
- 128 texture slots (2 TMUs x 64 cache entries)
- Per-slot VkImage (BGRA8, up to 256x256)
- Staging buffer (host-visible, per-frame)
- Upload: memcpy to staging, vkCmdCopyBufferToImage (outside render pass)

**Texture binding**:
- VkSampler created from Voodoo textureMode (wrap, filter)
- Per-frame VkDescriptorPool, allocated descriptor set per draw batch
- Set 0, binding 0 = TMU0 combined image sampler

**Texture identity tracking** (`vid_voodoo_vk.c`):
- Track `tex_addr[tmu]` + `tex_pal[tmu]` + `tex_lod[tmu]`
- On change: decode VRAM to BGRA8 (existing `voodoo_use_texture()` path)
- Copy decoded data (malloc), push VC_CMD_TEXTURE_UPLOAD with pointer
- Push VC_CMD_TEXTURE_BIND with slot + sampler params
- GPU thread frees data pointer after upload

**Texture cache refcount** (critical, v1 lesson):
- `voodoo_use_texture()` increments `refcount` on every call
- VK path must always increment `refcount_r[0]` to match (not just on change)
- Eviction check: `refcount == refcount_r[0]`

### Test

1. Run 3DMark99, Voodoo 2, gpu_renderer=1
2. Textured geometry visible (walls, floors, objects)
3. Texture changes (new levels/scenes) load correctly
4. No texture corruption, no missing textures
5. Verify texture cache does not leak (refcount balanced)

### Success Criteria

- [ ] TMU0 textures sampled correctly
- [ ] Texture upload via staging buffer (no validation errors)
- [ ] Descriptor sets allocated and bound per batch
- [ ] Texture identity tracking prevents redundant uploads
- [ ] Texture refcount balanced (no eviction stalls)
- [ ] Correct wrap/filter modes

---

## Phase 5: Core Pipeline Features ⏳ IN PROGRESS (90%)

**Goal**: Alpha test, alpha blending, depth test (Z and W buffer), fog, scissor.
This is where the rendered output starts looking correct.

### Modifies

| File | Change |
|------|--------|
| `shaders/voodoo_uber.frag` | Alpha test (8 compare funcs), texture combine (tc_*), fog (4 modes), chroma key |
| `src/video/videocommon/vc_pipeline.c` | Pipeline variant cache for blend state, depth state via EDS1 dynamic state |
| `src/video/vid_voodoo_vk.c` | Extract blend/depth/fog/scissor state from fbzMode, alphaMode, fogMode, clip regs |
| `src/video/videocommon/vc_texture.c` | Fog table upload (64x1 sampler2D) |
| `src/video/videocommon/vc_thread.c` | SPSC ring reserve/commit pattern (ARM64 race fix) |

### Sub-phases

**5.1 Depth Test** ✅:
- Dynamic state: depthTestEnable, depthWriteEnable, depthCompareOp (from fbzMode)
- Z-buffer (16-bit linear) and W-buffer (logarithmic, `depth_any` layout)
- Push constant: depth source (iterated vs zaColor)

**5.2 Alpha Test** ✅:
- Fragment shader: 8 compare functions (NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS)
- Push constant: alpha reference value, compare function
- discard on failure

**5.3 Alpha Blending** ✅:
- Pipeline variant cache with VkBlendFactor mapping from Voodoo src_afunc/dest_afunc
- 32-entry linear cache, lazy pipeline creation. Real games use 5-15 unique blend configs.
- MoltenVK does NOT support EDS3 — pipeline variants are mandatory (not optional)
- ACOLORBEFOREFOG (0xF) mapped to VK_BLEND_FACTOR_ONE as interim (dual-source deferred to Phase 6)

**5.4 Fog** (not yet implemented — deferred to Phase 6):
- Fog table: 64 entries, uploaded as 64x1 R32_SFLOAT sampler
- Fog modes: table (Z or W indexed), vertex, alpha
- Push constant: fogColor, fogMode bits
- ACOLORBEFOREFOG: dual-source blending (VK_BLEND_FACTOR_SRC1_COLOR)

**5.5 Scissor** ✅:
- Dynamic scissor rect from clipLeftRight/clipLowYHighY registers
- Per-triangle clip rect (vc_clip_rect_t) added to ring command (288->300 bytes)
- Applied via vkCmdSetScissor (always dynamic)

**5.6 Texture Combine** ✅:
- textureMode0 bits 12-29: tc_zero_other, tc_sub_clocal, tc_mselect (6 cases), tc_reverse_blend, tc_add_clocal/alocal, tc_invert_output
- Alpha equivalents (tca_*) implemented
- Single-TMU only (c_other=0), DETAIL and LOD_FRAC stubbed as 0

**5.7 SPSC Ring ARM64 Race Fix** ✅:
- Split vc_ring_push into vc_ring_reserve/vc_ring_commit pattern
- On ARM64, payload must be written before write_pos is published (release store)
- Old code could publish write_pos before payload, causing GPU thread to read garbage

### Test

1. Run 3DMark99 through all tests
2. Compare with SW renderer: depth ordering correct, transparent objects visible
3. Fog effect visible in foggy scenes
4. No ghost geometry (depth test working)
5. No z-fighting beyond what SW renderer shows

### Success Criteria

- [x] Depth test prevents incorrect overdraw
- [x] Alpha test culls transparent fragments
- [x] Alpha blending produces correct transparency
- [ ] Fog fades distant geometry (deferred to Phase 6)
- [x] Scissor clips correctly
- [x] Texture coordinate scrambling resolved (noperspective fix, cff427c79)
- [ ] 0 Vulkan validation errors (not yet verified with VC_VALIDATE=1)
- [ ] No swap_count regression during full 3DMark99 run with depth/blend/fog enabled

---

## Phase 6: Advanced Features

**Goal**: TMU1, texture combine, dither, stipple, color combine, depth bias.

### Sub-phases

**6.1 TMU1**:
- Second texture unit: coordinates, sampling, combine
- Descriptor set: binding 1 = TMU1 combined image sampler
- Push constant: textureMode[1]
- Detail blend, LOD blend, trilinear between TMU0 and TMU1

**6.2 Texture Combine** (partially done in Phase 5):
- Single-TMU texture combine stage done in Phase 5.6 (tc_zero_other, tc_sub_clocal, tc_mselect, tc_reverse_blend, tc_add_clocal/alocal, tc_invert_output + alpha equivalents)
- Remaining: multi-TMU combine (c_other from TMU1), DETAIL blend, LOD_FRAC
- Push constant bits for combine mode selection

**6.3 Color Combine** ✅ (completed in Phase 5, commit 6d7651879):
- fbzColorPath: cc_rgbselect, cc_aselect, cc_localselect, cc_mselect
- Iterated color, texture color, c_other, a_other combinations

**6.4 Dither**:
- 4x4 and 2x2 ordered Bayer dither matrices
- RGB565 quantization in fragment shader
- Push constant: dither enable, dither type

**6.5 Stipple**:
- 32-bit rotating stipple pattern
- Line/column mask from screen-space position
- Push constant: stipple pattern word

**6.6 Depth Bias / Source**:
- Depth from zaColor (lower 16 bits) when depth_source = 1
- Push constant: depth bias value, depth source flag

**6.7 Fastfill (VC_CMD_CLEAR)**:
- Bridge `fastfillCMD` -> `VC_CMD_CLEAR` for Glide games that use hardware
  clear instead of triangle-based clears (Quake 2, Unreal, etc.)
- In `vid_voodoo_reg.c`: if `use_gpu_renderer`, push `VC_CMD_CLEAR` with
  color, depth values, and which-buffers mask after existing fastfill processing
- GPU thread: `vkCmdClearAttachments` within the active render pass
- 3DMark99 does NOT use fastfill (clears via triangles), so other Glide
  games are needed for testing this path

### Test

1. Run 3DMark99, inspect multi-textured surfaces
2. Run other Voodoo games (Quake 2, Unreal) for broader coverage
3. Dithering visible on color gradients
4. TMU1 effects (lightmaps, detail textures) render correctly

### Success Criteria

- [ ] TMU1 textures sample and combine correctly
- [ ] Color combine modes match SW renderer
- [ ] Dither pattern visible and correct
- [ ] Stipple masking works
- [ ] All 3DMark99 tests render without major artifacts

---

## Phase 7: LFB Access

**Goal**: Linear framebuffer read and write through the Vulkan path.

### Creates

| File | Description |
|------|-------------|
| `src/video/videocommon/vc_readback.c/h` | Sync/async readback, shadow buffer, dirty tiles |

### Modifies

| File | Change |
|------|--------|
| `src/video/vid_voodoo_fb.c` | VK mode: read from staging/shadow, write to shadow |
| `src/video/vid_voodoo_vk.c` | LFB flush before draw/swap |

### Sub-phases

**7.1 Shadow Buffer Readback**:
- GPU thread maintains a host-visible shadow buffer, updated via
  `vkCmdCopyImageToBuffer` at each `VC_CMD_SWAP` (after render, before present)
- LFB reads (`voodoo_fb_readl`) return directly from the shadow buffer -- NO
  push to SPSC ring from the CPU thread. This avoids violating the SPSC
  single-producer invariant (only the FIFO thread may produce ring commands)
  and avoids blocking the CPU thread (which would stall CMDFIFO writes and
  indirectly freeze the FIFO thread -- the exact v1 failure mode)
- Shadow buffer is double-buffered (ping-pong): GPU writes to buffer A while
  CPU reads from buffer B, swap at each frame boundary
- Format conversion (RGBA8 -> RGB565/D16) happens on the CPU side at read time
- Voodoo LFB address decode (format, buffer select) unchanged

**7.2 Shadow Buffer Invalidation**:
- Shadow buffer is invalidated by draw commands and LFB writes
- Stale reads (between shadow updates) return data from the previous frame,
  which is acceptable -- games that read LFB mid-frame are rare
- Adaptive: if reads exceed a threshold per frame, the GPU thread copies
  more frequently (e.g., at batch breaks) to reduce staleness

**7.3 LFB Write**:
- Shadow buffer (CPU-writable)
- Per-row dirty tracking (bitmask)
- Auto-flush: push VC_CMD_LFB_WRITE before draw/swap if dirty

**7.4 Dirty Tile Tracking**:
- 64x64 tiles, 16x16 bitmask
- Region-based readback (only changed tiles)

### Test

1. Games that read LFB (screen captures, post-processing effects)
2. Games that write LFB (software text rendering, cursor overlay)
3. LFB reads return correct pixel values
4. LFB writes visible in rendered output

### Success Criteria

- [ ] LFB read returns correct framebuffer contents
- [ ] LFB write modifies visible output
- [ ] Dirty tracking limits transfer bandwidth
- [ ] No stalls beyond acceptable latency (~1 frame for async)
- [ ] Sync readback does not cause swap_count accumulation or FIFO thread stall

---

## Phase 8: Polish

**Goal**: Validation, visual comparison, performance, Banshee/V3 support.

### Sub-phases

**8.1 Validation**:
- Run full 3DMark99 suite with VC_VALIDATE=1
- Zero Vulkan validation errors
- No device lost, no hangs

**8.2 Visual Comparison**:
- Screenshot SW renderer and VK renderer side-by-side
- Document any visual differences
- Fix rendering bugs found

**8.3 Performance**:
- Benchmark 3DMark99: measure FPS with SW vs VK
- Profile GPU thread: identify bottlenecks
- Optimize batch sizes, reduce pipeline switches
- Target: VK path at least as fast as SW path (ideally 2-3x)

**8.4 Banshee / Voodoo 3**:
- Extend vid_voodoo_vk.c for Banshee register differences
- leftOverlayBuf swap path
- SVGA vsync callback integration
- 2D blitter operations (future phase, may defer)

**8.5 Platform Testing**:
- macOS (MoltenVK) -- primary development platform
- Windows (native Vulkan) -- verify volk loads vulkan-1.dll
- Linux (Mesa) -- verify xcb surface creation
- Pi 5 (V3DV) -- verify Vulkan 1.2 feature subset works

### Success Criteria

- [ ] 0 validation errors across all tests
- [ ] Visual output matches SW renderer (within dithering tolerance)
- [ ] No performance regression vs SW path
- [ ] Banshee/V3 basic rendering works
- [ ] Builds and runs on all 4 target platforms
- [ ] swap_count never reaches 3 during sustained rendering (verified via diagnostic logging)

---

## Phase Dependencies

```
Phase 1 (Infrastructure)
   |
   v
Phase 2 (Basic Rendering)
   |
   v
Phase 3 (Display)
   |
   +---> Phase 4 (Textures) ---> Phase 6 (Advanced Features)
   |                                      |
   +---> Phase 5 (Core Pipeline) --------+---> Phase 8 (Polish)
   |                                      |
   +---> Phase 7 (LFB Access) -----------+
```

Phases 4, 5, and 7 can be developed in parallel after Phase 3 is complete.
Phase 6 requires Phase 4 (textures) and benefits from Phase 5 (blending).
Phase 8 requires all other phases.

---

## Estimated Effort

| Phase | Files | Est. Lines | Agent |
|-------|-------|-----------|-------|
| 1. Infrastructure | 8 new, 4 modified | ~1500 | vc-lead |
| 2. Basic Rendering | 6 new, 2 modified | ~2000 | vc-lead + vc-shader |
| 3. Display | 4 new, 2 modified | ~1200 | vc-plumbing |
| 4. Textures | 1 new, 3 modified | ~800 | vc-shader |
| 5. Core Pipeline | 0 new, 4 modified | ~1000 | vc-shader |
| 6. Advanced Features | 0 new, 4 modified | ~800 | vc-shader (6.1-6.6), vc-lead (6.7 fastfill) |
| 7. LFB Access | 1 new, 2 modified | ~600 | vc-plumbing |
| 8. Polish | 0 new, varies | ~500 | vc-debug + all |
| **Total** | **~20 new** | **~8400** | |
