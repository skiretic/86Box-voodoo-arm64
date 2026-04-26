# ARM64 Dynarec Baseline Protocol (Single Hotkey Phase Markers)

Date: 2026-04-25

## Goal
Establish a repeatable baseline workflow with one operator hotkey and zero per-phase custom controls.

## Locked Workflow
- Workload order is fixed: `Q3 -> 3DMark99 -> WL-05`.
- Single hotkey for transitions: `1`.
- Logging starts automatically at run launch.
- Marker sequence semantics:
  - `seq=0` is auto-emitted at run start.
  - press 1 (`seq=1`): transition to `3DMark99`.
  - press 2 (`seq=2`): transition to `WL-05`.
  - press 3 (`seq=3`): WL-05 finished / run complete.

## Final Implementation Status
- Accelerator key `phase_marker` exists in core keybind list.
- Current effective marker bind is forced to `1` in Qt shortcut setup for reliability.
- Marker emits into main log as:
  - `PERF_PHASE_MARK seq=<n> phase=<name> source=<source> reason=<reason> host_ms=<ticks> delta_ms=<ms>`
- Auto run-start marker is emitted at init (`seq=0`).
- Manual marker function has debounce:
  - window `500 ms`
  - duplicate rapid presses emit `PERF_PHASE_MARK_IGNORED`
- Qt integration is now single-path:
  - app-level `QShortcut` with `Qt::ApplicationShortcut`
  - activation source logged as `source=qt_shortcut`

## Parser Output Contract
`scripts/dynarec/analyze-emu-speed-log.sh <86box.log>` emits:
- `EMU_SPEED_SUMMARY ...` (whole run)
- `EMU_PHASE_MARKERS ...` (marker validity/state)
- `EMU_PHASE_SPEED_SUMMARY phase=q3 ...`
- `EMU_PHASE_SPEED_SUMMARY phase=3dmark99 ...`
- `EMU_PHASE_SPEED_SUMMARY phase=wl05 ...`
- optional `EMU_PHASE_SPEED_SUMMARY phase=post_wl05 ...`
- `DYNAREC_3DNOW_OPSUMMARY_PARSED ...` (latest 3DNow opcode-family snapshot)
- `DYNAREC_3DNOW_ARITH_BREAKDOWN ...` (arith subgroup counters: `pfadd/pfsub/pfsubr/pfmul/pfacc/pavgusb`)

## Baseline Lock Status
- Baseline capture is complete and locked.
- Locked artifact:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md`
- Accepted runs:
  - `2026-04-25_21-41-24-Windows 98 Gaming PC-baseline-prelock-r1`
  - `2026-04-25_21-51-14-Windows 98 Gaming PC-baseline-prelock-r2`
  - `2026-04-25_22-00-12-Windows 98 Gaming PC-baseline-prelock-r3`
- Rejected replacements:
  - `2026-04-25_21-37-57-Windows 98 Gaming PC-baseline-prelock-r1`
  - `2026-04-25_21-40-40-Windows 98 Gaming PC-baseline-prelock-r1`
- Locked aggregate:
  - whole-run avg mean `99.564333`
  - whole-run p50 mean `100`
  - whole-run p95 mean `101`
  - whole-run p99 mean `102`
  - churn mean `0.001144`
- Continue to use the locked artifact as the comparison gate before any new code.

## Host Noise Control (Required)
- Before each run, record host-noise notes:
  - active heavy apps/processes
  - any known background load (indexing, builds, browser/video, etc.)
- Mark each run as `clean` or `noisy`.
- If a run is clearly noise-affected (outlier with matching host-load evidence), mark it tainted and replace it.
- Baseline lock requires `3 clean valid runs`, not just 3 valid runs.
- Final baseline artifact must include:
  - run-by-run noise notes
  - rejected/replaced run list with reason
  - final clean run IDs used for aggregate lock.

## Acceptance for Baseline Set
- Exactly 3 clean valid runs.
- No marker-sequence failures.
- No correctness/hash anomalies in workload checks.
- Report includes both speed and churn metrics.
- Baseline lock line is present:
  - `BASELINE LOCKED: use these averages as comparison gate before any new code.`
