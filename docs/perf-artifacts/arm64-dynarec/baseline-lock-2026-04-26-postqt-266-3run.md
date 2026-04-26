# ARM64 Dynarec Baseline Lock

Date: 2026-04-26
Branch: `ndr-3dnow-lab`
Profile: fixed `266666666` guest CPU profile, post-Qt single-step pacing baseline
Commit: `0785427b41e4609b7149d41f15f048bb22154c7e` (`qt: switch main loop to single-step pc_run pacing`)

BASELINE LOCKED: use these averages as comparison gate before any new code on the fixed 266 MHz post-Qt pacing lane.

This lock supersedes `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md` for fixed 266 MHz post-Qt pacing comparisons.

## Clean Runs
- r1: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_15-50-15-Windows 98 Gaming PC-s03a-telemetry`
  - host-noise: accepted for lock; no explicit host-noise note was preserved in `run-metadata.txt`
  - commit: `0785427b41e4609b7149d41f15f048bb22154c7e`
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - whole-run avg=99.571, p50=100, p95=101, p99=102, min=82, max=105, dips_lt100=88, dips_lt95=22, dips_lt90=6
  - crossings_100_boundary_raw=125
- r2: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_16-00-51-Windows 98 Gaming PC-s03a-telemetry`
  - host-noise: accepted for lock; no explicit host-noise note was preserved in `run-metadata.txt`
  - commit: `0785427b41e4609b7149d41f15f048bb22154c7e`
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - whole-run avg=99.671, p50=100, p95=101, p99=102, min=81, max=104, dips_lt100=74, dips_lt95=14, dips_lt90=1
  - crossings_100_boundary_raw=108
- r3: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_16-12-37-Windows 98 Gaming PC-s03a-telemetry`
  - host-noise: accepted for lock; no explicit host-noise note was preserved in `run-metadata.txt`
  - commit: `0785427b41e4609b7149d41f15f048bb22154c7e`
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - whole-run avg=99.672, p50=100, p95=101, p99=103, min=83, max=104, dips_lt100=99, dips_lt95=14, dips_lt90=1
  - crossings_100_boundary_raw=149

## Aggregate
### Whole-Run Speed
- avg: mean=99.638000 variance=0.002245
- p50: mean=100.000000 variance=0.000000
- p95: mean=101.000000 variance=0.000000
- p99: mean=102.333333 variance=0.222222
- min: mean=82.000000 variance=0.666667
- max: mean=104.333333 variance=0.222222
- dips_lt100: mean=87.000000 variance=104.666667
- dips_lt95: mean=16.666667 variance=14.222222
- dips_lt90: mean=2.666667 variance=5.555556
- crossings_100_boundary_raw: mean=127.333333 variance=282.888889

### Old-vs-New Comparison (vs 2026-04-25 Original Lock)
- avg: `99.638000` vs `99.564333` (`+0.073667`)
- p99: `102.333333` vs `102.000000` (`+0.333333`)
- dips_lt95: `16.666667` vs `20.000000` (`-3.333333`)
- dips_lt90: `2.666667` vs `8.000000` (`-5.333333`)
- crossings_100_boundary_raw: `127.333333` vs `120.666667` (`+6.666667`)

## Run Notes
- Accepted runs share branch `ndr-3dnow-lab`, commit `0785427b41e4609b7149d41f15f048bb22154c7e`, and matching telemetry environment settings recorded in `run-metadata.txt`.
- `crossings_100_boundary_raw` is a whole-run raw count backfilled from `EMU_SPEED_SAMPLE percent=<n>` lines using the `100%` boundary; it is not warmup-trimmed.
- These historical run directories do not yet include `wl05-hashes.txt`; baseline adoption here follows the operator-confirmed accepted 266 MHz post-Qt baseline epoch.

## Rejected / Replaced / Excluded
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_15-14-09-Windows 98 Gaming PC-s03a-telemetry/86box.log`: rejected; incomplete marker sequence (`max_seq=1`, `valid_for_q3_3dmark_wl05=0`).
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_15-27-32-Windows 98 Gaming PC-s03a-telemetry/86box.log`: replaced; valid run on prior pacing commit `bc938b8a2618bf170d7364ce2a905cb567a56119`, not the final same-code-state 266 MHz cluster used for this lock.
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_15-39-45-Windows 98 Gaming PC-s03a-telemetry/86box.log`: replaced; valid run on the final same-code-state commit, but superseded by the later three-run cluster (`15:50`, `16:00`, `16:12`) used as the active lock.
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-26_16-35-10-Windows 98 Gaming PC-s03a-telemetry/86box.log`: excluded from this lock; different CPU profile (300 MHz per operator note) and different commit (`ee4d5c5ae34f4803559bb21d46fe4e7f292dcb56`), so not comparable to the fixed 266 MHz gate.

## Final Gate
- clean valid runs used: `3/3`
- rejected/replaced/excluded attempts listed above: `4`
- no marker failures in accepted runs
- current active comparison gate for fixed 266 MHz post-Qt pacing work: this artifact
