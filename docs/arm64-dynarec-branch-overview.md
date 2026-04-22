# ARM64 Dynarec Branch Overview

This document explains, at a high level, what changed in this branch and how those changes have improved behavior on Apple Silicon hosts.

For deep technical detail, see:
- `docs/arm64-dynarec-wave1-implementation-plan.md`
- `docs/arm64-dynarec-investigation.md`

## Branch Goal

Improve ARM64 dynarec performance and stability in real workloads while keeping correctness strict.

Ground rules:
- ARM64-targeted behavior changes only (x86-64 behavior preserved).
- Keep fallback paths safe.
- Keep telemetry low overhead by default.
- Validate on repeatable Win98 workloads.

## What Was Improved

### 1) Faster branch/call path handling inside generated ARM64 code

The branch/call path selection was expanded and hardened so more local control-flow stays on efficient relative paths, while still falling back safely when needed.

Why this matters:
- less overhead in helper-heavy and branch-heavy code.
- more consistent real-time emulation speed in stress sections.
- no observed safety regression in fallback behavior.

Status:
- this area is now considered stable for the current wave and not the active tuning focus.

### 2) Lower recompilation churn from dirty-list escalation

The policy that decides when to force “no immediates” mode was tuned in steps to avoid escalating too early:

- reset stale retry debt after stable execution periods.
- require stronger repeated evidence before escalation.
- add burst-awareness so spaced-out retries do not accumulate as if they were one hot burst.

Why this matters:
- fewer unnecessary expensive recompiles/escalations.
- better balance between responsiveness and safety.
- smoother behavior under mixed workloads.

### 3) Logging/telemetry made practical for long runs

Default logging was moved to low-noise summary output.  
Detailed tracing remains available only when explicitly enabled.

Why this matters:
- avoids multi-GB logs in normal perf/regression runs.
- keeps runs comparable and easier to parse.
- still allows deep debugging when needed.

## Observed Impact So Far

Across repeated Q3 + 3DMark99 + WL-05 runs:
- stability gates remain clean.
- WL-05 hash outputs remained locked (no drift).
- safety marker `unexpected_noimm_without_bmask` stayed `0`.
- churn promotion ratio dropped significantly versus older baseline:
  - from about `0.014` to roughly `0.0011 - 0.0015` range in recent runs.
- user-observed real-time emulation speed consistency improved in heavy scenes.

## Validation Method

Each candidate change is accepted only if:
- Q3 timedemo finishes normally.
- 3DMark99 full run finishes normally.
- WL-05 quick/normal/smc hashes remain unchanged.
- safety counters remain clean.

## Current Runtime Profile

- VM frequency currently held at `266666666`.
- default telemetry environment:
  - `86BOX_NEW_DYNAREC_STATS=1`
  - `86BOX_NEW_DYNAREC_TELEMETRY=0`
  - `86BOX_A013_TRACE=0`

## Tooling Upgrades in This Branch

- more reliable VM launch flow on macOS (retry + fallback behavior).
- parser support for newer churn counters.
- focused parser mode for churn-only review:
  - `./scripts/dynarec/analyze-s03a-log.sh --s-only <current-log> [baseline-log]`

## Where We Are Now

- branch-path optimization work: stable and held.
- churn-policy refinement work: active and progressing.
- latest large code swing (adaptive burst-aware escalation): implemented and in validation.

## Next Steps

1. Complete validation of the newest churn-policy swing.
2. If gates stay clean, lock it in docs as accepted baseline.
3. Continue with next high-leverage code improvement before expanding test matrix further.
