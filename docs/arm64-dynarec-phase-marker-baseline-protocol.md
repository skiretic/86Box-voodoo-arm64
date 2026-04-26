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

## Baseline Capture Plan (Next Session)
1. Create baseline tag and run 1:
- launch telemetry run
- execute workload in order `Q3 -> 3DMark99 -> WL-05`
- press `1` at each boundary and once at end
2. Repeat for run 2 and run 3 with the same order and same marker press pattern.
3. For each run, parse:
- `scripts/dynarec/analyze-emu-speed-log.sh <86box.log>`
- `scripts/dynarec/analyze-s03a-log.sh <86box.log>`
4. Validate each run before inclusion:
- `start_seen=1`
- `max_seq >= 3`
- `valid_for_q3_3dmark_wl05=1`
5. Compute baseline aggregate from the 3 valid runs:
- whole-run: avg, p50, p95, p99, dip counts
- per-phase: avg/p50/p95/p99 for `q3`, `3dmark99`, `wl05`
- churn: `ratio_promote_no_immediates_per_dirty_hit`

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
