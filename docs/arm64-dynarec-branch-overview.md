# ARM64 Dynarec Branch Overview

This is a high-level snapshot of what this branch is doing, why it exists, what has landed, and what is next.

Use this file for quick orientation.  
Use the detailed files for deep dive:
- `docs/arm64-dynarec-wave1-implementation-plan.md`
- `docs/arm64-dynarec-investigation.md`

## Branch Intent

`ndr-analysis` is focused on **ARM64 new-dynarec correctness/performance improvements** with strict guardrails:
- ARM64-only behavior changes where intended.
- Preserve x86-64 behavior.
- Prefer low-noise telemetry by default.
- Validate changes with repeatable Win98 workloads (Q3, 3DMark99, WL-05 microstress).

## What Landed So Far

### 1) A-lane (A-013) branch-path shaping

A-013 is now treated as **closed/frozen** for this wave (unless correctness regression reopens it).

Major outcome:
- Relative-path branch/call shaping was broadened and hardened across:
  - `BL/B` local-path handling
  - `CBNZ` / `BEQ` shape paths
  - shared `B.cond` patch templates
  - guarded `TBZ/TBNZ + B` patch templates
- Safety and fallback behavior stayed intact.
- Default logging was throttled to low-noise summary mode; detailed path trace is opt-in with `86BOX_A013_TRACE=1`.

### 2) S-lane churn policy follow-ons

S-03 base work was already complete; this branch then added follow-on tuning:

- **S-03c** (landed, validated)
  - ARM64-only retry-decay:
    - stale dirty-list retry debt is reset during stable non-dirty-list execution.
  - Added `retry_resets` telemetry field.
  - Result: large drop in `promote_no_immediates_per_dirty_hit` versus older S-03 baseline.

- **S-03d** (landed, validated)
  - ARM64-only threshold tune:
    - `DYNAREC_S03B_NO_IMM_THRESHOLD` raised `2 -> 3`.
  - Result: further reduction in promotion ratio without safety regressions.

- **S-03e** (landed, currently being validated)
  - ARM64-only adaptive burst policy:
    - promotion now depends on dense retry bursts, not stale spaced retries.
    - per-block epoch state added for burst recency logic.
  - New telemetry fields:
    - `burst_resets`
    - `burst_promotions`

## Validation Model (How We Judge Pass/Fail)

Primary gates:
- `WL-05` hash lock unchanged.
- `unexpected_noimm_without_bmask=0`.
- no crash/hang or control-flow correctness issues in workload runs.

Secondary checks:
- Q3 timedemo output consistency.
- 3DMark99 completion/score trend.
- churn telemetry deltas (`promote_no_immediates_per_dirty_hit`, defers, retry/burst counters).

## Current Runtime Baseline

- VM profile held at `266666666` (K6-2 baseline used for current wave checks).
- Low-noise telemetry defaults:
  - `86BOX_NEW_DYNAREC_STATS=1`
  - `86BOX_NEW_DYNAREC_TELEMETRY=0`
  - `86BOX_A013_TRACE=0`

## Tooling Improvements in Branch

- Telemetry launcher hardened for macOS launch reliability (`open -a` retries + fallback path).
- Parser updated multiple times for new counters and cleaner S-lane-only views:
  - `./scripts/dynarec/analyze-s03a-log.sh --s-only <current> [baseline]`

## Current Status (As Of Now)

- A-013 lane: **frozen** (accepted).
- S-03c: **accepted**.
- S-03d: **accepted**.
- S-03e: **implemented and launched for validation**.

## Near-Term Next Steps

1. Finish S-03e validation run(s) and lock/rollback based on gates.
2. If stable, document S-lane closeout checkpoint.
3. Then decide whether to:
   - continue S-lane refinement, or
   - move to the next major wave area.

