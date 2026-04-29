# ARM64 3DNow Continuation Plan

## Goal

Close x86-64 dynarec 3DNow/3DNowExt gaps to reach parity with the already-validated ARM64 mapped surface, while preserving correctness and stability.

## Current Baseline (Known Good)

- Interpreter base table (`OP_TABLE(3DNOW)`): 19 opcodes
- Interpreter Ext table (`OP_TABLE(3DNOWE)`): 24 opcodes
- ARM64 dynarec mapped 3DNow/Ext opcodes: 24
- x86-host dynarec mapped 3DNow/Ext opcodes (in this tree): 16
- ARM64 currently covers all x86-host mapped ops plus 8 additional ops.
- On an Ext-capable guest profile, ARM64 dynarec is at parity with interpreter imm8 coverage (24/24).
- Existing validated-heavy opcodes include:
  - `PF2IW`, `PI2FW`
  - `PFNACC`, `PFPNACC`, `PFACC`
  - `PMULHRW`, `PSWAPD`, `PAVGUSB`

## Hit-vs-x86-64 Matrix

Legend:
- `Y` = mapped in that table
- `N` = not mapped in that table

| Opcode | Mnemonic | Hit In Workload | Interpreter Base (`3DNOW`) | Interpreter Ext (`3DNOWE`) | ARM64 Dynarec | x86-64 Dynarec |
|---|---|---:|---:|---:|
| `0x0c` | `PI2FW`   | Y | N | Y | Y | N |
| `0x1c` | `PF2IW`   | Y | N | Y | Y | N |
| `0x8a` | `PFNACC`  | Y | N | Y | Y | N |
| `0x8e` | `PFPNACC` | Y | N | Y | Y | N |
| `0xae` | `PFACC`   | Y | Y | Y | Y | N |
| `0xb7` | `PMULHRW` | Y | Y | Y | Y | N |
| `0xbb` | `PSWAPD`  | Y | N | Y | Y | N |
| `0xbf` | `PAVGUSB` | Y | Y | Y | Y | N |
| `0x0d` | `PI2FD`   | Y | Y | Y | Y | Y |
| `0x1d` | `PF2ID`   | Y | Y | Y | Y | Y |
| `0x90` | `PFCMPGE` | Y | Y | Y | Y | Y |
| `0x94` | `PFMIN`   | Y | Y | Y | Y | Y |
| `0x96` | `PFRCP`   | Y | Y | Y | Y | Y |
| `0x97` | `PFRSQRT` | Y | Y | Y | Y | Y |
| `0x9a` | `PFSUB`   | Y | Y | Y | Y | Y |
| `0x9e` | `PFADD`   | Y | Y | Y | Y | Y |
| `0xa0` | `PFCMPGT` | Y | Y | Y | Y | Y |
| `0xa4` | `PFMAX`   | Y | Y | Y | Y | Y |
| `0xa6` | `PFRCPIT` | Y | Y | Y | Y | Y |
| `0xa7` | `PFRSQIT1`| Y | Y | Y | Y | Y |
| `0xaa` | `PFSUBR`  | Y | Y | Y | Y | Y |
| `0xb0` | `PFCMPEQ` | Y | Y | Y | Y | Y |
| `0xb4` | `PFMUL`   | Y | Y | Y | Y | Y |
| `0xb6` | `PFRCPIT` | Y | Y | Y | Y | Y |

Summary:
- Overlap with x86-64 mapped set: 16 opcodes
- ARM64-only mapped extras (also hit): 8 opcodes

## Active x86-64 Parity Backlog

Current x86-64 dynarec imm8 gaps relative to ARM64 (from matrix above):
- `0x0c` `PI2FW`
- `0x1c` `PF2IW`
- `0x8a` `PFNACC`
- `0x8e` `PFPNACC`
- `0xae` `PFACC`
- `0xb7` `PMULHRW`
- `0xbb` `PSWAPD`
- `0xbf` `PAVGUSB`

Companion non-imm8 gaps (already validated on ARM64, fallback on x86-64):
- `0F 0D /r` `PREFETCH/PREFETCHW`
- `0F 0E` `FEMMS`

Completion condition for this track:
- x86-64 dynarec column matches ARM64 dynarec coverage for the implemented 86Box 3DNow surface in this plan.

## Coverage Model Clarification

- There are two interpreter opcode tables:
  - Base-only: `OP_TABLE(3DNOW)` (19)
  - Ext-capable: `OP_TABLE(3DNOWE)` (24)
- Runtime selects table by guest CPU feature bit (`3DNowExt` present or not).
- Current `win98-3dnowcov` opcase set (24) matches the Ext table.
- Companion non-imm8 opcodes (`PREFETCH`/`FEMMS`) are tracked separately from imm8 table counts.

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

## Validation Protocol for New Opcodes

For each newly added opcode, require all of the following before marking validated:

1. Interpreter semantics check:
   - Confirm behavior against expected opcode semantics and nearby established patterns.
2. Differential path check:
   - Verify interpreter and dynarec produce matching output for deterministic vectors.
3. Deterministic regression check:
   - Verify per-op and total hash stability on repeated runs with fixed seeds/inputs.
4. Workload sanity check:
   - Confirm no functional regressions in normal workload usage.
5. Batch isolation:
   - Land in small slices (1-3 opcodes) so any mismatch is attributable and quickly reversible.

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

## Immediate Next Actions (x86-64 parity track)

1. Reconfirm current x86-64 gap list from `recomp_opcodes_3DNOW` and `recomp_opcodes_0f{,_no_mmx}`.
2. Implement one small x86-64 gap slice (1-3 opcodes).
3. Run validation gate:
   - `3DNOWCOV` pass/hash check
   - normal workload sanity (`Q3 -> 3DMark99 -> MRUNALL`)
   - host log checks (`DYNAREC_3DNOW_SUMMARY`, fallback behavior, no errors)
4. Update matrix and tracker immediately after each landed slice.
