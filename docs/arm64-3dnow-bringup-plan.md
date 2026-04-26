# ARM64 3DNow Dynarec Bring-Up Plan

## Scope and Intent

This plan defined the controlled bring-up path for enabling 3DNow recompilation on ARM64 in the new dynarec, with strict rollback boundaries and deterministic validation.

Current final state (2026-04-23):
- ARM64 `recomp_opcodes_3DNOW` mapping is enabled for all opcodes covered by `3DNOWCOV`, including 3DNowExt opcodes.
- 3DNow/3DNowExt coverage profile validates at `pass=24 fail=0 skip=0` with stable total hash `28aeb9ef`.
- Host telemetry confirms no fallback in validation and gameplay soak runs:
  - `s03g-ext-pswapd`: `DYNAREC_3DNOW_SUMMARY tag=final total=48 recompiled=48 fallback=0`
  - `s03h-game-3dnow-soak-01`: `DYNAREC_3DNOW_SUMMARY tag=final total=4427 recompiled=4427 fallback=0`
- Pre-logging locked baseline still stands as the comparison gate:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md`
- Current logging-on analysis baseline for op-family mix is separate:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2`

## Baseline Evidence (Lab Entry)

Validation run (`3dnowcov-r2`) confirms stable baseline and usable harness:
- guest markers:
  - `3DNOWCOV_DONE`
  - `3DNOWCOV_COUNTS pass=19 fail=0 skip=5 iters=200000`
  - `3DNOWCOV_TOTAL hash=83e69a2e`
- skipped opcodes are expected on non-3DNowExt profile:
  - `0c PI2FW`, `1c PF2IW`, `8a PFNACC`, `8e PFPNACC`, `bb PSWAPD`
- host telemetry (`86BOX_3DNOW_COV_STATS=1`) confirms heavy runtime coverage (`DYNAREC_3DNOW_SUMMARY` with high total/recompiled counts and explicit fallback count).
- Current logging-on runs also emit `DYNAREC_3DNOW_OPSUMMARY` when `86BOX_3DNOW_COV_STATS=1` is enabled.

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
7. Clean-code enforcement for this bring-up:
   - any newly written code that fails validation must be fixed or removed before handoff.
   - do not leave partial/experimental dead paths from the current slice in tree.
   - do not silently modify unrelated pre-existing code; call out and justify such edits before making them.

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
- `DYNAREC_3DNOW_SUMMARY` host logging in `src/cpu/x86_ops_3dnow.h`

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
- For perf-family analysis, use the logging-on baseline artifact from `2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2`.

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
- complete companion non-imm8 3DNow-era opcodes in `0F` map:
  - `0F 0D /r` (`PREFETCH/PREFETCHW`)
  - `0F 0E` (`FEMMS`)

Gate:
- `3DNOWCOV` remains `pass=19 fail=0 skip=5`.
- host telemetry confirms high `opae`, `opb7`, `opbf` hits during dynarec execution.

Rollback trigger:
- any regression in `3DNOWCOV` pass/fail or numerical hash instability.

### Phase 3: 3DNowExt Enablement (Completed)

Goal:
- support 3DNowExt paths under profile exposing 3DNowExt bit.

Work:
- implement and enable:
  - `0c PI2FW`
  - `1c PF2IW`
  - `8a PFNACC`
  - `8e PFPNACC`
  - `bb PSWAPD`
- run on CPU profile exposing 3DNowExt bit.

Gate (achieved):
- `3DNOWCOV` transitioned from `skip=5` to `skip=0` with `fail=0`.
- final Ext checkpoint (`s03g-ext-pswapd`) reached `fallback=0`.

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
5. Parse host `DYNAREC_3DNOW_SUMMARY` lines and verify expected opcode activity.

## Branching / Rollback Strategy

- Work remained on `ndr-3dnow-lab` through completion.
- Commit each phase separately:
  - `P1` table enable
  - `P2` missing opcode implementations
  - `P3` 3DNowExt expansion
- If a phase fails, revert only that phase commit; retain harness and prior stable phases.

## Exit Criteria for Merge Candidate

A merge candidate is ready when all are true:
- Phase 1, Phase 2, and Phase 3 complete with stable deterministic `3DNOWCOV` output.
- Final coverage checkpoints are stable:
  - `s03g-ext-pswapd`: `pass=24 fail=0 skip=0`, hash `28aeb9ef`, `fallback=0`
  - `s03h-game-3dnow-soak-01`: gameplay telemetry `fallback=0`
- No regressions in existing locked workloads (`WL-05`, Q3, 3DMark99).
- ARM64-only scope preserved; x86-64 unchanged.
- Documentation updated with final opcode coverage map and known unsupported cases.
