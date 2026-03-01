# Full VideoCommon Code Audit Prompt

Copy everything below the line into a new Claude Code session.

---

## Task: Systematic Code Audit of ALL VideoCommon Code (Phases 1-9)

VideoCommon is a GPU-accelerated rendering infrastructure for 86Box using Vulkan 1.2. It was built across 9 phases over 3 days, totaling ~13,500 lines of new code plus modifications to ~15 existing files. The code was written rapidly with multiple bug fix layers on top. It needs a thorough, systematic audit to identify all issues before proceeding.

**IMPORTANT RULES**:
- Use vc-debug agent for all code reading and analysis. Do NOT read source files directly — spawn agents.
- Work **slowly and methodically**. Do one file (or one logical group of small files) at a time.
- Report findings after each file before moving to the next.
- This is a LARGE audit (~13,500 lines). Pace yourself. Don't rush.
- Read the planning docs first for architecture context.

### Context

- **Branch**: videocommon-voodoo
- **Planning docs**: `videocommon-plan/STATUS.md`, `videocommon-plan/CHECKLIST.md`, `videocommon-plan/CHANGELOG.md`, `videocommon-plan/DESIGN.md` — read STATUS.md and CHANGELOG.md first for full context on all phases, bugs found and fixed, and known issues.
- **Architecture**: Vulkan 1.2, volk loader, VMA allocator, SPSC lock-free command ring between emulation thread and render thread, uber-shader for Voodoo pixel pipeline, LFB read/write via staging buffers, Qt VCRenderer for zero-copy display.
- **Platform**: macOS (MoltenVK), with code paths for Windows and Linux (untested).
- **Runtime validated**: Apple M1 Pro, Vulkan 1.2.323, MoltenVK 1.4.0. 3DMark99 renders (with known visual artifacts from unimplemented features).

### Critical Known Issue

**Queue mutex contention freeze (Phase 9)**: Three threads share one VkQueue via queue_mutex:
1. **Render thread** (vc_thread.c) — submits draw commands
2. **Qt GUI thread** (qt_vcrenderer.cpp) — calls vkQueuePresentKHR
3. **Display/readback thread** (vid_voodoo_display.c) — LFB readback submits

FIFO present blocks on vsync (~16ms) while holding queue_mutex, starving the render thread. Proposed fix: move presentation to render thread via SPSC ring (VC_CMD_PRESENT).

### Complete File Inventory (audit in this order)

**Total**: ~13,500 lines across 28 VideoCommon-specific files + modifications in ~15 existing files.

#### Group 1: Core VideoCommon Infrastructure (Phases 1-2)

These are the foundational modules. Audit thoroughly — everything else builds on them.

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 1 | `src/video/videocommon/vc_internal.h` | 78 | 1 | Internal macros, logging, common includes |
| 2 | `src/video/videocommon/vc_core.h` | 179 | 1+9 | Context struct definition, all declarations |
| 3 | `src/video/videocommon/vc_core.c` | 1168 | 1+9 | Vulkan instance/device, sub-module init/close, accessors, WSI extensions |
| 4 | `src/video/videocommon/vc_render_pass.h` | 95 | 1 | Render pass + framebuffer types |
| 5 | `src/video/videocommon/vc_render_pass.c` | 512 | 1 | VkRenderPass, dual VkImage framebuffers, layout transitions |
| 6 | `src/video/videocommon/vc_shader.h` | 59 | 1 | Shader module types |
| 7 | `src/video/videocommon/vc_shader.c` | 94 | 1 | SPIR-V loading, VkShaderModule creation |
| 8 | `src/video/videocommon/vc_batch.h` | 89 | 1 | Vertex buffer ring types |
| 9 | `src/video/videocommon/vc_batch.c` | 127 | 1 | 1MB vertex ring buffer, push_triangle, flush |
| 10 | `src/video/videocommon/vc_pipeline.h` | 115 | 1+6 | Pipeline key, cache types |
| 11 | `src/video/videocommon/vc_pipeline.c` | 383 | 1+6 | VkPipeline creation, dynamic state, pipeline cache |

#### Group 2: Texture and Descriptor Management (Phase 3)

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 12 | `src/video/videocommon/vc_texture.h` | 263 | 3 | Texture pool, sampler cache, descriptor types |
| 13 | `src/video/videocommon/vc_texture.c` | 1120 | 3 | VkImage pool, staging upload, descriptor sets, sampler cache |

#### Group 3: Render Thread and SPSC Ring (Phase 1, extended in 4-8)

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 14 | `src/video/videocommon/vc_thread.h` | 270 | 1+4-8 | SPSC ring types, command enum, frame resources |
| 15 | `src/video/videocommon/vc_thread.c` | 1004 | 1+4-8 | Lock-free ring, render thread, command dispatch |

#### Group 4: LFB Readback (Phase 8)

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 16 | `src/video/videocommon/vc_readback.h` | 597 | 1+8 | Readback types, staging buffer, dirty tiles |
| 17 | `src/video/videocommon/vc_readback.c` | 1743 | 1+8 | Sync/async readback, LFB write, shadow buffer, dirty tiles |

#### Group 5: Shaders (Phases 2-7, 9)

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 18 | `src/video/videocommon/shaders/voodoo_uber.vert` | 90 | 2 | Vertex shader: NDC conversion, W encoding |
| 19 | `src/video/videocommon/shaders/voodoo_uber.frag` | 1242 | 2-7 | **THE BIG ONE**: Full Voodoo pixel pipeline uber-shader |
| 20 | `src/video/videocommon/shaders/postprocess.vert` | 40 | 9 | Fullscreen triangle for post-processing |
| 21 | `src/video/videocommon/shaders/postprocess.frag` | 85 | 9 | CRT effects (barrel distortion, scanlines) |

#### Group 6: Voodoo-to-Vulkan Bridge (Phases 2-8)

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 22 | `src/include/86box/vid_voodoo_vk.h` | 66 | 2 | VK bridge public API |
| 23 | `src/video/vid_voodoo_vk.c` | 1428 | 2-8 | **CRITICAL**: Gradient vertex reconstruction, push constants, texture wiring |

#### Group 7: Public API (Phases 1-9)

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 24 | `src/include/86box/videocommon.h` | 739 | 1+9 | Public API, no-op stubs for non-VC builds |

#### Group 8: Qt VCRenderer (Phase 9)

| # | File | Lines | Phase | Purpose |
|---|------|-------|-------|---------|
| 25 | `src/qt/qt_vcrenderer.hpp` | 214 | 9 | VCRenderer class definition |
| 26 | `src/qt/qt_vcrenderer.cpp` | 1551 | 9 | **LARGE**: Full VCRenderer implementation (known freeze bug) |
| 27 | `src/qt/qt_vc_metal_layer.mm` | 47 | 9 | macOS CAMetalLayer helper |

#### Group 9: Modified Existing Files (VideoCommon changes only)

Audit only the `#ifdef USE_VIDEOCOMMON` blocks and related changes in these files:

| # | File | What changed |
|---|------|-------------|
| 28 | `src/include/86box/vid_voodoo_common.h` | Added vc_ctx, use_gpu_renderer, vc_readback_buf, vc_init_pending, vc_init_thread fields |
| 29 | `src/video/vid_voodoo.c` | Init/close hooks, gpu_renderer config, vc_set_global_ctx |
| 30 | `src/video/vid_voodoo_reg.c` | Deferred init trigger, swap buffer hook, fastfill hook, vc_set_global_ctx |
| 31 | `src/video/vid_voodoo_render.c` | VK path branch in voodoo_queue_triangle() |
| 32 | `src/video/vid_voodoo_display.c` | VK scanout path, direct_present skip logic |
| 33 | `src/video/vid_voodoo_fb.c` | LFB read/write VK path |
| 34 | `src/qt/qt_rendererstack.cpp` | VCRenderer instantiation in createRenderer() |
| 35 | `src/qt/qt_rendererstack.hpp` | Renderer::VideoCommon enum entry |
| 36 | `src/qt/qt_mainwindow.cpp` | vid_api mapping |
| 37 | `src/qt/qt.c` | plat_vidapi mapping |
| 38 | `src/include/86box/renderdefs.h` | RENDERER_VIDEOCOMMON enum |

#### Group 10: Build System

| # | File | What changed |
|---|------|-------------|
| 39 | `CMakeLists.txt` | option(VIDEOCOMMON), USE_VIDEOCOMMON define |
| 40 | `src/CMakeLists.txt` | Link videocommon, volk, vma |
| 41 | `src/video/CMakeLists.txt` | add_subdirectory(videocommon) |
| 42 | `src/video/videocommon/CMakeLists.txt` | OBJECT library, shader compile, WSI platform defs |
| 43 | `src/qt/CMakeLists.txt` | VCRenderer files, volk link, QuartzCore framework |

### What to Look For

1. **Thread safety**: Race conditions, missing locks, lock ordering violations, use-after-free across threads. The system has 3+ threads (emulation/FIFO, render, display/readback, Qt GUI). Map out which data is accessed from which thread.
2. **Vulkan correctness**: Missing pipeline barriers, wrong image layouts, invalid handle usage, spec violations, missing VK_SUCCESS checks. Cross-reference with Vulkan 1.2 spec behavior.
3. **Resource leaks**: Unreleased Vulkan objects in error paths, missing cleanup, double-free potential.
4. **SPSC ring correctness**: Lock-free ring buffer relies on C11 atomics with specific memory ordering. Verify acquire/release pairs, ABA safety, ring full/empty conditions.
5. **Fixed-point math**: Voodoo uses 12.4 (positions), 12.12 (colors), 20.12 (depth), 18.32 (texture coords). Verify gradient reconstruction doesn't overflow int64 intermediates.
6. **Shader correctness**: Compare uber-shader logic against the software rasterizer in `vid_voodoo_render.c`. Look for bit extraction errors, wrong combine logic, off-by-one in table lookups.
7. **Error handling**: Silent failures that should be caught, unchecked VkResult values.
8. **Dead code**: Unused variables, unreachable branches, leftover debugging artifacts.
9. **Platform concerns**: macOS-only code that claims cross-platform, missing Windows/Linux paths.
10. **Memory ordering**: ARM64 weak memory model — verify all cross-thread data access uses appropriate atomics or mutexes.

### Output Format

For each file (or small group), produce:
```
## [filename] — [line count] lines (Phase X)

### Issues Found
- [CRITICAL] Line XX: description
- [HIGH] Line XX: description
- [MODERATE] Line XX: description
- [LOW] Line XX: description

### Notes
- Observations about design, patterns, or potential improvements
```

### Final Summary

After ALL files are audited, produce a comprehensive summary:

1. **Total issue count by severity** (CRITICAL / HIGH / MODERATE / LOW)
2. **Top 5 most critical issues** with file + line + description
3. **Thread safety map**: Which threads access which data, and how it's protected
4. **Recommended fix order**: Prioritized list of what to fix first
5. **Architecture assessment**: Is the overall design sound? Are there fundamental problems?
6. **Queue mutex fix assessment**: Is VC_CMD_PRESENT (move present to render thread) the right approach? Are there alternatives?
7. **Shader correctness assessment**: How closely does the uber-shader match the SW rasterizer? What's likely causing the visual artifacts?
