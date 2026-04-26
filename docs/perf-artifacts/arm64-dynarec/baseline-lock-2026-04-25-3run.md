# ARM64 Dynarec Baseline Lock

Date: 2026-04-25
Branch: `ndr-3dnow-lab`

BASELINE LOCKED: use these averages as comparison gate before any new code.

## Clean Runs
- r1: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-25_21-41-24-Windows 98 Gaming PC-baseline-prelock-r1`
  - host-noise: clean; load avg seen from CLI, no live `86Box` process before launch, no obvious heavy app spike in `ps` snapshot
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - whole-run avg=99.592, p50=100, p95=101, p99=102, min=84, max=105, dips_lt100=100, dips_lt95=21, dips_lt90=6
  - churn ratio_promote_no_immediates_per_dirty_hit=0.001214
- r2: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-25_21-51-14-Windows 98 Gaming PC-baseline-prelock-r2`
  - host-noise: clean; load avg seen from CLI, no live `86Box` process before launch, no obvious heavy app spike in `ps` snapshot
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - whole-run avg=99.535, p50=100, p95=101, p99=103, min=83, max=104, dips_lt100=91, dips_lt95=19, dips_lt90=8
  - churn ratio_promote_no_immediates_per_dirty_hit=0.001092
- r3: `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-25_22-00-12-Windows 98 Gaming PC-baseline-prelock-r3`
  - host-noise: clean; load avg seen from CLI, no live `86Box` process before launch, no obvious heavy app spike in `ps` snapshot
  - marker gate: start_seen=1, max_seq=3, valid_for_q3_3dmark_wl05=1
  - whole-run avg=99.566, p50=100, p95=101, p99=101, min=83, max=103, dips_lt100=57, dips_lt95=20, dips_lt90=10
  - churn ratio_promote_no_immediates_per_dirty_hit=0.001127

## Aggregate
### Whole-Run Speed
- avg: mean=99.564333 variance=0.000814
- p50: mean=100.000000 variance=0.000000
- p95: mean=101.000000 variance=0.000000
- p99: mean=102.000000 variance=1.000000
- min: mean=83.333333 variance=0.333333
- max: mean=104.000000 variance=1.000000
- dips_lt100: mean=82.666667 variance=514.333333
- dips_lt95: mean=20.000000 variance=1.000000
- dips_lt90: mean=8.000000 variance=4.000000

### Phase q3
- avg: mean=99.751667 variance=0.002761
- p50: mean=100.000000 variance=0.000000
- p95: mean=101.333333 variance=0.333333
- p99: mean=102.333333 variance=1.333333
- min: mean=83.333333 variance=0.333333
- max: mean=103.333333 variance=2.333333
- dips_lt100: mean=30.000000 variance=169.000000
- dips_lt95: mean=2.333333 variance=2.333333
- dips_lt90: mean=1.333333 variance=0.333333
### Phase 3dmark99
- avg: mean=99.481667 variance=0.001262
- p50: mean=100.000000 variance=0.000000
- p95: mean=101.000000 variance=0.000000
- p99: mean=101.666667 variance=1.333333
- min: mean=86.000000 variance=3.000000
- max: mean=103.000000 variance=1.000000
- dips_lt100: mean=48.333333 variance=100.333333
- dips_lt95: mean=16.666667 variance=1.333333
- dips_lt90: mean=6.000000 variance=4.000000
### Phase wl05
- avg: mean=99.521000 variance=0.034033
- p50: mean=100.000000 variance=0.000000
- p95: mean=102.333333 variance=1.333333
- p99: mean=102.666667 variance=0.333333
- min: mean=88.666667 variance=1.333333
- max: mean=102.666667 variance=0.333333
- dips_lt100: mean=2.666667 variance=1.333333
- dips_lt95: mean=1.000000 variance=0.000000
- dips_lt90: mean=0.666667 variance=0.333333

### Churn
- ratio_promote_no_immediates_per_dirty_hit: mean=0.001144 variance=0.000000003946

## Run Notes
- r1: clean; load avg `1.75 1.43 2.55`; no live `86Box` process before launch; no obvious heavy app in CLI view.
- r2: clean; load avg `1.79 2.29 2.57`; no live `86Box` process before launch; no obvious heavy app in CLI view.
- r3: clean; load avg `1.98 2.40 2.55`; no live `86Box` process before launch; no obvious heavy app in CLI view.

## Rejected / Replaced
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-25_21-37-57-Windows 98 Gaming PC-baseline-prelock-r1/86box.log`: rejected; incomplete capture; only seq=0 seen, no full Q3 -> 3DMark99 -> WL-05 marker sequence.
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/2026-04-25_21-40-40-Windows 98 Gaming PC-baseline-prelock-r1/86box.log`: rejected; incomplete capture; only seq=0 seen, no full Q3 -> 3DMark99 -> WL-05 marker sequence.

## Final Gate
- clean valid runs used: `3/3`
- rejected/replaced attempts: `2`
- no marker failures in accepted runs
- no code changes made before lock
