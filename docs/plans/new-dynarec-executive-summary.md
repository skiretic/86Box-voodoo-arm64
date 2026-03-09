# New Dynarec Executive Summary

Detailed investigation: [new-dynarec-investigation.md](./new-dynarec-investigation.md)
Changelog: [new-dynarec-changelog.md](./new-dynarec-changelog.md)
Optimization overview: [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md)

## Purpose

This document is the executive-level status view for the CPU new dynarec investigation and follow-on improvement work. It is meant to stay short, update cleanly, and show progress at a glance while the detailed technical analysis lives in the linked investigation report.

## Status snapshot

Last updated: 2026-03-09

| Area | Status | Progress | Notes |
|---|---|---:|---|
| Investigation and architecture review | Complete | 100% | Detailed report written and saved |
| Implementation work | In progress | 93% | Phase 1 invalidation/reclamation hardening is complete; durable Phase 0/1 observability remains in tree, allocator-policy experiments are paused, the measured protected-mode and mixed-group hotspot sequence is closed through `0x9a`, `0xca`, `0xcb`, `0xf7`, `0xf6`, and `0xff`, the full `SETcc` row (`0x90`-`0x9f`) remains in tree with corrected boolean normalization, the `BSWAP` row (`0xc8`-`0xcf`) remains guest-validated, and base `0xa6` / `0xa7` (`CMPSB` / `CMPSW` / `CMPSD`) are now guest-validated on `Windows 98 SE` while both the `BSF` / `BSR` pair and the retried strict `0x0f 0xaf` path are currently kept at host-harness-only status pending guest-safe debugging; base `D0`-`D3` `RCL` / `RCR` now also has a narrow compare-only direct path in tree, but normal guest dispatch remains disabled |
| Correctness risk mitigation | Complete | 100% | Phase 1 is closed: page-0 sentinel, stale-head purge churn, post-purge dirty-list reuse, stale code-presence state, and bogus bulk-dirty enqueue churn were fixed and validated; remaining high random eviction is treated as reclaimable-page scarcity for this workload |
| Coverage closure | In progress | 82% | REP remains open and softfloat/x87 follow-up is intentionally deferred for now, but the direct 3DNow/3DNowE generator gap is now closed with cumulative regression coverage, a legality gate for 3DNowE-only suffixes, initial guest-visible direct-path validation, a measured fallback priority order, the protected-mode far-transfer / far-return sequence (`0x9a`, `0xca`, `0xcb`), the mixed-group cleanup sequence (`0xf7`, `0xf6`, `0xff`), the full `SETcc` row (`0x90`-`0x9f`), the guest-validated `BSWAP` row (`0xc8`-`0xcf`), and the guest-validated low-risk `CMPS` table-hole pair (`0xa6`, `0xa7`) are now in tree; `BSF` / `BSR` and `0x0f 0xaf` host-harness coverage now exist, but both direct guest paths remain paused after guest-visible boot failures, and the `0xaf` compare-debug pass indicates the remaining issue is outside the sampled result/`CF`-`OF` semantics path |
| Observability and validation tooling | In progress | 92% | Core CPU dynarec counters, runtime summary logging, purge failure-mode counters, empty-purge accounting, purge-list lifecycle counters, selective verify sampling, per-hit 3DNow shutdown logging, fallback-family logging, base-opcode fallback logging, the new `D0`-`D3` compare-only shutdown summary / mismatch logging surface, and Phase 2 mark-deferral accounting are in tree; temporary scarcity-boundary probes were retired after Phase 1 closure, and the temporary `0xaf` compare-only guest override has already been removed after producing its evidence |
| Performance optimization | Paused | 8% | Initial allocator/reclaim policy experiments were stable but low-yield on the 3DMark99 workload, so this track is paused while coverage work proceeds |

## Current executive readout

- Phase 0 observability is now reduced to a small, explicit remainder rather than an open-ended tooling bucket.
- The page-0 purgeable-page evict-list sentinel collision is now fixed and covered by a focused regression test.
- The byte-mask dirty-page reclaim path has been hardened in the purge/flush logic, and stale-head purge pruning now removes most dead-head list entries under workload pressure.
- Windows 98 + 3DMark99 reruns still show allocator pressure ending in heavy random eviction, so the remaining issue is no longer dominated by dead-head purge entries.
- CPU dynarec now exposes counters for block mark/recompile activity, direct-vs-helper fallback behavior, invalidations, block degradation, and allocator pressure, plus a structured trace-hook API for rare events.
- CPU dynarec now also exposes a selective verification/reproducibility hook on the direct-vs-helper decision boundary, with optional `86BOX_NEW_DYNAREC_VERIFY_PC`, `86BOX_NEW_DYNAREC_VERIFY_OPCODE`, and `86BOX_NEW_DYNAREC_VERIFY_BUDGET` filters plus per-sample counters in the existing runtime summary surface.
- Base `D0`-`D3` `RCL` / `RCR` now has the first narrow base-opcode compare-only path in tree as well: `86BOX_NEW_DYNAREC_DEBUG_D0D3_RCLRCR=1` enables the sampled direct-vs-helper check, `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE=1` emits mismatch/shutdown evidence, `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE_SITES=1` emits first-hit site discovery lines for later `VERIFY_PC` targeting, and normal guest dispatch still stays disabled unless that debug knob is explicitly set.
- That compare/debug path has now answered the main semantic question strongly enough for this branch stage: recent broad and locked-site runs matched helper semantics throughout sampled `RCL` / `RCR` traffic, so the next productive session should pivot back to low-risk implementation work instead of adding more `D0`-`D3` probes.
- That low-risk pivot has now validated cleanly too: base `0xa6` / `0xa7` (`CMPSB` / `CMPSW` / `CMPSD`) reached a normal `Windows 98 SE` shutdown with no remaining shutdown base-fallback entries for either opcode.
- The best first Phase 2 target was block-demand reduction rather than another reclaim-path rewrite: under the confirmed empty-purge scarcity signature, a first-hit mark-only block is the lowest-value resident because it consumes a block slot before it has ever produced direct code.
- The first Phase 2 policy step now in tree is a narrow cold-block admission experiment: after a random eviction, the next 8 first-hit mark opportunities are interpreted without allocating a new mark-only block, and each skipped mark is counted as `deferred_block_marks`.
- This keeps Phase 1 closed: the new policy is explicitly trying to reduce structurally high random eviction under scarcity, not reclassify the remaining behavior as another invalidation/reclamation correctness bug.
- The allocator-policy experiments are now paused rather than extended further: the crash path is fixed, the policy is active and stable under 3DMark99, but the stronger trigger still did not show a clear reduction in random eviction for this workload.
- Active implementation focus now shifts to CPU dynarec coverage closure, where the remaining open gaps are more concrete and likely to produce higher-confidence wins than continued short-loop admission-policy tuning.
- The first coverage-closure step now in tree is to enable direct `PMADDWD` recompilation on arm64, matching the existing shared MMX frontend and the already-present arm64 `UOP_PMADDWD` backend support instead of forcing that opcode through helper fallback.
- The next similarly narrow parity step now in tree is to stop zeroing the existing direct 3DNow table on arm64. This does not add new 3DNow generators; it enables the already-populated shared table entries against arm64 backend uOP support that was already present.
- The remaining direct 3DNow/3DNowE generator gap has now been closed in one cumulative batch: `PI2FW`, `PF2IW`, `PFNACC`, `PFPNACC`, `PFACC`, `PMULHRW`, `PSWAPD`, and `PAVGUSB` now have direct CPU new dynarec handlers, and 3DNowE-only suffixes are gated so they do not direct-compile on plain 3DNow CPUs.
- The standalone coverage-policy test now locks the exact 24-suffix direct 3DNow set instead of only asserting that "some 3DNow support exists", so future regressions in older or newer suffixes stay visible until guest-level validation is complete.
- CPU dynarec now also exposes optional per-hit 3DNow shutdown logging through `86BOX_NEW_DYNAREC_LOG_3DNOW_HITS=1`, which records every 3DNow suffix actually exercised by a guest run and whether each hit stayed direct, fell back because the table entry was `NULL`, or bailed out of the direct handler.
- Initial guest-visible validation is now in hand: Windows 98 + 3DMark99 and longer mixed runs exercised 16 suffixes (`0x0d`, `0x1d`, `0x90`, `0x94`, `0x96`, `0x97`, `0x9a`, `0x9e`, `0xa0`, `0xa4`, `0xa6`, `0xaa`, `0xae`, `0xb0`, `0xb4`, `0xb6`), and every observed hit stayed on the direct path with `helper_table_null=0` and `helper_bailout=0`.
- Fallback-family measurement is now in hand too: the first broad breakdown showed `base=48526`, `rep=8100`, `0f=6763`, `x87=2843`, and `3dnow=0`, so the next evidence-backed implementation target is the plain base-opcode fallback bucket rather than more 3DNow, REP, or x87 work.
- A deeper base-opcode breakdown then showed the dominant plain-opcode fallback cluster is not generic noise but a narrow set led by far control-transfer / frame ops and string ops: `0x9a`, `0xca`, `0xab`, `0xc8`, `0xf7`, `0xad`, `0x9d`, `0xcb`, `0xa5`, `0x6b`, `0xac`, and `0xff`.
- The current measured priority order is therefore: base string/far-control opcodes first, REP second, `0F` fallback third, and x87 / 3DNow behind those for this workload set.
- The first measured base-opcode reduction batch now in tree takes the narrowest coherent string-op slice from that hotspot list: non-REP `STOS`/`LODS` (`0xaa`, `0xab`, `0xac`, `0xad`) now direct-recompile in both address-size modes, and the focused coverage-policy test locks that exact four-opcode subset.
- A follow-up 3DMark99 rerun with fallback-family and base-opcode shutdown logging now confirms that result guest-visibly: `0xaa`, `0xab`, `0xac`, and `0xad` are absent from the shutdown base-fallback report, while the family summary tightened to `base=31127`, `0f=4880`, `x87=319`, `rep=6818`, and `3dnow=0`.
- That closes the first string-op batch and narrows the remaining measured base-opcode list to `0x9a`, `0xca`, `0xc8`, `0xf7`, `0x9d`, `0xcb`, `0xa5`, `0x6b`, and `0xff`, with the far control/frame subset then becoming the clear next measured implementation target ahead of `MOVS`, REP, x87, or more 3DNow work.
- The first landed far control/frame follow-up is the narrowest legality-safe slice from that set: `ENTER` (`0xc8`) now direct-recompiles in both operand-size modes, pairing with the already-direct `LEAVE` path without taking on protected-mode far-call / far-return or `POPF` privilege semantics in the same batch.
- A guest-visible rerun after that `ENTER` batch confirmed the payoff directly: shutdown fallback families reached `base=28738`, `0f=4886`, `x87=410`, `rep=6700`, and `3dnow=0`, and the base-opcode shutdown report no longer contained `0xc8`.
- The second landed far control/frame legality-first slice is now `POPF` (`0x9d`), implemented with direct non-V86 handling while still falling back on the protected/VME-sensitive V86 path.
- The first `POPF` runtime attempt exposed an IR-width bug in the new upper-EFLAGS combine path; that bug is now fixed, the app rebuilt and re-signed, and a follow-up 3DMark99 rerun reached clean shutdown with fallback families `base=25331`, `0f=4672`, `x87=451`, `rep=6754`, and `3dnow=0`.
- That rerun no longer contains either `0xc8` or `0x9d` in the shutdown base-fallback report, so the remaining measured base-opcode list at that point was `0x9a`, `0xca`, `0xf7`, `0xcb`, `0xa5`, `0x6b`, and `0xff`; within the far control/frame bucket, the open subset was `0x9a`, `0xca`, and `0xcb`.
- The next landed non-protected measured batch now covers `MOVS` word/dword (`0xa5`) and `IMUL r, r/m, imm8` (`0x6b`) together: `MOVS` extends the earlier string-op work without taking on REP or compare/scan flag semantics, while `0x6b` is the clean immediate-8 IMUL sibling that avoids the mixed-group `0xf7`/`0xff` surface.
- The first `0x6b` guest run exposed a backend-illegal `MOVZX` width mix in the new helper-backed flag-mask path; that crash is now fixed, the app rebuilt and re-signed, and the follow-up 3DMark99 rerun reached clean shutdown with fallback families `base=19237`, `0f=4063`, `x87=231`, `rep=5626`, and `3dnow=0`.
- That latest shutdown base-fallback report no longer contains `0xa5`, `0x6b`, `0x9d`, or `0xc8`, so the measured remaining hotspot list is now `0x9a`, `0xca`, `0xcb`, `0xf7`, and `0xff`, with smaller newly visible siblings like `0x69` and `0xa4` now easier to justify as the next low-risk non-protected follow-ups.
- The next low-risk follow-up first landed as `IMUL r, r/m, imm16/32` (`0x69`), chosen ahead of `MOVSB` (`0xa4`) because it is the exact immediate-width sibling of the already-validated `0x6b` path and reuses the same helper-backed result/overflow-mask structure without reopening byte-string move semantics.
- A confirming 3DMark99 rerun after that `0x69` step reached clean shutdown with fallback families `base=21172`, `0f=4693`, `x87=478`, `rep=6794`, and `3dnow=0`; that shutdown base-fallback report no longer contained `0x69`, while `0xa4` remained present as the last low-risk non-protected sibling.
- The next measured low-risk cleanup now in tree is `MOVSB` (`0xa4`): the direct `MOVS` path now covers the byte stride as well, the focused coverage-policy test includes `0xa4`, and a follow-up 3DMark99 rerun reached clean shutdown with fallback families `base=32181`, `0f=7533`, `x87=717`, `rep=11137`, and `3dnow=0`.
- That latest shutdown base-fallback report no longer contains `0xa4`, so the remaining measured hotspot list is still led by `0x9a`, `0xca`, `0xcb`, `0xf7`, and `0xff`, with `0xa6`, `0xa7`, `0xae`, and `0xaf` still outside the chosen low-risk string subset.
- The next landed measured step now returns to the protected-mode far-transfer cluster with the narrowest remaining true table hole: immediate far call (`0x9a`) now direct-recompiles through a helper-backed direct path instead of falling through the `NULL` base table entry.
- A confirming 3DMark99 rerun with `/tmp/new_dynarec_callf_3dmark_validation.log` reached shutdown with fallback families `base=19132`, `0f=6897`, `x87=1671`, `rep=9029`, and `3dnow=0`; that shutdown base-fallback report no longer contains `0x9a`, while `0xca`, `0xcb`, `0xf7`, and `0xff` remain.
- Protected-mode work is now explicitly in scope for the next batches, and that branch-point is now closed in favor of taking the far-return pair before the mixed-group cleanup.
- That decision is now made in favor of the far-return pair: `0xca` and `0xcb` were the better batch because they stay within one semantic family, already had direct real/V86 handling in tree, and only needed the protected-mode helper-backed completion path, unlike `0xf7` and `0xff` which still span broader mixed-group bailout surfaces.
- The protected-mode far-return pair is now confirmed guest-visibly too: the rerun logged to `/tmp/new_dynarec_retf_validation.log` reached shutdown with fallback families `base=10076`, `0f=6801`, `x87=1595`, `rep=9191`, and `3dnow=0`, and the shutdown base-fallback report no longer contains `0xca` or `0xcb`.
- The next measured mixed-group rerun then showed that `0xf7` was closed first while `0xff` still remained, with `/tmp/new_dynarec_f7_ff_validation.log` reaching shutdown at `base=6475`, `0f=6805`, `x87=1421`, `rep=9092`, and `3dnow=0` and no shutdown `0xf7` entry.
- The follow-up narrow fix pair then closed the remaining adjacent helper-bailout work: `/tmp/new_dynarec_f6_ff_validation.log` reached shutdown with fallback families `base=5142`, `0f=6906`, `x87=1607`, `rep=9182`, and `3dnow=0`, and the shutdown base-fallback report no longer contains `0xf6` or `0xff`.
- That means the measured hotspot sequence currently closed on this older CPU mix is `0x9a`, `0xca`, `0xcb`, `0xf7`, `0xf6`, and `0xff`; the next target was left pending a fresh MMX-only CPU re-baseline.
- That MMX-only re-baseline first landed with `/tmp/new_dynarec_mmx_only_validation.log`: shutdown fallback families reached `base=5004`, `0f=6455`, `x87=2124`, `rep=9343`, and `3dnow=0`.
- That materially changes the previous mixed-CPU ordering: REP is now the hottest open family, `0F` is second, and plain base opcodes are third instead of first.
- The new base-opcode shutdown report is also much narrower and more system-heavy than the old mixed-CPU list, led by `0xd1`, `0xd3`, `0xee`, `0xcd`, `0xe6`, `0xcf`, `0xec`, `0x8e`, `0xd0`, and `0x9b`.
- That observability gap is now closed too: `/tmp/new_dynarec_mmx_only_0f_validation.log` adds exact `0F` fallback shutdown logging and shows a longer-run family line of `base=7881`, `0f=11311`, `x87=4001`, `rep=15503`, and `3dnow=0`, preserving the same ordering under a much longer sample.
- The longest-run exact `0F` hotspot list is now led by `0xaf`, `0xba`, `0x94`, `0x95`, `0x02`, `0xc8`, `0xb3`, `0xab`, `0xa3`, and `0x03`.
- Those exact `0F` hits are all currently `helper_table_null`, not `helper_bailout`, so the MMX-only result now points to missing direct coverage rather than unstable existing handlers.
- The recommended next coherent batch after this longer MMX-only pass is therefore the first exact `0F` coverage slice: start by mapping the hottest exact opcodes to instruction families and then implement the leading coherent subset, with `0xaf`, `0xba`, `0x94`, and `0x95` as the first candidate group and `0xd0`-`0xd3` still the clearest secondary generic base batch.
- Softfloat/x87 follow-up remains intentionally out of the near-term batch discussion despite still being open in the long-range coverage picture.
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
| WS1: Observability | Add counters, traces, and verify hooks | Nearly complete | 94% | Core counters and runtime summary logging are in; empty-purge and purge-list lifecycle counters were kept as durable validation surface; temporary scarcity-boundary probes were removed after Phase 1 closure; selective verify sampling plus 3DNow/fallback/base-opcode shutdown logging now cover the current reproducibility need |
| WS2: Invalidation and reclamation | Fix page-list correctness and dirty-page reclaim behavior | Complete | 100% | Page-list sentinel bug fixed; stale-head pruning landed; allocator reuses dirty-list blocks created by purge before random eviction, stale code-presence masks are cleared when pages lose their last block, bulk-dirty enqueue skips non-reclaimable pages, and the remaining empty-purge behavior was closed out as expected scarcity for this workload |
| WS3: Coverage closure | Reduce silent helper-path and unsupported direct coverage gaps | In progress | 93% | Pivoted into active implementation after allocator-policy pause; direct arm64 `PMADDWD`, the existing 3DNow table enable, the remaining direct 3DNow generator batch, first guest-visible per-hit validation, measured fallback-priority ordering, the first measured base-opcode reduction batch (`STOS`/`LODS`, `0xaa`-`0xad`), the first two far-control/frame legality-first slices (`ENTER`, `0xc8`; `POPF`, `0x9d`), the next non-protected measured batch (`MOVS`, `0xa5`; `IMUL r, r/m, imm8`, `0x6b`), the immediate-width `IMUL` sibling follow-up (`0x69`), the byte-`MOVS` sibling cleanup (`MOVSB`, `0xa4`), and the first protected-mode far-transfer slice (`CALLF`, `0x9a`) are now in tree |
| WS4: Backend parity | Close meaningful arm64 vs x86-64 CPU dynarec deltas | Not started | 0% | Backend differences are intentional, measured, and documented |
| WS5: Performance | Improve warmup, eviction policy, and hot-path code quality | Paused | 10% | Initial allocator/reclaim policy work produced stable instrumentation-backed experiments, but this track is paused until coverage closure work is further along |
| WS6: Validation and benchmarking | Add regression corpus and benchmark gates | In progress | 15% | Initial Windows 98 + 3DMark99 workload evidence exists; scripted gates still do not |

## Phase progress charts

Scale: 20 slots per phase. `#` = completed progress, `-` = remaining work.

| Phase | Status | Chart | Progress | Current readout |
|---|---|---|---:|---|
| Phase 0: Observability and reproducibility | Effectively complete for Phase 2 entry | `[###################-]` | 95% | Core counters, runtime stats summaries, workload-level logging, purge failure-mode counters, empty-purge instrumentation, purge-list lifecycle counters, selective verify sampling, 3DNow/fallback/base-opcode shutdown logging, and mark-deferral accounting landed; only a future optional shadow-execution tier remains |
| Phase 1: Invalidation and reclamation hardening | Complete | `[####################]` | 100% | Page-0 list fix, byte-mask reclaim hardening, stale-head pruning, post-purge dirty-list reuse, stale code-mask cleanup, and bulk-dirty overlap-gated enqueue all landed and validated; remaining empty-purge behavior is treated as expected scarcity for this workload |
| Phase 2: Allocator/reclaim policy | Paused | `[##------------------]` | 10% | Two narrow mark-deferral experiments landed and are stable, but they did not show a clear enough workload win to justify more tuning before coverage closure |
| Phase 3: Coverage closure and backend performance work | In progress | `[##################--]` | 89% | Coverage closure is now the active implementation track; arm64 direct `PMADDWD`, direct 3DNow table enable, the remaining direct 3DNow generator batch, first guest-visible direct-path validation, measured fallback-priority ordering, the first base string-op batch, the first two far-control/frame legality-first slices, the next non-protected `MOVS`/`IMUL imm8` batch, the immediate-width `IMUL` sibling follow-up (`0x69`), the byte-`MOVS` sibling cleanup (`0xa4`), the protected-mode far-transfer / far-return steps (`0x9a`, `0xca`, `0xcb`), the mixed-group cleanup (`0xf7`, `0xf6`, `0xff`), and the MMX-only re-baseline are now in hand; that re-baseline now points next work toward `0F`/MMX follow-up rather than another base-first batch |
| Phase 4: Release-quality validation | In progress | `[###-----------------]` | 15% | Initial Windows 98 + 3DMark99 workload evidence exists, but scripted sweeps and threshold-based regression checks do not |

### Phase 0 chart basis

- Completed slice: counter surface, trace-hook surface, direct-vs-helper fallback accounting, invalidation/degradation accounting, allocator-pressure/random-eviction accounting, focused API verification, selective verify sampling at the direct-vs-helper boundary, and the extra accounting needed to measure Phase 2 mark deferral.
- Remaining slice: a future shadow-execution tier if needed, reproducibility matrix, and benchmark-backed validation beyond the current 3DNow/fallback/base-opcode reports.

## Top risks tracker

| Rank | Risk | Severity | Current status | Planned response |
|---|---|---|---|---|
| 1 | Empty purge list under allocator pressure | High | Characterized | Latest workload evidence shows `allocator_pressure_empty_purgable_list=756683` out of `772998`; final boundary validation stayed at zero for the late-Phase-1 bug probes, so the remaining behavior is currently best explained by scarce reclaimable pages rather than another confirmed Phase 1 bug |
| 2 | Purgeable-page list collides with page 0 | Critical | Closed | Distinct evict-list sentinels implemented and regression-tested |
| 3 | Base-opcode helper fallback cluster | Medium | Re-baselined | The first measured batch now covers non-REP `STOS`/`LODS` (`0xaa`, `0xab`, `0xac`, `0xad`), the first two far-control/frame legality-first follow-ups cover `ENTER` (`0xc8`) and `POPF` (`0x9d`), the next non-protected work covers `MOVS` (`0xa5`), `IMUL r, r/m, imm8` (`0x6b`), `IMUL r, r/m, imm16/32` (`0x69`), and `MOVSB` (`0xa4`), the protected-mode far-transfer/return work covers `CALLF` (`0x9a`) and `RETF` (`0xca`, `0xcb`), and the mixed-group follow-up has now moved `0xf7`, `0xf6`, and `0xff` out of the shutdown base-fallback report on the older CPU mix; the MMX-only re-baseline cut base to `5004` and now leaves the generic base cluster led by `0xd1`, `0xd3`, `0xee`, `0xcd`, `0xe6`, `0xcf`, `0xec`, `0x8e`, `0xd0`, and `0x9b`, with `0xd0`-`0xd3` as the clearest secondary base batch behind the new `0F`/MMX priority |
| 4 | Known direct-recompile correctness regressions | High | Open | Reproduce and track guest-facing failures separately |
| 5 | CPU dynarec verify tooling is still shallow | Medium | Narrowed | Core counters + runtime summaries landed, and selective verify sampling now exists; only a deeper shadow-execution tier remains optional later |
| 6 | Fixed exec pool plus random block eviction | Medium | Characterized | Current workload still behaves as expected scarcity after stable policy experiments; further allocator-policy tuning is paused while coverage gaps are addressed |

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
- [x] Implement the first narrow Phase 2 allocator/reclaim policy experiment on cold-block admission under pressure
- [x] Validate the initial allocator-policy experiments under 3DMark99 and decide whether to continue or pivot
- [x] Implement the first narrow CPU dynarec coverage-closure change
- [x] Close the remaining direct 3DNow/3DNowE generator gap with cumulative regression coverage
- [x] Add per-hit 3DNow runtime logging and capture initial guest-visible direct-path evidence
- [x] Measure fallback family and base-opcode fallback concentration under real workloads
- [x] Implement the first measured base-opcode fallback reduction step
- [x] Re-measure the narrowed base-opcode hotspot list after the non-REP `STOS`/`LODS` batch
- [x] Implement the first measured far control / frame follow-up slice: `ENTER` (`0xc8`)
- [x] Implement the next measured base-opcode subset after `ENTER`: `POPF` (`0x9d`)
- [x] Implement the next non-protected measured batch after `POPF`: `MOVS` (`0xa5`) plus `IMUL r, r/m, imm8` (`0x6b`)
- [x] Choose and implement the next measured low-risk follow-up: `IMUL ... , imm16/32` (`0x69`) ahead of byte `MOVSB` (`0xa4`)
- [x] Implement the first protected-mode far-transfer follow-up: `CALLF` (`0x9a`)
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
| Phase 2 cold-block mark deferral experiment | Complete | `src/cpu/386_dynarec.c`, `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_allocator_pressure_policy_test.c`, `tests/codegen_new_dynarec_observability_test.c` |
| Phase 3 arm64 direct `PMADDWD` parity and direct 3DNow table enable | Complete | `src/codegen_new/codegen_ops.c`, `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_opcode_coverage_policy_test.c` |
| Phase 3 first measured base-opcode reduction batch | Complete | `src/codegen_new/codegen_ops.c`, `src/codegen_new/codegen_ops_string.c`, `src/codegen_new/codegen_ops_string.h`, `src/codegen_new/CMakeLists.txt`, `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_opcode_coverage_policy_test.c` |
| Phase 3 measured far-control/frame legality-first slices | Complete | `src/codegen_new/codegen_ops.c`, `src/codegen_new/codegen_ops_stack.c`, `src/codegen_new/codegen_ops_stack.h`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_opcode_coverage_policy_test.c` |
| Phase 3 measured non-protected `MOVS` / `IMUL imm8` batch | Complete | `src/codegen_new/codegen_ops.c`, `src/codegen_new/codegen_ops_string.c`, `src/codegen_new/codegen_ops_string.h`, `src/codegen_new/codegen_ops_arith.c`, `src/codegen_new/codegen_ops_arith.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen_backend_arm64_uops.c`, `src/codegen_new/codegen_backend_x86-64_uops.c`, `tests/codegen_new_opcode_coverage_policy_test.c` |
| Phase 3 measured `IMUL imm16/32` sibling follow-up | Complete | `src/codegen_new/codegen_ops.c`, `src/codegen_new/codegen_ops_arith.c`, `src/codegen_new/codegen_ops_arith.h`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_opcode_coverage_policy_test.c` |
| Phase 3 measured `MOVSB` sibling follow-up | Complete | `src/codegen_new/codegen_ops.c`, `src/codegen_new/codegen_ops_string.c`, `src/codegen_new/codegen_ops_string.h`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_opcode_coverage_policy_test.c` |
| Phase 3 measured protected-mode `CALLF` follow-up | Complete | `src/codegen_new/codegen_ops.c`, `src/codegen_new/codegen_ops_jump.c`, `src/codegen_new/codegen_ops_jump.h`, `src/cpu/x86seg.c`, `src/cpu/x86seg.h`, `src/codegen_new/codegen_observability.c`, `tests/codegen_new_opcode_coverage_policy_test.c` |
| Phase 3 per-hit 3DNow runtime logging | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen.c`, `src/86box.c`, `tests/codegen_new_dynarec_observability_test.c` |
| Phase 3 fallback-family and base-opcode fallback logging | Complete | `src/cpu/codegen_public.h`, `src/codegen_new/codegen_observability.c`, `src/codegen_new/codegen.c`, `src/86box.c`, `tests/codegen_new_dynarec_observability_test.c` |
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

- Is a deeper CPU shadow-execution tier still worth building later, or is selective sampling plus focused regressions enough for the expected Phase 2 policy work?
- The first follow-up answered the far-control/frame granularity question in favor of a still-smaller legality-first slice: `0xc8` landed first because it was the only remaining table hole in that cluster without protected-mode far-transfer or `POPF` permission semantics.
- The next follow-up kept the same legality-first strategy: `0x9d` is now in tree for the non-V86 path, and that separate protected-mode far-transfer decision has now started with `0x9a` while `0xca` and `0xcb` remain.
- The next non-protected follow-up after that pause first answered the sibling-choice question in favor of `0x69`: `0xa5`, `0x6b`, and then `0x69` dropped out of the shutdown base-fallback report before the branch took the remaining byte-string sibling.
- That remaining low-risk byte-string sibling is now closed too: `0xa4` is out of the shutdown base-fallback report, and the branch has since returned to the higher-payoff protected-mode far-transfer subset by moving `0x9a` out of the shutdown base-fallback report as well.
- The next measured choice between the far-return pair and the mixed-group bailout pair is now fully closed: `0xca` / `0xcb` dropped out first, then `0xf7`, `0xf6`, and `0xff` also dropped out on the follow-up reruns.
- The fresh MMX-only CPU re-baseline is now complete, and it changed the ordering materially: `rep=9343`, `0f=6455`, `base=5004`, `x87=2124`, `3dnow=0`.
- The next measured subset should now come from that MMX-only result, not the older mixed-CPU hotspot list.
- The exact MMX-only `0F` mapping pass is now complete too: the hottest ranked opcodes split into a coherent `SETcc` row candidate, a higher-payoff but higher-risk bit-test family, and a less-coherent protected/data-path remainder.
- The first selected larger `0F` batch is the full `SETcc` row (`0x90`-`0x9f`), chosen over the hotter bit-test family because it reuses the existing condition-evaluation machinery and keeps backend/validation risk materially lower for a same-session landing.
- The attempted direct bit-test-family follow-up was backed out after guest-visible Windows 98 protection errors, so that family is no longer the safest near-term path despite its higher raw count.
- The attempted `0xaf` (`IMUL r16/32, r/m16/32`) follow-up was also backed out after guest boot failure.
- The strict retry for `0xaf` now sharpened that conclusion further: host-side semantics coverage and a narrow helper-backed direct path were both clean, the later compare-enabled guest debug pass showed zero sampled destination-writeback and `CF` / `OF` mismatches against helper-visible expected values, but the `Windows 98 Gaming PC` checkpoint still failed during boot with the guest-visible “Insufficient memory to initialize Windows” error, so direct `0xaf` dispatch is again left disabled while the reusable harness/direct-handler code stays in tree and the temporary guest override has been removed.
- The next safe multi-op `0F` follow-up is now the full `BSWAP` row (`0xc8`-`0xcf`), because it is register-only and has no flags, memory, or protected-mode semantics.
- That `BSWAP` row is now guest-validated: the latest logged shutdown report has `base=7072 0f=6633 x87=2982 rep=12579 3dnow=0`, and no `0xc8`-`0xcf` `0F` fallback entries remain.
- The first host-harness follow-up after `BSWAP` was the `BSF` / `BSR` pair (`0xbc`, `0xbd`), and that trial clarified the workflow further: host-side semantics coverage is necessary but not sufficient, because the first guest run still reached a Windows 98 protection error during `IOS` init.
- The current stable branch state therefore keeps the host-side `BSF` / `BSR` harness support and generator code available for continued debugging, but leaves direct `0xbc` / `0xbd` table dispatch disabled until the remaining guest-visible bug is fixed.
- A follow-up i686-focused review on Monday, March 9, 2026 confirmed that the exact on-disk shutdown evidence is still proxy-only for this question: the current stable exact `0F` ranking comes from the K6-2 `Windows 98 Gaming PC` log, while the only exact base-opcode and longer exact `0F` shutdown breakdowns still come from the Tillamook `Windows 98 Low End copy` MMX-only logs.
- That same review also confirmed that strict i686-class local baselines do exist (`Windows 98 TESTING`, `Windows 98 SE copy`, `Windows 2000 copy`), so the safest next i686-focused move is now one observability-only Mendocino shutdown baseline before another opcode landing is chosen.
- With that evidence gap made explicit, the current i686-oriented shortlist changes shape slightly: `0x0f 0xaf` and the bit-test family remain hotter on the proxy logs but are not the safest next branch step, while base `0xd0`-`0xd3` is now the leading low-risk implementation family to revisit immediately after the strict i686 re-baseline if it still holds up there.
- That strict Mendocino baseline is now in hand too at `/tmp/windows98_testing_i686_baseline.log`: `base=23216`, `0f=8719`, `x87=2148`, `rep=17238`, and `3dnow=0`, with base dominated by I/O/control opcodes (`0xee`, `0xe6`, `0xec`, `0xef`) and `0F` still led by `0xaf`, `0xba`, and `0x02`.
- That result keeps the recommendation narrow rather than changing it: the hotter base leaders are not safer, so base `0xd0`-`0xd3` remains the best next implementation family for strict i686-focused work because it still has measurable payoff while staying out of the I/O/control-heavy base surface and the already-blocked `0F` retries.
- That `D0`-`D3` follow-up has now been tried and backed out again: the host-side `RCL` / `RCR` semantics helpers and harness coverage stay in tree, but direct guest enablement for those subcases is disabled again after the strict Mendocino validation reproduced an early Windows boot regression.
- The current open item for that i686-focused slice is therefore back to debugging rather than rollout: the remaining question is not whether `D0`-`D3` is measured enough to try, but what integration-level effect makes the helper-backed `RCL` / `RCR` direct path guest-unsafe even though the host-side semantics harness is clean.
- A later planning reset on Sunday, March 8, 2026 made the broader issue explicit: recent failures are no longer best modeled as “bad next-opcode picks”, but as a missing workflow layer for helper-backed bailout-closure work.
- The practical reset is now firm: do not spend another session guest-enabling hot bailout families directly. First add the missing arm64 helper-call capability checklist, risk-class labeling, and base-opcode compare/debug path, then pick the next candidate from that tighter framework.
- That first base-opcode compare/debug prerequisite is now implemented for `D0`-`D3` `RCL` / `RCR`; the next session should use it for a compare-only run on `Windows 98 SE`, not for another default-on guest retry.
- Future low-risk `0F` work should keep the same sequence: host-side synthetic semantics first, then one logged guest confirmation run, and if the guest run fails, revert only the new table dispatch rather than forcing the unstable direct path to stay enabled.
- What guest workload set will be the standing regression corpus for CPU dynarec changes?
- The standing strict i686-class VM going forward is now `Windows 98 SE` on Mendocino/BF6; earlier `Windows 98 TESTING` logs remain the historical baseline lineage for prior validation evidence.

## Update instructions

When updating this document:

- Keep the status snapshot and workstream tracker current.
- Keep the phase progress charts aligned with the current percentages and phase readouts.
- Update progress percentages conservatively.
- Mark risks closed only when the fix is implemented and validated.
- Add new deliverables rather than rewriting history.
