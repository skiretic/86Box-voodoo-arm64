# New Dynarec Executive Summary

Detailed investigation: [new-dynarec-investigation.md](./new-dynarec-investigation.md)
Changelog: [new-dynarec-changelog.md](./new-dynarec-changelog.md)
Optimization overview: [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md)

## Purpose

This document is the executive-level status view for the CPU new dynarec investigation and follow-on improvement work. It is meant to stay short, update cleanly, and show progress at a glance while the detailed technical analysis lives in the linked investigation report.

## Status snapshot

Last updated: 2026-03-08

| Area | Status | Progress | Notes |
|---|---|---:|---|
| Investigation and architecture review | Complete | 100% | Detailed report written and saved |
| Implementation work | In progress | 67% | Phase 1 invalidation/reclamation hardening is complete; durable Phase 0/1 observability remains in tree, and Phase 0 now has a selective direct-vs-helper verification hook for targeted repro runs |
| Correctness risk mitigation | Complete | 100% | Phase 1 is closed: page-0 sentinel, stale-head purge churn, post-purge dirty-list reuse, stale code-presence state, and bogus bulk-dirty enqueue churn were fixed and validated; remaining high random eviction is treated as reclaimable-page scarcity for this workload |
| Coverage closure | Not started | 0% | REP, softfloat, arm64 parity gaps remain |
| Observability and validation tooling | In progress | 78% | Core CPU dynarec counters, runtime summary logging, purge failure-mode counters, empty-purge accounting, purge-list lifecycle counters, and selective verify sampling are in tree; temporary scarcity-boundary probes were retired after Phase 1 closure |
| Performance optimization | Not started | 0% | Deferred until correctness + observability |

## Current executive readout

- Phase 0 observability is now reduced to a small, explicit remainder rather than an open-ended tooling bucket.
- The page-0 purgeable-page evict-list sentinel collision is now fixed and covered by a focused regression test.
- The byte-mask dirty-page reclaim path has been hardened in the purge/flush logic, and stale-head purge pruning now removes most dead-head list entries under workload pressure.
- Windows 98 + 3DMark99 reruns still show allocator pressure ending in heavy random eviction, so the remaining issue is no longer dominated by dead-head purge entries.
- CPU dynarec now exposes counters for block mark/recompile activity, direct-vs-helper fallback behavior, invalidations, block degradation, and allocator pressure, plus a structured trace-hook API for rare events.
- CPU dynarec now also exposes a selective verification/reproducibility hook on the direct-vs-helper decision boundary, with optional `86BOX_NEW_DYNAREC_VERIFY_PC`, `86BOX_NEW_DYNAREC_VERIFY_OPCODE`, and `86BOX_NEW_DYNAREC_VERIFY_BUDGET` filters plus per-sample counters in the existing runtime summary surface.
- Runtime logging now distinguishes purge attempts that hit a dead head-page entry (`no overlap`) from purge attempts that do flush work but still fail to free a block, and now counts when a purge leaves reusable blocks in the dirty list for immediate recovery.
- A Windows 98 + 3DMark99 rerun after the dirty-list reuse fix showed `purgable_flush_dirty_list_reuses=7691` and reduced `random_evictions` from `795608` to `744661`, confirming the fix is real but also showing a remaining no-free-block failure mode.
- The next confirmed failure mode was stale `code_present_mask` / `byte_code_present_mask` state on pages with no live blocks, which can make purge see overlap but invalidate nothing. The code now clears stale code-presence state when the last block leaves a page, and logs a `purgable_flush_no_blocks` counter to measure any remaining occurrences.
- A follow-up Windows 98 + 3DMark99 run after stale-mask cleanup showed `purgable_flush_no_blocks=346` and reduced `purgable_flush_no_free_block` from `9055` to `7605`, confirming the stale-mask case was real. The next dominant signal is that allocator pressure (`782156`) still dwarfs purge attempts (`10065`), pointing to an often-empty purge list under pressure.
- The code now also counts `allocator_pressure_empty_purgable_list`, which is the minimal next measurement needed to confirm how often allocator pressure has no purge candidates at all before any further Phase 1 behavior change.
- The latest workload confirmed that empty purge list under pressure is dominant: `allocator_pressure_empty_purgable_list=746905` out of `allocator_pressure_events=766311`. The code now also records purge-list lifecycle counters for enqueues and dequeue reasons, and the shutdown/runtime summary buffer is large enough to keep the full counter line intact.
- A follow-up workload with enqueue-source counters showed that bulk-dirty invalidation is the largest enqueue source (`12887`), while dequeue reasons still sum exactly to total enqueues (`24174`), confirming that the purge list fully cycles back to empty under pressure rather than staying populated.
- The next confirmed Phase 1 fix now in tree is to stop `mem_invalidate_range()` from enqueuing bulk-dirty pages unless they actually have dirty/code overlap, directly targeting the observed bulk-dirty-to-stale-dequeue churn without changing later reclamation policy.
- A corrected follow-up run after the bulk-dirty overlap gate showed `purgable_page_enqueues_bulk_dirty=221`, `purgable_page_dequeues_stale=0`, `purgable_flush_no_overlap=0`, and `random_evictions=730188`, confirming that the bogus bulk-dirty churn was removed.
- The final Phase 1 boundary validation showed `purgable_page_missed_write_enqueue_overlap=0`, `purgable_page_reenqueues_after_flush=0`, and `purgable_page_reenqueues_after_no_blocks=0` under the same Windows 98 + 3DMark99 workload. Those probes have now been removed from the durable surface because they closed the late-Phase-1 bug-hypothesis loop cleanly.
- Direct-recompile coverage is materially incomplete in REP, softfloat, and arm64 MMX/3DNow paths.
- Full shadow-execution verify mode is still not implemented, but the minimal Phase 0 selective sampling hook needed before Phase 2 is now in tree.
- Phase 1 is complete. The remaining random-eviction rate for this workload is currently treated as structural to the current allocator/reclaim policy. Further reduction is Phase 2 policy/performance work, not more Phase 1 correctness fixing.

## Workstream tracker

| Workstream | Objective | Status | Progress | Exit condition |
|---|---|---|---:|---|
| WS0: Investigation | Capture architecture, risks, gaps, and plan | Complete | 100% | Report accepted as planning baseline |
| WS1: Observability | Add counters, traces, and verify hooks | Nearly complete | 90% | Core counters and runtime summary logging are in; empty-purge and purge-list lifecycle counters were kept as durable validation surface; temporary scarcity-boundary probes were removed after Phase 1 closure; selective verify sampling now covers the minimal pre-Phase-2 reproducibility need |
| WS2: Invalidation and reclamation | Fix page-list correctness and dirty-page reclaim behavior | Complete | 100% | Page-list sentinel bug fixed; stale-head pruning landed; allocator reuses dirty-list blocks created by purge before random eviction, stale code-presence masks are cleared when pages lose their last block, bulk-dirty enqueue skips non-reclaimable pages, and the remaining empty-purge behavior was closed out as expected scarcity for this workload |
| WS3: Coverage closure | Reduce silent helper-path and unsupported direct coverage gaps | Not started | 0% | Direct support matrix is explicit and top fallback clusters are addressed |
| WS4: Backend parity | Close meaningful arm64 vs x86-64 CPU dynarec deltas | Not started | 0% | Backend differences are intentional, measured, and documented |
| WS5: Performance | Improve warmup, eviction policy, and hot-path code quality | Not started | 0% | Performance work is guided by instrumentation and benchmark baselines |
| WS6: Validation and benchmarking | Add regression corpus and benchmark gates | In progress | 15% | Initial Windows 98 + 3DMark99 workload evidence exists; scripted gates still do not |

## Phase progress charts

Scale: 20 slots per phase. `#` = completed progress, `-` = remaining work.

| Phase | Status | Chart | Progress | Current readout |
|---|---|---|---:|---|
| Phase 0: Observability and reproducibility | Nearly complete | `[##################--]` | 90% | Core counters, runtime stats summaries, workload-level logging, purge failure-mode counters, empty-purge instrumentation, purge-list lifecycle counters, and selective verify sampling landed; only a future optional shadow-execution tier remains |
| Phase 1: Invalidation and reclamation hardening | Complete | `[####################]` | 100% | Page-0 list fix, byte-mask reclaim hardening, stale-head pruning, post-purge dirty-list reuse, stale code-mask cleanup, and bulk-dirty overlap-gated enqueue all landed and validated; remaining empty-purge behavior is treated as expected scarcity for this workload |
| Phase 2: Coverage closure and policy | Not started | `[--------------------]` | 0% | Backend support matrix, REP policy, and bailout reduction plan not started |
| Phase 3: Backend performance work | Not started | `[--------------------]` | 0% | Eviction-policy work, arm64 optimization pass, and reciprocal/rsqrt cleanup not started |
| Phase 4: Release-quality validation | In progress | `[###-----------------]` | 15% | Initial Windows 98 + 3DMark99 workload evidence exists, but scripted sweeps and threshold-based regression checks do not |

### Phase 0 chart basis

- Completed slice: counter surface, trace-hook surface, direct-vs-helper fallback accounting, invalidation/degradation accounting, allocator-pressure/random-eviction accounting, focused API verification, and selective verify sampling at the direct-vs-helper boundary.
- Remaining slice: optional richer runtime consumers, a future shadow-execution tier if needed, reproducibility matrix, benchmark-backed validation.

## Top risks tracker

| Rank | Risk | Severity | Current status | Planned response |
|---|---|---|---|---|
| 1 | Empty purge list under allocator pressure | High | Characterized | Latest workload evidence shows `allocator_pressure_empty_purgable_list=756683` out of `772998`; final boundary validation stayed at zero for the late-Phase-1 bug probes, so the remaining behavior is currently best explained by scarce reclaimable pages rather than another confirmed Phase 1 bug |
| 2 | Purgeable-page list collides with page 0 | Critical | Closed | Distinct evict-list sentinels implemented and regression-tested |
| 3 | REP / softfloat / arm64 3DNow coverage cliffs | High | Open | Build explicit support matrix and prioritize high-value gaps |
| 4 | Known direct-recompile correctness regressions | High | Open | Reproduce and track guest-facing failures separately |
| 5 | CPU dynarec verify tooling is still shallow | Medium | Narrowed | Core counters + runtime summaries landed, and selective verify sampling now exists; only a deeper shadow-execution tier remains optional later |
| 6 | Fixed exec pool plus random block eviction | Medium | Open | Replace with data-driven reuse/eviction policy later |

## Immediate next-step tracker

- [x] Write detailed investigation report
- [x] Set up executive summary / progress tracker
- [x] Define and implement the first CPU dynarec observability package for Phase 0
- [x] Fix the page-0 evict-list sentinel collision with a focused regression test
- [x] Harden byte-mask reclaim logic with focused standalone regression coverage
- [x] Collect initial Windows 98 + 3DMark99 workload evidence for allocator-pressure / random-eviction behavior
- [x] Prune stale purge-list head entries and verify the behavior with focused standalone coverage
- [x] Confirm and fix the post-purge dirty-list reuse gap with focused regression coverage
- [x] Re-run the Windows 98 + 3DMark99 workload to measure the remaining random-eviction rate after post-purge dirty-list reuse
- [x] Isolate and fix the stale code-presence / no-live-block purge case
- [x] Re-run the Windows 98 + 3DMark99 workload after stale code-mask cleanup
- [x] Instrument allocator pressure with empty purge list before attempting another Phase 1 behavior change
- [x] Re-run workload with purge-list lifecycle counters to determine whether the empty-list problem is caused by weak enqueue volume or premature dequeue/removal
- [x] Re-run workload with enqueue-source counters to identify which path is feeding the purge list most heavily
- [x] Stop bulk-dirty invalidation from enqueuing non-reclaimable pages
- [x] Re-run workload after the bulk-dirty overlap gate to measure whether stale dequeues and empty-purge pressure improve
- [x] Probe whether write paths leave reclaimable overlap off-list
- [x] Probe whether flush/no-block dequeues commonly re-enter the purge list
- [x] Decide whether to accept current allocator-pressure behavior as expected policy or start policy work to reduce random eviction
- [x] Add the minimal selective verification/reproducibility hook needed before allocator/reclaim Phase 2 work
- [ ] Decide explicit policy for REP, softfloat, and arm64 parity gaps
- [ ] Define initial benchmark workload set

## Deliverables tracker

| Deliverable | Status | Location |
|---|---|---|
| Detailed investigation report | Complete | `docs/plans/new-dynarec-investigation.md` |
| Executive summary / tracker | Complete | `docs/plans/new-dynarec-executive-summary.md` |
| Running changelog | Complete | `docs/plans/new-dynarec-changelog.md` |
| Optimization overview | Complete | `docs/plans/new-dynarec-optimization-overview.md` |
| Phase 0 core instrumentation | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen.c`, `src/codegen_new/codegen_block.c`, `src/cpu/386_dynarec.c` |
| Phase 0 runtime stats summaries | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen_block.c`, `src/86box.c`, `tests/codegen_new_dynarec_observability_test.c` |
| Phase 0 selective verify sampling | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen.c`, `tests/codegen_new_dynarec_observability_test.c` |
| Phase 1 page-list sentinel fix | Complete | `src/include/86box/mem.h`, `src/mem/mem_evict_list.c`, `src/mem/mem.c`, `src/mem/row.c`, `tests/mem_evict_list_test.c` |
| Phase 1 byte-mask reclaim hardening | Complete | `src/include/86box/mem.h`, `src/mem/mem_evict_list.c`, `src/codegen_new/codegen_block.c`, `tests/mem_evict_list_test.c` |
| Phase 1 purge failure-mode counters | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen_block.c`, `tests/codegen_new_dynarec_observability_test.c` |
| Phase 1 stale-head purge pruning | Complete | `src/include/86box/mem.h`, `src/mem/mem_evict_list.c`, `src/codegen_new/codegen_block.c`, `tests/mem_evict_list_test.c` |
| Phase 1 post-purge dirty-list reuse fix | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_block.c`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_allocator_pressure_policy_test.c`, `tests/codegen_new_dynarec_observability_test.c` |
| Phase 1 stale code-mask cleanup | Complete | `src/include/86box/mem.h`, `src/mem/mem_evict_list.c`, `src/codegen_new/codegen.h`, `src/codegen_new/codegen_block.c`, `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `tests/mem_evict_list_test.c`, `tests/codegen_new_dynarec_observability_test.c` |
| Phase 1 empty-purge observability | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_block.c`, `src/codegen_new/codegen_observability.c`, `src/mem/mem_evict_list.c`, `src/86box.c`, `tests/codegen_new_dynarec_observability_test.c`, `tests/mem_evict_list_test.c` |
| Phase 1 bulk-dirty overlap-gated enqueue | Complete | `src/include/86box/mem.h`, `src/mem/mem.c`, `src/mem/mem_evict_list.c`, `tests/mem_evict_list_test.c` |
| Phase 1 boundary validation and probe cleanup | Complete | `src/cpu/codegen_public.h`, `src/include/86box/mem.h`, `src/mem/mem.c`, `src/mem/mem_evict_list.c`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_dynarec_observability_test.c`, `tests/mem_evict_list_test.c` |
| Initial workload validation evidence | Complete | `/tmp/phase1_user_run.log`, `/tmp/phase1_user_run_after_fix.log`, `/tmp/phase1_post_purge_dirty_reuse.log`, `/tmp/phase1_stale_mask_cleanup.log` |
| Validation matrix | Reduced to small remainder | Selective verify sampling is in tree; broader workload matrix still TBD |
| Benchmark plan | Not started | TBD |

## Open decisions

- Is full arm64 3DNow parity a project goal, or should it be an explicit non-goal with instrumentation?
- Is a deeper CPU shadow-execution tier still worth building later, or is selective sampling plus focused regressions enough for the expected Phase 2 policy work?
- Should REP direct recompilation be pursued immediately, or tracked first with per-opcode fallback counters?
- What guest workload set will be the standing regression corpus for CPU dynarec changes?

## Update instructions

When updating this document:

- Keep the status snapshot and workstream tracker current.
- Keep the phase progress charts aligned with the current percentages and phase readouts.
- Update progress percentages conservatively.
- Mark risks closed only when the fix is implemented and validated.
- Add new deliverables rather than rewriting history.
