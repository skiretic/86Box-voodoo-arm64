# Verification Report: PHASES.md and LESSONS.md

**Date**: 2026-03-01
**Reviewer**: vc-arch agent
**Documents reviewed**:
- `videocommon-plan/PHASES.md` (implementation phases)
- `videocommon-plan/LESSONS.md` (v1 post-mortem)
- `videocommon-plan/DESIGN.md` (architecture, cross-reference)
- `videocommon-plan/research/emulator-gpu-threading.md` (cross-reference)
- `videocommon-plan/research/voodoo-swap-lifecycle-audit.md` (cross-reference)

**Rating scale**: CRITICAL (blocks implementation or introduces bugs), WARNING (should be addressed before implementation), INFO (documentation improvement, no functional impact)

---

## Summary

Overall the three core documents are well-written, internally consistent, and form
a solid foundation for v2 implementation. The PHASES.md ordering is sound, the
LESSONS.md analysis is accurate, and the DESIGN.md architecture addresses every v1
failure mode. I found **1 WARNING** (inter-document contradiction on swap_count ownership),
**1 WARNING** (missing ring command in command table), and several INFO-level items.

---

## PHASES.md Verification

### 1. Phase Dependencies

**VERDICT: CORRECT**

The dependency graph is:

```
Phase 1 (Infrastructure) -> Phase 2 (Basic Rendering) -> Phase 3 (Display)
  Phase 3 -> Phase 4 (Textures) -> Phase 6 (Advanced Features)
  Phase 3 -> Phase 5 (Core Pipeline) -> Phase 8 (Polish)
  Phase 3 -> Phase 7 (LFB Access) -> Phase 8 (Polish)
```

- Phase 2 requires Phase 1 (GPU thread, ring, Vulkan init). Correct.
- Phase 3 requires Phase 2 (offscreen framebuffer to blit from). Correct.
- Phase 4 can run after Phase 3 (textures are independent of display). Correct.
- Phase 5 can run after Phase 3 (pipeline features need basic rendering). Correct.
- Phase 6 requires Phase 4 (TMU1 needs TMU0 infrastructure). Correct.
- Phase 6 benefits from Phase 5 but PHASES.md does not require it. Acceptable --
  color combine and texture combine are logically independent of depth/blend, though
  in practice they interact. The dependency graph shows Phase 6 requiring Phase 4
  and "benefiting from Phase 5" which is appropriately cautious.
- Phase 7 can run after Phase 3 (LFB needs offscreen FB to read from). Correct.
- Phase 8 requires all others. Correct.

**[INFO] Phase 4/5 parallel ordering**: The claim that Phases 4, 5, and 7 can be
developed "in parallel" is true at the architectural level but in practice they share
files (`voodoo_uber.frag`, `vid_voodoo_vk.c`, `vc_pipeline.c`). Agents working on
these phases simultaneously would face merge conflicts. This is an implementation
logistics concern, not an architectural one.

### 2. File Lists vs DESIGN.md

**VERDICT: CORRECT with one gap**

Cross-referencing PHASES.md file creation with DESIGN.md section 11.2:

| DESIGN.md File | Created in Phase | Match? |
|----------------|-----------------|--------|
| `vc_core.c/h` | Phase 1 | YES |
| `vc_thread.c/h` | Phase 1 | YES |
| `vc_internal.h` | Phase 1 | YES |
| `videocommon.h` | Phase 1 | YES |
| `vc_render_pass.c/h` | Phase 2 | YES |
| `vc_pipeline.c/h` | Phase 2 | YES |
| `vc_shader.c/h` | Phase 2 | YES |
| `vc_batch.c/h` | Phase 2 | YES |
| `vid_voodoo_vk.c` | Phase 2 | YES |
| `voodoo_uber.vert/frag` | Phase 2 | YES |
| `vc_display.c/h` | Phase 3 | YES |
| `qt_vcrenderer.cpp/hpp` | Phase 3 | YES |
| `postprocess.vert/frag` | Phase 3 | YES |
| `vc_texture.c/h` | Phase 4 | YES |
| `vc_readback.c/h` | Phase 7 | YES |
| `CMakeLists.txt` (videocommon) | Phase 1 | YES |

All files accounted for. No DESIGN.md file is missing from PHASES.md and no PHASES.md
file is absent from DESIGN.md.

### 3. Success Criteria

**VERDICT: CLEAR AND TESTABLE**

Every phase has concrete success criteria with checkboxes. Key observations:

- Phase 1: "volk loads MoltenVK successfully", "GPU thread starts and enters sleep loop",
  "VC_CMD_SHUTDOWN causes clean thread exit" -- all directly observable via logs.
- Phase 2: "vkCmdDraw records successfully (validation clean)" -- testable via
  VC_VALIDATE=1. Note that Phase 2 success criteria state "swap_count operates
  correctly (display callback unchanged)" which is critical and testable.
- Phase 3: "swap_count lifecycle correct (guest-visible)" -- the most important
  criterion, explicitly called out. This aligns with LESSONS.md P6.
- Phase 6: "All 3DMark99 tests render without major artifacts" -- slightly vague.
  "Major" is subjective.

**[INFO] Phase 6 criterion vagueness**: "without major artifacts" could be tightened to
"without artifacts that were not present in the SW renderer" for objectivity.

### 4. Missing Phases or Steps

**VERDICT: NO CRITICAL GAPS**

**[INFO] No explicit "pipeline cache persistence" step**: DESIGN.md section 7.6
mentions VkPipelineCache for startup performance, and Phase 2 creates `vc_pipeline.c`,
but no phase explicitly calls out serializing the pipeline cache to disk. This is a
nice-to-have optimization that could fit in Phase 8 (Polish).

**[INFO] No explicit Banshee/V3 testing before Phase 8**: Phases 1-7 focus entirely on
Voodoo 1/2. Phase 8.4 adds Banshee/V3 support. This is fine -- Banshee shares 95% of
the pipeline code and is a natural extension. However, the `vid_voodoo_vk.c`
integration surface touches Banshee-specific paths (leftOverlayBuf, banshee_set_overlay_addr),
and PHASES.md Phase 2 should note that the initial `vid_voodoo_vk.c` bridge should be
written with Banshee compatibility in mind even if not tested until Phase 8.

**[INFO] No explicit error recovery phase**: DESIGN.md section 12.2 describes runtime
Vulkan error handling (device lost -> error state -> ring fills -> emergency swap ->
degraded operation). No phase explicitly implements this. It could be added to Phase 8
or treated as defensive coding throughout.

### 5. Phase Ordering and Testability

**VERDICT: EXCELLENT**

Each phase produces a testable increment:

- Phase 1: Boot to Windows desktop, SW fallback works, GPU thread logs visible. Testable.
- Phase 2: Triangles submitted to GPU (verifiable via validation layers or RenderDoc).
  SW fallback still works. Testable.
- Phase 3: **First visual output from VK path.** This is the critical milestone per
  LESSONS.md P6. Testable.
- Phase 4: Textured geometry visible. Testable by visual comparison.
- Phase 5: Correct depth ordering, transparency, fog. Testable by visual comparison.
- Phase 6: Multi-textured surfaces, dither, stipple. Testable.
- Phase 7: LFB reads return correct data. Testable programmatically.
- Phase 8: Cross-platform, performance, visual parity. Testable.

The ordering is sensible: infrastructure first, then rendering primitives, then display
(so you can see results), then features, then correctness. This matches LESSONS.md P6
("test the swap lifecycle early").

### 6. Estimated File/Line Counts

**VERDICT: REASONABLE with minor accounting note**

**[INFO] File count methodology**: PHASES.md states "~20 new" files total. This counts
table rows, not individual files. For example, Phase 2 says "6 new" but lists
`vc_render_pass.c/h` (2 files), `vc_pipeline.c/h` (2), `vc_shader.c/h` (2),
`vc_batch.c/h` (2), `vid_voodoo_vk.c` (1), `voodoo_uber.vert` (1), `voodoo_uber.frag`
(1) = 11 actual files from 6 table rows. The total of individual files is closer to
~30. This is cosmetic -- the line count estimates are what matter for effort planning.

**Line count reasonability check vs v1**:

MEMORY.md records that v1's VCRenderer alone was ~1700 lines. v2 targets ~300 lines for
VCRenderer (Phase 3). The total v2 estimate of ~8400 lines across all phases is plausible:

- v1 was functionally complete at roughly similar scope
- v2 eliminates the present channel (~400 lines), queue_mutex machinery (~200 lines),
  custom frame pacing (~300 lines) -- about ~900 lines saved
- v2 adds SPSC ring infrastructure (~500 lines) not present in v1 in this form
- Net is similar order of magnitude

The per-phase estimates also seem reasonable:
- Phase 1 (1500 lines for Vulkan init + ring + thread) -- plausible
- Phase 2 (2000 lines for render pass + pipeline + batch + bridge) -- plausible
- Phase 3 (1200 lines for swapchain + post-process + VCRenderer) -- plausible
- Phase 7 (600 lines for LFB readback) -- possibly low given v1's LFB complexity,
  but v2's LFB is simpler (no present channel interaction)

---

## LESSONS.md Verification

### 1. Bug Descriptions vs Actual Events

**VERDICT: ACCURATE**

Cross-referencing each bug against MEMORY.md commit records and research docs:

- **Bug 1 (Texture Refcount)**: MEMORY.md confirms commit `feb954b2c`, cause was
  `refcount_r[0]` only inside `if (changed)`. The audit doc
  (`voodoo-swap-lifecycle-audit.md`) does not cover texture refcount but the
  emulator-gpu-threading doc doesn't contradict. The fix description matches the commit
  message and the pattern described in MEMORY.md ("Moved refcount_r increment outside
  if (changed) to always run").

- **Bug 2 (Present Channel Crash)**: MEMORY.md confirms commit `4cc1084e4`, describes
  adding `vc_present_drain()`. The swapchain-lifetime research doc (referenced in
  MEMORY.md) describes exactly this pattern: MVKSwapchain::destroy() immediately frees
  images, so any in-flight present references become dangling.

- **Bug 3 (Spin-Loop Drain)**: MEMORY.md confirms commit `8652cd537`, "replaced spin-loop
  drain with event-based wait (thread_wait_event, 10s timeout)."

- **Bug 4 (Swapchain Thrashing)**: Same commit as Bug 2 (`4cc1084e4`). MEMORY.md confirms
  "changed frame-drop handler to drain+retry instead of full swapchain recreation."

- **Bug 5 (swap_count Stuck)**: MEMORY.md confirms this was the blocking bug. The
  description of the root cause -- split swap_count ownership between GPU thread and
  display callback -- matches the diagnostic logging results described in MEMORY.md.

**All five bug descriptions match the recorded history.**

### 2. Root Cause Analyses

**VERDICT: ACCURATE**

The root cause analysis in section 3 correctly identifies the architectural flaw:
"v1 tried to replace the Voodoo display/swap lifecycle with a Vulkan-native one."

The complexity comparison table (section 3) is verifiable:
- v1 had 2 threads accessing VkQueue: confirmed (GPU thread + GUI via present channel)
- v1 had 2 threads modifying swap_count: confirmed (GPU thread + display callback)
- v1 had 4 new sync primitives: queue_mutex, present channel, drain event, atomic flag
  -- confirmed per MEMORY.md
- v2 targets 1 sync primitive (SPSC ring wake semaphore) -- matches DESIGN.md section 4

### 3. "How v2 Prevents This" Claims vs DESIGN.md

**VERDICT: CORRECT, with one important caveat (see WARNING below)**

- **Bug 1 Prevention**: LESSONS.md says DESIGN.md section 7.7 calls out the refcount
  issue. Verified: DESIGN.md section 7.7 states "VK path must always increment
  refcount_r[0] to match (not just on change)". PHASES.md Phase 4 success criteria
  includes "Texture refcount balanced (no eviction stalls)". Match confirmed.

- **Bug 2 Prevention**: LESSONS.md says v2 eliminates the present channel. Verified:
  DESIGN.md has no present channel. GUI thread passes VkSurfaceKHR via atomic (section
  8.1). GPU thread owns swapchain exclusively (section 6.1). Match confirmed.

- **Bug 3 Prevention**: LESSONS.md says v2 has no drain operation. Verified: DESIGN.md
  teardown (section 8.1) uses atomic flag + GPU thread self-cleanup + completion event.
  No spin-loops. Match confirmed.

- **Bug 4 Prevention**: LESSONS.md says swapchain owned by GPU thread, resize via ring.
  Verified: DESIGN.md section 8.1 describes GPU thread swapchain creation/destruction.
  VC_CMD_RESIZE in ring (section 4.3). Match confirmed.

- **Bug 5 Prevention**: LESSONS.md says v2 does not touch swap_count. Verified: DESIGN.md
  section 5.1 states "VideoCommon does NOT modify swap_count." Section 5.6 shows display
  callback retains ALL swap/retrace timing logic. Match confirmed.

### 4. General Principles (P1-P9)

**VERDICT: SOUND AND WELL-SUPPORTED**

Each principle is supported by evidence:

| Principle | Evidence |
|-----------|----------|
| P1: Don't fight existing architecture | Bug 5 (swap_count stuck from split ownership) |
| P2: One thread owns each resource | Table in LESSONS.md matches DESIGN.md section 6.1 |
| P3: Swap is a ring command | PCSX2 PostVsyncStart, DuckStation SubmitFrame (research doc) |
| P4: Throttle the producer | Research doc Pattern 4 (all emulators do this) |
| P5: Cross-thread sync requires proper primitives | Bug 3 (spin-loop), DuckStation wake pattern |
| P6: Test swap lifecycle early | Bug 5 found in Phase 9 after 8 phases of rendering |
| P7: Match contracts exactly | Bug 1 (refcount), Bug 5 (swap_count) |
| P8: Diagnostic logging saves hours | macOS `sample` tool breakthrough (MEMORY.md) |
| P9: Study how others solved it | Research doc surveys 4 emulators, universal patterns found |

**[INFO] P9 could reference the specific research doc**: P9 mentions
"research/emulator-gpu-threading.md" but a direct link/path would be clearer.

---

## Cross-Document Consistency

### 1. PHASES.md Implements Everything in DESIGN.md

**VERDICT: YES, with one minor gap**

Systematic check of DESIGN.md sections against PHASES.md:

| DESIGN.md Section | Covered in Phase |
|-------------------|-----------------|
| 3. Threading Model | Phase 1 (thread), Phase 2 (ring integration) |
| 4. SPSC Ring Buffer | Phase 1 |
| 5. Swap/Sync Lifecycle | Phase 2 (VC_CMD_SWAP push), Phase 3 (display callback skip) |
| 6. GPU Thread Architecture | Phase 1 (main loop), Phase 2 (command handling) |
| 7.1-7.2 Instance/Device/Loader | Phase 1 |
| 7.3-7.4 Offscreen FB/Render Pass | Phase 2 |
| 7.5-7.6 Pipeline/Variants | Phase 2 (basic), Phase 5 (variants) |
| 7.7 Texture Management | Phase 4 |
| 7.8 Vertex Format | Phase 2 |
| 8. Display Integration | Phase 3 |
| 9. LFB Access | Phase 7 |
| 10. MoltenVK | Phase 1 (config), Phase 8 (testing) |
| 11. Integration Surface | Phase 1-2 (existing file mods) |
| 12. Error Handling | Not explicitly phased (see INFO above) |

**[INFO] DESIGN.md section 12 (Error Handling)**: Not covered by any specific phase.
Defensive error handling should be woven into each phase as it is built, with Phase 8
(Validation) serving as the final check.

### 2. LESSONS.md Prevention Claims Match DESIGN.md

**VERDICT: MATCH, except for the research doc contradiction (see WARNING below)**

### 3. Contradictions Between Documents

**[WARNING] swap_count ownership: Research doc vs DESIGN.md**

The emulator GPU threading research document (`research/emulator-gpu-threading.md`)
section 10 recommendation R1 states:

> "Render thread processes it, does vkQueuePresentKHR, **decrements swap_count**"
> "FIFO thread does NOT decrement swap_count itself"

And the "What v1 Got Wrong" table states:

> "swap_count stuck at 3 | swap_count-- in display callback (wrong thread) | Swap as ring command, render thread decrements (R1)"

This **directly contradicts** DESIGN.md section 5.1:

> "VideoCommon does NOT modify swap_count, swap_pending, retrace_count, or the display callback."

And DESIGN.md section 5.7:

> "Frame pacing is handled by the existing retrace system (swap_count, swap_pending, retrace_count, display callback)."

The DESIGN.md approach is the CORRECT one for the following reasons:

1. The research doc's R1 was written as a general recommendation based on how other
   emulators work (PCSX2, DuckStation). Those emulators don't have an independent
   retrace timing system like Voodoo does. PCSX2's VSync is a ring command because the
   PS2 GS has no independent retrace counter. Voodoo's display callback already provides
   retrace timing, and it already decrements swap_count at the correct moment (vblank).

2. The v1 bug was not that swap_count-- was in the display callback. The v1 bug was that
   BOTH the GPU thread AND the display callback decremented swap_count, creating a race.
   The research doc's diagnosis in the "What v1 Got Wrong" table is imprecise -- the
   root cause was dual-ownership, not wrong-thread ownership.

3. DESIGN.md's approach of leaving the display callback completely unchanged is simpler
   and preserves 20+ years of proven Voodoo timing behavior. It needs zero new
   synchronization for swap_count.

**Recommendation**: The research doc's R1 text and "What v1 Got Wrong" table should be
annotated with a note that DESIGN.md made a deliberate decision to deviate from the
pure emulator pattern here, and the rationale is in DESIGN.md section 2. The research
doc's analysis was correct for PCSX2/DuckStation but does not directly apply to Voodoo
because Voodoo has an independent retrace timing system that those consoles lack.

**[WARNING] Missing VC_CMD_READBACK in DESIGN.md command table**

DESIGN.md section 4.3 (Command Types) lists 10 command types:
VC_CMD_TRIANGLE, VC_CMD_SWAP, VC_CMD_TEXTURE_UPLOAD, VC_CMD_TEXTURE_BIND,
VC_CMD_STATE_UPDATE, VC_CMD_CLEAR, VC_CMD_LFB_WRITE, VC_CMD_RESIZE,
VC_CMD_SHUTDOWN, VC_CMD_WRAPAROUND.

However, DESIGN.md section 9.1 (LFB Read, Sync) references:

> "Push VC_CMD_READBACK (sync) to SPSC ring"

`VC_CMD_READBACK` is not in the command table and not in the GPU thread main loop
pseudocode (section 6.3). This command is needed for the LFB sync readback path
described in section 9.1.

**Recommendation**: Add `VC_CMD_READBACK` to the section 4.3 command table with payload
description "buffer select, region, completion event pointer" and description "Sync
readback of framebuffer to staging buffer". Also add a case for it in the section 6.3
main loop pseudocode.

**[INFO] Research doc R2 (frame pacing) not adopted**

The research doc recommends R2: "Frame pacing via atomic counter + semaphore." DESIGN.md
deliberately rejects this in favor of the existing retrace_count + swap_interval
mechanism (Appendix B: "Frame pacing | Custom (atomic queued_frame_count) | Existing
(retrace_count + swap_interval)"). This is a deliberate and well-justified design
choice, not a contradiction. The existing mechanism is simpler and already proven.

**[INFO] DESIGN.md section 7.1 mentions VK_KHR_dynamic_rendering**

DESIGN.md section 7.1 lists "VK_KHR_dynamic_rendering OR render pass fallback" as a
required extension. However, dynamic_rendering is a Vulkan 1.3 feature (not guaranteed
in Vulkan 1.2). The "OR render pass fallback" covers this, but since the entire
architecture is built on VkRenderPass (sections 7.3, 7.4), it would be clearer to just
list VkRenderPass as the primary approach and drop the dynamic_rendering mention, or
move it to "Optional extensions."

**[INFO] PHASES.md sub-phase numbering vs v1 phase numbering**

LESSONS.md refers to v1 "Phase 9" (display integration) as the problematic phase. PHASES.md
v2 has 8 phases with sub-phases. Display is Phase 3 in v2, matching LESSONS.md P6's
recommendation to test swap early. The renumbering is correct and intentional.

---

## Final Assessment

| Category | Rating | Count |
|----------|--------|-------|
| CRITICAL | -- | 0 |
| WARNING | | 2 |
| INFO | | 8 |

**The documents are ready for implementation.** The two WARNINGs are:

1. **Research doc R1 contradicts DESIGN.md on swap_count ownership.** DESIGN.md is
   correct. The research doc should be annotated to reflect this. No code impact --
   implementers should follow DESIGN.md.

2. **VC_CMD_READBACK missing from DESIGN.md command table and main loop.** This is a
   documentation gap that should be fixed before Phase 7 implementation begins. No
   impact on Phases 1-6.

The overall architecture is sound. The phase ordering correctly implements LESSONS.md P6
(test swap early). The DESIGN.md successfully addresses every v1 failure mode. The
LESSONS.md analysis is accurate and well-supported by the research. No blocking issues
found.
