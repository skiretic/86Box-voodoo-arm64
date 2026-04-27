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
  - while switching from `Q3` to `3DMark99`, press `1` once to emit `seq=1`.
  - while switching from `3DMark99` to `WL-05`, press `1` again to emit `seq=2`.
  - after `WL-05` completes and the guest-side workload sequence is being closed out, press `1` a third time to emit `seq=3`.

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
- Active 266 MHz post-Qt baseline capture is complete and locked.
- Active locked artifact:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-26-postqt-266-3run.md`
- Accepted runs:
  - `2026-04-26_15-50-15-Windows 98 Gaming PC-s03a-telemetry`
  - `2026-04-26_16-00-51-Windows 98 Gaming PC-s03a-telemetry`
  - `2026-04-26_16-12-37-Windows 98 Gaming PC-s03a-telemetry`
- Locked aggregate:
  - whole-run avg mean `99.638000`
  - whole-run p50 mean `100`
  - whole-run p95 mean `101`
  - whole-run p99 mean `102.333333`
  - whole-run raw crossings@100 mean `127.333333`
- Superseded prior lock artifact:
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md`
- Continue to use the active locked artifact as the comparison gate for fixed 266 MHz post-Qt pacing work.

## Historical Pre-Re-Lock Checkpoint (2026-04-26)
- Accepted phase-1 follow-on run (useful historical checkpoint, but superseded as the active gate baseline):
  - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_12-40-10-Windows 98 Gaming PC-3dnow-pfrcp-aliasfix-realcheck-r2`
- Gate validity:
  - `start_seen=1`, `max_seq=3`, `valid_for_q3_3dmark_wl05=1`
- Correctness:
  - `3DNOWCOV_TOTAL hash=28aeb9ef` matched expected
- Speed signal vs logging-on baseline (`3dnow-opcount-r2`):
  - `avg`: `99.671` vs `99.610` (`+0.061`)
  - dips `<95`: `17` vs `21`
  - dips `<90`: `3` vs `7`
- Churn note:
  - `ratio_promote_no_immediates_per_dirty_hit`: `0.001180` vs `0.001174` (tiny increase, accepted for stability gain)
- Runtime pacing companion checkpoint:
  - Qt single-step pacing lane was validated with the same marker workflow and later re-locked on the fixed 266 MHz profile.
  - phase-marker protocol itself did not change; still `seq=0..3` for normal `Q3 -> 3DMark99 -> WL-05` runs.

## Host Noise Control (Required)
- Before each run, record host-noise notes:
  - active heavy apps/processes
  - any known background load (indexing, builds, browser/video, etc.)
- Run this preflight host check before launch and keep a short note with the run:
  - `date`
  - `uptime`
  - `ps -axo pid,ppid,pcpu,pmem,comm | sort -k3 -nr | head -n 20`
  - `memory_pressure | sed -n '1,30p'`
  - `iostat -w 1 -c 2 | tail -n 10`
  - `pmset -g therm | sed -n '1,40p'`
- Preflight guidance:
  - postpone if `pmset -g therm` shows thermal/performance warnings.
  - postpone if swap activity is non-zero or a non-test workload is clearly saturating host CPU.
  - if proceeding with known background load, mark run `noisy` and replace if it becomes an outlier.
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

## WL-05 Hash Logging Clarification
- In this workflow, host `86box.log` is used for:
  - phase markers
  - emu-speed samples
  - dynarec summaries and opcode-family counters
- `WL-05` (`MRUNALL` / `MICROSTRESS`) hash lines are verified from guest-visible output/screenshot.
- Do not expect `MICRO_*` lines in host logs unless separate guest-to-host log plumbing is added.
