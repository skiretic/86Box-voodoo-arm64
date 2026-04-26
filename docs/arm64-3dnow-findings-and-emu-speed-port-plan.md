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

Status: completed.

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

### Phase B: Baseline Capture and Lock

Status: completed.

1. Capture exactly 3 clean valid runs with fixed order:
   - `Q3 -> 3DMark99 -> WL-05`
2. Press `1` at each phase boundary and once at run end.
3. For each run collect:
   - `EMU_SPEED_SUMMARY`
   - `EMU_PHASE_MARKERS`
   - `EMU_PHASE_SPEED_SUMMARY` per phase
   - `S03_LOG` churn ratio (`ratio_promote_no_immediates_per_dirty_hit`)
4. Reject any run with invalid marker sequencing or noise-tainted host evidence.
5. Lock the baseline artifact and freeze the comparison gate.

Locked artifact:
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md`

### Phase C: 3DNow Optimization Wave 1 (High ROI)

1. `PFRCP`/`PFRSQRT` optimization experiment:
   - add ARM64 estimate/refinement instruction support path
   - keep strict fallback path for safety if needed
2. Measure against baseline:
   - emu-speed summary (`EMU_SPEED_SUMMARY`)
   - existing dynarec/3DNow summaries (`DYNAREC_3DNOW_SUMMARY`, churn stats)
3. Keep only if both are true:
   - correctness unchanged (`3DNOWCOV` and workload markers remain stable)
   - speed/dip profile improves in targeted workloads

### Phase D: 3DNow Optimization Wave 2 (Shuffle/Pack Reduction)

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

### Phase E: Optional MMX-enter overhead experiment

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
2. Capture and lock the 3-run baseline.
3. Optimize `PFRCP`/`PFRSQRT`.
4. Optimize shuffle-heavy ops (`PFNACC`, `PFPNACC`, `PSWAPD`, `PI2FW`).
5. Evaluate optional MMX-enter path work only after steps 1-4.

## Baseline Lock Update (2026-04-25)

- Emu-speed sample logging is active and parser support is present.
- Phase marker logging is active with run-start auto marker and manual markers.
- Hotkey capture is stabilized with a single Qt shortcut path, and the effective marker key is `1` for baseline capture reliability.
- Verified runtime marker emission includes:
  - `PERF_PHASE_MARK seq=1 ... source=qt_shortcut reason=manual`

### Current Logging-On Baseline Artifact

- This is the current artifact for runs with `86BOX_3DNOW_COV_STATS=1` enabled.
- Run:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2`
- Logfile:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2/86box.log`
- Marker validation:
  - `start_seen=1`
  - `max_seq=3`
  - `valid_for_q3_3dmark_wl05=1`
- Whole-run speed:
  - `avg=99.610`
  - `p50=100`
  - `p95=101`
  - `p99=103`
- 3DNow family summary:
  - `DYNAREC_3DNOW_OPSUMMARY total=4010 recip=189 shuffle_pack=0 arith=3527 cmp=186 conv=108 other=0 pfrcp=75 pfrsqrt=20 pfnacc=0 pfpnacc=0 pswapd=0 pi2fw=0`
  - format now also includes arith subgroup fields:
    - `pfadd=... pfsub=... pfsubr=... pfmul=... pfacc=... pavgusb=...`
- Interpretation:
  - this run is the baseline artifact for logging-on family-mix analysis
  - reciprocal/sqrt is active in this workload
  - shuffle/pack family is cold in this workload

### Locked Baseline Artifact

- Baseline lock file:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md`
- Gate line in artifact:
  - `BASELINE LOCKED: use these averages as comparison gate before any new code.`
- Baseline capture was done after logging verification in the follow-on session, not during the initial logging-port validation pass.
- The locked 3-run artifact remains the pre-logging comparison gate; the logging-on artifact above is the current op-family baseline.
- Accepted runs:
  - `2026-04-25_21-41-24-Windows 98 Gaming PC-baseline-prelock-r1`
  - `2026-04-25_21-51-14-Windows 98 Gaming PC-baseline-prelock-r2`
  - `2026-04-25_22-00-12-Windows 98 Gaming PC-baseline-prelock-r3`
- Rejected replacements:
  - `2026-04-25_21-37-57-Windows 98 Gaming PC-baseline-prelock-r1`
  - `2026-04-25_21-40-40-Windows 98 Gaming PC-baseline-prelock-r1`

### Locked Comparison Values

- Whole-run mean avg:
  - `99.564333`
- Whole-run mean p50:
  - `100`
- Whole-run mean p95:
  - `101`
- Whole-run mean p99:
  - `102`
- Whole-run mean churn ratio:
  - `0.001144`

### Baseline Capture Notes

- The accepted 3-run set was captured with the fixed order:
  - `Q3 -> 3DMark99 -> WL-05`
- Marker validation passed on all 3 accepted runs:
  - `start_seen=1`
  - `max_seq=3`
  - `valid_for_q3_3dmark_wl05=1`
- Host-noise notes were clean for all 3 accepted runs.

## Phase-1 Stability Slice Lock (2026-04-26)

### Change Locked In

- File:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/src/codegen_new/codegen_ops_3dnow.c`
- Change:
  - In `ropPFRCPIT`, skip `uop_MOV` when `dst==src` (no-op self move elision).
- Scope:
  - ARM64 dynarec 3DNow front-end only.
  - No math semantic change.

### Lock Run Artifact

- Run:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_10-29-33-Windows 98 Gaming PC-3dnow-recip-safe-pfrcpit-r2`
- Log:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_10-29-33-Windows 98 Gaming PC-3dnow-recip-safe-pfrcpit-r2/86box.log`
- Marker gate:
  - `start_seen=1`
  - `max_seq=3`
  - `valid_for_q3_3dmark_wl05=1`
- 3DNow dispatch:
  - `DYNAREC_3DNOW_SUMMARY tag=final total=4256 recompiled=4256 fallback=0`

### Delta Summary (vs Logging-On Baseline)

Comparison baseline:
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2/86box.log`

Observed:
- whole-run `avg`: `99.605` vs `99.610` (flat / `-0.005`)
- whole-run `p95`: `101` vs `101` (flat)
- whole-run `p99`: `103` vs `103` (flat)
- `dips_lt100`: `87` vs `91` (improved)
- `dips_lt95`: `21` vs `21` (flat)
- `dips_lt90`: `4` vs `7` (improved)

Decision:
- Treat as a stability-positive, correctness-safe micro-optimization and keep it.

### Next Target

- Continue 3DNow optimization phase 1 with exact-semantics-only work.
- Arith subgroup visibility slice is now landed in `DYNAREC_3DNOW_OPSUMMARY`:
  - `pfadd`
  - `pfsub`
  - `pfsubr`
  - `pfmul`
  - `pfacc`
  - `pavgusb`
- Next execution step (pending run):
  - capture one clean telemetry run and pick the hottest arith opcode for backend instruction-count reduction.

## Arith Hot-Op Selection Run (2026-04-26)

### Selection Artifact

- Run:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_12-13-28-Windows 98 Gaming PC-3dnow-telemetry-r3`
- Log:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_12-13-28-Windows 98 Gaming PC-3dnow-telemetry-r3/86box.log`

### Gate Status

- Marker validation:
  - `start_seen=1`
  - `max_seq=3`
  - `valid_for_q3_3dmark_wl05=1`
- 3DNow dispatch:
  - `DYNAREC_3DNOW_SUMMARY tag=final total=4311 recompiled=4311 fallback=0`
- Guest correctness:
  - microstress hashes matched expected lock values (quick/normal/smc).

### Speed/Consistency Read (vs logging-on baseline)

Comparison baseline:
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2/86box.log`

Observed:
- whole-run `avg`: `99.542` vs `99.610` (near-flat / `-0.068`)
- whole-run `p99`: `102` vs `103` (improved)
- `dips_lt95`: `13` vs `21` (improved)
- `dips_lt90`: `1` vs `7` (improved)

### Arith Breakdown (new parser output)

- `pfmul=1982` (hottest)
- `pfadd=1095`
- `pfsub=455`
- `pfacc=167`
- `pfsubr=104`
- `pavgusb=0`

Decision:
- choose `PFMUL` as first arith hot-op target.

## Recip Alias-Safe Lock (2026-04-26)

### Change Locked In

- File:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/src/codegen_new/codegen_backend_arm64_uops.c`
- Change:
  - keep exact-semantics `PFRCP` lowerer
  - when `dst==src`, use temp-one path to avoid source clobber alias hazard
  - keep trimmed temp path for non-alias case

### Lock Run Artifact

- Run:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_12-40-10-Windows 98 Gaming PC-3dnow-pfrcp-aliasfix-realcheck-r2`
- Log:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_12-40-10-Windows 98 Gaming PC-3dnow-pfrcp-aliasfix-realcheck-r2/86box.log`
- Marker gate:
  - `start_seen=1`
  - `max_seq=3`
  - `valid_for_q3_3dmark_wl05=1`
- 3DNow dispatch:
  - `DYNAREC_3DNOW_SUMMARY tag=final total=3827 recompiled=3827 fallback=0`
- Guest correctness:
  - `3DNOWCOV_TOTAL hash=28aeb9ef` matched
  - row hashes matched expected lock set

### Delta Summary (vs Logging-On Baseline)

Comparison baseline:
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_05-49-33-Windows 98 Gaming PC-3dnow-opcount-r2/86box.log`

Observed:
- whole-run `avg`: `99.671` vs `99.610` (`+0.061`)
- whole-run `p95`: `101` vs `101` (flat)
- whole-run `p99`: `102` vs `103` (improved)
- `dips_lt100`: `86` vs `91` (improved)
- `dips_lt95`: `17` vs `21` (improved)
- `dips_lt90`: `3` vs `7` (improved)
- churn `ratio_promote_no_immediates_per_dirty_hit`: `0.001180` vs `0.001174` (tiny regression, accepted)

Decision:
- accept as phase-1 stability/correctness win
- keep locked pre-logging 3-run baseline unchanged as gate

## Recip Refine Telemetry Refresh (2026-04-26)

### Tooling Update

- Files:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/src/codegen_new/codegen_ops_3dnow.c`
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/scripts/dynarec/analyze-emu-speed-log.sh`
- Change:
  - split reciprocal-refine counters into explicit fields:
    - `pfrcpit1` (`a6`)
    - `pfrsqit1` (`a7`)
    - `pfrcpit2` (`b6`)
  - parser now emits:
    - `DYNAREC_3DNOW_RECIP_BREAKDOWN ...`

### Telemetry Run Artifact

- Run:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_13-20-21-Windows 98 Gaming PC-3dnow-recip-refine-telemetry-r1`
- Log:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_13-20-21-Windows 98 Gaming PC-3dnow-recip-refine-telemetry-r1/86box.log`
- Marker gate:
  - `start_seen=1`
  - `max_seq=3`
  - `valid_for_q3_3dmark_wl05=1`
- 3DNow dispatch:
  - `DYNAREC_3DNOW_SUMMARY tag=final total=4276 recompiled=4276 fallback=0`

### Recip Breakdown

- `pfrcp=84`
- `pfrsqrt=20`
- `pfrcpit1=56`
- `pfrsqit1=0`
- `pfrcpit2=56`

Interpretation:
- `PFRCP` is still the hottest reciprocal op.
- refine ops are active (`a6`/`b6`) and worth tracking.
- `a7` is cold in this workload.

## Next Target (Phase-1, Retargeted)

- Prior `PFMUL` target is dropped for now:
  - backend `PFMUL` lowering is already a single `FMUL.V2S` instruction, so trim headroom is low.
- New target lane:
  - reciprocal refine path (`PFRCPIT1`/`PFRCPIT2`) with exact semantics preserved.
- Execution plan:
  - first optimize shared `ropPFRCPIT` path if real instruction savings exist.
  - if savings are marginal, move to next highest-impact reciprocal path (`PFRCP`) where runtime volume is highest.
- Guardrails:
  - no estimate/refinement semantic changes
  - keep `fallback=0`
  - keep marker-valid full workload flow (`seq=0..3`)
