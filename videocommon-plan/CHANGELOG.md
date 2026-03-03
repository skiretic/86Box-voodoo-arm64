# VideoCommon v2 -- Changelog

All notable changes to the VideoCommon v2 implementation are documented here.
Format: newest entries first. Each entry includes the phase, what changed, and which agent did the work.

---

## [In Progress] -- Phase 5: Core Pipeline (2026-03-02)

### MILESTONE: Textured 3D with Correct Texture Coords, Blending, Scissor, Texture Combine!
- 3DMark99 race benchmark renders fully: buildings, road, sky, vehicles, HUD transparency
- Stable at 60 Hz, no crashes, no blue diagonal streaks (ring race fixed)
- Texture coordinate scrambling FIXED — noperspective interpolation
- Alpha blending working (speedometer HUD shows transparency)
- Remaining: sky banding, transparency edge cases, fog (Phase 6)

### Implemented (vc-lead, vc-shader)
- **Scissor clipping** (ec116a7b3) — wired Voodoo clipLeft/clipRight/clipLowY/clipHighY to vkCmdSetScissor per-triangle. Added vc_clip_rect_t to ring command (288->300 bytes). Fixes grey bar on left, corruption on bottom-right.
- **Alpha blending** (e7e5a9a39) — pipeline variant cache maps Voodoo alphaMode blend factors to VkBlendFactor. 32-entry linear cache, lazy creation. MoltenVK lacks EDS3 so pipeline variants are mandatory. ACOLORBEFOREFOG (0xF) mapped to ONE for now.
- **Texture combine stage** (5da589dcf) — added textureMode0 bits 12-29 processing to voodoo_uber.frag (~100 lines). Handles tc_zero_other, tc_sub_clocal, tc_mselect (6 cases), tc_reverse_blend, tc_add_clocal/alocal, tc_invert_output. Alpha equivalents (tca_*). Single-TMU only (c_other=0). DETAIL and LOD_FRAC stubbed as 0.
- **Per-triangle push constants** (bf0a2ab39) — each triangle gets its own `vkCmdPushConstants` + `vkCmdDraw`
- **Full color/alpha combine** (6d7651879) — complete fbzColorPath pipeline in uber-shader:
  cc_rgbsel, cc_mselect, cc_add, cc_sub, cc_reverse, cc_invert (+ alpha equivalents)
- **Alpha test** (6d7651879) — 8 compare functions with discard, alphaMode bit extraction
- **Chroma key** (6d7651879) — 8-bit integer RGB comparison against chromaKey register
- **Alpha mask** (6d7651879) — fbzMode bit 13, low-bit test
- **EDS1 dynamic depth state** (6d7651879, f64248f8a) — per-triangle depthTestEnable, depthWriteEnable, depthCompareOp
- **Depth clear fix** (1e3ab6c96) — changed clear from 0.0 to 1.0 (was rejecting all fragments)
- **dirty_line marking** (1e3ab6c96) — readback now sets dirty_line[] so SW display callback blits

### Fixed
- **SPSC ring publish-before-write race** (3fd71c710) — on ARM64 weak memory model, `vc_ring_push` published write_pos before payload was written. GPU thread read garbage (blue diagonal streaks). Fix: split into `vc_ring_reserve()`/`vc_ring_commit()` pattern — reserve writes header but doesn't publish; commit does the release store after payload is filled.
- **NULL function pointer crash** (f64248f8a) — EDS1 functions must use `EXT` suffix on Vulkan 1.2 (volk loads extension variants, not 1.3 core names)
- **Black screen** (1e3ab6c96) — depth clear was 0.0, all fragments failed `depth < 0.0` test
- **Diagnostic logging added** (0723f3496) — fprintf to swap/present path for debugging

### Fixed — display_active feedback loop (e53f7c836, 05ae03693)
- **Root cause**: `vc_display_active` served dual purpose — triangle routing (FIFO thread reads) AND VGA suppression (GPU thread clears on timeout). Clearing for VGA re-enable killed triangle routing → feedback loop. 7 prior commits of bandaids failed to fix it.
- **Fix**: split into two independent flags:
  - `vc_divert_to_gpu` (volatile int on `voodoo_t`) — permanent triangle routing, set once on VK surface create, never cleared until device close
  - `display_owner` (plain int on `vc_display_t`, GPU-thread-only) — VGA vs Voodoo display, toggled by presents and VGA timeout
- **Also fixed** (05ae03693): VGA timeout counter was incrementing per GPU thread iteration (~hundreds/frame) instead of per VGA frame (~60 Hz). Now only counts when `vga_frame_ready` is set.
- Removed bandaid state: `has_presented`, `vga_frames_since_present`, `empty_swap_count`, `has_rendered`
- 8 files changed, net -42 lines
- **Verified**: 57 presents with 0 spurious timeouts, 1 clean VGA timeout on benchmark exit

### Fixed — TMU index bug (9223a2729, 2fd9b9835)
- **Root cause**: Voodoo 2 TMU pipeline flows TMU 1 → TMU 0 → color combine. For single-texture rendering, Glide writes all texture state (tLOD, texBaseAddr, S/T/W gradients) to TMU 1 (chip=0x4). The VK path was hardcoded to read TMU 0 everywhere — which only had the init dummy (texBaseAddr=0x0, tLOD=0x820 = 1x1 mip).
- **Debugging process**: Added fprintf diagnostics at triangle diversion, texture upload, and register write paths. Register write logging confirmed: chip=0x2 (TMU 0) received 1 write, chip=0x4 (TMU 1) received 20,909 tLOD writes with real values.
- **Fix**: `int active_tmu = (params->textureMode[1] & 1) ? 1 : 0` — detect active TMU, pass to texture upload + vertex extraction. Three locations fixed: push_triangle, push_texture caller, extract_vertices.
- **Also fixed** (9223a2729): Texture identity tracking replaced XOR hash (collision-prone) with direct 3-field comparison (slot + base + tLOD + palette_checksum).
- **Result**: First textured 3D output! 3DMark99 race scene shows bridge, sky, HUD with real textures.

### Fixed — Texture coordinate scrambling (cff427c79)
- **Root cause**: texture coordinate varyings used `smooth` (default) interpolation, but Voodoo iterates S/W, T/W, 1/W linearly in screen space with per-pixel perspective divide. GPU hardware perspective correction double-corrected these values, causing mosaic/scrambled textures.
- **Fix**: changed vTexCoord0/vTexCoord1 to `noperspective` in both vertex and fragment shaders. Simplified gl_Position to W=1.0 (all varyings now noperspective, vertices already in NDC).
- **Audit also found**: missing FBZ_PARAM_ADJUST subpixel correction (minor, ~0.5px shimmer — lower priority).

### Known Issues (next)
- Sky banding/smearing (likely fog or multi-pass issue, Phase 6)
- Transparency edge cases (ghostly objects, alpha combine)
- "3DMARK" text overlay smeared (multi-pass alpha)
- Fog not yet implemented (Phase 6)
- ACOLORBEFOREFOG uses VK_BLEND_FACTOR_ONE instead of dual-source (Phase 6)

---

## [Complete] -- Phase 3→4 Bugfixes (2026-03-01 → 2026-03-02)

### Fixed — 4 blockers preventing VK rendering from activating

1. **`voodoo_use_texture()` not called on VK path** (ac4d7b9be)
   - Texture decode was below the VK diversion point in `voodoo_queue_triangle()`
   - Moved texture decode to top of function, before VK path branches off

2. **`display_active_ptr` race** (7a3d8cfad)
   - `ctx->display_active_ptr` was wired after `vc_start_gpu_thread()`, creating a race
   - Fixed: wire before GPU thread starts with proper happens-before ordering

3. **FIFO/GPU thread deadlock** (483f302e4)
   - Triangle pushes didn't wake the GPU thread — it could sleep forever
   - Fix: use `vc_ring_push_and_wake()` for triangles; push VK swap BEFORE
     `voodoo_wait_for_swap_complete()` in double-buffer path

4. **VGA display tick interrupting active Voodoo rendering** (339fa72c9, 5b70030ff, 6e8c1da85)
   - `vc_display_tick` was force-ending Voodoo render passes and presenting VGA frames
     while Voodoo was actively rendering
   - Fix: gate VGA passthrough on `display_active_ptr` — suppress VGA present while Voodoo active
   - Replaced swap_seen flag/countdown with direct `display_active_ptr` check

### Key Lesson
- ALWAYS add diagnostic logging FIRST when debugging — never guess at fixes blindly

---

## [Complete] -- Phase 4: Textures

### Added (vc-shader)
- TMU0 texture upload and sampling via descriptor sets
- Vulkan image management, staging upload, invalidation
- Synchronous readback hack (vkWaitForFences per swap) as interim LFB solution

---

## [Complete] -- Phase 3: Display

### Added
- VGA passthrough display path via Qt VCRenderer
- `vc_display_tick` — display callback integration
- Per-frame VGA command buffers with non-blocking acquire/fence
- `vc_ring_wake` calls for proper GPU thread signaling

### Fixed
- **Phase 3 VGA freeze** — Voodoo driver sends test triangles without a swap command,
  leaving render pass open (`render_pass_active=1` stuck permanently) and blocking VGA present.
  Fix: force-end render pass via `vc_gpu_end_frame()` when VGA frame is pending.
- **Glide detection bug** — `vc_notify_renderer_ready()` was called during VK init, switching
  Qt to VCRenderer too early. VCRenderer's VGA passthrough interfered with `_GlideInitEnvironment`.
  Fix: defer renderer switch to first VC_CMD_SWAP (in vc_thread.c).
- **MoltenVK present mode** — MAILBOX not available, FIFO only. Non-blocking acquire (timeout=0)
  handles this correctly.

### Research (vc-arch)
- `research/phase3-display.md` — Comprehensive display integration research (1388 lines)
- `research/phase2-audit-for-phase3.md` — Phase 2 code audit:
  - Per-triangle push constants not drawn per-triangle (Phase 5 blocker)
  - fogColor byte order mismatch (Phase 6)

---

## [Complete] -- Phase 2: Basic Rendering

### Research (vc-arch)
- `research/phase2-implementation.md` — Comprehensive Vulkan architecture research for Phase 2:
  offscreen framebuffer, graphics pipeline, vertex extraction, SPIR-V CMake integration, MoltenVK considerations.
  Cross-referenced v1 archive docs; flagged Y-axis flip removal (OpenGL→Vulkan), D16→D32_SFLOAT upgrade,
  descriptor set deferral to Phase 4, cull mode NONE.
- `research/phase2-shader-design.md` — Uber-shader design research for Phase 2:
  reviewed v1 uniform mapping (still valid), push constant layout (still valid), perspective correction.
  Studied Dolphin/DuckStation uber-shader patterns. Confirmed Phase 2 `fragColor = v_color` is correct
  for default color combine. Full GLSL shader templates with all I/O declared, pipeline stages stubbed.
- Moved stale `push-constant-layout.md` from plan root — v1 archive already has canonical copy.
- **All v1 archive docs now considered historical reference only. v2 research in `research/` is authoritative.**

### Key Findings
- v1 uniform mapping and push constant layout (64 bytes, 16 fields) remain fully valid for Vulkan
- Y-axis: Vulkan Y-down matches Voodoo — NO flip needed (v1 OpenGL code had a flip)
- Depth format: D32_SFLOAT (v2) vs D16_UNORM (v1) — deliberate upgrade for W-buffer
- Phase 2 pipeline: no descriptors needed, push constants only, viewport+scissor dynamic state
- Ring command size per triangle: 300 bytes (header + push constants + clip rect + 3 vertices, aligned to 304)
- Voodoo setup engine handles face culling — keep VK_CULL_MODE_NONE permanently

---

## [Unreleased] -- Phase 1: Infrastructure

### Added (vc-lead)
- `src/video/videocommon/` directory with `CMakeLists.txt` (OBJECT library target)
- `extra/volk/` — Vendored dynamic Vulkan loader (volk.h + volk.c)
- `extra/VMA/` — Vendored Vulkan Memory Allocator (vk_mem_alloc.h)
- `vc_internal.h` — Ring types, command enum, vc_ctx_t, vc_ring_t, vc_caps_t, logging
- `vc_core.c/h` — Vulkan instance/device creation via volk, queue selection, VMA init via C++ wrapper, capability detection (extended_dynamic_state, push_descriptor, dual_src_blend)
- `vc_thread.c/h` — GPU thread main loop (handles SHUTDOWN + WRAPAROUND), thread lifecycle
- `vc_vma_impl.cpp` — VMA_IMPLEMENTATION (C++ required), extern "C" wrappers
- `src/include/86box/videocommon.h` — Public C11 API (vc_init, vc_destroy, vc_voodoo_init/close)

### Modified (vc-lead)
- `CMakeLists.txt` — Added `option(VIDEOCOMMON ...)` gate
- `src/CMakeLists.txt` — Added `USE_VIDEOCOMMON` compile def + link videocommon
- `src/video/CMakeLists.txt` — Added `add_subdirectory(videocommon)` conditional
- `src/include/86box/vid_voodoo_common.h` — Added `void *vc_ctx` + `int use_gpu_renderer` to `voodoo_t`
- `src/video/vid_voodoo.c` — Added gpu_renderer config, vc_voodoo_init/close calls in both voodoo_card_init and voodoo_2d3d_card_init

### Added (vc-plumbing)
- Full DuckStation-style SPSC ring buffer replacing Phase 1 stubs:
  - Platform counting semaphore abstraction (dispatch_semaphore_t on macOS, CreateSemaphoreW on Windows, sem_t on POSIX)
  - Atomic wake_counter (+2 on wake, -1 on sleep) with semaphore post only when transitioning from negative
  - `vc_ring_push()` with wraparound sentinel and backpressure via `vc_ring_wait_for_space()`
  - `vc_ring_push_and_wake()` for swap/shutdown commands
  - `vc_ring_free_space()` with proper acquire/release memory ordering
  - Platform yield (`sched_yield` on POSIX, `SwitchToThread` on Windows)

### Validated (manual)
- Runtime validation on macOS ARM64 (Apple M1 Pro, MoltenVK 1.2.323):
  - volk loads MoltenVK successfully
  - VkInstance + VkDevice created with Vulkan 1.2
  - All optional capabilities detected: eds=1 eds2=1 eds3=1 push_desc=1 dual_src=1
  - GPU thread starts, enters idle loop, receives VC_CMD_SHUTDOWN, exits cleanly
  - No Vulkan validation errors, no crashes
  - SW fallback works when Vulkan unavailable (tested with missing ICD)

---

## Planning Phase (2026-03-01)

### Added
- `DESIGN.md` -- Full architecture design document (12 sections)
- `PHASES.md` -- 8-phase implementation plan with success criteria
- `LESSONS.md` -- v1 post-mortem (5 bugs, root cause analysis)
- `research/emulator-gpu-threading.md` -- Dolphin/PCSX2/DuckStation threading analysis
- `research/voodoo-swap-lifecycle-audit.md` -- swap_count/swap_pending flow audit
- Updated vc-* agent definitions for v2 architecture
- Archived v1 code at tag `videocommon-v1-archive`, reset branch to master

### Key Decisions
- SPSC ring buffer (8MB, variable-size) instead of v1's command queue
- GPU thread owns ALL Vulkan objects (no cross-thread sharing)
- DO NOT touch swap_count/swap_pending -- existing Voodoo mechanism is correct
- DuckStation-style wake counter + semaphore for GPU thread sleep
- Shadow buffer for LFB readback (no sync push from CPU thread)
