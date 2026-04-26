# ARM64 3DNow Findings and Emu-Speed Logging Port Plan

Date: 2026-04-25
Current branch: `ndr-3dnow-lab`
Reference branch: `ndr-analysis`

## Executive Summary

- ARM64 new dynarec 3DNow decode mapping is active for the intended opcode set, including 3DNowExt opcodes in this branch state (`src/codegen_new/codegen_ops.c`, `recomp_opcodes_3DNOW` table).
- Runtime evidence shows stable 3DNow dynarec dispatch with `fallback=0` in recent soak runs (example: `DYNAREC_3DNOW_SUMMARY tag=final total=4427 recompiled=4427 fallback=0`).
- Main optimization headroom is in a few ARM64 lowerers with high arithmetic/shuffle cost:
  - reciprocal and inverse-sqrt paths (`PFRCP`, `PFRSQRT`)
  - pairwise negative-accumulate and swap paths (`PFNACC`, `PFPNACC`, `PSWAPD`)
  - word-to-float pack/convert path (`PI2FW`)
- Requested add-on (from `ndr-analysis`) is clear and isolated: restore logging of the top-right emulation speed sample (`EMU_SPEED_SAMPLE`) and add host-side parser support script.

## Findings (Code and Telemetry)

### 1) 3DNow dynarec routing is enabled and in active use

- 3DNow opcode mapping table is populated for ARM64 in:
  - `src/codegen_new/codegen_ops.c` (`recomp_opcodes_3DNOW`, ARM/ARM64 section)
- 3DNow dispatch and fallback accounting path is in:
  - `src/codegen_new/codegen.c` (`opcode_3dnow` decode, `saw_3dnow_dispatch`, recompiled/fallback counters)

Evidence snapshots:
- `docs/perf-artifacts/arm64-dynarec/2026-04-23_19-30-46-Windows 98 Gaming PC-s03h-game-3dnow-soak-01/86box.log`
  - `DYNAREC_3DNOW_SUMMARY tag=final total=4427 recompiled=4427 fallback=0`
- `docs/perf-artifacts/arm64-dynarec/2026-04-22_22-41-23-Windows 98 Gaming PC-3dnowcov-r1/86box.log`
  - high-volume opcode activity includes `opae`, `opb7`, `opbf` and other mapped ops.

### 2) Highest-payoff ARM64 lowering hotspots

Current ARM64 implementations:
- `PFRCP`: scalar one/divide + duplicate lane (`src/codegen_new/codegen_backend_arm64_uops.c`)
- `PFRSQRT`: scalar sqrt + divide + duplicate lane (`src/codegen_new/codegen_backend_arm64_uops.c`)
- `PFNACC` and `PFPNACC`: multi-step `DUP/FSUB|FADD/ZIP` sequences
- `PSWAPD`: snapshot + `DUP/DUP/INS`
- `PI2FW`: scalar register extraction and lane rebuild before `SCVTF`

Implication:
- Reciprocal/inv-sqrt paths are likely the largest single-op cycle consumers.
- Shuffle-heavy paths are good second-wave optimization targets by reducing instruction count and temporary register pressure.

### 3) MMX entry path is function-call based on ARM64 front-end

- ARM64-side `uop_MMX_ENTER` macro currently emits helper-call uops (`codegen_mmx_enter`) when first entering MMX state in a block (`src/codegen_new/codegen_ir_defs.h`).
- A backend lowering for `codegen_MMX_ENTER` exists in `src/codegen_new/codegen_backend_arm64_uops.c`.

Implication:
- There is potential overhead in call-based MMX entry on short/churn-heavy blocks.
- This is lower priority than reciprocal/sqrt hot ops, but worth a controlled experiment.

### 4) Emu-speed sample logging is currently missing in this branch

Current `ndr-3dnow-lab`:
- `src/86box.c` updates title text with speed percent but does not emit `EMU_SPEED_SAMPLE` lines.
- `scripts/dynarec/analyze-emu-speed-log.sh` is not present.

`ndr-analysis` source of truth for this feature:
- commit: `24d7ea14f8f6e599d101ccd28d6a2598e8b0dc7c`
- file changes in that commit:
  - `src/86box.c` (adds `speed_percent` local and `pclog("EMU_SPEED_SAMPLE ...")`)
  - `scripts/dynarec/analyze-emu-speed-log.sh` (new parser script)
  - docs updates (not required for functional port)

## Port Scope for Emu-Speed Logging

### Minimum required port

1. Port the `src/86box.c` hunk from `24d7ea14f`:
   - compute `speed_percent` once per title update
   - keep existing title formatting behavior
   - log:
     - `EMU_SPEED_SAMPLE percent=%d fps_raw=%d force_10ms=%d`

2. Add new script from `24d7ea14f`:
   - `scripts/dynarec/analyze-emu-speed-log.sh`
   - keep executable bit
   - output `EMU_SPEED_SUMMARY ...` stats

### Explicit non-goals for this port

- Do not import the large docs refresh from `24d7ea14f`.
- Do not import unrelated churn-rename script changes from `ndr-analysis` unless explicitly requested.

## Execution Plan

### Phase A: Logging Port (Requested)

1. Bring over `src/86box.c` logging hunk from `ndr-analysis`.
2. Add `scripts/dynarec/analyze-emu-speed-log.sh`.
3. Sanity validation:
   - Build succeeds.
   - Launch run produces `EMU_SPEED_SAMPLE` lines in `86box.log`.
   - Parser returns `EMU_SPEED_SUMMARY samples=...`.

Acceptance criteria:
- At least one representative run generates non-zero sample count.
- No behavior regression in window-title update path.

Rollback trigger:
- Build failure or runtime instability attributable to title-update/logging path.

### Phase B: 3DNow Optimization Wave 1 (High ROI)

1. `PFRCP`/`PFRSQRT` optimization experiment:
   - add ARM64 estimate/refinement instruction support path
   - keep strict fallback path for safety if needed
2. Measure against baseline:
   - emu-speed summary (`EMU_SPEED_SUMMARY`)
   - existing dynarec/3DNow summaries (`DYNAREC_3DNOW_SUMMARY`, churn stats)
3. Keep only if both are true:
   - correctness unchanged (`3DNOWCOV` and workload markers remain stable)
   - speed/dip profile improves in targeted workloads

### Phase C: 3DNow Optimization Wave 2 (Shuffle/Pack Reduction)

Targets:
- `PFNACC`
- `PFPNACC`
- `PSWAPD`
- `PI2FW`

Method:
- reduce instruction count and temp register churn using tighter lane-manipulation sequences.

Gates:
- no change in deterministic correctness markers
- maintain `fallback=0` behavior where currently achieved

### Phase D: Optional MMX-enter overhead experiment

Goal:
- evaluate impact of reducing helper-call overhead around MMX entry path.

Constraint:
- do not merge unless measurable gain appears in targeted workloads and correctness remains unchanged.

## Validation Matrix

- Correctness:
  - `3DNOWCOV` pass/fail/hash unchanged
  - `WL-05` markers unchanged
  - no new aborts/crashes in soak run
- Dispatch health:
  - `DYNAREC_3DNOW_SUMMARY` keeps high recompiled ratio and `fallback=0` in known-good scenarios
- Performance:
  - emu-speed summary improves or remains neutral with fewer severe dips (`dips_lt95`, `dips_lt90`)
- Regression safety:
  - compare against latest baseline epoch artifacts before accepting changes

## Suggested Implementation Order

1. Port emu-speed logging (`src/86box.c` + parser script).
2. Re-establish baseline artifact with `EMU_SPEED_SUMMARY`.
3. Optimize `PFRCP`/`PFRSQRT`.
4. Optimize shuffle-heavy ops (`PFNACC`, `PFPNACC`, `PSWAPD`, `PI2FW`).
5. Evaluate optional MMX-enter path work only after steps 1-4.

## Baseline Prep Update (2026-04-25 late)

- Emu-speed sample logging is active and parser support is present.
- Phase marker logging is active with run-start auto marker and manual markers.
- Hotkey capture was stabilized with a single Qt shortcut path, and the effective marker key is now `1` for baseline capture reliability.
- Verified runtime marker emission includes:
  - `PERF_PHASE_MARK seq=1 ... source=qt_shortcut reason=manual`

### Baseline capture plan to execute next

1. Capture 3 runs with fixed order:
   - `Q3 -> 3DMark99 -> WL-05`
2. Press `1` at each phase boundary and once at workload end.
3. For each run collect:
   - `EMU_SPEED_SUMMARY`
   - `EMU_PHASE_MARKERS`
   - `EMU_PHASE_SPEED_SUMMARY` per phase
   - `S03_LOG` churn ratio (`ratio_promote_no_immediates_per_dirty_hit`)
4. Record host-noise notes per run (`clean` vs `noisy`) and reject noise-tainted outliers.
5. Exclude any run with invalid marker sequencing.
6. Establish baseline from averages/dispersion across `3 clean valid runs` before starting new optimization code.
7. Write and freeze baseline lock artifact with explicit gate line:
   - `BASELINE LOCKED: use these averages as comparison gate before any new code.`
