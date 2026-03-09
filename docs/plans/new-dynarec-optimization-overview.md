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
- Phase 0 is now effectively complete enough for Phase 2 allocator/reclaim policy entry
- initial allocator/reclaim policy experiments are now paused after stable but low-yield 3DMark99 results
- active implementation has moved to coverage closure work
- remaining high random eviction under the Windows 98 + 3DMark99 workload is now best explained by scarcity of reclaimable pages
- not another confirmed Phase 1 reclamation correctness bug

That means future work to reduce the remaining random eviction rate should be treated as allocator/reclaim policy work, not as more Phase 1 bug fixing unless new evidence appears.

The current active implementation track is now coverage closure, starting with the smallest parity gap that already had backend support in place.

The latest small table-hole follow-up after the `D0`-`D3` debug pause first landed base `0xa6` / `0xa7` (`CMPSB` / `CMPSW` / `CMPSD`):

- the direct handlers are now in tree
- they reuse the existing non-REP string-op address/index helpers
- they set lazy `SUB`-style flag state directly rather than introducing new helper-call shapes
- focused local policy/build verification is complete
- guest validation on `Windows 98 SE` is now complete, and the shutdown base-fallback report contains no `0xa6` / `0xa7` entries

That validated `CMPS` result then fed the next same-class sibling directly:

- base `0xae` / `0xaf` (`SCASB` / `SCASW` / `SCASD`) is now in tree too
- it reuses the same non-REP string-op address/index helpers
- it keeps the same lazy `SUB`-style flag pattern, with accumulator-vs-memory compare semantics
- focused local policy/build verification is complete
- guest validation on `Windows 98 SE` is now complete too, and the shutdown base-fallback report contains no `0xae` / `0xaf` entries

That `SCAS` result then fed the next low-risk non-memory table-hole pair directly:

- base `0x9e` / `0x9f` (`SAHF` / `LAHF`) is now in tree too
- it reuses the existing flags materialization path and register high-byte transfer support already present in both backends
- it does not add any new helper-call ABI shape
- `SAHF` also now preserves interpreter intent by treating the instruction as a local lazy-flags barrier after the architectural flag-byte overwrite
- focused local policy/build verification is complete
- guest validation on `Windows 98 SE` is now complete too, and the shutdown base-fallback report contains no `0x9e` / `0x9f` entries

## Current opcode status

This section is the short current-state view for opcode-related NDR closure work. It is split into:

- the measured base-opcode fallback-reduction campaign
- other opcode-family coverage already landed on the branch

### Measured base-opcode closures

These opcodes have direct CPU new dynarec coverage in tree and have dropped out of the shutdown base-fallback report in guest validation:

- non-REP `STOS` / `LODS`: `0xaa`, `0xab`, `0xac`, `0xad`
- non-REP `CMPS`: `0xa6`, `0xa7`
- non-REP `SCAS`: `0xae`, `0xaf`
- `SAHF` / `LAHF`: `0x9e`, `0x9f`
- far-control / frame legality-first slices: `0xc8`, `0x9d`
- non-protected follow-up batch: `0xa5`, `0x6b`
- low-risk sibling cleanup: `0x69`, `0xa4`
- first protected-mode far-transfer slice: `0x9a`
- protected-mode far return pair: `0xca`, `0xcb`
- mixed-group / adjacent helper-bailout cleanup: `0xf7`, `0xf6`, `0xff`

Latest confirming shutdown logs:

- `/tmp/new_dynarec_retf_validation.log`
- fallback families: `base=10076`, `0f=6801`, `x87=1595`, `rep=9191`, `3dnow=0`
- `0xca` and `0xcb` are absent from the shutdown base-fallback report
- `/tmp/new_dynarec_f7_ff_validation.log`
- fallback families: `base=6475`, `0f=6805`, `x87=1421`, `rep=9092`, `3dnow=0`
- `0xf7` is absent from the shutdown base-fallback report
- `/tmp/new_dynarec_f6_ff_validation.log`
- fallback families: `base=5142`, `0f=6906`, `x87=1607`, `rep=9182`, `3dnow=0`
- `0xf6` and `0xff` are absent from the shutdown base-fallback report
- `/tmp/windows98_se_cmps_validation.log`
- fallback families: `base=18915`, `0f=3016`, `x87=435`, `rep=6571`, `3dnow=0`
- `0xa6` and `0xa7` are absent from the shutdown base-fallback report, while sibling `SCAS` opcodes `0xae=33` and `0xaf=16` still remain as `helper_table_null`
- `/tmp/windows98_se_scas_validation.log`
- fallback families: `base=20856`, `0f=5565`, `x87=4568`, `rep=9016`, `3dnow=0`
- `0xae` and `0xaf` are absent from the shutdown base-fallback report
- `/tmp/windows98_se_sahf_lahf_validation.log`
- fallback families: `base=19873`, `0f=4578`, `x87=1919`, `rep=8853`, `3dnow=0`
- `0x9e` and `0x9f` are absent from the shutdown base-fallback report

### MMX-only re-baseline

The fresh MMX-only Windows 98 Low End rerun is now in hand:

- `/tmp/new_dynarec_mmx_only_validation.log`
- fallback families: `base=5004`, `0f=6455`, `x87=2124`, `rep=9343`, `3dnow=0`
- `/tmp/new_dynarec_mmx_only_0f_validation.log`
- longer-run fallback families: `base=7881`, `0f=11311`, `x87=4001`, `rep=15503`, `3dnow=0`

This changes the prioritization materially relative to the older mixed-CPU hotspot ordering:

- REP is now the hottest remaining family overall
- `0F` is now the hottest non-REP family and sits well above the remaining plain base bucket
- plain base opcodes are now third, not first

The shutdown base-opcode report from the same MMX-only run is now led by:

- `0xd1` = 719
- `0xd3` = 605
- `0xee` = 592
- `0xcd` = 518
- `0xe6` = 400
- `0xcf` = 334
- `0xec` = 314
- `0x8e` = 259
- `0xd0` = 244
- `0x9b` = 204

Interpretation:

- the old mixed-CPU string/far-control closure campaign did its job; those older leaders are gone from the base report
- the remaining generic base bucket is now dominated by shift/rotate bailouts, I/O opcodes, and interrupt/control opcodes
- the MMX-only signal is now strong enough to move `0F`/MMX work ahead of another generic base-first batch
- the new exact `0F` shutdown report closes the observability gap that blocked MMX-only ranking

The longest-run exact `0F` report is now led by:

- `0xaf` = 3416
- `0xba` = 2108
- `0x94` = 914
- `0x95` = 828
- `0x02` = 585
- `0xc8` = 500
- `0xb3` = 368
- `0xab` = 336
- `0xa3` = 330
- `0x03` = 299

Exact opcode-to-instruction mapping for that hottest set in this codebase:

- `0x0f 0xaf` = `IMUL r16/32, r/m16/32` (`opIMUL_*`, still `NULL` in `recomp_opcodes_0f`)
- `0x0f 0xba` = Group 8 immediate bit-test family (`BT/BTS/BTR/BTC r/m16/32, imm8`, selected by ModRM `/4`-`/7`)
- `0x0f 0x94` = `SETE r/m8`
- `0x0f 0x95` = `SETNE r/m8`
- `0x0f 0x02` = `LAR r16/32, r/m16/32`
- `0x0f 0xc8` = `BSWAP EAX` (`0xc8`-`0xcf` are the `BSWAP r32` row)
- `0x0f 0xb3` = `BTR r/m16/32, r16/32`
- `0x0f 0xab` = `BTS r/m16/32, r16/32`
- `0x0f 0xa3` = `BT r/m16/32, r16/32`
- `0x0f 0x03` = `LSL r16/32, r/m16/32`

Interpretation of the new `0F` report:

- these are all `helper_table_null` hits, not `helper_bailout` hits
- the new priority is therefore missing direct `0F` coverage, not fixing unstable existing `0F` handlers
- the next decision point is no longer “what observability is missing?” but “which of these opcodes form the tightest coherent implementation slice after instruction-family mapping?”

### Strict i686 baseline gap

A later review on Monday, March 9, 2026 narrowed an important limitation in that MMX-only section:

- the exact base and longer exact `0F` shutdown reports above are still only proxy evidence for i686-focused planning
- the current stable exact `0F` shutdown report on disk is the K6-2 `Windows 98 Gaming PC` log, not a strict i686-class guest
- the actual strict i686-class VMs already available locally are:
  - `Windows 98 TESTING` (`celeron_mendocino`, `bf6`)
  - `Windows 98 SE copy` (`celeron_mendocino`, `ax6bc`)
  - `Windows 2000 copy` (`pentium2_klamath`, `bf6`)

Interpretation:

- the existing K6-2 and Tillamook logs are still useful for hotspot mapping
- they are not a good enough reason on their own to choose the next i686 landing blindly
- the safest next i686-focused trial is now one observability-only Mendocino shutdown baseline with fallback-family, base-fallback, and exact `0F` logging enabled
- if that strict re-baseline preserves the same generic-base ordering, base `0xd0`-`0xd3` becomes the leading low-risk implementation family ahead of another `0x0f` retry

That strict Mendocino re-baseline has now been captured too:

- `/tmp/windows98_testing_i686_baseline.log`
- fallback families: `base=23216`, `0f=8719`, `x87=2148`, `rep=17238`, `3dnow=0`
- hottest base opcodes:
  - `0xee`, `0xe6`, `0xec`, `0xef`
  - then `0xd1`, `0xd3`, `0x8e`, `0xcd`, `0xcf`, `0xd0`
- hottest `0F` opcodes:
  - `0xaf`, `0xba`, `0x02`
  - then `0xb3`, `0xa3`, `0x03`, `0x22`, `0xab`, `0x20`, `0xbc`

Interpretation after the strict i686 run:

- the proxy logs were directionally right about the risky `0F` cluster; the Mendocino result still does not justify another `0x0f 0xaf`, `BSF` / `BSR`, or bit-test-family retry first
- the strict i686 base bucket is even more dominated by system and I/O behavior than the proxy runs suggested
- that makes the hottest base leaders worse first landing candidates despite their count
- base `0xd0`-`0xd3` remains the best narrow next implementation family because it still has real measured payoff (`2747` combined hits), already has direct handlers in tree, and stays outside the hotter I/O/control-heavy base surface
- that narrow family step has now been attempted and then backed out: the host-side rotate-through-carry helpers remain available, but the direct `D0` / `D1` / `D2` / `D3` `RCL` / `RCR` guest path is disabled again after the strict Mendocino validation hit an early Windows boot regression
- the most recent failed-boot validation log (`/tmp/windows98_testing_i686_d0d3_validation.log`) is not a valid replacement baseline because it ended at `base=15803 0f=43 x87=0 rep=202`, so the next evidence task is either deeper debugging of that regression or a cleaner strict-i686 re-baseline on a less I/O-polluted image
- the broader planning reset after that result is now: repeated host-clean / guest-regressed trials are a workflow problem, not just a prioritization problem
- near-term work should therefore shift away from more guest-facing bailout-closure attempts and toward three missing prerequisites:
  - arm64 helper-call capability documentation
  - base-opcode direct-vs-helper compare/debug support
  - a cleaner strict-i686 baseline image for future hotspot ranking
- The first of those three prerequisites is now in tree too:
  - base `D0`-`D3` `RCL` / `RCR` has an opt-in compare-only direct path
  - `86BOX_NEW_DYNAREC_DEBUG_D0D3_RCLRCR=1` enables that compare path for sites selected by `86BOX_NEW_DYNAREC_VERIFY_PC`, `86BOX_NEW_DYNAREC_VERIFY_OPCODE`, and `86BOX_NEW_DYNAREC_VERIFY_BUDGET`
  - `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE=1` emits compact mismatch records plus a shutdown summary
  - `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE_SITES=1` emits the first sampled hit per unique compare site, and shutdown logging now preserves the discovered site list so later runs can lock onto real `VERIFY_PC` targets
  - shutdown logging also preserves a small `CPU new dynarec D0-D3 compare sample [shutdown]: ...` ring so locked-site runs retain exact recent sample state even if live match logs are noisy or missing
  - per-site shutdown lines now also expose `result_changed` and `flags_nonzero`, which helps separate semantically inert hot sites from ones that are actually mutating data or flags during the sampled compare traffic
  - this path has now served its purpose for the current branch stage: the sampled direct-vs-helper evidence is strongly match-only, so the next productive work should move back to low-risk implementation families rather than more `D0`-`D3` instrumentation
  - mismatch samples exit back to helper execution for that sampled run
  - normal guest dispatch for `RCL` / `RCR` remains disabled by default

### First coherent `0F` batch comparison

Two plausible larger first batches came out of the mapping pass:

1. Full `SETcc` row: `0x90`-`0x9f`
   - Measured current-run payoff from the observed hot subset: `0x92` + `0x93` + `0x94` + `0x95` + `0x97` + `0x9c` + `0x9d` + `0x9e` + `0x9f` = `2215`
   - Code reuse: one shared decode shape (`r/m8` destination), one shared result shape (`0` or `1`), and direct reuse of the existing condition-evaluation helpers already used by `ropJcc_common`
   - Backend risk: low, because the direct path only computes a boolean and stores one byte; no new MMX state, bit-addressing, or protected-mode rules
   - Validation scope: one focused policy test can lock the whole row

2. Bit-test family: `0xa3`, `0xab`, `0xb3`, `0xba`
   - Measured current-run payoff: `330 + 336 + 368 + 2108 = 3142`
   - Code reuse: coherent semantic family, but it needs new dynamic bit-index handling, memory element selection beyond the base EA, and read-modify-write support for the mutating variants
   - Backend risk: materially higher than `SETcc`, especially on the memory forms and `0x0f 0xba` subgroup decode
   - Validation scope: broader, because both register and memory forms plus the ModRM `/4`-`/7` subgroup need focused coverage

Selected first batch:

- full `0x0f 0x90`-`0x0f 0x9f` `SETcc`

Why it beat the alternatives:

- it is the largest low-risk row-sized batch in the exact ranking, rather than another one-opcode slice
- it removes a whole coherent decoder row and captures the currently hot `SETE` / `SETNE` traffic immediately
- it reuses existing branch-condition helper logic instead of introducing new address-crossing or bit-RMW machinery
- it keeps validation tight enough for this session while leaving the higher-payoff but higher-risk bit-test family as the next obvious `0F` follow-up

Implementation now in tree:

- `recomp_opcodes_0f` and `recomp_opcodes_0f_no_mmx` now direct-cover the full `0x90`-`0x9f` row with a shared `ropSETcc` handler
- the shared handler lives in `src/codegen_new/codegen_ops_branch.c` and computes the condition through the existing `CF_SET` / `ZF_SET` / `PF_SET` / `NF_SET_01` / `VF_SET_01` helper surface before storing a byte result
- the focused coverage-policy surface now exposes `new_dynarec_has_direct_0f_opcode_recompile()` and `new_dynarec_direct_0f_setcc_opcode_count()`, and the standalone policy test locks the full 16-opcode `SETcc` row

### Still pending from the measured hotspot list

The old measured mixed-group hotspot cluster is now closed on this CPU mix:

- `0xf7`
- `0xf6`
- `0xff`

Current batching guidance:

- the next primary batch should be a `0F`-family follow-up on the MMX-only CPU path
- the mapping pass is now complete, and the first landed `0F` slice is the full `SETcc` row (`0x90`-`0x9f`)
- the failed direct bit-test-family attempt showed that `0xa3` / `0xab` / `0xb3` / `0xba` are not a safe near-term batch without tighter opcode-level runtime validation
- the narrow `0x0f 0xaf` trial was also backed out after guest boot failure
- the later strict `0x0f 0xaf` retry kept that opcode isolated and used a host-side synthetic semantics harness first, but the single allowed `Windows 98 Gaming PC` checkpoint still failed with the guest-visible “Insufficient memory to initialize Windows” boot error, so only the harness/debugging support stays in tree
- the follow-up compare-enabled `0xaf` debug pass then showed zero sampled mismatches for destination writeback or `CF` / `OF` versus helper-visible expected values, which rules out the obvious `IMUL` result/overflow semantics as the primary explanation for that guest regression
- the next safe multi-op `0F` batch is the `BSWAP` row (`0xc8`-`0xcf`): register-only, no flags, no memory forms, and no protected-mode rules
- the logged `BSWAP` rerun is now complete too: `0f` fell to `6633`, and there are no remaining shutdown `0xc8`-`0xcf` fallback entries in `/tmp/new_dynarec_0f_bswap_validation.log`
- the first host-harness follow-up after `BSWAP` was the `BSF` / `BSR` pair (`0xbc`, `0xbd`), but the first guest-visible attempt is not stable enough to keep enabled
- the guest-debugging pass found two real bugs in that combined `9c77c0fa6` state: generic `SETcc` was incorrectly normalizing raw flag masks as boolean values, and `BSF` / `BSR` reused stale helper arguments for the ZF-mask helper
- after fixing the `SETcc` bug, guest boot progressed further but still failed during Windows 98 `IOS` initialization until direct `BSF` / `BSR` table dispatch was removed again
- the current stable state is therefore: `SETcc` enabled with corrected boolean normalization, `BSWAP` enabled and guest-validated, host-side `BSF` / `BSR` harness support retained, but direct guest dispatch for `0xbc` / `0xbd` disabled pending further debugging
- the current stable state now also keeps host-side `0x0f 0xaf` semantics coverage and helper-backed direct-handler code available, but direct guest dispatch for `0xaf` is disabled again after the strict retry reproduced a new early-boot regression on the `Windows 98 Gaming PC` VM and the later compare pass still failed to clear it; the temporary guest override used for that compare pass has since been removed
- future low-risk `0F` follow-ups should still use a host-side synthetic semantics harness first, then one logged guest confirmation run afterward, but a failed guest run should revert only the new table dispatch instead of leaving the unstable direct path enabled
- if a generic base-only follow-up is needed in parallel or afterward, `0xd0`-`0xd3` is now the clearest coherent base batch
- REP is still the hottest family overall, but it is a separate non-MMX campaign and no longer blocks doing the MMX-only `0F` follow-up first
- softfloat / x87 follow-up is not a near-term batch priority for this branch pass

Project-scope note:

- protected-mode work is in scope for NDR follow-up batches

### Other opcode-family coverage already landed

The measured base-opcode list above is not the full opcode story for this branch. Other already-landed opcode-family work includes:

- arm64 direct `PMADDWD`
- direct 3DNow table enable on arm64
- the remaining direct 3DNow / 3DNowE generator batch:
  - `PI2FW`, `PF2IW`, `PFNACC`, `PFPNACC`, `PFACC`, `PMULHRW`, `PSWAPD`, `PAVGUSB`
- guest-visible direct 3DNow hit validation across Windows 98 + 3DMark99 and longer mixed runs for:
  - `0x0d`, `0x1d`, `0x90`, `0x94`, `0x96`, `0x97`, `0x9a`, `0x9e`, `0xa0`, `0xa4`, `0xa6`, `0xaa`, `0xae`, `0xb0`, `0xb4`, `0xb6`

Interpretation:

- the measured base-opcode closure list is intentionally narrow
- it shows the exact opcodes that were selected from the shutdown fallback reports and then verified out of those reports
- it does not try to restate every direct opcode that already existed in NDR before this hotspot campaign

## Other major completed NDR work

The opcode list above is not the whole branch status. Other major completed CPU new dynarec work already in tree includes:

- Phase 1 invalidation / reclamation hardening:
  - page-0 evict-list sentinel fix
  - byte-mask reclaim hardening
  - stale-head purge pruning
  - post-purge dirty-list reuse
  - stale code-mask cleanup
  - bulk-dirty overlap-gated enqueue
- Observability and validation surface:
  - runtime/shutdown dynarec stats summaries
  - fallback-family shutdown logging
  - base-opcode fallback shutdown logging
  - selective verify sampling
  - focused standalone regression coverage for the retained observability APIs
- Coverage closure outside the current base-opcode list:
  - arm64 direct `PMADDWD`
  - direct 3DNow table enable on arm64
  - remaining direct 3DNow / 3DNowE generator coverage
  - guest-visible 3DNow direct-path validation
- Allocator / reclaim policy experiments:
  - initial cold-block mark-deferral work landed and is stable
  - this line of work is currently paused after low-yield 3DMark99 results

Short version:

- the branch is well past “just a few opcodes”
- the remaining measured hotspot discussion is specifically about the current highest-payoff base-opcode follow-ups, not the total amount of landed NDR work

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

### 12. Phase 2 cold-block mark deferral under scarcity pressure

Status:
- paused
- kept as the first allocator/reclaim policy experiment

What changed:

- after a random eviction, the next 8 first-hit mark opportunities are interpreted without allocating a new mark-only block
- the policy does not change the recompile path or the Phase 1 reclaim path
- the runtime summary now counts those skipped first-hit marks as `deferred_block_marks`

Why it was needed:

- Phase 1 evidence showed that the dominant remaining signature is empty purge list under allocator pressure
- that makes demand reduction the narrowest defensible next lever
- a first-hit mark-only block is the lowest-value resident under that signature because it occupies a block slot before it has produced direct code

What evidence confirmed it:

- focused standalone policy coverage now checks that a random eviction arms a fixed deferral window and that the window is consumed exactly once per skipped mark
- the observability test now checks that deferred marks are counted and traced through the same retained summary surface used for the rest of Phase 0/1

Interpretation:

- this is intentionally a small Phase 2 admission-policy experiment, not a claim that Phase 1 was incomplete
- the crash path discovered during the first real 3DMark99 run is now fixed: deferred mark-only passes no longer try to delete a block that was never allocated
- a follow-up stronger trigger that arms the same window on empty-purge allocator pressure was also stable, but neither version showed a clear enough workload win to justify more immediate tuning
- this work is therefore paused rather than extended, and the project is pivoting to coverage closure before revisiting allocator-policy ideas later

### 13. Arm64 direct `PMADDWD` parity

Status:
- complete
- kept

What changed:

- arm64 no longer forces `PMADDWD` through helper fallback in the `recomp_opcodes_0f` table
- the same direct recompiler entry is now used on arm64 and x86-64 for that opcode

Why it was needed:

- this was a narrow, explicit frontend-table parity gap
- the shared MMX frontend recompiler already existed
- the arm64 backend already had `UOP_PMADDWD` support, so this was a table-level closure step rather than a new backend implementation

What evidence confirmed it:

- a focused standalone coverage-policy test now asserts that direct `PMADDWD` recompilation is enabled
- the target build for `86Box`, `cpu`, `dynarec`, and `mem` still succeeds after the table change

Interpretation:

- this is a better first post-pivot coverage target than broad 3DNow work because it closes a concrete arm64 parity gap with minimal surface area
- it also serves as a template for future table-level parity closures where backend support already exists

Key files:

- `src/codegen_new/codegen_ops.c`
- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_observability.c`
- `tests/codegen_new_opcode_coverage_policy_test.c`

### 14. Arm64 direct 3DNow table enable

Status:
- complete
- kept

What changed:

- arm64 no longer compiles `recomp_opcodes_3DNOW` to an all-zero table
- the existing shared non-`NULL` direct 3DNow entries are now available on arm64 as well as x86-64

Why it was needed:

- after the `PMADDWD` parity fix, the next smallest concrete arm64 frontend gap was the table-level 3DNow disable
- the arm64 backend already had the relevant 3DNow uOP handlers, so the remaining block was the frontend table gate rather than missing backend machinery

What evidence confirmed it:

- the focused coverage-policy test still passes after the table change
- the target build for `86Box`, `cpu`, `dynarec`, and `mem` still succeeds

Interpretation:

- this does not close all 3DNow work, but it removes the artificial "arm64 gets zero direct 3DNow entries" policy
- future 3DNow work should now focus on guest-visible validation and any remaining missing generator entries, not this backend-wide table gate

Key files:

- `src/codegen_new/codegen_ops.c`
- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_observability.c`
- `tests/codegen_new_opcode_coverage_policy_test.c`

### 15. Remaining direct 3DNow generator batch

Status:
- complete
- kept

What changed:

- the remaining direct CPU dynarec 3DNow/3DNowE generators are now implemented: `PI2FW`, `PF2IW`, `PFNACC`, `PFPNACC`, `PFACC`, `PMULHRW`, `PSWAPD`, and `PAVGUSB`
- the exact direct 3DNow support surface is now asserted cumulatively by test as a 24-opcode matrix rather than a boolean capability bit
- `src/codegen_new/codegen.c` now gates the Enhanced 3DNow-only suffixes so they only direct-compile when the active dynarec opcode table is `dynarec_ops_3DNOWE`

Why it was needed:

- after the arm64 table gate was removed, the remaining 3DNow work was no longer "arm64 has no direct path"; it was a concrete finite set of missing generators
- leaving those suffixes outside the direct path would keep helper-path coverage cliffs and make future regressions hard to see because the previous test only checked for any 3DNow support at all
- the smallest coherent batch was therefore the entire remaining generator gap rather than another series of tiny table toggles

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now passes while asserting the complete 24-suffix direct 3DNow set
- the focused allocator-pressure and observability standalone tests still pass unchanged
- `cmake --build build --target 86Box cpu dynarec mem -j4` still succeeds, with only the pre-existing macOS/Homebrew deployment-target linker warnings

Interpretation:

- the direct generator gap is now closed, but this is not the same thing as guest-visible validation being complete
- some of the newly covered ops are lowered through existing IR composition, while the hardest pair (`PMULHRW` and `PAVGUSB`) use dedicated uOPs that lower through backend helper calls rather than reopening the old direct-vs-helper dispatch path
- the next 3DNow task is therefore runtime validation of the new suffixes under guest workloads, not more generator-table expansion

Key files:

- `src/codegen_new/codegen.c`
- `src/codegen_new/codegen_ops.c`
- `src/codegen_new/codegen_ops_3dnow.c`
- `src/codegen_new/codegen_backend_arm64_uops.c`
- `src/codegen_new/codegen_backend_x86-64_uops.c`
- `src/cpu/codegen_public.h`
- `src/codegen_new/codegen_observability.c`
- `tests/codegen_new_opcode_coverage_policy_test.c`

### 16. Per-hit 3DNow runtime logging and initial guest-visible validation

Status:
- complete
- kept

What changed:

- CPU dynarec now exposes an optional per-hit 3DNow shutdown report gated by `86BOX_NEW_DYNAREC_LOG_3DNOW_HITS=1`
- each reported line includes the suffix opcode and three counters: `direct`, `helper_table_null`, and `helper_bailout`
- the 3DNow direct/helper decision points in `src/codegen_new/codegen.c` now feed that reporting surface for every exercised 3DNow suffix

Why it was needed:

- the earlier selective verify sampling was useful for proving one suffix at a time, but it was too narrow for workload validation of the whole 3DNow surface
- after the direct generator gap was closed, the next useful question was no longer "is one opcode direct?"; it was "which opcodes does a real guest workload actually hit, and do any of them still fall back?"
- that required a cumulative workload report rather than another round of one-opcode filters

What evidence confirmed it:

- `tests/codegen_new_dynarec_observability_test.c` now covers per-suffix hit accounting and the shutdown formatter
- guest runs with `/tmp/3dnow_hit_report.log` now emit one shutdown line per exercised 3DNow opcode
- across Windows 98 + 3DMark99 and longer mixed runs, the observed direct set is now:
  - `0x0d`, `0x1d`, `0x90`, `0x94`, `0x96`, `0x97`, `0x9a`, `0x9e`, `0xa0`, `0xa4`, `0xa6`, `0xaa`, `0xae`, `0xb0`, `0xb4`, `0xb6`
- every observed hit in those runs stayed direct, with `helper_table_null=0` and `helper_bailout=0`

Interpretation:

- this does not prove that all 24 direct-capable suffixes are exercised by the current workload set
- it does prove that the observed 16-suffix subset is not silently falling back through the old direct-vs-helper dispatch boundary
- the remaining guest-visible gap is now explicitly the still-unhit suffix set: `0x0c`, `0x1c`, `0x8a`, `0x8e`, `0xa7`, `0xb7`, `0xbb`, `0xbf`
- the next useful expansion is broader workload coverage, not more table work for 3DNow itself

### 17. Fallback-family narrowing and base-opcode hotspot discovery

Status:
- complete
- kept

What changed:

- CPU dynarec now exposes an optional helper fallback-family shutdown report gated by `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1`
- that report classifies helper fallback load into `base`, `0f`, `x87`, `rep`, and `3dnow`
- CPU dynarec now also exposes an optional per-opcode base-fallback shutdown report gated by `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1`

Why it was needed:

- once 3DNow direct coverage was validated, the next open question was not "is 3DNow still falling back?" but "what family is actually dominating the remaining helper traffic?"
- the first family-level pass showed that `3dnow=0` and `x87` was smaller than both `base` and `rep`, so guessing at REP or MMX next would still have been premature
- the next smallest useful step was therefore to break the dominant `base` bucket down into exact opcodes

What evidence confirmed it:

- `/tmp/fallback_family_report.log` showed `base=48526`, `rep=8100`, `0f=6763`, `x87=2843`, and `3dnow=0`
- `/tmp/base_fallback_report.log` then showed the dominant base fallback cluster is led by:
  - `0x9a`, `0xca`, `0xab`, `0xc8`, `0xf7`, `0xad`, `0x9d`, `0xcb`, `0xa5`, `0x6b`, `0xac`, and `0xff`
- those results split naturally into:
  - string ops: `0xab`, `0xad`, `0xa5`, `0xac`, plus smaller `0xaa`
  - far control transfer / frame / flags: `0x9a`, `0xca`, `0xcb`, `0xc8`, `0x9d`
  - a smaller remaining bailout cluster: `0xf7`, `0xff`, `0x6b`

Interpretation:

- this changes the next implementation target materially
- the next evidence-backed move is no longer REP or x87 work
- the best next target is the hot base-opcode cluster, starting with either:
  - the string-op subset, or
  - the far control-transfer / frame subset
- REP remains relevant, but as a measured second-tier target behind the base bucket for this workload set

### 18. First measured base-opcode reduction batch: non-REP STOS/LODS

Status:
- complete
- kept

What changed:

- the first implementation batch out of the measured base-opcode hotspot list takes the narrowest coherent string-op slice: non-REP `STOS`/`LODS`
- direct CPU dynarec handlers now exist for:
  - `0xaa` (`STOSB`)
  - `0xab` (`STOSW` / `STOSD`)
  - `0xac` (`LODSB`)
  - `0xad` (`LODSW` / `LODSD`)
- the new handlers cover both address-size modes and keep the existing direction-flag behavior by updating `SI`/`DI` or `ESI`/`EDI` from the runtime `D_FLAG`
- the standalone coverage-policy test now locks that exact four-opcode subset and still asserts that nearby string ops like `MOVS` / `CMPS` / `SCAS` remain open

Why it was needed:

- the measured hotspot list said to start with base string/far-control work, but mixing those two buckets in one change would have expanded both code surface and risk
- `STOS`/`LODS` was the best first subset because it covers four hot opcodes immediately while avoiding the extra flag-setting work in `SCAS` / `CMPS` and the privilege / segment-load complexity in far control-transfer ops
- this is therefore the smallest batch that still follows the measured priority order with credible payoff

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now asserts that `0xaa`, `0xab`, `0xac`, and `0xad` direct-recompile, while `0xa5`, `0xa6`, `0xa7`, `0xae`, and `0xaf` still do not
- the three required standalone tests pass, `cmake --build build --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep build/src/86Box.app` succeeds after the build
- a follow-up 3DMark99 rerun with `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1` and `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1` confirmed the guest-visible effect:
  - fallback families at shutdown: `base=31127`, `0f=4880`, `x87=319`, `rep=6818`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0xaa`, `0xab`, `0xac`, or `0xad`

What remains from the measured hotspot list:

- far control / frame / flags:
  - `0x9a`, `0xca`, `0xc8`, `0x9d`, `0xcb`
- string:
  - `0xa5`
- smaller bailout cluster:
  - `0xf7`, `0x6b`, `0xff`

Interpretation:

- the base hotspot is no longer an undifferentiated mixed bucket
- the next measured subset is now far control / frame (`0x9a`, `0xca`, `0xc8`, `0x9d`, `0xcb`)
- `MOVS` (`0xa5`) is still the next string-op follow-up after that
- REP, `0F`, x87, and more 3DNow work remain behind those for this workload set

### 19. First far control / frame follow-up slice: ENTER

Status:
- complete
- kept

What changed:

- the next landed batch after non-REP `STOS`/`LODS` is the narrowest legality-safe member of the measured far control / frame bucket: `ENTER` (`0xc8`)
- direct CPU dynarec handlers now exist for operand-size 16 and operand-size 32 `ENTER`
- the new handlers keep the interpreter-visible frame-chain behavior by pushing the previous frame pointer, copying nested frame links when the immediate nesting count is non-zero, then installing the new frame pointer and subtracting the immediate stack allocation
- the standalone coverage-policy test now asserts that `0xc8` direct-recompiles while `0x9a`, `0x9d`, `0xca`, and `0xcb` remain out of scope for this batch

Why it was needed:

- measured prioritization said the next work after the first string slice should stay inside the far control / frame hotspot cluster
- `0xc8` was the best first member of that cluster because it was still a direct-table hole and it does not require segment-load or privilege-sensitive protected-mode far-transfer behavior
- `0x9a`, `0xca`, and `0xcb` all cross into far call / far return semantics, and `0x9d` adds `POPF` permission and VME handling, so landing `0xc8` first keeps the batch minimal and systematic

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now asserts that `0xc8` direct-recompiles while the rest of the far control / frame subset still does not
- the three required standalone tests pass, `cmake --build build --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep build/src/86Box.app` succeeds after the build
- the prior 3DMark99 rerun remains the confirming baseline for the measured ordering that this batch follows:
  - fallback families at shutdown: `base=31127`, `0f=4880`, `x87=319`, `rep=6818`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0xaa`, `0xab`, `0xac`, or `0xad`

What remains from the measured hotspot list now:

- far control / frame / flags:
  - `0x9a`, `0xca`, `0x9d`, `0xcb`
- string:
  - `0xa5`
- smaller bailout cluster:
  - `0xf7`, `0x6b`, `0xff`

Interpretation:

- measured prioritization is still being followed; the batch simply entered that far control / frame cluster through its safest remaining table hole
- `0x9d` is now the next defensible legality-first follow-up inside the same cluster
- `0x9a`, `0xca`, and `0xcb` remain plausible payoff candidates, but they should be treated as a separate protected-mode far-transfer step rather than folded into another low-risk batch by default

### 20. Second far control / frame legality-first slice: POPF

Status:
- complete
- kept

What changed:

- the next landed batch after `ENTER` keeps the same measured-cluster strategy but takes the next smallest legality-first member: `POPF` (`0x9d`)
- direct CPU dynarec handlers now exist for operand-size 16 and operand-size 32 `POPF`, covering the common non-V86 path while still falling back when V86 permission-sensitive behavior is in play
- the new handlers update low flags directly, preserve the architectural reserved bits that the interpreter keeps, and mark the flag state unknown for later consumers
- the standalone coverage-policy test now asserts that `0x9d` direct-recompiles while `0x9a`, `0xca`, and `0xcb` remain out of scope for this batch

Why it was needed:

- measured prioritization still pointed at the far control / frame cluster after `ENTER`
- `0x9d` was the best next member because it does not require segment-load or gate-transfer handling like `0x9a`, `0xca`, and `0xcb`
- the legality-first approach remains intact because the V86-sensitive path still falls back instead of pretending the full protected/VME semantics are already covered

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now asserts that `0x9d` direct-recompiles while `0x9a`, `0xca`, and `0xcb` still do not
- the three required standalone tests pass, `cmake --build build --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep build/src/86Box.app` succeeds after the build
- the first guest-visible runtime attempt exposed a real backend legality bug in the new EFLAGS-merge IR sequence; that mixed-width `AND_IMM` path is now fixed
- a follow-up 3DMark99 rerun with `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1` and `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1` confirmed the corrected guest-visible effect:
  - fallback families at shutdown: `base=25331`, `0f=4672`, `x87=451`, `rep=6754`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0x9d`
  - `0xc8` also remains absent from that shutdown base-opcode report

What remains from the measured hotspot list now:

- far control / frame / flags:
  - `0x9a`, `0xca`, `0xcb`
- string:
  - `0xa5`
- smaller bailout cluster:
  - `0xf7`, `0x6b`, `0xff`

Interpretation:

- the measured hotspot list is still being worked in order; the branch has now removed the two lowest-risk members of the far control / frame cluster first
- the remaining far-transfer subset should be treated as its own protected-mode semantics batch, not as a casual extension of the `ENTER`/`POPF` work
- if the next step should stay low-risk, `MOVS` (`0xa5`) is now the safer measured follow-up

## What is still considered open

These are not closed by the work above:

- REP direct recompilation coverage
- softfloat direct-coverage cliffs
- hot base-opcode fallback reduction, starting from the measured string/far-control cluster
- the remaining measured post-`MOVS`/`IMUL imm8` base-opcode list: `0x9a`, `0xca`, `0xcb`, `0xf7`, and `0xff`
- lower-risk sibling cleanup that is now easier to justify separately: `0x69` and `0xa4`
- broader guest-visible coverage for the still-unhit direct 3DNow suffixes
- a possible future full CPU shadow-execution verify tier
- allocator/reclaim policy work to reduce structurally high random eviction under pressure

### 21. Next non-protected measured batch: MOVS (`0xa5`) and IMUL r, r/m, imm8 (`0x6b`)

Status:
- complete
- kept

What changed:

- the next landed batch after the far-control/frame legality-first pause stays on the measured base-opcode list but avoids protected-mode far-transfer work
- direct CPU dynarec handlers now exist for `MOVS` word/dword (`0xa5`) in both address-size modes, reusing the existing string-op addressing and direction-flag machinery to move from `ea_seg:SI/ESI` to `ES:DI/EDI`
- direct CPU dynarec handlers now also exist for `IMUL r, r/m, imm8` (`0x6b`) in 16-bit and 32-bit operand-size forms, using helper-backed result/overflow-mask calculation while keeping the interpreter-visible `CF`/`OF` outcome
- the focused coverage-policy test now asserts that both `0xa5` and `0x6b` direct-recompile

Why it was needed:

- measured prioritization after `POPF` still pointed at the same remaining base-opcode hotspot list, but the remaining far-transfer subset (`0x9a`, `0xca`, `0xcb`) was still higher-risk protected-mode work
- `0xa5` was the cleanest next string-op follow-up because it extends the earlier `STOS`/`LODS` batch without taking on REP, `CMPS`, or `SCAS` flag semantics
- `0x6b` was the cleanest arithmetic follow-up because it is a standalone immediate-8 IMUL form rather than a mixed-group bailout bucket like `0xf7` or `0xff`

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now asserts that both `0xa5` and `0x6b` direct-recompile
- the three required standalone tests pass, `cmake --build build --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep build/src/86Box.app` succeeds after the build
- the first guest runtime attempt exposed a real backend legality bug: `MOVZX 113 113` from an invalid `W <- W` `MOVZX` in the new `0x6b` flag-mask path; that form is now removed
- a follow-up 3DMark99 rerun with `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1` and `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1` confirmed the corrected guest-visible effect:
  - fallback families at shutdown: `base=19237`, `0f=4063`, `x87=231`, `rep=5626`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0xa5`
  - the shutdown base-opcode report no longer contains `0x6b`
  - `0x9d` and `0xc8` also remain absent

What remains from the measured hotspot list now:

- protected-mode far control / return:
  - `0x9a`, `0xca`, `0xcb`
- mixed-group bailout cluster:
  - `0xf7`, `0xff`
- lower-risk siblings newly exposed after this batch:
  - `0x69`
  - `0xa4`

Interpretation:

- measured prioritization was still followed; the branch just took the best non-protected subset available while leaving the protected-mode far-transfer work untouched
- `0x69` and `0xa4` are now the clearest low-risk cleanup candidates if the next step should stay non-protected
- if payoff now matters more than risk, the branch should return to the far-transfer subset (`0x9a`, `0xca`, `0xcb`) as a separate semantics-heavy batch

### 22. Next low-risk sibling follow-up: IMUL r, r/m, imm16/32 (`0x69`)

Status:
- complete
- kept

What changed:

- the next low-risk non-protected follow-up chose `0x69` ahead of `0xa4`
- direct CPU dynarec handlers now exist for `IMUL r, r/m, imm16/32` (`0x69`) in 16-bit and 32-bit operand-size forms
- the new handlers deliberately mirror the existing `0x6b` structure: the source operand still comes from the same ModRM decode path, and helper-backed result/overflow-mask calculation still preserves interpreter-visible `CF` / `OF` behavior
- the focused coverage-policy test now asserts that `0x69` direct-recompiles while `0xa4` remains open

Why it was needed:

- measured prioritization was still to stay non-protected for one more batch unless new evidence justified taking `0x9a` / `0xca` / `0xcb`
- between the two low-risk siblings, `0x69` was narrower and more coherent because it is the exact immediate-width sibling of the already validated `0x6b` path
- `0xa4` remained lower risk than the protected-mode far-transfer batch, but it would still reopen byte-string move handling as a separate string-op variant instead of extending the already-proven IMUL helper pattern

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now asserts that `0x69` direct-recompiles and still keeps `0xa4` on the fallback side
- the three required standalone tests pass, `cmake --build build --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep build/src/86Box.app` succeeds after the build
- a confirming 3DMark99 rerun with `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1` and `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1` reached clean shutdown with:
  - fallback families at shutdown: `base=21172`, `0f=4693`, `x87=478`, `rep=6794`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0x69`
  - `0xa4` remains present as `helper_table_null=148`

What remains from the measured hotspot list now:

- protected-mode far control / return:
  - `0x9a`, `0xca`, `0xcb`
- mixed-group bailout cluster:
  - `0xf7`, `0xff`
- remaining low-risk non-protected sibling:
  - `0xa4`

Interpretation:

- measured prioritization was followed exactly: the branch stayed non-protected for one more batch and took the narrower coherent sibling first
- the next low-risk measured subset should now be `MOVSB` (`0xa4`) if the goal is to keep grinding down non-protected fallbacks before touching the protected-mode far-transfer cluster
- if payoff now matters more than risk, the next step should instead return to `0x9a`, `0xca`, and `0xcb` as a distinct protected-mode semantics batch

### 23. Next low-risk sibling follow-up: MOVSB (`0xa4`)

Status:
- complete
- kept

What changed:

- code inspection for this session confirmed the earlier `0x69` step was already in tree, so `MOVSB` (`0xa4`) became the remaining narrow non-protected sibling
- the existing direct non-REP `MOVS` path now covers the byte-stride case as well through a dedicated `ropMOVSB` entry
- the focused coverage-policy test now asserts that `0xa4` direct-recompiles, increasing the tracked direct base-string subset from 5 opcodes to 6

Why it was needed:

- measured prioritization for this session was still to stay non-protected and take the narrowest coherent subset
- with `0x69` already implemented, `0xa4` was the only remaining low-risk sibling from the measured pair
- this was smaller and safer than switching immediately to the protected-mode far-transfer subset (`0x9a`, `0xca`, `0xcb`) or the mixed-group bailout pair (`0xf7`, `0xff`)

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now asserts direct support for `0xa4`
- the three required standalone tests pass, `cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep out/build/llvm-macos-aarch64.cmake/src/86Box.app` succeeds after the build
- a confirming 3DMark99 rerun with `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1` and `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1` reached clean shutdown with:
  - fallback families at shutdown: `base=32181`, `0f=7533`, `x87=717`, `rep=11137`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0xa4`

What remains from the measured hotspot list now:

- protected-mode far control / return:
  - `0x9a`, `0xca`, `0xcb`
- mixed-group bailout cluster:
  - `0xf7`, `0xff`

Interpretation:

- measured prioritization was still followed: the branch stayed non-protected for one more narrow cleanup after confirming `0x69` was already complete
- the low-risk sibling pair is now closed
- the next measured subset should return to the higher-payoff far-transfer batch (`0x9a`, `0xca`, `0xcb`) unless new evidence makes the mixed-group bailout pair (`0xf7`, `0xff`) more attractive

### 24. First protected-mode far-transfer follow-up: CALLF (`0x9a`)

Status:
- complete
- kept

What changed:

- the next measured step returned to the protected-mode far-transfer cluster with the narrowest remaining true base-table hole
- immediate far call (`0x9a`) now direct-recompiles through helper-backed `ropCALL_far_16` / `ropCALL_far_32` entries instead of falling through a `NULL` base-opcode table slot
- the new direct path reuses dedicated helper entry points in `src/cpu/x86seg.c` so the segment-load and stack-update semantics stay centralized and consistent with existing CPU handling
- the focused coverage-policy test now asserts that `0x9a` direct-recompiles while `0xca` and `0xcb` remain intentionally out of scope for this batch

Why it was needed:

- measured prioritization had already closed the non-protected sibling pair, so the branch had reached the next real branch point between the protected-mode far-transfer cluster and the mixed-group bailout cluster
- `0x9a` was the hottest remaining true table hole, making it the narrowest coherent protected-mode step with immediate guest-visible payoff
- taking `0x9a` first avoided mixing far-return semantics (`0xca`, `0xcb`) or the broader grouped bailout surface (`0xf7`, `0xff`) into the same batch

What evidence confirmed it:

- `tests/codegen_new_opcode_coverage_policy_test.c` now asserts direct support for `0x9a` while `0xca` and `0xcb` still stay out of scope
- the three required standalone tests pass, `cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep out/build/llvm-macos-aarch64.cmake/src/86Box.app` succeeds after the build
- a confirming 3DMark99 rerun with `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1` and `86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS=1` reached shutdown with:
  - fallback families at shutdown: `base=19132`, `0f=6897`, `x87=1671`, `rep=9029`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0x9a`
  - the remaining measured hotspots in that report are `0xca`, `0xcb`, `0xf7`, and `0xff`

What remains from the measured hotspot list now:

- protected-mode far return:
  - `0xca`, `0xcb`
- mixed-group bailout cluster:
  - `0xf7`, `0xff`

Interpretation:

- measured prioritization was still followed, but the branch has now crossed into protected-mode work because the payoff is higher than continuing to avoid that surface
- softfloat / x87 remains intentionally out of the near-term batch discussion
- the next highest-impact coherent batch is the far-return pair (`0xca`, `0xcb`) before the mixed-group bailout pair (`0xf7`, `0xff`)

### 25. Protected-mode far return follow-up: RETF (`0xca`, `0xcb`)

Status:
- complete
- kept

What changed:

- protected-mode `RETF` now uses helper-backed direct CPU dynarec paths in both operand sizes while preserving the already-direct real-mode and V86 handling
- the focused coverage-policy test now asserts direct support for `0xca` and `0xcb`

Why it was needed:

- after `0x9a`, the remaining measured choice was between the protected-mode far-return pair and the broader mixed-group bailout surface
- `0xca` and `0xcb` were the highest-impact coherent batch because they stayed inside one far-return semantic family and only needed the protected-mode completion path

What evidence confirmed it:

- the three required standalone tests pass, `cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep out/build/llvm-macos-aarch64.cmake/src/86Box.app` succeeds after the build
- the confirming guest rerun logged to `/tmp/new_dynarec_retf_validation.log` reached shutdown with:
  - fallback families at shutdown: `base=10076`, `0f=6801`, `x87=1595`, `rep=9191`, `3dnow=0`
  - the shutdown base-opcode report no longer contains `0xca` or `0xcb`

### 26. Mixed-group and adjacent helper-bailout cleanup: `0xf7`, `0xf6`, `0xff`

Status:
- complete
- kept

What changed:

- `0xf7` now covers its arithmetic half through helper-backed terminal direct paths for `MUL` / `IMUL` / `DIV` / `IDIV`
- `0xff` now includes indirect far `CALL` through helper-backed direct handling in both operand sizes
- `0xf6` now covers the byte-group `MUL` / `IMUL` / `DIV` / `IDIV` forms through zero-extended helper-backed direct paths
- the ARM64 helper ABI now accepts register-backed `LOAD_FUNC_ARG1`, which the indirect far-call path requires

Why it was needed:

- once `0xca` and `0xcb` were closed, the remaining measured helper-bailout pressure was concentrated in `0xf7` and `0xff`
- the first rerun showed that `0xf7` closed cleanly but `0xff` still remained, and the resulting narrower view exposed the adjacent byte-group bailout `0xf6`
- fixing `0xff` and `0xf6` together was the narrowest coherent follow-up after that partial result

What evidence confirmed it:

- the three required standalone tests pass, `cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box cpu dynarec mem -j4` succeeds, and `codesign -s - --force --deep out/build/llvm-macos-aarch64.cmake/src/86Box.app` succeeds after the build
- the first mixed-group rerun logged to `/tmp/new_dynarec_f7_ff_validation.log` reached shutdown with:
  - fallback families at shutdown: `base=6475`, `0f=6805`, `x87=1421`, `rep=9092`, `3dnow=0`
  - no shutdown `0xf7` entry
  - shutdown `0xff helper_bailout=1103` still present
- the follow-up rerun logged to `/tmp/new_dynarec_f6_ff_validation.log` then reached shutdown with:
  - fallback families at shutdown: `base=5142`, `0f=6906`, `x87=1607`, `rep=9182`, `3dnow=0`
  - no shutdown `0xf6` entry
  - no shutdown `0xff` entry

Interpretation:

- the old measured mixed-group hotspot sequence on this CPU mix is now closed
- the next base-opcode target should be chosen only after a fresh MMX-only CPU re-baseline
- softfloat / x87 still remains intentionally out of the near-term batch discussion

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
- coverage closure after an MMX-only CPU re-baseline
- deeper verification infrastructure only if Phase 2 uncovers a concrete correctness gap

## Update instructions

When updating this document:

- organize by optimization area, not by date
- keep explanations short and concrete
- include the reason a change mattered
- include the current status of each area
- keep workload numbers only where they prove a point that still matters
