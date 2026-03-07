# New Dynarec Executive Summary

Detailed investigation: [new-dynarec-investigation.md](./new-dynarec-investigation.md)
Changelog: [new-dynarec-changelog.md](./new-dynarec-changelog.md)

## Purpose

This document is the executive-level status view for the CPU new dynarec investigation and follow-on improvement work. It is meant to stay short, update cleanly, and show progress at a glance while the detailed technical analysis lives in the linked investigation report.

## Status snapshot

Last updated: 2026-03-07

| Area | Status | Progress | Notes |
|---|---|---:|---|
| Investigation and architecture review | Complete | 100% | Detailed report written and saved |
| Implementation work | In progress | 20% | Phase 0 core counters and trace hooks landed |
| Correctness risk mitigation | In progress | 15% | Page-0 evict-list sentinel bug fixed and regression-tested |
| Coverage closure | Not started | 0% | REP, softfloat, arm64 parity gaps remain |
| Observability and validation tooling | In progress | 45% | Core CPU dynarec counters, trace hook API, and a focused API test are in tree |
| Performance optimization | Not started | 0% | Deferred until correctness + observability |

## Current executive readout

- Phase 0 implementation has started with low-overhead CPU dynarec observability hooks.
- The page-0 purgeable-page evict-list sentinel collision is now fixed and covered by a focused regression test.
- The highest-priority remaining correctness issue is the byte-mask dirty-page reclaim path.
- CPU dynarec now exposes counters for block mark/recompile activity, direct-vs-helper fallback behavior, invalidations, block degradation, and allocator pressure, plus a structured trace-hook API for rare events.
- Direct-recompile coverage is materially incomplete in REP, softfloat, and arm64 MMX/3DNow paths.
- Full verify-mode and dynarec-vs-interpreter reproducibility tooling are still not implemented.
- Performance work should not begin until invalidation/reclamation behavior is fixed and measured with the new counters.

## Workstream tracker

| Workstream | Objective | Status | Progress | Exit condition |
|---|---|---|---:|---|
| WS0: Investigation | Capture architecture, risks, gaps, and plan | Complete | 100% | Report accepted as planning baseline |
| WS1: Observability | Add counters, traces, and verify hooks | In progress | 45% | Core counters and trace hooks are in; verify mode and runtime reporting remain |
| WS2: Invalidation and reclamation | Fix page-list correctness and dirty-page reclaim behavior | In progress | 15% | Page-list sentinel bug fixed; dirty-page reclaim behavior still open |
| WS3: Coverage closure | Reduce silent helper-path and unsupported direct coverage gaps | Not started | 0% | Direct support matrix is explicit and top fallback clusters are addressed |
| WS4: Backend parity | Close meaningful arm64 vs x86-64 CPU dynarec deltas | Not started | 0% | Backend differences are intentional, measured, and documented |
| WS5: Performance | Improve warmup, eviction policy, and hot-path code quality | Not started | 0% | Performance work is guided by instrumentation and benchmark baselines |
| WS6: Validation and benchmarking | Add regression corpus and benchmark gates | Not started | 0% | Reproducible correctness and performance checks exist |

## Phase progress charts

Scale: 20 slots per phase. `#` = completed progress, `-` = remaining work.

| Phase | Status | Chart | Progress | Current readout |
|---|---|---|---:|---|
| Phase 0: Observability and reproducibility | In progress | `[#########-----------]` | 45% | Core counters and structured trace hooks landed; selective dynarec-vs-interpreter checks still open |
| Phase 1: Invalidation and reclamation hardening | In progress | `[###-----------------]` | 15% | Page-0 list fix landed; byte-mask reclaim fix and targeted invalidation tests remain |
| Phase 2: Coverage closure and policy | Not started | `[--------------------]` | 0% | Backend support matrix, REP policy, and bailout reduction plan not started |
| Phase 3: Backend performance work | Not started | `[--------------------]` | 0% | Eviction-policy work, arm64 optimization pass, and reciprocal/rsqrt cleanup not started |
| Phase 4: Release-quality validation | Not started | `[--------------------]` | 0% | Benchmark corpus, scripted sweeps, and threshold-based regression checks not started |

### Phase 0 chart basis

- Completed slice: counter surface, trace-hook surface, direct-vs-helper fallback accounting, invalidation/degradation accounting, allocator-pressure/random-eviction accounting, focused API verification.
- Remaining slice: selective dynarec-vs-interpreter verification, runtime reporting/consumers, reproducibility matrix, benchmark-backed validation.

## Top risks tracker

| Rank | Risk | Severity | Current status | Planned response |
|---|---|---|---|---|
| 1 | Byte-mask dirty pages likely not reclaimed through purge path | High | Open | Rework reclaim flow and add focused tests |
| 2 | Purgeable-page list collides with page 0 | Critical | Closed | Distinct evict-list sentinels implemented and regression-tested |
| 3 | REP / softfloat / arm64 3DNow coverage cliffs | High | Open | Build explicit support matrix and prioritize high-value gaps |
| 4 | Known direct-recompile correctness regressions | High | Open | Reproduce and track guest-facing failures separately |
| 5 | No CPU dynarec observability or verify tooling | High | In progress | Core counters + trace hooks landed; selective A/B validation still needed |
| 6 | Fixed exec pool plus random block eviction | Medium | Open | Replace with data-driven reuse/eviction policy later |

## Immediate next-step tracker

- [x] Write detailed investigation report
- [x] Set up executive summary / progress tracker
- [x] Define and implement the first CPU dynarec observability package for Phase 0
- [x] Fix the page-0 evict-list sentinel collision with a focused regression test
- [ ] Define focused invalidation/reclamation test matrix for byte-mask reclaim behavior
- [ ] Decide explicit policy for REP, softfloat, and arm64 parity gaps
- [ ] Define initial benchmark workload set

## Deliverables tracker

| Deliverable | Status | Location |
|---|---|---|
| Detailed investigation report | Complete | `docs/plans/new-dynarec-investigation.md` |
| Executive summary / tracker | Complete | `docs/plans/new-dynarec-executive-summary.md` |
| Running changelog | Complete | `docs/plans/new-dynarec-changelog.md` |
| Phase 0 core instrumentation | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen.c`, `src/codegen_new/codegen_block.c`, `src/cpu/386_dynarec.c` |
| Phase 1 page-list sentinel fix | Complete | `src/include/86box/mem.h`, `src/mem/mem_evict_list.c`, `src/mem/mem.c`, `src/mem/row.c`, `tests/mem_evict_list_test.c` |
| Validation matrix | Not started | TBD |
| Benchmark plan | Not started | TBD |

## Open decisions

- Is full arm64 3DNow parity a project goal, or should it be an explicit non-goal with instrumentation?
- Should CPU verify mode be selective sampling, full shadow execution, or both in different tiers now that the counter baseline exists?
- Should REP direct recompilation be pursued immediately, or tracked first with per-opcode fallback counters?
- What guest workload set will be the standing regression corpus for CPU dynarec changes?

## Update instructions

When updating this document:

- Keep the status snapshot and workstream tracker current.
- Keep the phase progress charts aligned with the current percentages and phase readouts.
- Update progress percentages conservatively.
- Mark risks closed only when the fix is implemented and validated.
- Add new deliverables rather than rewriting history.
