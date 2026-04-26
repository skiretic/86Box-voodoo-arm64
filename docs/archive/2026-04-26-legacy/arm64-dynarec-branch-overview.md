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
- retire stale `NO_IMMEDIATES` state after sustained stable execution, so old hotspots can return to faster immediate-friendly mode.
- add post-retirement re-promotion hysteresis so stale hotspots do not immediately bounce back into `NO_IMMEDIATES`.

Why this matters:
- fewer unnecessary expensive recompiles/escalations.
- better balance between responsiveness and safety.
- smoother behavior under mixed workloads.

### 3) Logging/telemetry focused on correctness verification

Default logging was moved to low-noise summary output.  
Detailed tracing remains available only when explicitly enabled.

Why this matters:
- keeps correctness and safety checks visible on every run.
- keeps runs comparable and easier to parse.
- still allows deep debugging when needed.

## Key Commits By Improvement

### Branch/call-path optimization improvements
- `69ea86118` — switched to compact default telemetry policy (summary-first) for path stats.
- `a89166718` — expanded conditional branch shaping (`BEQ`, broader nonlocal-relative branch eligibility).
- `07e0914e0` — broadened conditional patch-path shaping through shared branch patch path.
- `a79adfceb` — hardened branch-template guard matching to avoid accidental rewrites.
- `c3776d99c` — added guarded `TBZ/TBNZ` patch-path shaping and related counters.

### Recompile churn policy improvements
- `318688e58` — retry-debt decay during stable execution (`retry_resets` telemetry added).
- `5a167d5c7` — raised escalation threshold from `2` to `3`.
- `79580357a` — added adaptive burst-aware escalation (`burst_resets`, `burst_promotions`).

### Tooling and telemetry usability improvements
- `5036321f4` — hardened telemetry launcher with retries/fallback behavior.
- `2012d0f5a` — added parser `--s-only` mode for churn-focused output.
- `4b57a2f59` — fixed parser handling of A-path totals for reliable comparisons.

## Observed Impact So Far

Across repeated Q3 + 3DMark99 + WL-05 runs:
- stability gates remain clean.
- WL-05 hash outputs remained locked (no drift).
- safety marker `unexpected_noimm_without_bmask` stayed `0`.
- churn promotion ratio dropped significantly versus older baseline:
  - from about `0.014` to roughly `0.0011 - 0.0015` range in recent runs.
- user-observed real-time emulation speed consistency improved in heavy scenes.
- latest validated run markers:
  - Q3 demo four timedemo: `1260 frames, 36.3 seconds: 34.7 fps`
  - 3DMark99: `2545 3DMarks`, `6053 CPU 3DMarks`
- latest `S-03f` trial markers (not yet promoted to baseline):
  - Q3 demo four timedemo: `1260 frames, 36.6 seconds: 34.4 fps`
  - 3DMark99: `2492 3DMarks`, `5894 CPU 3DMarks`
  - `WL-05` and safety marker stayed clean, but churn ratio regressed versus lock (`0.001404` vs `0.001091`), so tuning remains active.
- latest recheck (`S-03f r3`) confirmed mixed-but-promising behavior:
  - Q3 returned to lock-level marker (`1260 frames, 36.4 seconds: 34.7 fps`).
  - 3DMark99 remained below lock score (`2478`, CPU `5882`).
  - churn ratio improved versus `r2` (`0.001276` vs `0.001404`) but is still above lock (`0.001091`).
  - operator observed major improvement in 3DMark99 16 MB / 32 MB texture tests (roughly `90-97%` host-speed range versus older runs often near `~60%`), indicating real gain in a known host-stress hotspot.
- latest `S-03g` file-copy check (`PERF_RUN.BAT`) improved churn pressure further:
  - `promote_no_immediates/dirty_list_hits = 0.001153` (better than `S-03f` file-copy `0.001335`), with safety marker still clean.
- full mixed-workload recheck for `S-03g` did not hold that gain:
  - Q3 and WL-05 remained stable, 3DMark improved vs some `S-03f` runs.
  - churn ratio rose to `0.001375`, worse than both lock (`0.001091`) and `S-03f r3` (`0.001276`).
  - result: `S-03g` remains a non-baseline experiment.
- current follow-on (`S-03h`) switches to split-state guard logic:
  - separate stability-retirement streak from dirty retry state.
  - keep post-retirement re-promotion guard short and targeted to early dirty retries.
  - goal: retain disk-heavy gain without full-workload churn regression.
- latest outcome:
  - `S-03h` did not hold on full workload (`ratio 0.001494`) even though correctness stayed clean.
  - branch reverted to `S-03e`-baseline churn behavior for stability.
  - restore-check run landed near lock again (`0.001158` vs lock `0.001091`) with WL-05 unchanged and user-observed texture-stress responsiveness improved versus failed experimental variants.
- post-reboot logging recheck (`S-03e` baseline) showed expected variance without correctness drift:
  - `r1`: `0.001096` (`8 / 7299`, near lock).
  - `r2`: `0.001242` (`23 / 18520`, above lock).
  - safety marker remained `0` and WL-05 stayed locked in both runs.
- file-copy gate status:
  - dedicated `PERF_RUN.BAT` run completed with full marker sequence and no `PERF_ERROR` observed in guest console.

## What WL-05 Means

`WL-05` is the deterministic Win98 microstress correctness workload.

In practice:
- inside the guest, `MRUNALL.BAT` runs three variants (quick/normal/smc) of `MICROSTR.EXE`.
- those runs exercise core dynarec-sensitive behavior:
  - immediate-heavy arithmetic/register updates:
    - repeated constant materialization + writes + dependent reads (32-bit and subregister touch paths), which catches immediate encoding/writeback mistakes and stale register-state issues.
  - branch/helper-heavy control-flow paths:
    - dense conditional branch loops and helper-invoked paths, which catches wrong branch patching, wrong helper dispatch/return behavior, and control-flow divergence under dynarec.
  - self-modifying/churn-touch behavior (smc mode):
    - intentional write-then-execute/recompile pressure patterns, which catches dirty-list transition bugs, over-aggressive `NO_IMMEDIATES` escalation behavior, and stale block reuse hazards.
- each variant emits `MICROSTRESS_DONE total=<hash>`.
- if a hash changes, guest-visible instruction behavior changed; run is treated as correctness regression until explained.

Current locked `WL-05` totals:
- quick: `45db7b65`
- normal: `2520dd5e`
- smc: `b86f22a1`

## Validation Method

Each candidate change is accepted only if:
- Q3 timedemo finishes normally.
- 3DMark99 full run finishes normally.
- WL-05 quick/normal/smc hashes remain unchanged.
- safety counters remain clean.
- perf ISO file-copy workload (`PERF_RUN.BAT`) completes with the full marker sequence:
  - `PERF_RUN_START`
  - `PERF_PREP_START` / `PERF_PREP_END`
  - `PERF_LOOP_START 1` / `PERF_LOOP_END 1`
  - `PERF_LOOP_START 2` / `PERF_LOOP_END 2`
  - `PERF_DONE`
  - and no `PERF_ERROR`.

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
- latest large code swings (`S-03f/g/h`) were tested and rejected for baseline promotion after full-workload churn regressions; branch is operating on restored `S-03e` baseline.
- dedicated file-copy/disk-heavy validation is now part of active gate flow (`PERF_RUN.BAT` from perf ISO).
- validated caveat for interpretation of K6-2 workload behavior: ARM64 builds currently gate 3DNow dynarec decode-table entries off, so 3DNow execution still routes through instruction-function dispatch path rather than active ARM64 3DNow dynarec decode flow.

## Later-Phase Goals (Not Hit Yet)

- Sustain `100%` emulation speed more consistently at higher guest CPU settings (beyond the current `266666666` working profile) without correctness loss.
- Expand branch/control-flow efficiency beyond the current wave’s completed scope only where real workloads show meaningful host-side gain.
- Reduce recompilation churn further in stubborn hotspot patterns while keeping current safety behavior (`unexpected_noimm_without_bmask=0`).
- Keep x86-64 behavior unchanged while ARM64 path continues to improve.
- Finish wave closeout with stable repeatability across the locked workload set (Q3, 3DMark99, WL-05) before moving to broader post-wave refactors.
