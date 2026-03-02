# VideoCommon v2 -- Changelog

All notable changes to the VideoCommon v2 implementation are documented here.
Format: newest entries first. Each entry includes the phase, what changed, and which agent did the work.

---

## [In Progress] -- Phase 5: Core Pipeline (2026-03-02)

### In Progress
- Per-triangle push constants — fix batch/draw system to call `vkCmdPushConstants` before each draw (vc-lead)
- Uber-shader color/alpha combine — full fbzColorPath cc_mselect/cc_add/cc_sub pipeline (vc-shader)
- Alpha test — discard fragments below alpha threshold via alpha_mode (vc-shader)
- Depth/blend dynamic state — map Voodoo depth/blend modes to Vulkan dynamic state (vc-shader)
- Scissor — dynamic scissor from Voodoo clipLeft/Right/Top/Bottom (vc-shader)

### Context
- Output is BLACK despite ~2000 tris/frame and successful vkQueuePresentKHR
- Root cause: all triangles in a batch share the LAST triangle's push constants
- This is the Phase 2 audit blocker: "Per-triangle push constants not drawn per-triangle"

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
- Ring command size per triangle: 288 bytes (header + push constants + 3 vertices)
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
