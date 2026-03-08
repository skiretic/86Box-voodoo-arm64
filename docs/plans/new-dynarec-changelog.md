# New Dynarec Changelog

Related docs:

- [new-dynarec-investigation.md](./new-dynarec-investigation.md)
- [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md)
- [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md)

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

## 2026-03-08

### Added
- Added the second measured far control / frame legality-first slice after `ENTER`: `POPF` (`0x9d`) now has direct CPU dynarec handlers for the non-V86 path, with focused coverage-policy assertions for that exact one-opcode follow-up.
- Added the first far control / frame follow-up after the measured non-REP `STOS`/`LODS` batch: `ENTER` (`0xc8`) now has direct CPU dynarec handlers in both operand-size modes, with focused coverage-policy assertions for that exact one-opcode slice.
- Added an explicit pivot note across the Phase docs recording that allocator/reclaim policy experiments are paused after stable but low-yield 3DMark99 results, and that active implementation has moved to CPU dynarec coverage closure.
- Added the first coverage-closure implementation step after that pivot: arm64 no longer forces `PMADDWD` through helper fallback, and the repo now has a focused coverage-policy test for that support decision.
- Added the next narrow coverage-closure step: arm64 now uses the existing direct 3DNow table entries instead of compiling that whole table out to zero.
- Added the cumulative direct 3DNow coverage step that closes the remaining generator gap: `PI2FW`, `PF2IW`, `PFNACC`, `PFPNACC`, `PFACC`, `PMULHRW`, `PSWAPD`, and `PAVGUSB` now have direct CPU dynarec handlers.
- Added optional per-hit 3DNow shutdown logging through `86BOX_NEW_DYNAREC_LOG_3DNOW_HITS=1`, so guest runs can now report every exercised 3DNow suffix and whether each hit stayed direct, hit a `NULL` direct-table entry, or bailed out of the direct handler.
- Added optional helper fallback family shutdown logging through `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1`, plus optional base-opcode fallback shutdown logging through `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1`, so workload runs can now narrow the remaining helper-path load from family level down to exact plain opcodes.
- Added the first measured base-opcode reduction batch: non-REP `STOS`/`LODS` (`0xaa`, `0xab`, `0xac`, `0xad`) now have direct CPU dynarec handlers in both address-size modes, with a focused coverage-policy assertion for that exact four-opcode subset.
- Added the first explicit Phase 2 allocator/reclaim policy experiment: after a random eviction, the next 8 first-hit mark opportunities are interpreted without allocating a new mark-only block, and the runtime summary now counts those skips as `deferred_block_marks`.
- Added a selective CPU dynarec verify-sampling surface on the direct-vs-helper decision boundary, including optional `86BOX_NEW_DYNAREC_VERIFY_PC`, `86BOX_NEW_DYNAREC_VERIFY_OPCODE`, and `86BOX_NEW_DYNAREC_VERIFY_BUDGET` filters plus per-sample counters in the existing summary output.
- Added an explicit Phase 1 closeout pass across the executive summary, changelog, and optimization overview so the remaining Windows 98 + 3DMark99 allocator-pressure behavior is documented as expected scarcity for this workload rather than an open Phase 1 bug.

### Changed
- Changed the next far-control/frame implementation choice again from the remaining mixed cluster to a second legality-first slice: `0x9d` landed ahead of `0x9a`, `0xca`, and `0xcb` because it avoids far-transfer segment/gate semantics and can still fall back in the V86-sensitive path.
- Changed the next far-control/frame implementation choice from the full five-opcode bucket to a legality-first slice: `0xc8` landed ahead of `0x9a`, `0xca`, `0x9d`, and `0xcb` because it is the only remaining table hole in that cluster that does not require protected-mode far-transfer or `POPF` privilege semantics.
- Fixed the first `0x9d` implementation attempt by correcting the IR-width mix in the upper-EFLAGS combine path, which had produced an unsupported backend `AND_IMM` form and an immediate guest crash on the first logged runtime attempt.
- Changed the allocator-policy readout from "active tuning" to "paused after first experiments" because the crash fix held, the policy activated at scale, and the stronger empty-purge trigger still did not produce a clear enough random-eviction win to justify more immediate tuning.
- Changed the `recomp_opcodes_0f` arm64 table so `PMADDWD` uses the same direct recompiler entry as x86-64, aligning the frontend table with the already-existing arm64 `UOP_PMADDWD` backend support.
- Changed `recomp_opcodes_3DNOW` so arm64 no longer compiles it to an all-zero table; the shared non-`NULL` entries are now enabled on both backends.
- Changed the 3DNow direct-coverage test from a boolean smoke test into an exact 24-opcode support matrix so earlier coverage stays visible while new suffixes are added.
- Changed the 3DNow dispatch gate in `src/codegen_new/codegen.c` so Enhanced 3DNow-only suffixes only direct-compile when the active dynarec opcode table is `dynarec_ops_3DNOWE`, preserving legality on plain 3DNow CPUs.
- Changed the 3DNow validation strategy from one-opcode-at-a-time verify filtering to cumulative per-hit runtime logging, which is a better fit for guest workload sweeps.
- Changed the post-3DNow coverage prioritization strategy from speculation about REP/softfloat to measured fallback narrowing: first by family, then by exact base opcode.
- Changed the first base-opcode implementation choice from the still-open mixed string/far-control bucket to the narrower non-REP `STOS`/`LODS` slice because it covers four measured hot opcodes (`0xaa`, `0xab`, `0xac`, `0xad`) without taking on far-call / privilege / flags complexity in the same batch.
- Changed the mark-only path in `src/cpu/386_dynarec.c` so cold first-hit blocks can be deferred briefly after random-eviction pressure instead of immediately consuming another block slot under the same scarcity window.
- Expanded the retained observability surface with a `NEW_DYNAREC_TRACE_BLOCK_MARK_DEFERRED` event, a fixed Phase 2 deferral budget constant, and summary/test coverage for the new `deferred_block_marks` counter.
- Wired the selective verify samples into `src/codegen_new/codegen.c` so targeted direct hits, NULL-table fallbacks, and direct-handler bailouts can be reproduced through the existing trace-hook and runtime-summary path without adding a larger framework.
- Trimmed the temporary late-Phase-1 boundary probes from the durable CPU dynarec stats surface by removing `purgable_page_missed_write_enqueue_overlap`, `purgable_page_reenqueues_after_flush`, and `purgable_page_reenqueues_after_no_blocks`.
- Trimmed dequeue-by-enqueue-source probe counters from the durable stats surface, keeping the aggregate purge-list lifecycle counters and enqueue-source counters that remain useful for future Phase 2 allocator/reclaim policy work.
- Simplified the purgeable-page list bookkeeping by removing the last-dequeue-reason state that only existed to feed the temporary re-enqueue probes.

### Validated
- Confirmed a red/green cycle for the narrowed far-control/frame coverage-policy test after moving `0x9d`, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully, and re-signed `build/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed guest-visible `ENTER` payoff with `/tmp/new_dynarec_enter_validation.log`: the shutdown fallback-family summary reached `base=28738`, `0f=4886`, `x87=410`, `rep=6700`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0xc8`.
- Confirmed the first `POPF` runtime crash reproduced with `/tmp/new_dynarec_popf_validation.log`, then traced it to the unsupported mixed-width `AND_IMM` / `OR` sequence in the new upper-EFLAGS merge logic.
- Confirmed the post-fix 3DMark99 rerun with `/tmp/new_dynarec_popf_validation.log` reached clean shutdown with fallback families `base=25331`, `0f=4672`, `x87=451`, `rep=6754`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0x9d` or `0xc8`.
- Confirmed a red/green cycle for the narrowed far-control/frame coverage-policy test after asserting that only `0xc8` moved in this batch, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully, and re-signed `build/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed a clean 3DMark99 shutdown after the deferred-mark cleanup fix with no `Deleting deleted block` fatal in `/tmp/phase2_mark_deferral.log`.
- Confirmed the first allocator-policy experiment was active at scale under 3DMark99 (`deferred_block_marks=1912405`) but still ended with `random_evictions=239633`, so the workload remained scarcity-dominated.
- Confirmed the stronger trigger that arms the same fixed deferral window on empty-purge allocator pressure also remained stable under 3DMark99 and reached shutdown with `deferred_block_marks=1938759` and `random_evictions=239338`, which was not a clear enough win to keep prioritizing allocator-policy tuning ahead of coverage closure.
- Confirmed a red/green cycle for the new coverage-policy test in `tests/codegen_new_opcode_coverage_policy_test.c`, then rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully after enabling direct arm64 `PMADDWD`.
- Confirmed the same focused coverage-policy test and target build still pass after enabling the existing direct 3DNow table on arm64.
- Confirmed a second red/green cycle for `tests/codegen_new_opcode_coverage_policy_test.c` after expanding it to the full 24-opcode direct 3DNow matrix, then rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully after landing the remaining generator batch.
- Confirmed a red/green cycle for the new per-hit 3DNow logging API in `tests/codegen_new_dynarec_observability_test.c`, then re-signed and rebuilt `86Box` successfully before guest runtime checks.
- Confirmed guest-visible direct-path evidence with `/tmp/3dnow_hit_report.log`: across Windows 98 + 3DMark99 and longer mixed runs, 16 suffixes (`0x0d`, `0x1d`, `0x90`, `0x94`, `0x96`, `0x97`, `0x9a`, `0x9e`, `0xa0`, `0xa4`, `0xa6`, `0xaa`, `0xae`, `0xb0`, `0xb4`, `0xb6`) were exercised, and every observed hit stayed direct with `helper_table_null=0` and `helper_bailout=0`.
- Confirmed a red/green cycle for the new fallback-family and base-opcode fallback logging APIs in `tests/codegen_new_dynarec_observability_test.c`, then rebuilt and re-signed `86Box` successfully before guest runtime checks.
- Confirmed fallback-family workload evidence with `/tmp/fallback_family_report.log`: `base=48526`, `rep=8100`, `0f=6763`, `x87=2843`, and `3dnow=0`, which moves the next implementation target away from 3DNow/softfloat and toward the base-opcode bucket.
- Confirmed the first base-opcode breakdown with `/tmp/base_fallback_report.log`: the dominant plain-opcode fallback cluster is led by `0x9a`, `0xca`, `0xab`, `0xc8`, `0xf7`, `0xad`, `0x9d`, `0xcb`, `0xa5`, `0x6b`, `0xac`, and `0xff`.
- Confirmed a red/green cycle for the narrowed base-opcode coverage-policy test after locking the first exact plain-opcode subset (`0xaa`, `0xab`, `0xac`, `0xad`), then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully, and re-signed `build/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed the guest-visible payoff of that first base-opcode batch with `/tmp/new_dynarec_base_hotspot_rerun.log`: the shutdown fallback-family summary tightened to `base=31127`, `0f=4880`, `x87=319`, `rep=6818`, and `3dnow=0`, and the shutdown base-opcode report no longer contains `0xaa`, `0xab`, `0xac`, or `0xad`.
- Confirmed from that same rerun that the next measured base-opcode set is now led by far control/frame rather than the first string slice: `0x9a`, `0xca`, `0xc8`, `0x9d`, `0xcb`, then `0xa5`, `0xf7`, `0x6b`, and `0xff`.
- Confirmed a focused red/green cycle for the Phase 2 policy helper surface in `tests/codegen_new_allocator_pressure_policy_test.c`, including deferral-budget consumption after random eviction and summary formatting for `deferred_block_marks`.
- Confirmed the updated observability API test covers the new deferred-mark trace event, counter reset behavior, and summary formatting in `tests/codegen_new_dynarec_observability_test.c`.
- Confirmed focused standalone coverage for the new verify-sampling API in `tests/codegen_new_dynarec_observability_test.c`, including filter matching, budget exhaustion, trace emission, and summary counter formatting.
- Reconfirmed the late-Phase-1 boundary evidence from `/tmp/phase1_reenqueue_probe.log`: `allocator_pressure_events=772998`, `allocator_pressure_empty_purgable_list=756683`, `purgable_page_enqueues=11341`, `purgable_page_enqueues_write=8042`, `purgable_page_enqueues_codegen=3084`, `purgable_page_enqueues_bulk_dirty=215`, `purgable_page_dequeues_stale=0`, `purgable_page_dequeues_flush=8328`, `purgable_page_dequeues_no_blocks=3013`, `purgable_flush_attempts=8008`, `purgable_flush_successes=101`, `purgable_flush_no_overlap=0`, `purgable_flush_no_free_block=7907`, `purgable_flush_no_blocks=339`, `purgable_flush_dirty_list_reuses=6998`, and `random_evictions=748517`.
- Reconfirmed from that same boundary run that the removed temporary probes all closed at zero before cleanup: `purgable_page_missed_write_enqueue_overlap=0`, `purgable_page_reenqueues_after_flush=0`, and `purgable_page_reenqueues_after_no_blocks=0`.

### Open
- There is still no full CPU shadow-execution verify mode or benchmark corpus, but the minimal selective sampling surface needed before Phase 2 is now present.
- The allocator-policy path is paused, not closed permanently. It should be revisited later if coverage work changes the hot-path mix or exposes a better eviction/admission lever.
- Larger coverage gaps remain: the narrowed base-opcode list now includes `0x9a`, `0xca`, `0xf7`, `0xcb`, `0xa5`, `0x6b`, and `0xff`; within the far control/frame cluster, `0x9a`, `0xca`, and `0xcb` still carry the protected-mode far-transfer risk, while `0xa5` is the safer non-far measured follow-up. REP and `0F` still trail behind that as measured secondary targets, and broader guest-visible workload coverage for the still-unhit direct 3DNow suffixes (`0x0c`, `0x1c`, `0x8a`, `0x8e`, `0xa7`, `0xb7`, `0xbb`, `0xbf`) is still open.

## 2026-03-07

### Added
- Added `docs/plans/new-dynarec-optimization-overview.md`, a maintained explanation of the CPU dynarec optimizations and hardening work organized by area instead of by date.
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
