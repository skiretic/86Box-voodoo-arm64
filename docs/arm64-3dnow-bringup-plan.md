# ARM64 3DNow Dynarec Bring-Up Plan

## Scope and Intent

This plan defines a controlled bring-up path for enabling 3DNow recompilation on ARM64 in the new dynarec, with strict rollback boundaries and deterministic validation.

Current state:
- ARM64 builds currently disable `recomp_opcodes_3DNOW` table entries, so 3DNow execution routes through instruction-function dispatch.
- ARM64 backend already contains lowerers for a subset of 3DNow uops.
- A new Win98 guest validation workload (`3DNOWCOV`) now provides deterministic opcode-level pass/fail markers and hashes visible directly in-guest.

## Baseline Evidence (Lab Entry)

Validation run (`3dnowcov-r2`) confirms stable baseline and usable harness:
- guest markers:
  - `3DNOWCOV_DONE`
  - `3DNOWCOV_COUNTS pass=19 fail=0 skip=5 iters=200000`
  - `3DNOWCOV_TOTAL hash=83e69a2e`
- skipped opcodes are expected on non-3DNowExt profile:
  - `0c PI2FW`, `1c PF2IW`, `8a PFNACC`, `8e PFPNACC`, `bb PSWAPD`
- host telemetry (`86BOX_3DNOW_COV_STATS=1`) confirms heavy runtime coverage (`COV3DNOW_SUMMARY` with high `opXX` hit counts).

## Safety Rules

1. ARM64-only behavior changes for 3DNow bring-up.
2. Preserve x86-64 behavior exactly.
3. Keep default telemetry low-noise:
   - `86BOX_NEW_DYNAREC_STATS=1`
   - `86BOX_NEW_DYNAREC_TELEMETRY=0`
   - `86BOX_A013_TRACE=0`
4. Keep 3DNow coverage telemetry opt-in only:
   - `86BOX_3DNOW_COV_STATS=1` to enable.
5. Move forward-only; no history rewrite.
6. Each phase must be independently revertible.

## Proven Low-Churn Method (Keep Using This)

Use this exact implementation order for each new opcode slice:
1. Add real ARM64 dynarec path first (no helper-call substitution).
2. Keep unsupported opcodes as `NULL` fallback until their real lowering exists.
3. Mirror x86 semantics exactly from interpreter path before coding.
4. Verify ARM64 instruction encodings with assembler/disassembly (`clang` + `otool`) before relying on handwritten constants.
5. Prefer exact architecture ops over synthesized approximations when available.
6. Validate with `3DNOWCOV` guest hash/markers first; only then broaden to general workload feel checks.
7. Keep telemetry low-noise by default; enable `86BOX_3DNOW_COV_STATS=1` only when dispatch split evidence is needed.

This method was used to stabilize `ae` (`PFACC`) and to land `b7` (`PMULHRW`) / `bf` (`PAVGUSB`) with clean validation.

## Bring-Up Phases

### Phase 0: Harness Lock (Done)

Goals:
- deterministic guest-visible coverage + hash reporting
- host-side opcode histogram telemetry

Assets:
- `tools/win98-3dnowcov/` ISO kit and scripts
- `COV3DNOW_SUMMARY` host logging in `src/cpu/x86_ops_3dnow.h`

Gate to proceed:
- `3DNOWCOV_DONE` with `fail=0` on baseline interpreter path.

### Phase 1: ARM64 Decode-Table Soft Enable (Minimal Set)

Goal:
- enable ARM64 `recomp_opcodes_3DNOW` only for opcodes with complete `rop` + ARM64 uop backend coverage.

Initial opcode set (from existing uop support):
- `0d PI2FD`
- `1d PF2ID`
- `90 PFCMPGE`
- `94 PFMIN`
- `96 PFRCP`
- `97 PFRSQRT`
- `9a PFSUB`
- `9e PFADD`
- `a0 PFCMPGT`
- `a4 PFMAX`
- `a6 PFRCPIT1` (mapped via existing `ropPFRCPIT`)
- `a7 PFRSQIT1`
- `aa PFSUBR`
- `b0 PFCMPEQ`
- `b4 PFMUL`
- `b6 PFRCPIT2` (mapped via existing `ropPFRCPIT`)

Explicitly keep interpreter fallback for now:
- `ae PFACC`
- `b7 PMULHRW`
- `bf PAVGUSB`
- all `3DNowExt`-only ops currently skipped on K6-2 profile.

Implementation notes:
- remove ARM64 blanket zeroing of `recomp_opcodes_3DNOW`.
- under ARM64, provide a curated table with only known-safe entries above.
- leave unsupported entries as `NULL` to preserve fallback behavior.

Gate:
- `3DNOWCOV` must remain `fail=0` and total hash stable against phase baseline for covered opcodes.
- no crash/hang in guest run.

Rollback trigger:
- any `3DNOWCOV_ERROR`, hash drift for stable cases, or emulator instability.

### Phase 2: PFACC / PMULHRW / PAVGUSB Completion

Goal:
- close the remaining non-3DNowExt gaps in the current K6-2 workload profile.

Work:
- add missing IR/uop paths and ARM64 lowerers for:
  - `PFACC`
  - `PMULHRW`
  - `PAVGUSB`
- then map opcodes `ae`, `b7`, `bf` into ARM64 recomp table.

Gate:
- `3DNOWCOV` remains `pass=19 fail=0 skip=5`.
- host telemetry confirms high `opae`, `opb7`, `opbf` hits during dynarec execution.

Rollback trigger:
- any regression in `3DNOWCOV` pass/fail or numerical hash instability.

### Phase 3: 3DNowExt Enablement (Optional Profile Expansion)

Goal:
- support 3DNowExt paths currently skipped on K6-2 profile.

Work:
- implement and enable:
  - `0c PI2FW`
  - `1c PF2IW`
  - `8a PFNACC`
  - `8e PFPNACC`
  - `bb PSWAPD`
- run on CPU profile exposing 3DNowExt bit.

Gate:
- `3DNOWCOV` transitions expectedly from `skip=5` to `skip=0` with `fail=0`.

Rollback trigger:
- any mismatch in deterministic hashes or instability in 3DNowExt-capable profile.

## Runtime Validation Matrix

Required per bring-up phase:
1. Build/sign pass.
2. Launch telemetry run with:
   - `86BOX_NEW_DYNAREC_STATS=1`
   - `86BOX_NEW_DYNAREC_TELEMETRY=0`
   - `86BOX_A013_TRACE=0`
   - `86BOX_3DNOW_COV_STATS=1`
3. Guest run:
   - `D:\SCRIPTS\COV3D_RUN.BAT D:`
4. Capture guest screenshot containing markers + compact status/hash rows.
5. Parse host `COV3DNOW_SUMMARY` lines and verify expected opcode activity.

## Branching / Rollback Strategy

- Keep this work on `ndr-3dnow-lab` until Phase 2 is stable.
- Commit each phase separately:
  - `P1` table enable
  - `P2` missing opcode implementations
  - `P3` 3DNowExt expansion
- If a phase fails, revert only that phase commit; retain harness and prior stable phases.

## Exit Criteria for Merge Candidate

A merge candidate is ready when all are true:
- Phase 1 and Phase 2 complete with stable deterministic `3DNOWCOV` output.
- No regressions in existing locked workloads (`WL-05`, Q3, 3DMark99).
- ARM64-only scope preserved; x86-64 unchanged.
- Documentation updated with final opcode coverage map and known unsupported cases.
