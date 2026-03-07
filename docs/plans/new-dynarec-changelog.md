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
- Added a focused allocator-pressure policy regression test at `tests/codegen_new_allocator_pressure_policy_test.c` covering the post-purge decision boundary.
- Documented the reliable GUI launch path for the Windows 98 Low End VM in `scripts/test-with-vm.sh`, including optional logfile and CPU dynarec stats support.
- Added a focused stale-code-presence regression in `tests/mem_evict_list_test.c` covering pages that retain code masks after their last live block is gone.
- Added an `allocator_pressure_empty_purgable_list` counter so Phase 1 can measure how often allocator pressure finds no purge candidates at all.
- Added purge-list lifecycle counters for page enqueue, stale-head dequeue, flush dequeue, and no-blocks dequeue so the empty-purge result can be traced to enqueue vs removal behavior.
- Added a focused `page_enqueue_if_reclaimable()` regression in `tests/mem_evict_list_test.c` covering the bulk-dirty no-overlap case.
- Added source-attributed dequeue, missed-write-enqueue, and re-enqueue probes so the remaining allocator-pressure behavior can be classified as churn versus expected scarcity.
- Added CPU dynarec runtime summary logging so allocator-pressure, purge-success, random-eviction, and shutdown snapshots can be captured from real guest workloads.
- Added purge failure-mode counters for `purgable_flush_no_overlap` and `purgable_flush_no_free_block`.
- Added CPU new dynarec observability counters for block mark/recompile activity, direct-recompiled instruction count, helper-call fallbacks, NULL direct-table hits, direct-handler bailouts, invalidations, block degradation, and allocator pressure/random eviction events.
- Added a structured CPU dynarec trace-hook API in `src/cpu/codegen_public.h` and `src/codegen_new/codegen_observability.c`.
- Added a focused standalone observability API test at `tests/codegen_new_dynarec_observability_test.c`.
- Added a focused standalone purgeable-page evict-list regression test at `tests/mem_evict_list_test.c`.

### Changed
- Wired the new counters into the CPU new dynarec front-end and block lifecycle in `src/codegen_new/codegen.c`, `src/codegen_new/codegen_block.c`, and `src/cpu/386_dynarec.c`.
- Updated the executive summary to mark Phase 0 observability work as started and to reflect the new baseline capability.
- Fixed the purgeable-page evict-list to use distinct "not in list" and "end of list" sentinels, which makes real page 0 representable and removes the empty-list collision.
- Moved the evict-list operations into `src/mem/mem_evict_list.c` so the page-list logic can be tested directly.
- Hardened byte-mask reclaim handling so purge checks consider byte-level overlap and flush completion clears byte-level dirty/code state before removing the page from the evict list.
- Split purge failures into dead-head (`no overlap`) versus post-flush-no-free-block cases so workload logs can show whether the reclaim path is selecting useless pages or failing after invalidation work.
- Changed purge walking so it prunes stale head entries and continues across reclaimable pages until it either frees a block or exhausts the purge list.
- Changed allocator-pressure recovery so a purge that invalidates blocks into the dirty list now retries dirty-list reuse before falling through to random eviction.
- Added a `purgable_flush_dirty_list_reuses` counter plus a small post-purge classifier API so workload logs and focused tests can distinguish "purge found reusable dirty blocks" from "purge truly left no reclaim path".
- Changed last-block removal to clear stale `code_present_mask` / `byte_code_present_mask` state when a page no longer has any live blocks, preventing later purge attempts from seeing overlap on pages with nothing left to invalidate.
- Added a `purgable_flush_no_blocks` counter so workload logs can distinguish "purge found overlap but invalidated zero blocks" from other no-free-block outcomes.
- Added empty-purge instrumentation at the allocator-pressure decision point so the next workload rerun can confirm whether an empty purge list is the dominant remaining failure mode.
- Expanded the runtime/shutdown summary buffers so the full CPU dynarec stats line no longer truncates as counters are added.
- Changed `mem_invalidate_range()` so bulk-dirty invalidation only enqueues pages that actually have dirty/code overlap, instead of unconditionally feeding non-reclaimable pages into the purge list.
- Fixed the source-attribution probe so failed bulk-dirty enqueue attempts do not leave stale source tags behind.

### Validated
- Confirmed a red/green cycle for the new observability API with a standalone compile-and-run test.
- Confirmed page 0 can be inserted and removed from the purgeable-page evict list correctly with a standalone compile-and-run regression test.
- Confirmed byte-mask-only overlap is recognized and cleared by the standalone purgeable-page regression test.
- Confirmed stale purge-list head entries are pruned until the next reclaimable page, with focused standalone coverage in `tests/mem_evict_list_test.c`.
- Confirmed the initial Phase 0 implementation stays focused on CPU dynarec observability rather than coverage or performance tuning.
- Confirmed runtime stats summaries are emitted for real Windows 98 guest workloads and at clean shutdown.
- Confirmed a Windows 98 run through the first two 3DMark99 benchmarks reached sustained allocator pressure: shutdown counters showed `allocator_pressure_events=759218`, `purgable_flush_attempts=714642`, `purgable_flush_successes=60`, `purgable_flush_no_overlap=707776`, `purgable_flush_no_free_block=6806`, and `random_evictions=730387`.
- Confirmed a same-workload rerun after stale-head pruning cut `purgable_flush_attempts` from `714642` to `10455` and `purgable_flush_no_overlap` from `707776` to `2592`, but `random_evictions` remained high at `795608`, so the dead-head issue is reduced without resolving the overall allocator-pressure failure.
- Confirmed with focused regression coverage that the post-purge state `(free_list empty, dirty_list non-empty)` is now classified as dirty-list reuse instead of random eviction.
- Confirmed standalone compile-and-run coverage for the new allocator-pressure policy test, updated observability test, and existing evict-list test.
- Confirmed `cmake --build build --target 86Box cpu dynarec mem -j4` succeeds after the fix; the link step still emits the pre-existing macOS deployment-target warnings about Homebrew dylibs built for newer macOS versions.
- Confirmed the reliable GUI launch form in this workspace is `open -n -a "/Users/anthony/projects/code/86Box-voodoo-arm64/build/src/86Box.app" --args -P "/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy"`, with optional `-L <log>` and `env 86BOX_NEW_DYNAREC_STATS=1`.
- Confirmed a Windows 98 + 3DMark99 rerun after the post-purge dirty-list reuse fix reached shutdown with `allocator_pressure_events=771237`, `purgable_flush_attempts=11427`, `purgable_flush_successes=112`, `purgable_flush_no_overlap=2260`, `purgable_flush_no_free_block=9055`, `purgable_flush_dirty_list_reuses=7691`, and `random_evictions=744661`.
- Confirmed the dirty-list reuse fix is materially exercised under workload (`purgable_flush_dirty_list_reuses=7691`) and reduces random eviction versus the prior post-stale-head baseline (`795608 -> 744661`) without resolving the entire allocator-pressure failure.
- Confirmed the stale-code-presence regression, observability test, allocator-pressure policy test, and `cmake --build build --target 86Box cpu dynarec mem -j4` all pass after the stale-mask cleanup change.
- Confirmed a Windows 98 + 3DMark99 rerun after stale-mask cleanup reached shutdown with `allocator_pressure_events=782156`, `purgable_flush_attempts=10065`, `purgable_flush_successes=106`, `purgable_flush_no_overlap=2354`, `purgable_flush_no_free_block=7605`, `purgable_flush_no_blocks=346`, `purgable_flush_dirty_list_reuses=6730`, and `random_evictions=758004`.
- Confirmed the stale-mask cleanup reduces residual no-free-block purge churn (`9055 -> 7605`), but allocator pressure still dwarfs purge attempts (`782156` vs `10065`), making empty purge list under pressure the next logical Phase 1 target.
- Confirmed the updated observability test, stale-code-presence regression, allocator-pressure policy test, and `cmake --build build --target 86Box cpu dynarec mem -j4` all pass after adding the empty-purge allocator-pressure counter.
- Confirmed a follow-up Windows 98 + 3DMark99 rerun with the new empty-purge counter reached shutdown with `allocator_pressure_events=766311` and `allocator_pressure_empty_purgable_list=746905`, confirming that empty purge list under pressure is the dominant remaining Phase 1 failure mode.
- Confirmed the purge-list lifecycle instrumentation, expanded summary buffers, updated observability test, updated evict-list regression, allocator-pressure policy test, and `cmake --build build --target 86Box cpu dynarec mem -j4` all pass.
- Confirmed a follow-up Windows 98 + 3DMark99 rerun with enqueue-source counters reached shutdown with `allocator_pressure_events=804451`, `allocator_pressure_empty_purgable_list=785363`, `purgable_page_enqueues=24174`, `purgable_page_enqueues_write=7801`, `purgable_page_enqueues_codegen=3486`, `purgable_page_enqueues_bulk_dirty=12887`, `purgable_page_dequeues_stale=12649`, `purgable_page_dequeues_flush=7863`, `purgable_page_dequeues_no_blocks=3662`, `purgable_flush_attempts=10489`, `purgable_flush_successes=102`, `purgable_flush_no_overlap=2128`, `purgable_flush_no_free_block=8259`, `purgable_flush_no_blocks=356`, `purgable_flush_dirty_list_reuses=7314`, and `random_evictions=779455`.
- Confirmed from that workload plus code inspection that bulk-dirty invalidation was the single largest enqueue source and a substantial source of stale-list churn, making overlap-gated bulk-dirty enqueue the next concrete Phase 1 fix.
- Confirmed the updated evict-list regression, observability test, and `cmake --build build --target 86Box cpu dynarec mem -j4` all pass after the bulk-dirty overlap gate change.
- Confirmed a corrected follow-up Windows 98 + 3DMark99 rerun after the bulk-dirty overlap gate reached shutdown with `allocator_pressure_events=755099`, `allocator_pressure_empty_purgable_list=738750`, `purgable_page_enqueues_bulk_dirty=221`, `purgable_page_dequeues_stale=0`, `purgable_flush_no_overlap=0`, and `random_evictions=730188`, showing that the bogus bulk-dirty churn was removed.
- Confirmed a missed-write-enqueue probe run reached shutdown with `purgable_page_missed_write_enqueue_overlap=0`, ruling out write-side enqueue omission as the remaining empty-purge cause.
- Confirmed a re-enqueue probe run reached shutdown with `purgable_page_reenqueues_after_flush=0` and `purgable_page_reenqueues_after_no_blocks=0`, showing that pages are not commonly cycling back into the purge list after dequeue on this workload.
- Confirmed the remaining empty-purge behavior is therefore best explained by genuine scarcity of reclaimable pages under this workload, not another confirmed Phase 1 reclamation correctness bug.

### Open
- There is still no selective dynarec-vs-interpreter verify mode or benchmark corpus.
- The next step is no longer another Phase 1 bug hunt. Any further attempt to reduce the remaining random-eviction rate should be treated as allocator/reclaim policy work and evaluated separately from correctness fixes.

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
