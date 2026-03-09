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

## 2026-03-09 (`D0-D3` rotate-through-carry bailout closure)

### Added
- Added helper-backed host semantics support for `RCL` / `RCR` in `src/codegen_new/codegen_test_support.c` and extended `tests/codegen_new_0f_semantics_test.c` with focused rotate-through-carry result and `CF` / `OF` mask coverage for 8-bit, 16-bit, and 32-bit forms.

### Changed
- Changed the measured `0xd0`-`0xd3` follow-up from a pure recommendation into a narrow implementation trial, then backed the guest path out again after validation: `src/codegen_new/codegen_ops_shift.c` returns to the stable immediate-fallback behavior for `RCL` / `RCR`, while the host-side rotate helpers and harness coverage remain in tree.
- Changed the branch status for base `0xd0`-`0xd3`: the family is still the best narrow i686-oriented next opportunity on paper, but direct `RCL` / `RCR` guest enablement is not currently safe on this branch.

### Validated
- Confirmed the focused semantics harness passes after the new helper coverage lands via `cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu -Isrc/codegen_new tests/codegen_new_0f_semantics_test.c src/codegen_new/codegen_test_support.c -o /tmp/codegen_new_0f_semantics_test && /tmp/codegen_new_0f_semantics_test`.
- Confirmed `cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box -j4` succeeds with the `D0` / `D1` / `D2` / `D3` handler changes in tree.
- Confirmed the first guest validation attempt crashed in the dynarec backend with `codegen_LOAD_FUNC_ARG2 113`, which traced to the new three-argument helper-call shape rather than guest execution itself.
- Confirmed the follow-up two-argument packed-helper retry booted far enough to reach the same class of guest-visible early Windows boot failure seen on other unstable trials, showing “Insufficient conventional memory to run Windows” instead of reaching a comparable stable shutdown profile.
- Confirmed the failed-boot shutdown log at `/tmp/windows98_testing_i686_d0d3_validation.log` is not comparable to the stable strict i686 baseline: it ended at `base=15803 0f=43 x87=0 rep=202`, far short of the stable `/tmp/windows98_testing_i686_baseline.log` shape (`base=23216 0f=8719 x87=2148 rep=17238`).

### Open
- Keep the host-side `RCL` / `RCR` helper coverage for future debugging, but treat direct `D0` / `D1` / `D2` / `D3` `RCL` / `RCR` guest enablement as backed out until the remaining integration-level boot regression is understood.
- Re-measure strict i686 ranking on a cleaner pre-install guest image if I/O-heavy behavior from the current Windows 98 TESTING image is now distorting the base-bucket ordering.

## 2026-03-09 (guest-regression planning reset)

### Added
- Added `docs/plans/2026-03-09-guest-regression-planning-reset.md`, a workflow-reset note that explains why recent host-clean opcode trials (`BSF` / `BSR`, `0x0f 0xaf`, `D0`-`D3`) still regressed in guest execution, and classifies remaining work into safe landing candidates, debug-first candidates, and blocked system-risk candidates.

### Changed
- Changed the near-term recommendation from “pick the next measured hotspot family” to “stop guest-facing bailout-closure trials until the missing planning/debugging layer is in place,” because the repeated failures now point to workflow and backend-audit gaps rather than a single bad opcode choice.
- Changed the required preconditions for future guest-facing opcode work: any new helper-backed or bailout-closure trial now needs an explicit arm64 backend capability audit, a Class A/B/C risk label, a defined guest compare/debug path when applicable, and a named clean baseline image before guest enablement is allowed.

### Validated
- Confirmed the current repeated-failure pattern spans three separate families rather than one isolated bug: `0x0f 0xbc` / `0xbd`, `0x0f 0xaf`, and base `D0` / `D1` / `D2` / `D3` all cleared host-side gates but still failed guest validation strongly enough to force backout.
- Confirmed the existing workflow is still effective for lower-risk table-hole families like `BSWAP`, but is not sufficient on its own for helper-backed bailout-closure work.

### Open
- Write down the concrete arm64 helper-call and helper-result constraints for CPU dynarec helper-backed paths.
- Add a narrow base-opcode direct-vs-helper compare path before retrying any Class B bailout-closure family.

## 2026-03-09 (i686 opportunity review and strict-baseline recommendation)

### Added
- Added `docs/plans/2026-03-09-i686-opportunity-review.md`, a focused analysis note that confirms the stable `d960e431a` baseline, gathers the current on-disk proxy fallback evidence, maps the hottest remaining opcode families, and ranks the best i686-oriented opportunities without starting a new implementation slice.
- Added the strict i686 VM inventory to that review: `Windows 98 TESTING` and `Windows 98 SE copy` (`celeron_mendocino`) plus `Windows 2000 copy` (`pentium2_klamath`) are available locally even though the exact on-disk shutdown logs still come from the K6-2 `Windows 98 Gaming PC` baseline and the Tillamook `Windows 98 Low End copy` MMX-only baseline.

### Changed
- Changed the immediate i686 recommendation from “pick the next opcode directly from the current proxy logs” to “take one observability-only Mendocino shutdown baseline first,” because the current exact logs are still K6-2 and Pentium MMX flavored rather than strict i686-class evidence.
- Changed the safest post-baseline implementation guidance accordingly: base `0xd0`-`0xd3` (group-2 shift/rotate bailout closure) now leads the i686 shortlist on safety grounds, while `0x0f 0xaf`, `0xbc` / `0xbd`, and the `0x0f 0xba` / bit-test family remain blocked for this branch state.

### Validated
- Confirmed from the current stable K6-2 shutdown log at `/tmp/windows98_gaming_pc_new_dynarec.log` that the post-backout exact `0F` ranking is still led by `0xaf=2863`, `0xba=2067`, then `0x02`, `0xb3`, `0xa3`, `0xab`, and `0x03`, with all of those still recorded as `helper_table_null`.
- Confirmed from the MMX-only shutdown logs at `/tmp/new_dynarec_mmx_only_validation.log` and `/tmp/new_dynarec_mmx_only_0f_validation.log` that the remaining exact base bucket is now led by `0xd1`, `0xd3`, `0xee`, `0xcd`, `0xe6`, `0xcf`, `0xec`, `0x8e`, `0xd0`, and `0x9b`, while the longer exact `0F` ranking still leads with `0xaf`, `0xba`, `0x94`, `0x95`, `0x02`, `0xc8`, `0xb3`, `0xab`, `0xa3`, and `0x03`.
- Confirmed from the local VM configs that the strict i686-class baselines exist on disk now even though they do not yet have matching current exact shutdown logs.

### Open
- Capture one current-stable Mendocino shutdown log with fallback-family, base-fallback, and exact `0F` logging before choosing the next actual opcode landing for i686-focused work.
- Re-rank `0xd0`-`0xd3` versus `0x0f 0x02` / `0x03` only after that strict i686 run, rather than assuming the K6-2 and Tillamook proxy order is good enough.

## 2026-03-09 (strict Mendocino baseline captured)

### Added
- Added the completed strict i686-class shutdown evidence at `/tmp/windows98_testing_i686_baseline.log` for `Windows 98 TESTING` (`celeron_mendocino`, `bf6`) with fallback-family, base-fallback, and exact `0F` logging enabled.

### Changed
- Changed the i686 recommendation from “take one observability-only Mendocino shutdown baseline first” to a concrete next implementation family: base `0xd0`-`0xd3` now remains the best narrow trial after the strict baseline too.
- Changed the interpretation of the remaining strict-i686 base bucket: it is now clearly dominated by I/O and control opcodes (`0xee`, `0xe6`, `0xec`, `0xef`, `0xcd`, `0xcf`) rather than by plain integer helpers, which makes the hotter base leaders worse first landing candidates than the still-measured shift/rotate bailout family.

### Validated
- Confirmed the strict Mendocino shutdown families are `base=23216`, `0f=8719`, `x87=2148`, `rep=17238`, and `3dnow=0`.
- Confirmed the strict Mendocino base ranking is led by `0xee=5562`, `0xe6=5319`, `0xec=3578`, `0xef=3202`, `0xd1=1183`, `0xd3=983`, `0x8e=520`, `0xcd=500`, `0xcf=462`, and `0xd0=421`.
- Confirmed the strict Mendocino `0F` ranking is still led by the same risky families as the proxy logs: `0xaf=3056`, `0xba=2458`, `0x02=602`, `0xb3=446`, `0xa3=328`, `0x03=299`, `0x22=290`, `0xab=289`, `0x20=218`, and `0xbc=218`.
- Confirmed the measured `0xd0`-`0xd3` strict-i686 total is `2747`, and all of those hits remain `helper_bailout` rather than `helper_table_null`, which keeps the likely work inside the existing direct handlers.

### Open
- If the next session moves from planning to implementation, keep the trial narrow to `0xd0`-`0xd3` only and use a host-harness-first workflow before one logged Mendocino guest confirmation run.
- Keep `0x0f 0xaf`, `0xbc` / `0xbd`, and the `0x0f 0xba` / bit-test family out of the next landing despite their higher raw counts, because the strict i686 run did not improve their validation-risk profile.

## 2026-03-08 (`0F AF` strict host-harness trial and guest backout)

### Added
- Added host-side synthetic semantics harness coverage for `0x0f 0xaf` (`IMUL r16/32, r/m16/32`) in `tests/codegen_new_0f_semantics_test.c` and `src/codegen_new/codegen_test_support.c`, covering 16-bit and 32-bit result truncation plus `CF` / `OF` overflow-mask behavior for register-source and memory-equivalent source cases.
- Added narrow direct-handler code for `0x0f 0xaf` using the same helper-backed result/flag-mask shape already used by the existing immediate-`IMUL` direct paths, so the reusable decode/helper/writeback path stays available for future debugging even though guest dispatch is not kept enabled.
- Added a temporary `0xaf`-only guest debug override plus shutdown-summary logging for the compare pass that sampled direct attempts without restoring normal direct-coverage policy exposure.

### Changed
- Changed the `0x0f 0xaf` retry from a broad direct-coverage attempt to a strict one-opcode host-harness-first trial on the `Windows 98 Gaming PC` baseline VM.
- Changed the final kept branch state after the guest checkpoint: host-side `0x0f 0xaf` harness support remains in tree, but direct `0x0f 0xaf` table dispatch and direct-coverage policy exposure are backed out again because the single allowed guest run regressed during boot.
- Changed the post-trial understanding of the remaining `0xaf` risk: compare-enabled guest debugging now shows direct `0xaf` destination writeback and `CF` / `OF` mask matching helper-visible expected values on sampled executions, so the unresolved guest failure is no longer explained by the obvious `IMUL` math or `CF` / `OF` semantics path.
- Changed the kept code state after that debug pass: the temporary `0xaf` guest override and compare-only shutdown logging are removed again so the branch returns to the stable non-direct `0xaf` behavior while keeping the reusable host-side semantics helpers and retained direct handler code.

### Validated
- Confirmed the red/green cycle for the new `0x0f 0xaf` harness: the standalone semantics test first failed at link on the missing `new_dynarec_imul_rm16_*` / `new_dynarec_imul_rm32_*` helpers, then passed after the helpers landed.
- Confirmed the focused coverage-policy red/green cycle: the standalone coverage test first failed on `new_dynarec_has_direct_0f_opcode_recompile(0xaf) == 1`, then passed once the direct path was wired, and passed again after the final backout with `0xaf` restored to non-direct status.
- Confirmed the required host verification commands all pass on the final kept state, including the `0x0f` semantics harness test, the three focused standalone policy/observability tests, the target build, and app re-signing.
- Confirmed the single logged guest run at `/tmp/windows98_gaming_pc_0f_af_validation.log` did not reach desktop; it failed at boot with the guest-visible Windows message “Insufficient memory to initialize Windows,” and the shutdown log was only `base=688 0f=38 x87=0 rep=375 3dnow=0`, which is far short of the stable baseline profile.
- Confirmed the compare-enabled guest debug pass at `/tmp/windows98_gaming_pc_0f_af_debug.log` reached shutdown and reported `attempts=204 direct=204`, `dest_mismatches=0`, `flags_mismatches=0`, `last_expected_dest=0x00005100`, `last_actual_dest=0x00005100`, `last_expected_flags=0x00000801`, and `last_actual_flags=0x00000801`, while the guest still hit the same Windows boot error.

### Open
- `0x0f 0xaf` should still be treated as host-harness-covered but not guest-safe. The next debugging pass can reuse the helper-backed direct handler and semantics helpers, but direct guest dispatch must stay disabled until the remaining integration-level guest regression is understood.
- The next `0F` follow-up should not assume that a clean host harness is enough to keep arithmetic-family guest dispatch enabled; the `0x0f 0xaf` trial reinforced the same keep/backout rule already established by the `BSF` / `BSR` backout.

## 2026-03-08 (`0F` `BSF` / `BSR` guest backout after harness-first trial)

### Added
- Added a host-side synthetic semantics harness for the `BSF` / `BSR` pair (`0x0f 0xbc`, `0x0f 0xbd`) alongside the earlier `BSWAP` harness coverage.
- Added a guest-visible regression note for the first harness-first `BSF` / `BSR` landing attempt: the initial local `9c77c0fa6` batch had two real correctness bugs outside the standalone harness signal, one in generic `SETcc` boolean normalization and one in `BSF` / `BSR` helper-argument reuse.

### Changed
- Changed the stable branch state after guest retest: `SETcc` stays enabled with corrected boolean handling, `BSWAP` stays enabled, but direct `BSF` / `BSR` table dispatch is disabled again pending further debugging.
- Changed the interpretation of the harness-first workflow from "host harness plus one guest run is enough to keep the batch" to "host harness is a gate for attempting the guest run, but guest-visible failures still force the exact new opcode path back out."

### Validated
- Confirmed the `SETcc` boolean-normalization fix and the `BSF` / `BSR` helper-argument fix both moved the failure later in guest boot: the VM progressed from an early hang to a Windows 98 protection error during `IOS` initialization.
- Confirmed the exact isolation build with the `SETcc` fix retained, `BSWAP` retained, and only direct `BSF` / `BSR` dispatch removed boots cleanly to the Windows 98 desktop.
- Confirmed the remaining unstable surface in the `9c77c0fa6` landing is therefore the direct `BSF` / `BSR` guest path, not `BSWAP`.

### Open
- `BSF` / `BSR` should not be treated as landed even though the host harness and generator code are in tree; only the host-side semantics path is currently trusted.
- The next `0F` follow-up should start from the restored stable state (`SETcc` fixed, `BSWAP` enabled, `BSF` / `BSR` disabled) instead of assuming the first harness-first `BSF` / `BSR` attempt is shippable.

## 2026-03-08 (`0F` `BSWAP` row landing)

### Added
- Added direct CPU dynarec coverage for the full `0x0f 0xc8`-`0x0f 0xcf` `BSWAP r32` row with one shared register-only handler.
- Added focused policy-surface coverage for the row through `new_dynarec_direct_0f_bswap_opcode_count()` and explicit coverage-policy assertions for all eight `BSWAP` opcodes.

### Changed
- Changed the next safe multi-op `0F` follow-up from speculative single-op retries to the `BSWAP` row, because it avoids the backend and semantic risks that already broke the bit-test-family and `0xaf` attempts.
- Changed the validation approach for future low-risk `0F` follow-ups: start moving ring-3-safe families onto a host-side synthetic semantics harness before doing logged guest confirmation runs.

### Validated
- Confirmed the logged `BSWAP` rerun at `/tmp/new_dynarec_0f_bswap_validation.log` completed with a normal shutdown profile: `base=7072 0f=6633 x87=2982 rep=12579 3dnow=0`.
- Confirmed there were no remaining shutdown `0F` fallback entries for `0xc8`-`0xcf`, so the full `BSWAP` row is now taking the direct path in guest execution.

### Open
- `0xaf`, `0x02`, `0x03`, and the bit-test family still need a stricter opcode-level trial plan than the `BSWAP` row required.
- `0xbc` / `0xbd` (`BSF` / `BSR`) moved under the new host-side semantics harness, but the first guest-visible trial is not stable enough to keep enabled yet.

## 2026-03-08 (`0F` `IMUL` follow-up backout)

### Changed
- Reverted the first direct CPU dynarec attempt for `0x0f 0xaf` (`IMUL r16/32, r/m16/32`) after the guest stopped booting on the logged validation run.
- Restored the exact `0F` direct-coverage policy surface and opcode tables to the last known-good `SETcc`-only state.

### Validated
- The failed guest boot produced only a short shutdown log (`0f=157`) instead of a normal Windows 98 run, confirming that the `0xaf` landing is not safe enough to keep.

### Open
- The next exact-hotspot `0F` step needs a stricter trial plan; `0xaf`, `0x02`, `0x03`, and `0xc8` remain separate candidates, while the bit-test family is still paused after its earlier protection-error backout.

## 2026-03-08 (`0F` `SETcc` first-batch landing)

### Added
- Added the exact opcode-to-instruction mapping for the longest-run MMX-only `0F` leaders in the maintained overview: `0x0f 0xaf = IMUL`, `0x0f 0xba = BT/BTS/BTR/BTC imm8 group`, `0x0f 0x94/0x95 = SETE/SETNE`, `0x0f 0x02/0x03 = LAR/LSL`, `0x0f 0xc8 = BSWAP EAX`, and `0x0f 0xa3/0xab/0xb3 = BT/BTS/BTR`.
- Added the first explicit larger-batch comparison for the MMX-only `0F` follow-up: the full `SETcc` row (`0x90`-`0x9f`) versus the bit-test family (`0xa3`, `0xab`, `0xb3`, `0xba`), including measured payoff, code-reuse, backend-risk, and validation-scope tradeoffs.
- Added direct CPU dynarec coverage for the full `0x0f 0x90`-`0x0f 0x9f` `SETcc` row with a shared `ropSETcc` handler in `src/codegen_new/codegen_ops_branch.c`.
- Added focused policy-surface visibility for this decision through `new_dynarec_has_direct_0f_opcode_recompile()` and `new_dynarec_direct_0f_setcc_opcode_count()`, and extended the standalone coverage-policy test to lock the full 16-opcode `SETcc` row.

### Changed
- Changed the first MMX-only `0F` implementation recommendation from the provisional mixed candidate set (`0xaf`, `0xba`, `0x94`, `0x95`) to the full `SETcc` row (`0x90`-`0x9f`) after code inspection showed that the row can reuse the existing condition-evaluation helper surface while the hotter bit-test family still needs fresh dynamic bit-addressing and RMW handling.
- Changed the next remaining coherent `0F` candidate after this landing to the bit-test family (`0xa3`, `0xab`, `0xb3`, `0xba`).

### Validated
- Confirmed the red phase for the updated policy test first failed at link with missing `new_dynarec_has_direct_0f_opcode_recompile()` and `new_dynarec_direct_0f_setcc_opcode_count()` symbols.
- Confirmed the focused coverage-policy compile-and-run check passes after the `SETcc` row implementation and policy-surface wiring.

### Open
- The highest-payoff remaining coherent `0F` batch is now the bit-test family (`0xa3`, `0xab`, `0xb3`, `0xba`).
- `0x0f 0xaf`, `0x0f 0x02`, `0x0f 0x03`, and `0x0f 0xc8` remain outside that family and should not be mixed back into a less-coherent first follow-up batch.

## 2026-03-08 (MMX-only re-baseline update)

### Added
- Added the MMX-only Windows 98 Low End re-baseline log at `/tmp/new_dynarec_mmx_only_validation.log`, which records shutdown fallback families `base=5004`, `0f=6455`, `x87=2124`, `rep=9343`, and `3dnow=0` after the older mixed-CPU hotspot sequence had already been closed through `0xf7`, `0xf6`, and `0xff`.
- Added the first MMX-only post-closure base-opcode ranking from that rerun: `0xd1`, `0xd3`, `0xee`, `0xcd`, `0xe6`, `0xcf`, `0xec`, `0x8e`, `0xd0`, and `0x9b` now lead the remaining plain base bucket.
- Added the exact `0F` shutdown fallback log for the longer MMX-only rerun at `/tmp/new_dynarec_mmx_only_0f_validation.log`, including per-opcode `helper_table_null` / `helper_bailout` splits.
- Added the first exact long-run `0F` ranking from that new log: `0xaf`, `0xba`, `0x94`, `0x95`, `0x02`, `0xc8`, `0xb3`, `0xab`, `0xa3`, and `0x03` now lead the `0F` bucket, all as `helper_table_null` hits.

### Changed
- Changed the next-batch recommendation after the longer MMX-only rerun from “add `0F` observability first” to “use the new exact `0F` ranking to choose the first coherent `0F` coverage slice”, because that logging is now in tree and the longer sample preserved the same `rep > 0f > base > x87` ordering.
- Changed the generic base-opcode recommendation from "choose after MMX-only re-baseline" to a concrete secondary batch candidate: the shift/rotate bailout family `0xd0`-`0xd3` is now the clearest next generic base-opcode slice if a non-`0F` follow-up is needed.

### Validated
- Confirmed the MMX-only validation run used the rebuilt app bundle from `out/build/llvm-macos-aarch64.cmake`, the exact logfile path `/tmp/new_dynarec_mmx_only_validation.log`, and the Windows 98 Low End VM path `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy`.
- Confirmed the three required standalone compile-and-run checks still pass, `cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep out/build/llvm-macos-aarch64.cmake/src/86Box.app` succeeds before the MMX-only guest rerun.
- Confirmed the exact `0F` fallback observability path with the updated focused observability test, then rebuilt and re-signed the app before the longer MMX-only runtime rerun.

### Open
- The remaining gap is no longer raw `0F` observability; it is instruction-family mapping and batching. The next session should map the hottest exact `0F` opcodes from `/tmp/new_dynarec_mmx_only_0f_validation.log` to their instruction families, choose the tightest coherent first slice, and only then implement.

## 2026-03-08

### Added
- Added the follow-up narrow fix pair after `0xf7` validated by itself: byte-group `0xf6` now covers `MUL` / `IMUL` / `DIV` / `IDIV` through zero-extended helper-backed direct paths, and `0xff` indirect far call now has helper-backed direct handling in both operand sizes; the focused coverage-policy test now also asserts direct `0xf6` support.
- Added the next measured mixed-group follow-up after closing `0xca` / `0xcb`: `0xf7` now covers its arithmetic half through helper-backed terminal direct paths, and `0xff` now includes the previously skipped indirect far-call subgroup, with focused coverage-policy assertions for the opcode pair.
- Added the next measured protected-mode follow-up after `0x9a`: far return (`0xca`, `0xcb`) now has helper-backed direct CPU dynarec handling for the protected-mode path while preserving the existing direct real/V86 handling, with focused coverage-policy assertions for that exact pair.
- Added the first protected-mode far-transfer follow-up from the measured hotspot list: immediate far call (`0x9a`) now has direct CPU dynarec handlers via a helper-backed direct path, with focused coverage-policy assertions for that exact opcode while keeping `0xca` and `0xcb` explicitly out of scope for the same batch.
- Added the next measured low-risk non-protected sibling cleanup after the already-landed `0x69` step: `MOVSB` (`0xa4`) now has a direct CPU dynarec handler by extending the existing non-REP `MOVS` path to the byte stride, with focused coverage-policy assertions for that exact opcode.
- Added the next measured low-risk non-protected follow-up after the `0xa5` / `0x6b` batch: `IMUL r, r/m, imm16/32` (`0x69`) now has direct CPU dynarec handlers in 16-bit and 32-bit operand-size forms, with focused coverage-policy assertions for that exact sibling step.
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
- Changed the mixed-group follow-up status from a pending `0xf7` / `0xff` batch to a completed staged result: `0xf7` dropped out on the first rerun, then the remaining adjacent `0xf6` / `0xff` helper-bailout pair was fixed and validated on the next rerun.
- Changed the next measured branch-point from an open far-return-vs-mixed-group choice to a resolved implementation decision: `0xca` / `0xcb` landed ahead of `0xf7` / `0xff` because they stay inside one far-return semantic family and only required closing the existing protected-mode bailout path instead of taking on the broader grouped bailout surface.
- Changed the next measured branch-point decision from "far-transfer subset vs mixed-group bailout subset" to a narrower first protected-mode slice: `0x9a` landed ahead of `0xca`, `0xcb`, `0xf7`, and `0xff` because it was the hottest remaining true base-table hole and the smallest coherent protected-mode step.
- Changed the near-term prioritization notes across the plan set so protected-mode follow-up is explicitly in scope, while softfloat / x87 remains intentionally out of the near-term batch discussion.
- Changed the next low-risk non-protected implementation choice from the previously open sibling question to `0xa4` because code inspection confirmed `0x69` was already in tree and guest-validated on this branch, making `MOVSB` the only remaining narrow non-protected sibling follow-up.
- Changed the next low-risk non-protected implementation choice from the open sibling pair (`0x69`, `0xa4`) to `0x69` first because it is the exact immediate-width sibling of the already guest-validated `0x6b` path, while `0xa4` would reopen byte-string move semantics separately.
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
- Confirmed the follow-up rerun with `/tmp/new_dynarec_f6_ff_validation.log` reached shutdown with fallback families `base=5142`, `0f=6906`, `x87=1607`, `rep=9182`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0xf6` or `0xff`.
- Confirmed the intermediate mixed-group rerun with `/tmp/new_dynarec_f7_ff_validation.log` reached shutdown with fallback families `base=6475`, `0f=6805`, `x87=1421`, `rep=9092`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0xf7` while `0xff` still remained.
- Confirmed a red/green cycle for the narrowed coverage-policy test after asserting direct `0xf6`, `0xf7`, and `0xff` support, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully from `out/build/llvm-macos-aarch64.cmake`, and re-signed `out/build/llvm-macos-aarch64.cmake/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed a red/green cycle for the narrowed coverage-policy test after asserting direct `0xf7` and `0xff` support, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully from `out/build/llvm-macos-aarch64.cmake`, and re-signed `out/build/llvm-macos-aarch64.cmake/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed the guest-visible far-return result with `/tmp/new_dynarec_retf_validation.log`: shutdown fallback families reached `base=10076`, `0f=6801`, `x87=1595`, `rep=9191`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0xca` or `0xcb`.
- Confirmed a red/green cycle for the narrowed coverage-policy test after asserting direct `0xca` and `0xcb` support, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully from `out/build/llvm-macos-aarch64.cmake`, and re-signed `out/build/llvm-macos-aarch64.cmake/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed a red/green cycle for the protected-mode far-transfer coverage-policy test after moving `0x9a` while keeping `0xca` and `0xcb` open, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully from `out/build/llvm-macos-aarch64.cmake`, and re-signed `out/build/llvm-macos-aarch64.cmake/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings plus unrelated existing compiler warnings.
- Confirmed the 3DMark99 rerun with `/tmp/new_dynarec_callf_3dmark_validation.log` reached shutdown with fallback families `base=19132`, `0f=6897`, `x87=1671`, `rep=9029`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0x9a` while `0xca`, `0xcb`, `0xf7`, and `0xff` remained.
- Confirmed a red/green cycle for the narrowed low-risk sibling coverage-policy test after moving `0xa4`, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully from `out/build/llvm-macos-aarch64.cmake`, and re-signed `out/build/llvm-macos-aarch64.cmake/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings plus unrelated existing compiler warnings.
- Confirmed the 3DMark99 rerun with `/tmp/new_dynarec_movsb_validation.log` reached clean shutdown with fallback families `base=32181`, `0f=7533`, `x87=717`, `rep=11137`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0xa4`.
- Confirmed a red/green cycle for the narrowed low-risk sibling coverage-policy test after asserting direct `0x69` support while keeping `0xa4` open, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully, and re-signed `build/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed the 3DMark99 rerun with `/tmp/new_dynarec_imul_iw_validation.log` reached clean shutdown with fallback families `base=21172`, `0f=4693`, `x87=478`, `rep=6794`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0x69` while `0xa4` remained present.
- Confirmed a red/green cycle for the narrowed non-protected coverage-policy test after moving `0xa5` and `0x6b`, then reran the three focused standalone tests, rebuilt `86Box`, `cpu`, `dynarec`, and `mem` successfully, and re-signed `build/src/86Box.app`; the only build noise remained the pre-existing macOS/Homebrew deployment-target linker warnings.
- Confirmed the first `0x6b` guest-visible runtime crash with `MOVZX 113 113`, then traced it to an invalid `W <- W` `MOVZX` in the new helper-backed IMUL flag-mask path and removed that backend-illegal form.
- Confirmed the post-fix 3DMark99 rerun with `/tmp/new_dynarec_movs_imul_validation.log` reached clean shutdown with fallback families `base=19237`, `0f=4063`, `x87=231`, `rep=5626`, and `3dnow=0`, and the shutdown base-opcode report no longer contained `0xa5`, `0x6b`, `0x9d`, or `0xc8`.
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
- The next measured hotspot choice should be re-baselined on the planned MMX-only CPU configuration instead of assuming the old mixed-CPU ordering still applies unchanged.
- Larger coverage gaps remain beyond the now-closed mixed-group batch: REP and `0F` still trail as measured secondary targets, and broader guest-visible workload coverage for the still-unhit direct 3DNow suffixes (`0x0c`, `0x1c`, `0x8a`, `0x8e`, `0xa7`, `0xb7`, `0xbb`, `0xbf`) is still open.

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
