# ARM64 3DNow Opcode Coverage Tracker

## How to Read This

- `Guest Validation` reflects `3DNOWCOV` behavior (current baseline is interpreter/dispatch path).
- `ARM64 Dynarec` reflects whether opcode is actively mapped in `recomp_opcodes_3DNOW` on ARM64.
- This tracker is the live checklist for bring-up sequencing.

## Current Coverage Snapshot (Baseline)

Baseline validation run:
- run tag: `3dnowcov-r2`
- guest result: `3DNOWCOV_COUNTS pass=19 fail=0 skip=5 iters=200000`
- guest total hash: `83e69a2e`
- status: harness stable; Phase 1 ARM64 curated mapping landed in code, guest validation pending.

Legend:
- `PASS` = validated on current emulated profile
- `SKIP_NO_3DNOWEXT` = expected skip on non-3DNowExt profile
- `Pending` = not yet validated in current profile

| imm8 | Mnemonic | Class | Guest Validation | ARM64 Dynarec |
| --- | --- | --- | --- | --- |
| `0d` | `PI2FD` | 3DNow base | PASS | Enabled (pending validation) |
| `1d` | `PF2ID` | 3DNow base | PASS | Enabled (pending validation) |
| `90` | `PFCMPGE` | 3DNow base | PASS | Enabled (pending validation) |
| `94` | `PFMIN` | 3DNow base | PASS | Enabled (pending validation) |
| `96` | `PFRCP` | 3DNow base | PASS | Enabled (pending validation) |
| `97` | `PFRSQRT` | 3DNow base | PASS | Enabled (pending validation) |
| `9a` | `PFSUB` | 3DNow base | PASS | Enabled (pending validation) |
| `9e` | `PFADD` | 3DNow base | PASS | Enabled (pending validation) |
| `a0` | `PFCMPGT` | 3DNow base | PASS | Enabled (pending validation) |
| `a4` | `PFMAX` | 3DNow base | PASS | Enabled (pending validation) |
| `a6` | `PFRCPIT1` | 3DNow base | PASS | Enabled (pending validation) |
| `a7` | `PFRSQIT1` | 3DNow base | PASS | Enabled (pending validation) |
| `aa` | `PFSUBR` | 3DNow base | PASS | Enabled (pending validation) |
| `ae` | `PFACC` | 3DNow base | PASS | Enabled (validated) |
| `b0` | `PFCMPEQ` | 3DNow base | PASS | Enabled (pending validation) |
| `b4` | `PFMUL` | 3DNow base | PASS | Enabled (pending validation) |
| `b6` | `PFRCPIT2` | 3DNow base | PASS | Enabled (pending validation) |
| `b7` | `PMULHRW` | 3DNow base | PASS | Enabled (validated) |
| `bf` | `PAVGUSB` | 3DNow base | PASS | Enabled (validated) |
| `0c` | `PI2FW` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |
| `1c` | `PF2IW` | 3DNowExt | SKIP_NO_3DNOWEXT | Enabled (3DNowExt profile only, pending validation) |
| `8a` | `PFNACC` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |
| `8e` | `PFPNACC` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |
| `bb` | `PSWAPD` | 3DNowExt | SKIP_NO_3DNOWEXT | Enabled (3DNowExt profile only, pending validation) |

## ARM64 Dynarec Bring-Up Target Table

Use this table to track progress as ARM64 mapping is enabled opcode-by-opcode.

Status values:
- `Planned`
- `Enabled`
- `Validated`
- `Blocked`

| imm8 | Mnemonic | Phase Target | ARM64 Dynarec Status | Notes |
| --- | --- | --- | --- | --- |
| `0d` | `PI2FD` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `1d` | `PF2ID` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `90` | `PFCMPGE` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `94` | `PFMIN` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `96` | `PFRCP` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `97` | `PFRSQRT` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `9a` | `PFSUB` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `9e` | `PFADD` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `a0` | `PFCMPGT` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `a4` | `PFMAX` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `a6` | `PFRCPIT1` | Phase 1 | Enabled | Curated ARM64 table entry added via `ropPFRCPIT`; validate in next run. |
| `a7` | `PFRSQIT1` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `aa` | `PFSUBR` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `b0` | `PFCMPEQ` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `b4` | `PFMUL` | Phase 1 | Enabled | Curated ARM64 table entry added; validate in next run. |
| `b6` | `PFRCPIT2` | Phase 1 | Enabled | Curated ARM64 table entry added via `ropPFRCPIT`; validate in next run. |
| `ae` | `PFACC` | Phase 2 | Validated | ARM64 lowering switched to `FADDP.V2S` pairwise path and matched harness baseline. |
| `b7` | `PMULHRW` | Phase 2 | Validated | ARM64 lowering uses `SMULL + SRSHR + XTN` and matches harness baseline. |
| `bf` | `PAVGUSB` | Phase 2 | Validated | ARM64 lowering uses `URHADD.V8B` and matches harness baseline. |
| `0c` | `PI2FW` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |
| `1c` | `PF2IW` | Phase 3 (3DNowExt) | Enabled | Real dynarec `ropPF2IW` path added; runtime-gated on `CPU_FEATURE_3DNOWE` and preserves destination upper words per interpreter semantics. |
| `8a` | `PFNACC` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |
| `8e` | `PFPNACC` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |
| `bb` | `PSWAPD` | Phase 3 (3DNowExt) | Enabled | Real dynarec `ropPSWAPD` path added; gated at runtime on `CPU_FEATURE_3DNOWE` to preserve non-3DNowExt behavior. |

## Progress Update Log

Update this table every time a 3DNow bring-up slice lands or a validation run completes.

| Date (ET) | Change / Run | Result | Coverage Delta | Notes |
| --- | --- | --- | --- | --- |
| 2026-04-22 | `3dnowcov-r1` first full harness run | `pass=17 fail=2 skip=5`, `ERROR` | Detected false-fail vectors on `PFMIN`/`PFMAX` | Prompted finite-input fix in harness. |
| 2026-04-22 | harness finite-domain fix + compact output | Build/ISO updated | N/A | Made one-screen screenshot workflow deterministic. |
| 2026-04-22 | `3dnowcov-r2` rerun | `pass=19 fail=0 skip=5`, `DONE` | Baseline interpreter coverage stable | Ready to start Phase 1 ARM64 dynarec mapping. |
| 2026-04-22 | Phase 1 ARM64 curated `recomp_opcodes_3DNOW` + dynarec/fallback counters | Code landed, ready to validate | Enabled 16 Phase 1 opcodes (`0d,1d,90,94,96,97,9a,9e,a0,a4,a6,a7,aa,b0,b4,b6`) | Added opt-in `DYNAREC_3DNOW_SUMMARY` periodic/final counters via `86BOX_3DNOW_COV_STATS=1`; VM run pending. |
| 2026-04-22 | `3dnowcov-r4/r5` regression triage (`PFRSQRT` fail) + ARM64 lowerer fix | Code landed, ready to validate | `97` path corrected in ARM64 `UOP_PFRSQRT` lowering (`1.0 / sqrt(src)` source-preserving sequence) | Observed guest `pass=18 fail=1` with `3DNOW_OP PFRSQRT FAIL`; fixed `codegen_backend_arm64_uops.c` clobber of sqrt temp. |
| 2026-04-22 | `3dnowcov-r6` post-fix validation | `pass=19 fail=0 skip=5`, `DONE` | Phase 1 curated ARM64 path validated against harness baseline hash `83e69a2e` | Host telemetry confirmed mixed dispatch with recompile preference: `DYNAREC_3DNOW_SUMMARY tag=final total=38 recompiled=32 fallback=6`. |
| 2026-04-22 | Phase 2 partial bring-up: `PFACC` dynarec path | Code landed, ready to validate | Added ARM64 dynarec coverage for opcode `ae` | Kept `b7`/`bf` fallback; real path uses new `UOP_PFACC` lowering (no helper-call substitution). |
| 2026-04-22 | `3dnowcov-r7` regression triage (`PFACC` fail) + PFACC lane-pack fix | Code landed, ready to validate | `ae` lowering switched from integer pairwise add to explicit float-lane sum+insert path | Observed guest `pass=18 fail=1` with `3DNOW_OP PFACC FAIL`; added ARM64 `INS_S` emitter and corrected `UOP_PFACC` lane construction. |
| 2026-04-23 | `3dnowcov-r8` regression triage (`PFACC` still fail) + alias-safe PFACC fix | Code landed, ready to validate | PFACC lowering now computes `sum_b` before mutating destination to preserve source when `src==dst` | Host telemetry showed `recompiled=34 fallback=4`, confirming active dynarec path; patched `codegen_backend_arm64_uops.c` ordering for ModRM alias safety. |
| 2026-04-23 | `3dnowcov-r9` regression triage (`PFACC` still fail) + ZIP lane compose fix | Code landed, ready to validate | PFACC now assembles `[sum_a,sum_b]` via `ZIP1_V2S` from lane0 sums, removing insert-op dependency | Observed persistent `3DNOW_OP PFACC FAIL` with `recompiled=34 fallback=4`; replaced ARM64 lane-pack in `codegen_backend_arm64_uops.c` and removed unused `INS_S` emitter path. |
| 2026-04-23 | `3dnowcov-r10/r11` PFACC triage conclusion + ARM64 safety gate | Code landed, ready to validate | Temporarily disabled ARM64 dynarec dispatch for `ae` (`PFACC`) to force known-good instruction-function fallback | Repeated runs stayed `pass=18 fail=1` with stable `PFACC` fail hash while dynarec active (`recompiled=34 fallback=4`); gated `ae` to `NULL` on ARM64 table (`fallback`) pending bit-exact lowerer fix. |
| 2026-04-23 | `3dnowcov-r12` safety-gated confirmation | `pass=19 fail=0 skip=5`, `DONE` | Confirmed harness stability with PFACC forced fallback | Host telemetry showed fallback-heavy split (`recompiled=32 fallback=6`) as expected for temporary safety gate. |
| 2026-04-23 | `3dnowcov-r13` PFACC dynarec real fix | `pass=19 fail=0 skip=5`, `DONE` | Re-enabled `ae` with ARM64 pairwise-add lowering via `FADDP.V2S` | Host telemetry returned to dynarec-forward split (`DYNAREC_3DNOW_SUMMARY ... recompiled=34 fallback=4`) with no `PFACC FAIL`. |
| 2026-04-23 | Phase 2 partial bring-up: `PMULHRW` + `PAVGUSB` dynarec path | Code landed, ready to validate | Added ARM64 dynarec coverage for opcodes `b7` and `bf` | Real codegen only (no helper fallback): `PMULHRW` lowered with `SMULL + SRSHR + XTN`, `PAVGUSB` lowered with `URHADD`; ARM64 `recomp_opcodes_3DNOW` now maps both. |
| 2026-04-23 | `3dnowcov-r16` validation for `b7`/`bf` slice | `pass=19 fail=0 skip=5`, `DONE` | `b7` and `bf` moved to validated | Final ARM64 PMULHRW fix corrected `SRSHR` encoding and kept non-saturating narrow; host telemetry: `DYNAREC_3DNOW_SUMMARY tag=final total=38 recompiled=38 fallback=0`. |
| 2026-04-23 | Phase 3 partial bring-up: `PSWAPD` dynarec path (`bb`) | Code landed, ready to validate | Added ARM64 dynarec coverage for opcode `bb` under 3DNowExt profile | Real codegen path in `ropPSWAPD` uses `PSRLQ + PSLLQ + OR` (no helper-call substitution) and runtime-gates on `CPU_FEATURE_3DNOWE` to keep non-3DNowExt behavior unchanged. |
| 2026-04-23 | `3dnowcov-r18-pswapd-ext` first 3DNowExt-profile validation run | `pass=23 fail=1 skip=0`, `ERROR` | Revealed failure on `1c PF2IW`; `bb` observed passing in guest row (`bbP`) | Host telemetry showed active split (`DYNAREC_3DNOW_SUMMARY ... total=48 recompiled=40 fallback=8`) on `k6_2p`; next slice targets real ARM64 `PF2IW` lowering. |
| 2026-04-23 | Phase 3 partial bring-up: `PF2IW` dynarec path (`1c`) | Code landed, ready to validate | Added ARM64 dynarec coverage for opcode `1c` under 3DNowExt profile | Real codegen path uses `PF2ID` conversion plus integer bit-lane compose to match interpreter `sw[0..1]` writes while preserving destination upper words; no helper-call substitution. |
