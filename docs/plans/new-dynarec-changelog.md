# New Dynarec Changelog

Related docs:

- [new-dynarec-investigation.md](./new-dynarec-investigation.md)
- [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md)

## Purpose

This is the running changelog for the CPU new dynarec investigation and follow-on work. Use it to record meaningful planning, implementation, validation, and benchmarking changes in chronological order.

## Entry rules

- Add newest entries at the top.
- Keep entries short and factual.
- Record what changed, why it mattered, and the current status.
- When possible, link to the document, plan, or code change that introduced the change.
- Do not log speculative ideas here unless they changed project direction or priorities.

## Entry template

```md
## YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Validated
- ...

### Open
- ...
```

## 2026-03-07

### Added
- Added CPU new dynarec observability counters for block mark/recompile activity, direct-recompiled instruction count, helper-call fallbacks, NULL direct-table hits, direct-handler bailouts, invalidations, block degradation, and allocator pressure/random eviction events.
- Added a structured CPU dynarec trace-hook API in `src/cpu/codegen_public.h` and `src/codegen_new/codegen_observability.c`.
- Added a focused standalone observability API test at `tests/codegen_new_dynarec_observability_test.c`.
- Added a focused standalone purgeable-page evict-list regression test at `tests/mem_evict_list_test.c`.

### Changed
- Wired the new counters into the CPU new dynarec front-end and block lifecycle in `src/codegen_new/codegen.c`, `src/codegen_new/codegen_block.c`, and `src/cpu/386_dynarec.c`.
- Updated the executive summary to mark Phase 0 observability work as started and to reflect the new baseline capability.
- Fixed the purgeable-page evict-list to use distinct "not in list" and "end of list" sentinels, which makes real page 0 representable and removes the empty-list collision.
- Moved the evict-list operations into `src/mem/mem_evict_list.c` so the page-list logic can be tested directly.

### Validated
- Confirmed a red/green cycle for the new observability API with a standalone compile-and-run test.
- Confirmed page 0 can be inserted and removed from the purgeable-page evict list correctly with a standalone compile-and-run regression test.
- Confirmed the initial Phase 0 implementation stays focused on CPU dynarec observability rather than coverage or performance tuning.

### Open
- There is still no selective dynarec-vs-interpreter verify mode or benchmark corpus.
- The byte-mask dirty-page reclaim path is still open follow-on work for Phase 1.

## 2026-03-07

### Added
- Created the detailed dynarec investigation report at `docs/plans/new-dynarec-investigation.md`.
- Created the executive summary tracker at `docs/plans/new-dynarec-executive-summary.md`.
- Created this living changelog at `docs/plans/new-dynarec-changelog.md`.

### Changed
- Established the working structure for this effort:
  - detailed technical report
  - executive summary with progress trackers
  - changelog for ongoing updates

### Validated
- Confirmed the current focus remains planning-only.
- Confirmed no source implementation changes have been made yet.

### Open
- Phase 0 observability package is not defined yet.
- Invalidation and reclamation fixes are not started yet.
- Validation matrix and benchmark plan are not started yet.
