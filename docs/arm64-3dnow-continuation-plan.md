# ARM64 3DNow Continuation Plan

## Goal

Expand ARM64 dynarec 3DNow/3DNowExt coverage beyond the current validated subset, while preserving correctness and performance.

## Current Baseline (Known Good)

- ARM64 mapped 3DNow opcodes: 24
- x86-host mapped 3DNow opcodes (in this tree): 16
- ARM64 currently covers all x86-host mapped ops plus 8 additional ops.
- Existing validated-heavy opcodes include:
  - `PF2IW`, `PI2FW`
  - `PFNACC`, `PFPNACC`, `PFACC`
  - `PMULHRW`, `PSWAPD`, `PAVGUSB`

## Hit-vs-x86-64 Matrix

Legend:
- `Y` = mapped in that table
- `N` = not mapped in that table

| Opcode | Mnemonic | Hit In Workload | ARM64 Map | x86-64 Map |
|---|---|---:|---:|---:|
| `0x0c` | `PI2FW`   | Y | Y | N |
| `0x1c` | `PF2IW`   | Y | Y | N |
| `0x8a` | `PFNACC`  | Y | Y | N |
| `0x8e` | `PFPNACC` | Y | Y | N |
| `0xae` | `PFACC`   | Y | Y | N |
| `0xb7` | `PMULHRW` | Y | Y | N |
| `0xbb` | `PSWAPD`  | Y | Y | N |
| `0xbf` | `PAVGUSB` | Y | Y | N |
| `0x0d` | `PI2FD`   | Y | Y | Y |
| `0x1d` | `PF2ID`   | Y | Y | Y |
| `0x90` | `PFCMPGE` | Y | Y | Y |
| `0x94` | `PFMIN`   | Y | Y | Y |
| `0x96` | `PFRCP`   | Y | Y | Y |
| `0x97` | `PFRSQRT` | Y | Y | Y |
| `0x9a` | `PFSUB`   | Y | Y | Y |
| `0x9e` | `PFADD`   | Y | Y | Y |
| `0xa0` | `PFCMPGT` | Y | Y | Y |
| `0xa4` | `PFMAX`   | Y | Y | Y |
| `0xa6` | `PFRCPIT` | Y | Y | Y |
| `0xa7` | `PFRSQIT1`| Y | Y | Y |
| `0xaa` | `PFSUBR`  | Y | Y | Y |
| `0xb0` | `PFCMPEQ` | Y | Y | Y |
| `0xb4` | `PFMUL`   | Y | Y | Y |
| `0xb6` | `PFRCPIT` | Y | Y | Y |

Summary:
- Overlap with x86-64 mapped set: 16 opcodes
- ARM64-only mapped extras (also hit): 8 opcodes

## Guardrails

- Stay on `ndr-pacing-lab`.
- Keep logging minimal; add temporary diagnostics only when needed and remove or gate them.
- No large batch landings without a build + runtime sanity gate.
- Preserve current behavior for already-validated opcode paths.

## Phase 1: Coverage Ground Truth Refresh

1. Re-run current opcode coverage capture on normal workload + targeted scenes.
2. Produce a fresh hit histogram (opcode + hit count + percent).
3. Split remaining unmapped opcodes into:
   - `hot`: observed and frequent
   - `warm`: observed and low frequency
   - `cold`: unobserved

Exit criteria:
- We have a ranked target list for next implementation batches.

## Phase 2: Mapping/Lowering Audit for Unmapped Ops

1. For each `hot`/`warm` unmapped opcode:
   - verify decode path exists in `x86_ops_3dnow.h`
   - verify `rop*` helper exists/behavior
   - verify lowerer/uop support needs (new vs reusable)
2. Build an opcode tracker table:
   - opcode
   - mnemonic
   - decode status
   - rop status
   - lowering status
   - planned owner batch

Exit criteria:
- Every target opcode is classified as `ready`, `needs lowerer`, or `blocked`.

## Phase 3: Implementation Batches (Hot First)

1. Batch size: 2-5 opcodes per change set.
2. Per batch:
   - add mapping in `recomp_opcodes_3DNOW`
   - implement/enable lowering path
   - add focused correctness checks where practical
3. Gate each batch with:
   - clean build
   - launch + workload sanity
   - quick perf regression spot check

Exit criteria:
- All `hot` opcodes implemented and stable.

## Phase 4: Correctness and Stability Hardening

1. Run full multi-run workload validation (same standard host-check/run cadence used on branch).
2. Compare against current locked baseline ranges.
3. Investigate and fix any regressions before expanding further.

Exit criteria:
- No functional regressions; performance remains within accepted ranges.

## Phase 5: Warm/Cold Expansion

1. Implement `warm` opcodes in small batches.
2. Defer `cold` opcodes unless:
   - low implementation risk, or
   - required for known titles/workloads.
3. Keep tracker updated after each landed batch.

Exit criteria:
- Practical coverage target achieved for real workloads.

## Deliverables

- Updated opcode coverage tracker in `docs/arm64-3dnow-opcode-coverage-tracker.md`.
- Incremental implementation commits on `ndr-pacing-lab`.
- Validation notes added under `docs/perf-artifacts/arm64-dynarec/` as needed.

## Immediate Next Actions

1. Refresh coverage capture and regenerate top-opcode ranking.
2. Draft the unmapped-opcode audit table.
3. Start first hot-opcode implementation batch.
