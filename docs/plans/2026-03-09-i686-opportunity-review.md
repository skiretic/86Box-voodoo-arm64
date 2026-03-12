# 2026-03-09 i686 Opportunity Review

> Historical opportunity-review note (updated 2026-03-12): this document records the strict-i686 ranking state from earlier on 2026-03-09 and the reasoning that fed later narrow trials. Since then, `CMPS`, `SCAS`, `SAHF` / `LAHF`, and the high-payoff REP string subset (`REP MOVS`, `REP STOS`, `REP SCASB`, `REP CMPSB`, `REP CMPSW` / `REP CMPSD`) have all landed and validated on `Windows 98 SE`, while direct `BSF` / `BSR`, direct `0x0f 0xaf`, and guest-facing base `D0`-`D3` `RCL` / `RCR` remain paused. The debug env-var recipes referenced later in this file should now also be read as devtools-only history: current builds need `NEW_DYNAREC_DEVTOOLS=ON` before those probes are active. Use the maintained summary docs for current branch status.

## Purpose

This note narrows the current CPU new dynarec opportunity space to i686-class follow-up work.

It started as analysis-only planning on top of `d960e431a`. A later same-day follow-up then used that recommendation to land a narrow `D0` / `D1` / `D2` / `D3` `RCL` / `RCR` bailout-closure attempt, so the evidence and recommendation below now also explain why that exact slice was chosen.

## Stable baseline confirmed

Current stable baseline at `d960e431a`:

- branch head on `cpu-optimizations` is `d960e431a`
- `SETcc` (`0x0f 0x90`-`0x0f 0x9f`) stays enabled with corrected boolean normalization
- `BSWAP` (`0x0f 0xc8`-`0x0f 0xcf`) stays enabled and guest-validated
- direct `BSF` / `BSR` (`0x0f 0xbc`, `0x0f 0xbd`) stays disabled
- direct `0x0f 0xaf` stays disabled
- host-side semantics harness support for `BSF` / `BSR` and `0x0f 0xaf` remains in tree
- the temporary `0xaf` guest compare override is removed again

## Best evidence sources on disk

### Strict i686-class VM configurations available locally

- `Windows 98 SE`
  - `cpu_family = celeron_mendocino`
  - `cpu_speed = 300000000`
  - `machine = bf6`
- `Windows 98 TESTING`
  - `cpu_family = celeron_mendocino`
  - `cpu_speed = 300000000`
  - `machine = bf6`
- `Windows 98 SE copy`
  - `cpu_family = celeron_mendocino`
  - `cpu_speed = 300000000`
  - `machine = ax6bc`
- `Windows 2000 copy`
  - `cpu_family = pentium2_klamath`
  - `cpu_speed = 233333333`
  - `machine = bf6`

### Existing fallback logs actually present on disk

- `/tmp/windows98_gaming_pc_new_dynarec.log`
  - current stable post-`SETcc` / post-`BSWAP` guest log
  - VM is `Windows 98 Gaming PC`
  - `cpu_family = k6_2`
  - useful as the current stable exact `0F` ranking, but not a strict i686 baseline
- `/tmp/new_dynarec_mmx_only_validation.log`
  - VM is `Windows 98 Low End copy`
  - `cpu_family = pentium_tillamook`
  - not i686-class, but it is the only on-disk exact base-opcode shutdown breakdown
- `/tmp/new_dynarec_mmx_only_0f_validation.log`
  - same `Windows 98 Low End copy` VM
  - not i686-class, but it is the only on-disk longer exact `0F` shutdown breakdown

### Evidence-source conclusion

- The chosen strict i686 working baseline going forward is `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE`.
- Why that path is acceptable:
  - it is also an actual Mendocino/Celeron Windows 98 configuration with dynarec enabled
  - it stays on the same `bf6` machine class as the existing strict Mendocino evidence
  - it keeps the same strict-i686 CPU/machine class while avoiding the stale `Windows 98 TESTING` pointer
- Caveat:
  - earlier strict baseline and failed-boot evidence on disk still come from `Windows 98 TESTING` and remain historically valid under their original paths
  - treat `Windows 98 SE` as the standing VM choice going forward, not as a retroactive rename of those earlier logs
- `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE copy` remains the alternate strict-i686 comparison candidate if later work needs a second Mendocino image.
- The best exact logs currently in hand are still only proxy evidence:
  - K6-2 for current stable exact `0F`
  - Tillamook for exact base and longer exact `0F`
- The strict i686 evidence gap is now closed by `/tmp/windows98_testing_i686_baseline.log`.

## Current fallback evidence gathered

### Current stable K6-2 proxy (`/tmp/windows98_gaming_pc_new_dynarec.log`)

- fallback families:
  - `base=7271`
  - `0f=7831`
  - `x87=3566`
  - `rep=13251`
  - `3dnow=0`
- hottest exact `0F` fallbacks:
  - `0xaf = 2863`
  - `0xba = 2067`
  - `0x02 = 499`
  - `0xb3 = 352`
  - `0xa3 = 268`
  - `0xab = 265`
  - `0x03 = 263`
  - `0x22 = 259`
  - `0x31 = 213`
  - `0x20 = 192`
  - `0xbc = 184`
  - `0x01 = 158`

### MMX-only proxy with exact base breakdown (`/tmp/new_dynarec_mmx_only_validation.log`)

- fallback families:
  - `base=5004`
  - `0f=6455`
  - `x87=2124`
  - `rep=9343`
  - `3dnow=0`
- hottest exact base fallbacks:
  - `0xd1 = 719`
  - `0xd3 = 605`
  - `0xee = 592`
  - `0xcd = 518`
  - `0xe6 = 400`
  - `0xcf = 334`
  - `0xec = 314`
  - `0x8e = 259`
  - `0xd0 = 244`
  - `0x9b = 204`

### MMX-only proxy with longer exact `0F` breakdown (`/tmp/new_dynarec_mmx_only_0f_validation.log`)

- fallback families:
  - `base=7881`
  - `0f=11311`
  - `x87=4001`
  - `rep=15503`
  - `3dnow=0`
- hottest exact `0F` fallbacks:
  - `0xaf = 3416`
  - `0xba = 2108`
  - `0x94 = 914`
  - `0x95 = 828`
  - `0x02 = 585`
  - `0xc8 = 500`
  - `0xb3 = 368`
  - `0xab = 336`
  - `0xa3 = 330`
  - `0x03 = 299`

### Strict Mendocino baseline (`/tmp/windows98_testing_i686_baseline.log`)

- VM:
  - `Windows 98 TESTING`
  - `cpu_family = celeron_mendocino`
  - `machine = bf6`
- fallback families:
  - `base=23216`
  - `0f=8719`
  - `x87=2148`
  - `rep=17238`
  - `3dnow=0`
- hottest exact base fallbacks:
  - `0xee = 5562`
  - `0xe6 = 5319`
  - `0xec = 3578`
  - `0xef = 3202`
  - `0xd1 = 1183`
  - `0xd3 = 983`
  - `0x8e = 520`
  - `0xcd = 500`
  - `0xcf = 462`
  - `0xd0 = 421`
  - `0x9b = 305`
  - `0xe4 = 294`
- hottest exact `0F` fallbacks:
  - `0xaf = 3056`
  - `0xba = 2458`
  - `0x02 = 602`
  - `0xb3 = 446`
  - `0xa3 = 328`
  - `0x03 = 299`
  - `0x22 = 290`
  - `0xab = 289`
  - `0x20 = 218`
  - `0xbc = 218`

## Gap breakdown by category

### Base opcodes

- The remaining exact base bucket is no longer string-heavy on the available proxy run.
- The strict Mendocino result shows the base bucket is now even more system-heavy than the proxy runs.
- It is now dominated by:
  - I/O: `0xe4`, `0xe6`, `0xec`, `0xee`, `0xef`
  - Group 2 shifts/rotates: `0xd0`-`0xd3`
  - interrupt/control flow: `0xcd`, `0xcf`
  - segment/control transfer: `0x8e`
  - FPU wait/control crossover: `0x9b`
- The key structural detail is that `0xd0`-`0xd3` are mostly `helper_bailout`, not `helper_table_null`, which means there is already direct-path code worth tightening.
- The key prioritization detail from the strict i686 run is that the hotter base leaders are not the safer ones:
  - `0xe6`, `0xee`, `0xec`, and `0xef` are hotter than `0xd0`-`0xd3`
  - but they are I/O-heavy and therefore worse next-trial candidates than the existing group-2 shift handlers

### `0F` opcodes

- Current stable proxy `0F` work is still dominated by missing direct coverage, not bailout cleanup.
- The strict Mendocino result keeps the same overall shape:
  - `0xaf`
  - `0xba`
  - `0x02`
  - then the bit-test family and protected/control remainder
- The hottest remaining exact `0F` items split into four clusters:
  - blocked arithmetic: `0xaf`
  - blocked bit-test family: `0xba`, `0xa3`, `0xab`, `0xb3`
  - protected/control/system pair: `0x02`, `0x03`
  - blocked bitscan pair: `0xbc`, `0xbd`

### REP

- REP remains the hottest overall family on every available proxy:
  - `rep=13251` on the stable K6-2 log
  - `rep=9343` on the MMX-only shutdown log
  - `rep=15503` on the longer MMX-only log
- This is still structural, not a narrow missing opcode, because REP direct recompilation is entirely disabled in `codegen.c`.

### x87 / softfloat

- x87 remains large in absolute terms:
  - `x87=3566` on the stable K6-2 log
  - `x87=2124` and `x87=4001` on the MMX-only logs
- The current guest configs on disk use `fpu_type = internal`; this is not a softfloat-driven hotspot right now.
- The architectural softfloat direct-coverage cliff still exists, but it is not the best near-term i686 choice without new evidence.

### Protected/control/system behavior

- This remains materially present in both base and `0F` buckets:
  - base: `0xcd`, `0xcf`, `0x8e`, `0x9b`
  - `0F`: `0x01`, `0x02`, `0x03`, `0x20`, `0x22`, `0x31`
- These are real counts, but they are higher-risk because they cross segment, control-register, privilege, or timing-sensitive behavior.

## Exact opcode-family mapping in this codebase

### Base

- `0xd0` / `0xd1` / `0xd2` / `0xd3`
  - Group 2 shift/rotate family
  - direct handlers already exist as `ropD0`, `ropD1_w`, `ropD1_l`, `ropD2`, `ropD3_w`, `ropD3_l`
  - code lives in `src/codegen_new/codegen_ops_shift.c`
  - the current bailouts are concentrated in:
    - `RCL` / `RCR`
    - `CL == 0`
    - `!block->ins` guard on the variable-count forms
- `0x8e`
  - `MOV Sreg, r/m16`
  - direct handler is `ropMOV_seg_w`
  - code lives in `src/codegen_new/codegen_ops_mov.c`
- `0xcd`
  - `INT imm8`
  - current direct table entry remains `NULL`
- `0xcf`
  - `IRET` / `IRETD`
  - current direct table entry remains `NULL`
- `0xe6` / `0xee`
  - `OUT imm8, AL` and `OUT DX, AL`
  - current direct table entries remain `NULL`
- `0xec`
  - `IN AL, DX`
  - current direct table entry remains `NULL`
- `0x9b`
  - `WAIT` / `FWAIT`
  - current direct table entry remains `NULL`

### `0F`

- `0x0f 0xaf`
  - `IMUL r16/32, r/m16/32`
  - retained direct handlers exist as `ropIMUL_0f_w_rm` and `ropIMUL_0f_l_rm`
  - code lives in `src/codegen_new/codegen_ops_arith.c`
  - direct table dispatch remains disabled in the stable baseline
- `0x0f 0xba`
  - Group 8 immediate bit-test family
  - `BT/BTS/BTR/BTC r/m16/32, imm8`
  - current direct table entry remains `NULL`
- `0x0f 0xa3` / `0x0f 0xab` / `0x0f 0xb3`
  - register-indexed bit-test family
  - `BT` / `BTS` / `BTR`
  - current direct table entries remain `NULL`
- `0x0f 0x02` / `0x0f 0x03`
  - `LAR` / `LSL`
  - current direct table entries remain `NULL`
- `0x0f 0xbc` / `0x0f 0xbd`
  - `BSF` / `BSR`
  - retained direct handlers exist as `ropBSF` and `ropBSR`
  - code lives in `src/codegen_new/codegen_ops_misc.c`
  - direct table dispatch remains disabled in the stable baseline
- `0x0f 0x01` / `0x0f 0x20` / `0x0f 0x22` / `0x0f 0x31`
  - Group 7 system ops, control-register moves, and `RDTSC`
  - these remain helper-table-null system/control work, not narrow ring-3-safe cleanup

## Payoff vs risk shortlist

## Candidate matrix

| Class | Family | Why it belongs there | Near-term disposition |
|---|---|---|---|
| A | No current measured strict-i686 hotspot family | The remaining top families are either helper-backed bailout cleanup or system/I/O-heavy behavior, not another low-side-effect table-hole row | Do not force a Class A pick from the current list |
| B | base `0xd0`-`0xd3` `RCL` / `RCR` | Existing direct handlers, measurable bailout count, prior guest failure, and a now-understood backend shape mistake | Require compare/debug path first |
| B | `0x0f 0xaf` | Existing direct handlers and helper harness support, but still guest-unsafe after host-clean and compare-enabled debugging | Keep guest-disabled |
| B | `0x0f 0xbc` / `0xbd` | Existing direct handlers and helper harness support, but guest-visible regression remained after obvious bugs were fixed | Keep guest-disabled |
| B | `0x0f 0xba` / `0xa3` / `0xab` / `0xb3` | High-payoff bit-test family with wider RMW/indexed semantics and adjacent guest-risk history | Do not attempt without compare/debug support |
| C | base I/O `0xe4` / `0xe6` / `0xec` / `0xee` / `0xef` | Dominated by device traffic and image-specific churn | Keep out of next implementation slot |
| C | `0xcd` / `0xcf` / `0x8e` / `0x9b` | Interrupt, segment, or control crossover work | Keep blocked for near-term CPU dynarec closure |
| C | `0x0f 0x02` / `0x03`, `0x0f 0x01`, `0x0f 0x20`, `0x0f 0x22`, `0x0f 0x31` | Protected/control/system semantics | Leave for a separate system-behavior plan |
| C | REP / x87 follow-up | Broad architectural campaigns rather than narrow opcode closure | Not a near-term guest-facing target |

### 1. Base `0xd0`-`0xd3` group-2 bailout closure

- payoff:
  - `1633` exact hits on the proxy MMX-only base log
  - `2747` exact hits on the strict Mendocino base log
  - generic i686-era integer work, not MMX-only or K6-specific
- implementation risk:
  - low to medium
  - handlers already exist
  - work is likely concentrated on the current bailout cases rather than a brand-new table row
- validation risk:
  - medium
  - flags-heavy, but ring-3-safe and amenable to harness-first validation

### 2. `0x0f 0x02` / `0x0f 0x03` (`LAR` / `LSL`)

- payoff:
  - `762` exact hits on the current stable K6-2 proxy
  - `884` on the longer MMX-only proxy
  - `901` on the strict Mendocino run
- implementation risk:
  - medium to high
  - coherent pair, but protected/control semantics
- validation risk:
  - medium to high
  - needs more than plain arithmetic harness coverage

### 3. REP family

- payoff:
  - clearly the hottest overall family on the strict i686 run too (`rep=17238`)
- implementation risk:
  - high
  - no direct REP table exists today
- validation risk:
  - high
  - broad string and direction-flag surface

### 4. `0x0f` bit-test family (`0xba`, `0xa3`, `0xab`, `0xb3`)

- payoff:
  - very high
  - `2952` exact hits on the current stable K6-2 proxy
- implementation risk:
  - high
  - dynamic bit indexing, memory element selection, and RMW semantics
- validation risk:
  - high
  - previous guest history and current session constraints keep this out of the safest-next-step slot

## Explicit recommendation

Recommended next i686-focused trial:

- that baseline has now been completed at `/tmp/windows98_testing_i686_baseline.log`
- do not roll straight into another guest-facing opcode trial from that evidence
- the next productive step should be:
  - base `0xd0`-`0xd3` compare/debug infrastructure only
  - no guest enablement
  - future compare-only validation should use `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE` as the named strict-i686 VM

Why this is the best next step:

- the strict Mendocino result keeps the same risky `0F` leaders (`0xaf`, `0xba`, `0x02`) but does not make any of them safer
- the hotter base leaders on this VM are mostly I/O and control instructions, which are not good first implementation candidates
- `0xd0`-`0xd3` still have meaningful measured payoff on the strict i686 run while remaining materially safer than the hotter I/O-heavy base opcodes
- they already have direct handlers, so the likely work is bailout debugging rather than introducing a brand-new semantic family
- the arm64 backend audit now shows exactly which helper-call shape must be preserved during that work

If an implementation family must be named immediately after that re-baseline, prefer:

- base `0xd0`-`0xd3`

Do not prefer next:

- `0x0f 0xaf`
- `0x0f 0xbc` / `0x0f 0xbd`
- `0x0f 0xba` or the broader bit-test family

Those remain either explicitly blocked by the session constraints or too risky relative to the current evidence quality.

Recommended standing strict-i686 baseline:

- `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE`
- note in future plans that earlier `windows98_testing_*` logs remain the historical baseline lineage for prior evidence, while new validation should target `Windows 98 SE`

## Follow-up implementation status

- The recommended narrow next trial was taken, validated, and then backed out for guest use:
  - host-side rotate-through-carry result and `CF` / `OF` mask helpers remain in `src/codegen_new/codegen_test_support.c`
  - `src/codegen_new/codegen_ops_shift.c` has been restored to the stable immediate-fallback behavior for `D0` / `D1` / `D2` / `D3` `RCL` / `RCR`
- The host-side gate for that slice is still in place and passing:
  - `tests/codegen_new_0f_semantics_test.c` now covers representative `RCL` / `RCR` result and flag-mask cases across 8-bit, 16-bit, and 32-bit widths
  - `tests/codegen_new_dynarec_observability_test.c` now also covers the compact `D0`-`D3` compare summary surface
- The guest-facing result was not stable:
  - the first validation attempt died in the backend on an unsupported third helper argument
  - the second validation attempt booted far enough to show the same class of early Windows “insufficient conventional memory” failure seen on other unstable dynarec trials
  - the failed-boot shutdown profile at `/tmp/windows98_testing_i686_d0d3_validation.log` is therefore not comparable to `/tmp/windows98_testing_i686_baseline.log`
- The new compare-only follow-up is now implemented too:
  - `86BOX_NEW_DYNAREC_DEBUG_D0D3_RCLRCR=1` enables only the sampled `D0`-`D3` `RCL` / `RCR` compare path
  - `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE=1` emits compact mismatch lines plus a shutdown summary
  - `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE_SITES=1` emits one first-hit line per unique sampled compare site, and shutdown logging now preserves the discovered site list with per-site `attempts`, `mismatches`, `no_block_ins_bailouts`, `zero_count_bailouts`, `count_zero`, `count_one`, `count_multi`, `cf_set`, `operand_zero`, `result_changed`, and `flags_nonzero` so a later run can promote a real site into `86BOX_NEW_DYNAREC_VERIFY_PC`
  - `86BOX_NEW_DYNAREC_VERIFY_PC`, `86BOX_NEW_DYNAREC_VERIFY_OPCODE`, and `86BOX_NEW_DYNAREC_VERIFY_BUDGET` remain the safest way to keep the compare run narrow on `Windows 98 SE`
  - enough broad and locked-site compare-only evidence now exists to stop expanding this `D0`-`D3` probe family for the moment; the next productive branch step should return to a low-risk implementation family instead
