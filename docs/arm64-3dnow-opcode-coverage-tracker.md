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
- status: harness stable; ARM64 dynarec 3DNow mapping not yet enabled.

Legend:
- `PASS` = validated on current emulated profile
- `SKIP_NO_3DNOWEXT` = expected skip on non-3DNowExt profile
- `Pending` = not yet validated in current profile

| imm8 | Mnemonic | Class | Guest Validation | ARM64 Dynarec |
| --- | --- | --- | --- | --- |
| `0d` | `PI2FD` | 3DNow base | PASS | Not enabled yet |
| `1d` | `PF2ID` | 3DNow base | PASS | Not enabled yet |
| `90` | `PFCMPGE` | 3DNow base | PASS | Not enabled yet |
| `94` | `PFMIN` | 3DNow base | PASS | Not enabled yet |
| `96` | `PFRCP` | 3DNow base | PASS | Not enabled yet |
| `97` | `PFRSQRT` | 3DNow base | PASS | Not enabled yet |
| `9a` | `PFSUB` | 3DNow base | PASS | Not enabled yet |
| `9e` | `PFADD` | 3DNow base | PASS | Not enabled yet |
| `a0` | `PFCMPGT` | 3DNow base | PASS | Not enabled yet |
| `a4` | `PFMAX` | 3DNow base | PASS | Not enabled yet |
| `a6` | `PFRCPIT1` | 3DNow base | PASS | Not enabled yet |
| `a7` | `PFRSQIT1` | 3DNow base | PASS | Not enabled yet |
| `aa` | `PFSUBR` | 3DNow base | PASS | Not enabled yet |
| `ae` | `PFACC` | 3DNow base | PASS | Not enabled yet |
| `b0` | `PFCMPEQ` | 3DNow base | PASS | Not enabled yet |
| `b4` | `PFMUL` | 3DNow base | PASS | Not enabled yet |
| `b6` | `PFRCPIT2` | 3DNow base | PASS | Not enabled yet |
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
| `0d` | `PI2FD` | Phase 1 | Planned | Existing uop/lowerer present. |
| `1d` | `PF2ID` | Phase 1 | Planned | Existing uop/lowerer present. |
| `90` | `PFCMPGE` | Phase 1 | Planned | Existing uop/lowerer present. |
| `94` | `PFMIN` | Phase 1 | Planned | Existing uop/lowerer present. |
| `96` | `PFRCP` | Phase 1 | Planned | Existing uop/lowerer present. |
| `97` | `PFRSQRT` | Phase 1 | Planned | Existing uop/lowerer present. |
| `9a` | `PFSUB` | Phase 1 | Planned | Existing uop/lowerer present. |
| `9e` | `PFADD` | Phase 1 | Planned | Existing uop/lowerer present. |
| `a0` | `PFCMPGT` | Phase 1 | Planned | Existing uop/lowerer present. |
| `a4` | `PFMAX` | Phase 1 | Planned | Existing uop/lowerer present. |
| `a6` | `PFRCPIT1` | Phase 1 | Planned | Uses `ropPFRCPIT` path. |
| `a7` | `PFRSQIT1` | Phase 1 | Planned | Existing rop/uop path. |
| `aa` | `PFSUBR` | Phase 1 | Planned | Existing rop/uop path. |
| `b0` | `PFCMPEQ` | Phase 1 | Planned | Existing uop/lowerer present. |
| `b4` | `PFMUL` | Phase 1 | Planned | Existing uop/lowerer present. |
| `b6` | `PFRCPIT2` | Phase 1 | Planned | Uses `ropPFRCPIT` path. |
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

