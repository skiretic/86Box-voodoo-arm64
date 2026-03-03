# VideoCommon v2 -- Executive Summary & Status

**Branch**: `videocommon-voodoo`
**Target**: GPU-accelerated Voodoo rendering via Vulkan 1.2
**Last Updated**: 2026-03-02

---

## Current Status: Phase 5 IN PROGRESS — First 3D Output Achieved!

Phase 5 core pipeline is partially working. **First ever Voodoo 3D output from Vulkan backend** confirmed in 3DMark99 — geometry renders with correct depth ordering and iterated vertex colors. Two remaining issues before Phase 5 is complete.

**Target hardware**: All Voodoo cards (V1, V2, Banshee, V3). Testing order: Voodoo 2 first, then Voodoo 3/Banshee.

---

## Phase Progress

```
Phase 1: Infrastructure     [XXXXXXXXXX] 100% COMPLETE
Phase 2: Basic Rendering     [XXXXXXXXXX] 100% COMPLETE
Phase 3: Display             [XXXXXXXXXX] 100% COMPLETE
Phase 4: Textures            [XXXXXXXXXX] 100% COMPLETE
Phase 5: Core Pipeline       [XXXXXXX...] 70%  IN PROGRESS
Phase 6: Advanced Features   [..........] 0%   BLOCKED (Phase 5)
Phase 7: LFB Access          [XX........] 20%  Readback hack in place
Phase 8: Polish              [..........] 0%   BLOCKED (All)
──────────────────────────────────────────────
Overall                      [XXXXXX....] 55%
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
| 5.7 | Texture rendering — flat grey, no textures visible | ! | — |
| 5.8 | display_active redesign (freeze on exit) | X | e53f7c836, 05ae03693 |
| 5.9 | Alpha blending (pipeline variants) | - | — |
| 5.10 | Scissor (clip rect wiring) | - | — |

Legend: `-` not started, `~` in progress, `X` done, `!` blocked/bug

---

## Known Bugs (Phase 5)

### BUG: No textures visible — flat greyscale output
- 3DMark99 renders geometry but only iterated vertex colors (grey/black/white)
- Log shows only 1 texture upload: `tex upload tmu=0 slot=14 1x1` (the dummy texture)
- The Phase 5 shader rewrite may have broken texture combine path
- Need to investigate: is texture data reaching the shader? Is the combine selecting texture?

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
| MoltenVK missing extended_dynamic_state3 | Baked pipeline variants (~20-50) | Planned |
| SPSC ring full stall | 8MB = ~65K cmds, 6 frames headroom | Designed |
| swap_count deadlock (v1 repeat) | v2 doesn't touch swap lifecycle AT ALL | Designed |
| LFB read stall (v1 repeat) | Shadow buffer, no sync push from CPU | Designed |
| Pi 5 missing dualSrcBlend | Fallback to SRC_COLOR for ACOLORBEFOREFOG | Planned |
| volk EDS1 function names | Must use EXT suffix on Vulkan 1.2 | FIXED (f64248f8a) |
| Depth clear convention | Must clear to 1.0 (far), not 0.0 | FIXED (1e3ab6c96) |
