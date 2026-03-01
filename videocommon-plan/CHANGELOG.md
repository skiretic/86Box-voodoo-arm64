# VideoCommon v2 -- Changelog

All notable changes to the VideoCommon v2 implementation are documented here.
Format: newest entries first. Each entry includes the phase, what changed, and which agent did the work.

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
