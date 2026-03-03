# VideoCommon v1 -- Lessons Learned

**Date**: 2026-03-01
**Purpose**: Post-mortem of v1 failures. Required reading before implementing v2.

---

## Table of Contents

1. [Summary of Failures](#1-summary-of-failures)
2. [Bug-by-Bug Analysis](#2-bug-by-bug-analysis)
3. [Root Cause Analysis](#3-root-cause-analysis)
4. [How v2 Prevents Each Failure](#4-how-v2-prevents-each-failure)
5. [General Principles](#5-general-principles)

---

## 1. Summary of Failures

v1 was functionally complete: 8 phases of rendering infrastructure, triangle path,
textures (TMU0 + TMU1), color/alpha combine, alpha test, alpha blending, fog,
stipple, dither, depth test (Z and W), LFB read/write, and display integration.
27/29 code audit items completed. Zero Vulkan validation errors.

Despite this, v1 could not reliably run 3DMark99 past test 8 without freezing.
Five distinct bugs were found and fixed, but each fix revealed the next problem.
The freeze was ultimately caused by swap_count getting stuck at 3, which caused the
guest driver to stall forever waiting for swap buffer availability.

**Total time debugging swap/sync issues: ~60% of the project.**

---

## 2. Bug-by-Bug Analysis

### Bug 1: Texture Cache Refcount Imbalance (commit feb954b2c)

**Symptom**: Emulator freezes after ~60 seconds of 3DMark99 rendering.
`sample` tool shows FIFO thread blocked in texture eviction loop.

**Root cause**: `voodoo_use_texture()` increments `refcount` on EVERY call
(cache hit or miss). In the SW path, `refcount_r[0]` is incremented by the
render thread after processing each triangle. In the VK path, `refcount_r[0]`
was only incremented inside `if (changed)` (cache miss), not on cache hits.
After many frames, `refcount >> refcount_r[0]`, and no cache entries could
be evicted (eviction requires `refcount == refcount_r[0]`). Eventually the
texture cache was full with all entries pinned.

**Fix**: Move `refcount_r[0]++` outside the `if (changed)` block to run
unconditionally, matching the SW render thread's behavior.

**Lesson**: When replacing a subsystem, match its contract with the rest of the
codebase exactly. The refcount contract was: "callers increment refcount,
consumers increment refcount_r to acknowledge." The VK path was a consumer
that failed to acknowledge.

### Bug 2: Present Channel Crash (commit 4cc1084e4)

**Symptom**: Crash in vkQueuePresentKHR with invalid swapchain handle.

**Root cause**: The VCRenderer (GUI thread) destroyed swapchain resources
while the present channel had a pending present operation. The GPU thread
read a dangling swapchain handle and crashed.

**Fix**: Added `vc_present_drain()` to wait for pending present operations
before destroying swapchain resources. Also changed the frame-drop handler
to drain+retry instead of full swapchain recreation.

**Lesson**: Cross-thread resource destruction requires explicit drain/fence.
A "pending operation" flag is not sufficient -- you must wait for the
operation to actually complete before destroying the resource it references.

### Bug 3: Present Channel Drain Spin-Loop (commit 8652cd537)

**Symptom**: Present drain used a spin-loop of 1 million `sched_yield()` calls
with a ~100ms timeout. On some systems this was too short, causing spurious
timeout failures.

**Root cause**: Spin-loop with yield is unpredictable across platforms and
load conditions. There was no proper notification mechanism between the GPU
thread (which completes the present) and the GUI thread (which waits for drain).

**Fix**: Replaced spin-loop with event-based wait (`thread_wait_event`, 10s
timeout). GPU thread signals the event after completing the present operation.

**Lesson**: Never use spin-loops for cross-thread synchronization unless you
have a very tight bound on the wait time (microseconds, not milliseconds).
Always use proper OS primitives (event, semaphore, futex).

### Bug 4: Swapchain Thrashing (commit 4cc1084e4, same as Bug 2)

**Symptom**: Visual glitches and momentary blackouts during 3DMark99 benchmark
transitions.

**Root cause**: When a present operation was dropped (swapchain out-of-date or
timeout), the GUI thread's error handler triggered a full swapchain recreation.
This was unnecessarily destructive -- most dropped frames are transient (e.g.,
during a resize that is already being handled).

**Fix**: Changed drop handler to drain+retry. Only recreate swapchain on
persistent VK_ERROR_OUT_OF_DATE, not on single dropped frames.

**Lesson**: Swapchain errors are often transient. Retry before recreating.
Full swapchain recreation is expensive and itself creates a window for more
errors.

### Bug 5: swap_count Stuck at 3 (THE BLOCKING BUG)

**Symptom**: 3DMark99 freezes at benchmark transition (test 8 or 9). Guest
polls SST_status, sees swap_count=3 and busy=1, stalls forever. `swap_count`
never decrements.

**Root cause**: `swap_count--` happens in the display callback
(`voodoo_callback` in `vid_voodoo_display.c`) when `swap_pending &&
retrace_count > swap_interval`. In v1, the display callback was partially
bypassed in VK mode, but the swap completion logic was not correctly
triggered. The v1 code tried to have the GPU thread manage swap_count
independently, but:

1. The GPU thread decremented swap_count after present, but present could
   succeed while the display callback's retrace_count was still too low.
2. The display callback's swap_pending was set but never cleared because
   the GPU thread was supposed to handle it.
3. With swap_pending stuck at 1, the FIFO thread (double buffer) was
   permanently blocked in `voodoo_wait_for_swap_complete()`.

The fundamental problem: v1 split swap_count management between two threads
(GPU thread and display callback) when the existing code was designed for a
single thread (display callback only) to manage it.

**No fix in v1**: This was the architectural flaw that motivated v2. The
individual bug fixes (1-4) were band-aids; this was the underlying disease.

**Lesson**: Do not split ownership of a state variable across threads unless
you can prove the invariants are maintained. The swap_count invariant
(`swap_count >= 0`, decremented exactly once per swap, at vblank timing) was
broken by having two decrementers with different timing.

---

## 3. Root Cause Analysis

All five bugs share a common architectural root cause:

**v1 tried to replace the Voodoo display/swap lifecycle with a Vulkan-native one.**

The existing Voodoo code has a carefully designed swap state machine:
- swap_count incremented immediately on CPU thread (guest sees it right away)
- swap_pending set by FIFO thread (after buffer rotation)
- swap_count decremented by display callback (at vblank timing)
- FIFO thread blocks on double-buffer until display callback clears swap_pending
- Emergency swap prevents deadlocks

v1 assumed this could be replaced with:
- GPU thread manages swap_count (decrement after vkQueuePresentKHR)
- Present channel for cross-thread present dispatch
- Custom frame pacing via atomic queued_frame_count

This failed because:
1. The GPU thread has no connection to the display callback's retrace timing
2. vkQueuePresentKHR completion does not correspond to vblank
3. The FIFO thread's blocking behavior depends on swap_pending, which depends
   on the display callback, which v1 partially bypassed
4. The present channel added a third thread (GUI) with access to Vulkan objects
5. Cross-thread Vulkan object access created race conditions

### Complexity Comparison

| Metric | v1 | v2 |
|--------|----|----|
| Threads accessing VkQueue | 2 (GPU + GUI via present channel) | 1 (GPU only) |
| Threads modifying swap_count | 2 (GPU + display callback) | 1 (display callback only) |
| New sync primitives | 4 (queue_mutex, present channel, drain event, atomic flag) | 1 (SPSC ring wake semaphore) |
| VCRenderer lines | ~1700 | ~300 |
| Cross-thread state variables | 6+ (swap_count, present state, surface, swapchain, resize flag, teardown flag) | 1 (surface handle, atomic) |
| Swap code paths | 3 (display callback, GPU thread, present channel) | 1 (display callback, unchanged) |

---

## 4. How v2 Prevents Each Failure

### Bug 1 Prevention (Texture Refcount)

v2 design document (section 7.7) explicitly calls out:
> "VK path must always increment refcount_r[0] to match (not just on change)"

The PHASES.md Phase 4 success criteria includes:
> "Texture refcount balanced (no eviction stalls)"

### Bug 2 Prevention (Present Channel Crash)

v2 eliminates the present channel entirely. There is no GUI thread access to
Vulkan objects. The GPU thread owns the swapchain exclusively. The GUI thread
passes a VkSurfaceKHR handle via atomic, and the GPU thread creates/destroys
the swapchain from its own context.

Cross-thread resource destruction cannot crash because no cross-thread resource
access exists.

### Bug 3 Prevention (Spin-Loop Drain)

v2 has no drain operation. The GPU thread owns all Vulkan resources and
destroys them on its own timeline. Teardown is: GUI sets atomic flag, GPU
thread sees it on next loop iteration, GPU thread destroys resources, GPU
thread signals completion event, GUI thread destroys surface.

No spin-loops anywhere. The SPSC ring uses a proper semaphore for wake.

### Bug 4 Prevention (Swapchain Thrashing)

v2's swapchain is owned by the GPU thread. Resize is detected via atomic flag
polling in `vc_display_tick()`, processed in-order on the GPU thread. No
concurrent recreation. No race between "present in flight" and "recreation in
progress" because they happen on the same thread. (Resize does NOT use a ring
command -- the GUI thread sets an atomic flag and the GPU thread checks it at
the top of each loop iteration, per DESIGN.md section 8.1.)

### Bug 5 Prevention (swap_count Stuck)

**This is the most important prevention.**

v2 does not touch swap_count. Period. The display callback handles swap_count
exactly as it does in the SW renderer:

```
Display callback (unchanged):
  if (swap_pending && retrace_count > swap_interval):
    front_offset = swap_offset
    swap_count--
    swap_pending = 0
    wake FIFO thread
```

The GPU thread handles rendering and display output but has no knowledge of or
connection to swap_count. The FIFO thread handles swap_pending as before. The
display callback handles swap_count as before.

One owner per state variable. No split ownership. No new timing dependencies.

---

## 5. General Principles

### P1: Don't Fight the Existing Architecture

The Voodoo display/swap system works. It has been tested across hundreds of games
over 20+ years. Replacing it is not a rendering task -- it is a timing task, and
timing is orders of magnitude harder to get right.

**Rule**: If the existing code handles a concern correctly, leave it alone. Only
replace the parts you need to (SW rasterization -> GPU rasterization).

### P2: One Thread Owns Each Resource

Every mutable shared resource must have exactly one owning thread. Other threads
communicate through message passing (ring buffer, atomic signals).

| Resource | Owner (v1) | Owner (v2) |
|----------|-----------|-----------|
| VkSwapchain | GPU + GUI (BUG) | GPU thread only |
| swap_count | GPU + display callback (BUG) | Display callback only |
| swap_pending | FIFO + display callback | FIFO + display callback (unchanged) |
| VkQueue | GPU + GUI via mutex (BUG) | GPU thread only |
| Offscreen FB | GPU thread | GPU thread |
| Texture VkImages | GPU thread | GPU thread |

### P3: Swap is a Ring Command, Not a Side Channel

PCSX2 and DuckStation both process swap/vsync as a command in the main ring buffer,
not as a cross-thread signal. v1's "present channel" was a side-band that bypassed
the ring, creating ordering issues and race conditions.

**Rule**: All communication to the GPU thread goes through the SPSC ring. No side
channels, no shared mutable state, no callbacks.

### P4: Throttle the Producer, Not the Consumer

The GPU thread should run as fast as the ring has data. Frame pacing is the
producer's problem (FIFO thread blocks via existing swap_pending mechanism).

v1 attempted to throttle the GPU thread via frame queue limits, which conflicted
with the existing FIFO thread throttling and created double-blocking scenarios.

**Rule**: The GPU thread never sleeps for frame pacing. It only sleeps when the
ring is empty.

### P5: Cross-Thread Sync Requires Proper Primitives

- Spin-loops with yield: unreliable, platform-dependent, hard to debug
- Mutexes: correct but heavy, risk contention and priority inversion
- Atomics + semaphore: lightweight, correct, proven (DuckStation pattern)
- Events: correct for one-shot notifications (teardown, drain)

**Rule**: Use atomics + semaphore for hot paths (ring wake). Use events for cold
paths (teardown). Never spin-loop for more than microseconds.

### P6: Test the Swap Lifecycle Early

v1 deferred swap testing until Phase 9 (after 8 phases of rendering work).
The swap bugs were the hardest to diagnose and fix, and they invalidated
architectural assumptions made in Phase 1.

**Rule**: Phase 3 of v2 is display integration. If swap does not work by Phase 3,
stop and fix it before building more rendering features on a broken foundation.

### P7: Match Contracts Exactly When Replacing Subsystems

The texture refcount bug (Bug 1) was caused by not matching the SW render thread's
contract with the texture cache. The swap_count bug (Bug 5) was caused by not
matching the display callback's contract with the FIFO thread.

**Rule**: Before replacing a subsystem, document its contract with every other
subsystem that interacts with it. Verify the replacement honors every obligation.

### P8: Diagnostic Logging Saves Hours

The breakthrough in debugging v1 was adding strategic logging:
- `sample <pid> <seconds>` on macOS to capture thread stacks of frozen processes
- Per-1000th-poll logging of SST_status fields (swap_count, busy)
- nopCMD counter logging in the FIFO thread
- Pixel counter estimation for VK path

**Rule**: Add diagnostic logging early (gated by `#ifdef ENABLE_*_LOG`). Remove
it after the feature is stable. The cost of logging is zero when disabled;
the cost of debugging without it is hours.

### P9: Study How Others Solved the Same Problem

The emulator GPU threading research (`research/emulator-gpu-threading.md`) revealed
that every major emulator uses the same pattern: single GPU-owner thread, ring buffer,
swap as ring command, throttle the producer. v1 deviated from this pattern. v2 follows it.

**Rule**: Before designing a system, survey 3+ existing implementations. Find the
universal patterns. Deviating from universal patterns requires explicit justification.

---

## 6. v2 Lessons Learned (Phase 5)

### L1: MoltenVK Does NOT Support EDS3 — Pipeline Variants Are Mandatory

Despite MoltenVK reporting VK_EXT_extended_dynamic_state support, it does NOT
support VK_EXT_extended_dynamic_state3 (blend state as dynamic state). This means
blend factors (src/dst color/alpha) must be baked into VkPipeline objects. This is
not an edge case — it affects all macOS users.

**Solution**: A 32-entry linear cache maps unique Voodoo blend configurations to
VkPipeline objects. Pipeline creation is lazy (first use). Real Voodoo games use
only 5-15 unique blend configs, so a small linear cache avoids hash table complexity.

**Lesson**: Always verify dynamic state support at runtime. Do not assume that
because EDS1 is available, EDS2/EDS3 will be too. MoltenVK's dynamic state
coverage is incomplete due to Metal API limitations.

### L2: SPSC Ring on ARM64 — Publish-Before-Write Race

On ARM64's weak memory model, a single `vc_ring_push()` that writes the payload
and then publishes `write_pos` via a release store can still expose stale payload
data to the consumer. This is because the compiler/CPU may reorder the payload
writes relative to the release store if they are done in the same function call.

**Symptom**: Blue diagonal streaks, random pixel corruption, garbage triangles —
all intermittent and timing-dependent.

**Solution**: Split into `vc_ring_reserve()` (writes header, returns payload pointer)
+ `vc_ring_commit()` (release store of write_pos). The caller fills the payload
between the two calls, creating a natural compiler barrier. The release store in
`vc_ring_commit()` ensures all prior writes (including payload) are visible before
the consumer reads write_pos.

**Lesson**: On weakly-ordered architectures, always fill data BEFORE publishing the
pointer/index that makes it visible to another thread. The reserve/commit pattern
makes this explicit and hard to get wrong.

### L3: Voodoo Alpha reverse_blend Polarity Inversion

The SW renderer inverts the `tc_reverse_blend` register bit for non-trilinear
rendering: `c_reverse = !tc_reverse_blend` (when not in trilinear mode). This means
the shader must match this inversion to produce correct output. The raw register bit
does NOT directly correspond to the blend direction.

**Lesson**: When reimplementing a pixel pipeline, always compare against the SW
renderer's actual behavior, not the register documentation. Register bits may be
inverted, shifted, or combined in non-obvious ways by the existing code.

### L4: Pipeline Variant Cache — Linear Search Is Fine for Voodoo

Voodoo games change blend state infrequently (typically 5-15 unique configurations
per game). A 32-slot linear cache with direct comparison is faster than a hash table
for this workload because:
1. The working set fits in a single cache line (or two)
2. No hash computation overhead
3. No collision handling
4. Iteration is sequential and branch-predictor-friendly

**Lesson**: Choose data structures based on actual workload characteristics, not
theoretical complexity. O(n) linear search beats O(1) hash lookup when n < 32 and
access patterns are repetitive.

---

## Appendix: v1 Timeline

| Date | Event |
|------|-------|
| Early Feb | Phases 1-2: Infrastructure, basic rendering (clean) |
| Mid Feb | Phases 3-5: Display, textures, pipeline features (clean) |
| Late Feb | Phases 6-7: Advanced features, LFB (clean) |
| Feb 28 | Phase 8: Code audit (27/29 items, clean) |
| Feb 28 | Phase 9: Display integration begins |
| Feb 28 | Bug 1 found: texture refcount imbalance |
| Feb 28 | Bug 1 fixed, Bug 2 found: present channel crash |
| Feb 28 | Bug 2 fixed, Bug 3 found: drain spin-loop timeout |
| Mar 1 | Bug 3 fixed, Bug 4 found: swapchain thrashing |
| Mar 1 | Bug 4 fixed, Bug 5 found: swap_count stuck |
| Mar 1 | Bug 5 diagnosed: architectural flaw, v2 design started |

**Total rendering implementation time: ~5 days (Phases 1-8, clean)**
**Total swap/sync debugging time: ~2 days (Phase 9, 5 bugs)**

The rendering was not the hard part. The display integration was.
