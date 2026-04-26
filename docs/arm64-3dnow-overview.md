# ARM64 3DNow Overview

## What This Is

This is the quick onboarding page for ARM64 3DNow/3DNowExt dynarec status in this repo.

Current state:
- 3DNow base + 3DNowExt coverage in `3DNOWCOV` is validated.
- Latest Ext validation profile result: `pass=24 fail=0 skip=0`, hash `28aeb9ef`.
- Latest full telemetry checkpoints show no fallback:
  - `s03g-ext-pswapd`: `DYNAREC_3DNOW_SUMMARY tag=final total=48 recompiled=48 fallback=0`
  - `s03h-game-3dnow-soak-01`: `DYNAREC_3DNOW_SUMMARY tag=final total=4427 recompiled=4427 fallback=0`
- Latest phase-1 accepted run after alias-safe `PFRCP` fix:
  - `3dnow-pfrcp-aliasfix-realcheck-r2`: `DYNAREC_3DNOW_SUMMARY tag=final total=3827 recompiled=3827 fallback=0`
  - run artifact: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_12-40-10-Windows 98 Gaming PC-3dnow-pfrcp-aliasfix-realcheck-r2`
- Latest non-opcode runtime pacing slice (Qt main loop):
  - branch commit: `ee4d5c5ae` (`qt: pace main loop with single-step pc_run`)
  - intent: reduce burst catch-up wobble by doing one `pc_run()` per scheduler pass.
  - correctness guard: `3DNOWCOV`/`MRUNALL` expected hashes remained unchanged in sanity runs.
  - details and run deltas are tracked in `docs/arm64-3dnow-findings-and-emu-speed-port-plan.md` (`Qt Pacing` + `Oscillation Pattern Analysis` sections).

Baseline note:
- Pre-logging locked baseline artifact:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md`
- Current logging-on analysis baseline:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2`

## Where To Read Next

1. `docs/README.md`
- Current docs map for this branch (active vs archived).
2. `docs/arm64-3dnow-opcode-coverage-tracker.md`
- Source of truth for per-opcode status and run history.
3. `docs/arm64-3dnow-authoritative-notes.md`
- Canonical opcode/CPUID references and implementation rules.
4. `docs/arm64-3dnow-bringup-plan.md`
- Historical phase plan and completion criteria.

## Key Code Locations

- `src/cpu/x86_ops_3dnow.h` (interpreter semantics + shared behavior)
- `src/codegen_new/codegen_ops_3dnow.c` (dynarec opcode front-end)
- `src/codegen_new/codegen_ops.c` (opcode dispatch tables)
- `src/codegen_new/codegen_ir_defs.h` (3DNow uop definitions)
- `src/codegen_new/codegen_backend_arm64_uops.c` (ARM64 lowerers)
- `tools/win98-3dnowcov/3dnowcov_win98.c` (guest harness)

## Validation Workflow (Short Form)

1. Build/sign:
- `./scripts/build-and-sign.sh`
2. Launch telemetry run:
- `env 86BOX_3DNOW_COV_STATS=1 ./scripts/dynarec/launch-vm-telemetry-run.sh <tag>`
3. In guest, run:
- `D:\SCRIPTS\COV3D_RUN.BAT D:`
4. Parse host log for:
- `DYNAREC_3DNOW_SUMMARY tag=final total=X recompiled=Y fallback=Z`
5. Parse consolidated speed + opcode mix summary with:
- `./scripts/dynarec/analyze-emu-speed-log.sh <86box.log>`
- includes:
  - `DYNAREC_3DNOW_OPSUMMARY_PARSED ...`
  - `DYNAREC_3DNOW_ARITH_BREAKDOWN ...`
  - `DYNAREC_3DNOW_RECIP_BREAKDOWN ...`

Interpretation:
- `Z=0`: every executed 3DNow/3DNowExt op in that run used dynarec lowering.
- `Z>0`: some executed ops still fell back.

## Scope Guardrails

- ARM64-only behavior changes for this bring-up.
- Keep x86-64 behavior unchanged.
- Prefer real dynarec lowering first (no helper substitution for new opcode paths).
