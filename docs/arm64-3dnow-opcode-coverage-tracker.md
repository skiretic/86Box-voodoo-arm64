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
| `ae` | `PFACC` | 3DNow base | PASS | Not enabled yet |
| `b0` | `PFCMPEQ` | 3DNow base | PASS | Enabled (pending validation) |
| `b4` | `PFMUL` | 3DNow base | PASS | Enabled (pending validation) |
| `b6` | `PFRCPIT2` | 3DNow base | PASS | Enabled (pending validation) |
| `b7` | `PMULHRW` | 3DNow base | PASS | Not enabled yet |
| `bf` | `PAVGUSB` | 3DNow base | PASS | Not enabled yet |
| `0c` | `PI2FW` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |
| `1c` | `PF2IW` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |
| `8a` | `PFNACC` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |
| `8e` | `PFPNACC` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |
| `bb` | `PSWAPD` | 3DNowExt | SKIP_NO_3DNOWEXT | Not enabled yet |

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
| `ae` | `PFACC` | Phase 2 | Planned | Needs explicit mapping/coverage confirmation. |
| `b7` | `PMULHRW` | Phase 2 | Planned | Needs explicit mapping/coverage confirmation. |
| `bf` | `PAVGUSB` | Phase 2 | Planned | Needs explicit mapping/coverage confirmation. |
| `0c` | `PI2FW` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |
| `1c` | `PF2IW` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |
| `8a` | `PFNACC` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |
| `8e` | `PFPNACC` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |
| `bb` | `PSWAPD` | Phase 3 (3DNowExt) | Planned | Gate on CPUID 3DNowExt profile. |

## Progress Update Log

Update this table every time a 3DNow bring-up slice lands or a validation run completes.

| Date (ET) | Change / Run | Result | Coverage Delta | Notes |
| --- | --- | --- | --- | --- |
| 2026-04-22 | `3dnowcov-r1` first full harness run | `pass=17 fail=2 skip=5`, `ERROR` | Detected false-fail vectors on `PFMIN`/`PFMAX` | Prompted finite-input fix in harness. |
| 2026-04-22 | harness finite-domain fix + compact output | Build/ISO updated | N/A | Made one-screen screenshot workflow deterministic. |
| 2026-04-22 | `3dnowcov-r2` rerun | `pass=19 fail=0 skip=5`, `DONE` | Baseline interpreter coverage stable | Ready to start Phase 1 ARM64 dynarec mapping. |
| 2026-04-22 | Phase 1 ARM64 curated `recomp_opcodes_3DNOW` + dynarec/fallback counters | Code landed, ready to validate | Enabled 16 Phase 1 opcodes (`0d,1d,90,94,96,97,9a,9e,a0,a4,a6,a7,aa,b0,b4,b6`) | Added opt-in `DYNAREC_3DNOW_SUMMARY` periodic/final counters via `86BOX_3DNOW_COV_STATS=1`; VM run pending. |
| 2026-04-22 | `3dnowcov-r4/r5` regression triage (`PFRSQRT` fail) + ARM64 lowerer fix | Code landed, ready to validate | `97` path corrected in ARM64 `UOP_PFRSQRT` lowering (`1.0 / sqrt(src)` source-preserving sequence) | Observed guest `pass=18 fail=1` with `3DNOW_OP PFRSQRT FAIL`; fixed `codegen_backend_arm64_uops.c` clobber of sqrt temp. |
