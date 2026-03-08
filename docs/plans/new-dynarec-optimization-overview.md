# New Dynarec Optimization Overview

Related docs:

- [new-dynarec-investigation.md](./new-dynarec-investigation.md)
- [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md)
- [new-dynarec-changelog.md](./new-dynarec-changelog.md)

## Purpose

This document is the maintained explanation of the CPU new dynarec work that has landed so far.

Unlike the changelog, this is organized by optimization or hardening area instead of by date. It should answer:

- what changed
- why it was needed
- what evidence confirmed it
- whether it is now considered complete, ongoing, or just observability for future work

Scope:

- CPU new dynarec only
- not Voodoo JIT except where tooling patterns were borrowed
- arm64 remains limited to ARMv8.0-A-compatible code generation

## Current overall read

The completed work so far falls into two buckets:

1. Correctness and reclamation hardening
2. Observability needed to prove whether remaining behavior is a bug or expected policy

That work materially improved the allocator-pressure path by removing several real failure modes:

- stale purge-list head churn
- missing post-purge dirty-list reuse
- stale code-presence state on pages with no live blocks
- bogus bulk-dirty enqueue churn

The current stopping point is:

- Phase 1 is complete
- minimal Phase 0 observability/reproducibility work is now in place for targeted repro runs
- remaining high random eviction under the Windows 98 + 3DMark99 workload is now best explained by scarcity of reclaimable pages
- not another confirmed Phase 1 reclamation correctness bug

That means future work to reduce the remaining random eviction rate should be treated as allocator/reclaim policy work, not as more Phase 1 bug fixing unless new evidence appears.

## Optimization map

### 1. Phase 0 dynarec observability surface

Status:
- kept
- foundational

What changed:

- added CPU dynarec counters for:
  - block marking
  - block recompilation
  - direct-recompiled instruction count
  - helper-call fallbacks
  - direct table NULL hits
  - direct handler bailouts
  - invalidations
  - byte-mask transitions
  - no-immediates transitions
  - allocator pressure
  - random eviction
- added a trace-hook API
- added formatted runtime/shutdown summary logging

Why it was needed:

- there was no durable way to tell whether the CPU dynarec was failing because of missing direct coverage, invalidation churn, allocator pressure, or reclaim failures

What it is used for now:

- all later Phase 1 work depended on this surface
- it remains useful for future policy and validation work

Key files:

- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_observability.c`
- `src/codegen_new/codegen_block.c`
- `src/86box.c`
- `tests/codegen_new_dynarec_observability_test.c`

### 2. Page-0 evict-list sentinel fix

Status:
- complete
- kept

What changed:

- fixed the purgeable-page evict-list so page 0 is representable
- split "end of list" from "not in list" sentinels

Why it was needed:

- the old list encoding collided with real page 0 and could corrupt page-list behavior

What evidence confirmed it:

- focused standalone regression coverage in `tests/mem_evict_list_test.c`

Key files:

- `src/include/86box/mem.h`
- `src/mem/mem_evict_list.c`
- `tests/mem_evict_list_test.c`

### 3. Byte-mask reclaim hardening

Status:
- complete for the Phase 1 scope
- kept

What changed:

- hardened reclaim behavior so byte-level dirty/code overlap is considered correctly
- flush completion now clears both coarse and byte-level dirty/code state

Why it was needed:

- byte-mask pages were part of the real reclaim surface and needed to behave consistently with coarse-mask pages

What evidence confirmed it:

- focused regression coverage in `tests/mem_evict_list_test.c`

Key files:

- `src/include/86box/mem.h`
- `src/mem/mem_evict_list.c`
- `src/codegen_new/codegen_block.c`
- `tests/mem_evict_list_test.c`

### 4. Stale-head purge pruning

Status:
- complete
- kept

What changed:

- purge walking now prunes dead head entries and continues to later reclaimable pages instead of repeatedly stalling on stale list heads

Why it was needed:

- workload data initially showed the purge path spending most of its time on non-overlapping head entries

What evidence confirmed it:

- workload before pruning:
  - `purgable_flush_attempts=714642`
  - `purgable_flush_no_overlap=707776`
- workload after pruning:
  - `purgable_flush_attempts=10455`
  - `purgable_flush_no_overlap=2592`

Effect:

- this removed the dominant useless purge churn in the original workload

Key files:

- `src/mem/mem_evict_list.c`
- `src/codegen_new/codegen_block.c`
- `tests/mem_evict_list_test.c`

### 5. Post-purge dirty-list reuse

Status:
- complete
- kept

What changed:

- allocator pressure now retries dirty-list reuse after purge before falling through to random eviction

Why it was needed:

- purge could invalidate blocks into the dirty list, but the allocator was not consuming those blocks immediately

What evidence confirmed it:

- workload showed:
  - `purgable_flush_dirty_list_reuses=7691`
  - `random_evictions` improved from `795608` to `744661` on the corresponding rerun

Effect:

- this was a real reclaim-path win, not just instrumentation

Key files:

- `src/codegen_new/codegen_block.c`
- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_observability.c`
- `tests/codegen_new_allocator_pressure_policy_test.c`

### 6. Stale code-mask cleanup on pages with no live blocks

Status:
- complete
- kept

What changed:

- when the last live block leaves a page, stale `code_present_mask` and `byte_code_present_mask` state is cleared

Why it was needed:

- purge could still see overlap on pages with no blocks left to invalidate

What evidence confirmed it:

- workload showed `purgable_flush_no_blocks=346`, proving the case was real
- `purgable_flush_no_free_block` dropped from `9055` to `7605` on the corresponding rerun

Key files:

- `src/mem/mem_evict_list.c`
- `src/codegen_new/codegen_block.c`
- `src/codegen_new/codegen.h`
- `tests/mem_evict_list_test.c`

### 7. Empty-purge instrumentation

Status:
- kept
- still useful

What changed:

- added counters to determine how often allocator pressure finds no purge candidates at all

Why it was needed:

- once the obvious stale-head bug was fixed, the next question was whether purge was still failing because it was choosing bad pages or because there simply were no reclaimable pages available

What evidence confirmed it:

- repeated workload runs showed `allocator_pressure_empty_purgable_list` at roughly 97-98% of allocator-pressure events

Interpretation:

- this was the measurement that changed the direction of the investigation

Key files:

- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_block.c`
- `src/codegen_new/codegen_observability.c`

### 8. Purge-list lifecycle counters

Status:
- kept
- useful for policy work

What changed:

- added counters for:
  - page enqueues
  - stale dequeues
  - flush dequeues
  - no-blocks dequeues

Why it was needed:

- once empty-purge was confirmed, the next question was whether the queue was underfed or whether it was being drained aggressively

What evidence confirmed it:

- a key workload run showed:
  - `purgable_page_enqueues=24951`
  - `purgable_page_dequeues_stale=13369`
  - `purgable_page_dequeues_flush=8175`
  - `purgable_page_dequeues_no_blocks=3407`
- dequeues summed exactly to enqueues, showing the queue was fully cycling back to empty

Interpretation:

- the purge list was active, not dead
- but it was not staying populated enough to cover allocator-pressure bursts

Key files:

- `src/mem/mem_evict_list.c`
- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_observability.c`

### 9. Bulk-dirty overlap-gated enqueue

Status:
- complete
- kept

What changed:

- `mem_invalidate_range()` now only enqueues bulk-dirty pages if they actually have dirty/code overlap

Why it was needed:

- bulk-dirty invalidation was the largest enqueue source in one run, but much of that traffic was useless and ended up as stale churn

What evidence confirmed it:

- before the fix:
  - `purgable_page_enqueues_bulk_dirty=12887`
  - `purgable_page_dequeues_stale=12649`
  - `purgable_flush_no_overlap=2128`
- after the fix:
  - `purgable_page_enqueues_bulk_dirty=221`
  - `purgable_page_dequeues_stale=0`
  - `purgable_flush_no_overlap=0`
  - `random_evictions=730188` on the corrected follow-up run

Effect:

- this removed a real source of bogus purge-list traffic

Key files:

- `src/mem/mem.c`
- `src/mem/mem_evict_list.c`
- `src/include/86box/mem.h`
- `tests/mem_evict_list_test.c`

### 10. Phase 1 boundary validation and probe cleanup

Status:
- complete
- temporary probes removed

What changed:

- the final late-Phase-1 validation used counters for:
  - `purgable_page_missed_write_enqueue_overlap`
  - `purgable_page_reenqueues_after_flush`
  - `purgable_page_reenqueues_after_no_blocks`
- the tree also temporarily split some dequeue outcomes by prior enqueue source
- after those probes closed the remaining hypotheses cleanly, they were removed from the durable stats surface

Why it was needed:

- these were the shortest path to deciding whether the remaining behavior was still a bug or just the natural limit of the current policy

What evidence confirmed it:

- workload showed:
  - `purgable_page_missed_write_enqueue_overlap=0`
  - `purgable_page_reenqueues_after_flush=0`
  - `purgable_page_reenqueues_after_no_blocks=0`

Interpretation:

- writes are not missing reclaimable enqueue opportunities
- pages are not meaningfully cycling back into the purge list after dequeue
- the remaining high empty-purge rate is best explained by lack of reclaimable supply, not churn

Current value:

- these probes proved the stopping point for Phase 1
- they are no longer needed as durable instrumentation because the remaining work is Phase 2 policy, not more Phase 1 bug classification

Key files:

- `src/cpu/codegen_public.h`
- `src/mem/mem.c`
- `src/mem/mem_evict_list.c`
- `src/codegen_new/codegen_observability.c`

### 11. Selective verify sampling on the direct-vs-helper boundary

Status:
- complete for the minimal Phase 0 target
- kept

What changed:

- added a small selective verification/reproducibility hook that records chosen direct hits, NULL-table fallbacks, and direct-handler bailouts at the `codegen_generate_call()` decision boundary
- reused the existing trace-hook and runtime-summary surface instead of introducing a second logging framework
- added optional runtime filters:
  - `86BOX_NEW_DYNAREC_VERIFY_PC`
  - `86BOX_NEW_DYNAREC_VERIFY_OPCODE`
  - `86BOX_NEW_DYNAREC_VERIFY_BUDGET`

Why it was needed:

- before allocator/reclaim Phase 2 work, the tree still lacked any selective way to reproduce and count the exact direct-vs-helper decisions taken for a suspicious instruction stream
- a full CPU shadow-execution tier would be larger than needed for the current handoff

What evidence confirmed it:

- focused standalone coverage now checks:
  - PC/opcode filter matching
  - budget exhaustion
  - trace emission for sampled decisions
  - summary formatting for verify counters

Interpretation:

- this is enough Phase 0 surface to make upcoming Phase 2 allocator/reclaim work defensible without reopening speculative Phase 1 debugging
- if future correctness work needs deeper A/B execution comparison, it can layer on top of this selective hook rather than replacing it

Key files:

- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_observability.c`
- `src/codegen_new/codegen.c`
- `tests/codegen_new_dynarec_observability_test.c`

## What is still considered open

These are not closed by the work above:

- REP direct recompilation coverage
- softfloat direct-coverage cliffs
- arm64 MMX / 3DNow parity gaps
- a possible future full CPU shadow-execution verify tier
- allocator/reclaim policy work to reduce structurally high random eviction under pressure

## What should be kept vs what could be trimmed later

Keep:

- core Phase 0 counters
- runtime/shutdown summaries
- selective verify sampling counters and filters
- purge failure-mode counters
- empty-purge counter
- purge-list lifecycle counters
- enqueue-source counters
- bulk-dirty overlap-gated enqueue behavior
- focused regression tests for real Phase 1 bug fixes

Trimmed at Phase 1 closeout:

- scarcity-boundary probes
- source-attributed dequeue counters

Reason:

- they answered specific late-Phase-1 bug-hypothesis questions and then went to zero or became redundant
- future Phase 2 policy work still retains aggregate queue lifecycle and enqueue-source visibility without carrying the extra probe-only state

## Current recommended next step

Do not continue treating the remaining Windows 98 + 3DMark99 random-eviction rate as a Phase 1 correctness bug by default.

If work continues, it should be framed explicitly as one of:

- allocator/reclaim policy work
- broader performance work
- coverage closure
- deeper verification infrastructure only if Phase 2 uncovers a concrete correctness gap

## Update instructions

When updating this document:

- organize by optimization area, not by date
- keep explanations short and concrete
- include the reason a change mattered
- include the current status of each area
- keep workload numbers only where they prove a point that still matters
