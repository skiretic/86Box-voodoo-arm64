# VideoCommon v2 -- Executive Summary & Status

**Branch**: `videocommon-voodoo`
**Target**: GPU-accelerated Voodoo rendering via Vulkan 1.2
**Last Updated**: 2026-03-03

---

## Current Status: Phase 5 COMPLETE — Vulkan Validation CLEAN!

Phase 5 core pipeline is complete. **3DMark99 race benchmark renders textured 3D at 60 Hz** — buildings, road, sky, vehicles, HUD transparency all visible. Texture coordinate scrambling FIXED (noperspective interpolation). Alpha blending via pipeline variant cache, scissor clipping, and texture combine stage all implemented. SPSC ring race condition on ARM64 fixed. **Vulkan validation is fully clean** — zero errors during rendering. Fog deferred to Phase 6.

**Target hardware**: All Voodoo cards (V1, V2, Banshee, V3). Testing order: Voodoo 2 first, then Voodoo 3/Banshee.

---

## Phase Progress

```
Phase 1: Infrastructure     [XXXXXXXXXX] 100% COMPLETE
Phase 2: Basic Rendering     [XXXXXXXXXX] 100% COMPLETE
Phase 3: Display             [XXXXXXXXXX] 100% COMPLETE
Phase 4: Textures            [XXXXXXXXXX] 100% COMPLETE
Phase 5: Core Pipeline       [XXXXXXXXXX] 100% COMPLETE (validation clean)
Phase 6: Advanced Features   [..........] 0%   READY
Phase 7: LFB Access          [XX........] 20%  Readback hack in place
Phase 8: Polish              [..........] 0%   BLOCKED (All)
──────────────────────────────────────────────
Overall                      [XXXXXXX...] 68%
```

### Phase 5 Sub-task Status

| # | Task | Status | Commit |
|---|------|--------|--------|
| 5.0 | Per-triangle push constants | X | bf0a2ab39 |
| 5.1 | Depth test (EDS1 dynamic state) | X | 6d7651879, f64248f8a |
| 5.2 | Alpha test (shader) | X | 6d7651879 |
| 5.3 | Color/alpha combine (shader) | X | 6d7651879 |
| 5.4 | Chroma key (shader) | X | 6d7651879 |
| 5.5 | Depth clear fix (0.0→1.0) | X | 1e3ab6c96 |
| 5.6 | dirty_line marking for readback | X | 1e3ab6c96 |
| 5.7 | Texture rendering — TMU index fix | X | 9223a2729, 2fd9b9835 |
| 5.8 | display_active redesign (freeze on exit) | X | e53f7c836, 05ae03693 |
| 5.9 | Alpha blending (pipeline variants) | X | e7e5a9a39 |
| 5.10 | Scissor (clip rect wiring) | X | ec116a7b3 |
| 5.11 | Texture combine stage (textureMode bits 12-29) | X | 5da589dcf |
| 5.12 | SPSC ring ARM64 race fix (reserve/commit) | X | 3fd71c710 |
| 5.13 | Texture coord fix (noperspective interpolation) | X | cff427c79 |

Legend: `-` not started, `~` in progress, `X` done, `!` blocked/bug

---

## Known Bugs / Remaining Work (Phase 5)

### Remaining rendering artifacts in 3DMark99 race scene
- **Sky banding/smearing** — sky textures show horizontal stretching (likely fog or multi-pass blending issue, Phase 6)
- **Transparency edge cases** — some objects appear ghostly that shouldn't (alpha combine edge cases)
- **"3DMARK" text overlay** — smeared/duplicated (multi-pass alpha overlay issue)

### FIXED: Texture coordinate scrambling (cff427c79)
- Root cause: texture coordinate varyings (`vTexCoord0`, `vTexCoord1`) used `smooth` (default) interpolation, but Voodoo hardware iterates S/W, T/W, 1/W linearly in screen space and does the perspective divide per-pixel. The GPU's hardware perspective correction double-corrected these already-perspective-divided values, causing mosaic/scrambled textures on oblique geometry.
- Fix: changed both varyings to `noperspective` in vertex and fragment shaders. Also simplified `gl_Position` to use `W=1.0` since all varyings are now noperspective (W component irrelevant for interpolation, vertices already in screen-space NDC).
- Secondary note: `FBZ_PARAM_ADJUST` subpixel correction not yet implemented (minor, causes ~0.5px shimmer).

### FIXED: SPSC ring publish-before-write race on ARM64 (3fd71c710)
- Root cause: `vc_ring_push` published write_pos (release store) before the payload was fully written. On ARM64 weak memory model, the GPU thread could read garbage from the ring (blue diagonal streaks, random corruption).
- Fix: split into `vc_ring_reserve()` (writes header, returns payload pointer) + `vc_ring_commit()` (release store of write_pos after caller fills payload). Ensures payload is visible before consumer sees the new write_pos.

### FIXED: Alpha blending (e7e5a9a39)
- Implemented pipeline variant cache: maps Voodoo alphaMode blend factors to VkBlendFactor
- 32-entry linear cache, lazy pipeline creation. Real games use only 5-15 unique blend configs.
- MoltenVK does NOT support EDS3 (extendedDynamicState3) — pipeline variants required for blend state.
- ACOLORBEFOREFOG (0xF) mapped to VK_BLEND_FACTOR_ONE as interim (dual-source blending deferred to Phase 6).

### FIXED: Scissor clipping (ec116a7b3)
- Wired Voodoo clipLeft/clipRight/clipLowY/clipHighY registers to vkCmdSetScissor per-triangle
- Added vc_clip_rect_t to ring command (288->300 bytes per triangle command)
- Fixed grey bar on left edge, corruption in bottom-right corner

### FIXED: Texture combine stage (5da589dcf)
- Added textureMode0 bits 12-29 processing to voodoo_uber.frag (~100 lines)
- Handles tc_zero_other, tc_sub_clocal, tc_mselect (6 cases), tc_reverse_blend, tc_add_clocal/alocal, tc_invert_output
- Alpha equivalents (tca_*) also implemented
- Single-TMU only (c_other=0), DETAIL and LOD_FRAC stubbed as 0

### FIXED: No textures visible — TMU index bug (9223a2729, 2fd9b9835)
- Root cause: Voodoo 2 TMU pipeline flows TMU 1 → TMU 0 → color combine. Glide writes texture state (tLOD, texBaseAddr, S/T/W gradients) to TMU 1 (chip=0x4), but VK path hardcoded TMU 0
- Fix: detect active TMU via `(params->textureMode[1] & 1) ? 1 : 0`, use correct TMU for texture upload, vertex extraction, and refcount
- Also fixed texture identity tracking — replaced XOR hash with direct 3-field comparison (9223a2729)

### FIXED: Freeze on benchmark exit (display_active redesign)
- Root cause: `vc_display_active` served dual purpose (triangle routing + VGA suppression)
- Clearing it for VGA re-enable also killed triangle routing → feedback loop
- Fix (e53f7c836, 05ae03693): split into `vc_divert_to_gpu` (permanent triangle routing) + `display_owner` (VGA timeout)
- VGA passthrough now returns cleanly after benchmark exits (~2 sec timeout)

---

## Phase 1 Checklist

| # | Task | Status | Agent |
|---|------|--------|-------|
| 1.1 | Create `src/video/videocommon/` directory + CMakeLists.txt | X | vc-lead |
| 1.2 | Vendor volk into `extra/volk/` | X | vc-lead |
| 1.3 | Vendor VMA into `extra/VMA/` | X | vc-lead |
| 1.4 | `vc_core.c/h` -- Vulkan instance/device/VMA init | X | vc-lead |
| 1.5 | `vc_internal.h` -- Shared internal defines | X | vc-lead |
| 1.6 | `src/include/86box/videocommon.h` -- Public C11 API | X | vc-lead |
| 1.7 | CMake integration (VIDEOCOMMON option, link) | X | vc-lead |
| 1.8 | `vc_thread.c/h` -- GPU thread lifecycle (create/join/loop) | X | vc-lead |
| 1.9 | SPSC ring buffer (8MB, atomic, wake mechanism) | X | vc-plumbing |
| 1.10 | Add `vc_ctx`/`use_gpu_renderer` to `voodoo_t` | X | vc-lead |
| 1.11 | Wire `vc_voodoo_init/close` into Voodoo card init/close | X | vc-lead |
| 1.12 | Build clean on macOS ARM64 | X | vc-lead |
| 1.13 | GPU thread starts + enters idle loop | X | vc-lead |
| 1.14 | VC_CMD_SHUTDOWN round-trip works | X | vc-plumbing |
| 1.15 | No Vulkan validation errors | X | manual |

Legend: `-` not started, `~` in progress, `X` done, `!` blocked

---

## Phase Dependency Graph

```
Phase 1 (Infrastructure)
   |
   v
Phase 2 (Basic Rendering)
   |
   v
Phase 3 (Display)
   |
   +---> Phase 4 (Textures) ---> Phase 6 (Advanced)
   |                                     |
   +---> Phase 5 (Core Pipeline) -------+---> Phase 8 (Polish)
   |                                     |
   +---> Phase 7 (LFB Access) ---------+
```

Phases 4, 5, 7 can run in parallel after Phase 3.

---

## Architecture Summary

```
CPU Thread          FIFO Thread              GPU Thread
(emulation)         (per card)               (per card, NEW)

PCI writes -------> FIFO/CMDFIFO drain       Owns ALL Vulkan objects
swap_count++        Triangle setup           VkDevice, VkQueue
SST_status poll     Push to SPSC ring =====> Consume ring commands
Timer/display cb    swap_pending=1           Record cmd buffers
(retrace timing)    Block on double-buf      vkQueueSubmit/Present
```

**Core v2 insight**: DO NOT touch swap_count/swap_pending. The existing Voodoo display callback handles swap pacing correctly. v1 failed by trying to replace it.

---

## Key Files (implemented)

| File | Purpose |
|------|---------|
| `src/video/videocommon/vc_core.c/h` | Vulkan instance, device, VMA, logging |
| `src/video/videocommon/vc_thread.c/h` | GPU thread, SPSC ring, wake mechanism |
| `src/video/videocommon/vc_render_pass.c/h` | Render pass, framebuffer management |
| `src/video/videocommon/vc_pipeline.c/h` | Graphics pipeline, dynamic state |
| `src/video/videocommon/vc_shader.c/h` | SPIR-V shader loading |
| `src/video/videocommon/vc_batch.c/h` | Vertex buffer, triangle batching |
| `src/video/videocommon/vc_texture.c/h` | Texture upload, sampler cache |
| `src/video/videocommon/vc_display.c/h` | Swapchain, post-process, present |
| `src/video/videocommon/vc_readback.c/h` | LFB readback (sync hack) |
| `src/video/videocommon/vc_internal.h` | Internal shared defines |
| `src/include/86box/videocommon.h` | Public C11 API |
| `src/video/vid_voodoo_vk.c` | Voodoo→VideoCommon bridge |
| `src/video/videocommon/shaders/voodoo_uber.vert` | Vertex uber-shader |
| `src/video/videocommon/shaders/voodoo_uber.frag` | Fragment uber-shader (Phase 5) |
| `src/qt/qt_vcrenderer.cpp/hpp` | Qt VCRenderer display integration |
| `extra/volk/` | Vendored dynamic Vulkan loader |
| `extra/VMA/` | Vendored Vulkan Memory Allocator |

---

## Estimated Scope

| Phase | New Files | Modified | Est. Lines |
|-------|-----------|----------|-----------|
| 1. Infrastructure | 8 | 4 | ~1500 |
| 2. Basic Rendering | 6 | 2 | ~2000 |
| 3. Display | 4 | 2 | ~1200 |
| 4. Textures | 1 | 3 | ~800 |
| 5. Core Pipeline | 0 | 4 | ~1000 |
| 6. Advanced Features | 0 | 4 | ~800 |
| 7. LFB Access | 1 | 2 | ~600 |
| 8. Polish | 0 | varies | ~500 |
| **Total** | **~20** | | **~8400** |

---

## Risk Register

| Risk | Mitigation | Status |
|------|-----------|--------|
| MoltenVK missing extended_dynamic_state3 | Baked pipeline variant cache (32-slot linear, lazy creation) | FIXED (e7e5a9a39) |
| SPSC ring full stall | 8MB = ~65K cmds, 6 frames headroom | Designed |
| swap_count deadlock (v1 repeat) | v2 doesn't touch swap lifecycle AT ALL | Designed |
| LFB read stall (v1 repeat) | Shadow buffer, no sync push from CPU | Designed |
| Pi 5 missing dualSrcBlend | Fallback to SRC_COLOR for ACOLORBEFOREFOG | Planned |
| volk EDS1 function names | Must use EXT suffix on Vulkan 1.2 | FIXED (f64248f8a) |
| Depth clear convention | Must clear to 1.0 (far), not 0.0 | FIXED (1e3ab6c96) |
