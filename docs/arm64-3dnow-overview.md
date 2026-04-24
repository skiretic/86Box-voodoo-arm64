# ARM64 3DNow Overview

## What This Is

This is the quick onboarding page for ARM64 3DNow/3DNowExt dynarec status in this repo.

## Branch Purpose (Current Working Branch)

Branch: `3dnow-arm64-ndr`

Purpose of this branch is isolation, not final release shape:
- keep only ARM64 3DNow/3DNowExt bring-up work needed for validation
- exclude unrelated pre-3DNow NDR optimization lanes that existed before `ndr-3dnow-lab` split from `ndr-analysis`
- keep telemetry/bring-up scripts temporarily while validation completes

Planned follow-up after confidence lock:
1. remove temporary telemetry/bring-up-only logging hooks/scripts
2. keep only functional 3DNow/3DNowExt implementation and required minimal docs
3. prepare a fresh clean-port branch (`arm64-3dnow-ndr-release`) for upstreaming workflow

Current state:
- 3DNow base + 3DNowExt coverage in `3DNOWCOV` is validated.
- Latest Ext validation profile result: `pass=24 fail=0 skip=0`, hash `28aeb9ef`.
- Latest full telemetry checkpoints show no fallback:
  - `s03g-ext-pswapd`: `DYNAREC_3DNOW_SUMMARY tag=final total=48 recompiled=48 fallback=0`
  - `s03h-game-3dnow-soak-01`: `DYNAREC_3DNOW_SUMMARY tag=final total=4427 recompiled=4427 fallback=0`

## Where To Read Next

1. `docs/arm64-3dnow-opcode-coverage-tracker.md`
- Source of truth for per-opcode status and run history.
2. `docs/arm64-3dnow-authoritative-notes.md`
- Canonical opcode/CPUID references and implementation rules.
3. `docs/arm64-3dnow-bringup-plan.md`
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

Interpretation:
- `Z=0`: every executed 3DNow/3DNowExt op in that run used dynarec lowering.
- `Z>0`: some executed ops still fell back.

## Scope Guardrails

- ARM64-only behavior changes for this bring-up.
- Keep x86-64 behavior unchanged.
- Prefer real dynarec lowering first (no helper substitution for new opcode paths).
