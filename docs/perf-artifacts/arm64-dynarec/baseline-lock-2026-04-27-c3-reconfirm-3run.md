# ARM64 Dynarec Baseline Lock (C3 Reconfirm)

Date: 2026-04-27
Branch: `ndr-pacing-lab`
Profile: fixed `266666666` guest CPU profile, post-Qt pacing lane, C3-confirmed code state
Commit: `a8480e98e55f1303849f921d5a09f1fa138f1a13` (pre-C7 working tree state, C3 kept)

BASELINE LOCKED: use these averages as comparison gate before subsequent consistency-track code changes on the fixed 266 MHz post-Qt pacing lane.

This lock supersedes `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-26-postqt-266-3run.md` as the active operator-approved baseline for `ndr-pacing-lab` follow-on work.

## Clean Runs
- r1: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-27_17-00-45-Windows 98 Gaming PC-c3-baseline-reconfirm-r1`
  - host-noise: clean (recorded in `run-metadata.txt`)
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - WL-05 hashes: quick=45db7b65, normal=2520dd5e, smc=b86f22a1, status=OK
  - whole-run avg=99.570, p50=100, p95=101, p99=102, min=81, max=104, dips_lt100=83, dips_lt95=21, dips_lt90=4
  - crossings_100_boundary_raw=104
- r2: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-27_17-13-23-Windows 98 Gaming PC-c3-baseline-reconfirm-r2`
  - host-noise: clean (recorded in `run-metadata.txt`)
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - WL-05 hashes: quick=45db7b65, normal=2520dd5e, smc=b86f22a1, status=OK
  - whole-run avg=99.576, p50=100, p95=101, p99=102, min=82, max=104, dips_lt100=67, dips_lt95=17, dips_lt90=7
  - crossings_100_boundary_raw=63
- r3: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-27_17-24-01-Windows 98 Gaming PC-c3-baseline-reconfirm-r3`
  - host-noise: clean (recorded in `run-metadata.txt`)
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - WL-05 hashes: quick=45db7b65, normal=2520dd5e, smc=b86f22a1, status=OK
  - whole-run avg=99.554, p50=100, p95=101, p99=103, min=81, max=104, dips_lt100=96, dips_lt95=20, dips_lt90=9
  - crossings_100_boundary_raw=125

## Aggregate
### Whole-Run Speed
- samples: mean=510.666667 variance=8.222222
- avg: mean=99.566667 variance=0.000086
- p50: mean=100.000000 variance=0.000000
- p95: mean=101.000000 variance=0.000000
- p99: mean=102.333333 variance=0.222222
- min: mean=81.333333 variance=0.222222
- max: mean=104.000000 variance=0.000000
- dips_lt100: mean=82.000000 variance=140.666667
- dips_lt95: mean=19.333333 variance=2.888889
- dips_lt90: mean=6.666667 variance=4.222222
- crossings_100_boundary_raw: mean=97.333333 variance=662.888889

### Frame-Style Smoothness Notes (Whole-Run)
- mean `|percent-100|`: `0.699480`
- p95 `|percent-100|`: `3.333333`
- `% samples <95`: `3.784185`
- `% samples <90`: `1.306711`
- `% samples >103`: `0.325911`
- `% samples >=105`: `0.000000`
- max contiguous run `<95`: `9.000000` samples
- max contiguous run `<90`: `2.000000` samples
- max contiguous run outside `98..102`: `10.666667` samples

### Comparison vs Superseded 2026-04-26 Post-Qt Lock
- avg: `99.566667` vs `99.638000` (`-0.071333`)
- p99: `102.333333` vs `102.333333` (`+0.000000`)
- dips_lt95: `19.333333` vs `16.666667` (`+2.666666`)
- dips_lt90: `6.666667` vs `2.666667` (`+4.000000`)
- crossings_100_boundary_raw: `97.333333` vs `127.333333` (`-30.000000`)
- operator interpretation: crossings and upper-tail overshoot improved (fewer dramatic swing crossings, no `>=105` samples), while low-tail counts were higher in this reconfirm cluster; promoted as active baseline by operator decision.

### Comparison vs Pre-Qt-Pacing Anchor (2026-04-25 Lock)
- avg: `99.566667` vs `99.564333` (`+0.002334`)
- p99: `102.333333` vs `102.000000` (`+0.333333`)
- dips_lt95: `19.333333` vs `20.000000` (`-0.666667`)
- dips_lt90: `6.666667` vs `8.000000` (`-1.333333`)
- crossings_100_boundary_raw: `97.333333` vs `102.000000` (`-4.666667`)

## Final Gate
- clean valid runs used: `3/3`
- marker validity: pass in all three accepted runs
- WL-05 lock hashes: pass in all three accepted runs
- current active comparison gate for fixed 266 MHz post-Qt pacing work: this artifact
