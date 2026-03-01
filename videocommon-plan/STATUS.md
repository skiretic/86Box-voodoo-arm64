# VideoCommon v2 -- Executive Summary & Status

**Branch**: `videocommon-voodoo`
**Target**: GPU-accelerated Voodoo rendering via Vulkan 1.2
**Last Updated**: 2026-03-01

---

## Current Status: Phase 1 -- COMPLETE

All Phase 1 tasks done. Vulkan 1.2 init, GPU thread lifecycle, SPSC ring buffer, CMake integration, and Voodoo wiring all verified on macOS ARM64 (Apple M1 Pro, MoltenVK 1.2.323).

---

## Phase Progress

```
Phase 1: Infrastructure     [XXXXXXXXXX] 100% COMPLETE
Phase 2: Basic Rendering     [..........] 0%   BLOCKED (Phase 1)
Phase 3: Display             [..........] 0%   BLOCKED (Phase 2)
Phase 4: Textures            [..........] 0%   BLOCKED (Phase 3)
Phase 5: Core Pipeline       [..........] 0%   BLOCKED (Phase 3)
Phase 6: Advanced Features   [..........] 0%   BLOCKED (Phase 4)
Phase 7: LFB Access          [..........] 0%   BLOCKED (Phase 3)
Phase 8: Polish              [..........] 0%   BLOCKED (All)
──────────────────────────────────────────────
Overall                      [X.........] 12%
```

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
| `src/video/videocommon/vc_internal.h` | Internal shared defines |
| `src/include/86box/videocommon.h` | Public C11 API |
| `src/video/videocommon/CMakeLists.txt` | Build integration |
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
