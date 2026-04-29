# ARM64 Throughput Campaign Plan

## Objective

Increase sustained emulation throughput so higher-clock guest CPU configurations can hold `100%` speed more consistently on ARM64 hosts.

This campaign is layered on top of current validated dynarec/3DNow work and is focused on measurable throughput gains, not feature bring-up.

## Scope

In scope:
- Dynarec throughput bottlenecks
- Main-loop pacing/overhead under load
- Hot-path codegen/back-end efficiency
- Memory/dispatch overhead that materially impacts sustained speed

Out of scope (for this campaign):
- Functional feature expansion unless needed for throughput changes
- Broad refactors without direct throughput evidence

## Branch and Workflow

- Working branch: `ndr-throughput-lab`
- Parent baseline: `ndr-pacing-lab`
- Rule: prefer targeted high-impact refactor waves over many micro-slices, with strong validation gates.

## Refactor Policy

Refactors are allowed in this campaign when they directly support throughput goals or remove structural blockers to throughput work.

Refactor acceptance requirements:
1. Correctness parity:
   - Functional behavior remains unchanged for validated workloads.
2. Fallback safety:
   - No unexpected increase in fallback on targeted paths.
3. Throughput evidence:
   - Either measurable throughput gain, or a clear maintainability unblock that enables the next measured optimization.
4. Reversibility:
   - Keep each wave bounded enough to revert cleanly if regressions appear.

## Success Criteria

Primary:
- One or more previously non-100% high-clock target configs now sustain `100%` in standard workload runs.

Secondary:
- Improved headroom on existing stable configs (lower host utilization for same guest speed).
- No functional regressions in normal workload checks.

## Baseline Matrix (To Fill)

Define and lock target scenarios before tuning.

| ID | Guest Machine/CPU | Clock Target | Workload | Current Avg % | Current Min % | Notes |
|---|---|---:|---|---:|---:|---|
| T1 | TBD | TBD | TBD | TBD | TBD | |
| T2 | TBD | TBD | TBD | TBD | TBD | |
| T3 | TBD | TBD | TBD | TBD | TBD | |

Notes:
- Prefer at least one “hard” target that currently misses 100%.
- Keep workload deterministic and repeatable.

## Measurement Protocol

For each target/scenario:
1. Host check snapshot (same standard cadence used in this repo).
2. Run fixed-duration workload window.
3. Capture:
   - in-emulator speed behavior (avg/min/variance)
   - relevant perf artifacts (where configured)
   - key dynarec/runtime counters already available
4. Repeat enough times to reduce host-noise effects.

Rules:
- Do not compare single noisy runs.
- Use identical VM config and launch method across A/B.

## Validation Architecture (Required)

Throughput refactor waves must be validated with a 3-layer stack:

1. Host-side fallback/throughput telemetry
   - Capture fallback counters and throughput-relevant summaries each run.
   - Enforce a "no unexpected fallback increase" gate for target paths.
2. Win98 validation kit ISO (new)
   - Build a dedicated in-guest validation executable and package as ISO.
   - Provide deterministic pass/fail/hash outputs.
   - Provide batch entry points:
     - `RUN_SMOKE.BAT`
     - `RUN_FULL.BAT`
     - `RUN_STRESS.BAT`
3. Real workload stability runs
   - Run representative real workloads for fixed windows.
   - Confirm no crash/hang and no invalid fallback regressions.

Current verification split:
- Host-verifiable now:
  - phase markers (`PERF_PHASE_MARK seq=0..3`)
  - dynarec summaries/fallback counters in host `86box.log`
- Operator-verifiable now:
  - in-guest `THROUGHCOV` `PASS/DONE`
  - in-guest disk-copy script completion (`PERF_RUN.BAT`) and absence of guest-visible errors

Note:
- `PERF_RUN.BAT` guest console markers (`PERF_RUN_START/PERF_DONE`) are not currently mirrored into host-side logs in this workflow.
- Treat this as expected until a dedicated host-side guest-output capture path is added.

## Pre-Phase Requirement: Build Validation Tooling First

Before Wave 1 refactor work, complete validation tooling setup:

1. Create `win98-throughputcov` kit (parallel to existing Win98 test kits):
   - deterministic stress phases
   - phase/checkpoint markers
   - per-phase and total hash output
   - planned paths:
     - source: `tools/win98-throughputcov/throughputcov_win98.c`
     - build script: `tools/win98-throughputcov/build-win98-throughputcov.sh`
     - ISO script: `tools/win98-throughputcov/create-win98-throughputcov-iso.sh`
     - staged ISO content: `tools/win98-throughputcov/iso-src/`
     - output ISO: `tools/win98-throughputcov/win98-throughputcov-kit.iso`
2. Package ISO with scripted runners (`SMOKE/FULL/STRESS`).
3. Add host-side parser/check script for:
   - guest completion markers
   - pass/fail/hash summary
   - fallback summary comparison
4. Record one locked baseline run using this tooling.

Exit criteria:
- Validation kit is reproducible and usable as a hard gate for all throughput waves.

## Quantitative Validation Rules

Use these fixed rules for all A/B comparisons unless explicitly overridden in the run note.

1. Throughput sampling:
   - Per target: minimum 5 runs per side (baseline vs candidate).
   - Fixed-duration window per run: 10 minutes.
   - Aggregation:
     - primary comparison: median `avg%`
     - stability comparison: p10 `min%`
2. Fallback gate:
   - Define baseline fallback ratio for each target path from locked baseline runs.
   - Candidate must not increase fallback ratio by more than 2% relative on target paths.
   - Any absolute fallback increase on a path expected to remain zero is a gate failure.
3. Real workload pass gate:
   - Minimum runtime window: 20 minutes per workload scenario.
   - Must emit completion marker/heartbeat checkpoints for all expected phases.
   - Any crash, hang, or missing terminal checkpoint is a failure.

## Bottleneck Discovery Order

1. Main thread pacing overhead and run-loop cadence under heavy load.
2. Dynarec churn/recompile pressure in hot regions.
3. ARM64 back-end hotspots in commonly emitted uop sequences.
4. Memory/TLB and indirect dispatch overhead in throughput-critical paths.

## Optimization Phases

### Phase 0: Baseline Lock

- Dependency: Pre-Phase validation tooling complete.
- Fill matrix with concrete targets.
- Capture baseline metrics/artifacts using the required validation architecture.
- Record “locked baseline” reference in `docs/perf-artifacts/arm64-dynarec/`.

Exit criteria:
- Baseline numbers are stable enough for A/B decisions.

### Phase 1: Candidate Discovery and Wave Selection

- Identify 2-3 high-impact refactor candidates in measured hot paths.
- Select one Wave 1 target with:
  - clear throughput hypothesis
  - bounded file/scope ownership
  - clear validation strategy

Exit criteria:
- One approved Wave 1 refactor target with explicit success metrics.

### Phase 2: Wave 1 Targeted Refactor

- Implement the selected high-impact refactor as a cohesive change.
- Avoid unrelated cleanup in the same wave.
- Keep temporary measurement instrumentation only as needed for verification.

Exit criteria:
- Wave 1 lands with correctness/fallback gates passing and measurable throughput delta.

### Phase 3: Wave Validation and Next-Wave Decision

- Re-run full target matrix.
- Confirm no target regressed materially.
- Document final deltas and remaining bottlenecks.

Exit criteria:
- Wave 1 results are publishable/reproducible and a clear go/no-go exists for Wave 2.

## Validation Gates Per Refactor Wave

For every targeted refactor wave:
1. Clean build.
2. Win98 validation kit run (`SMOKE` minimum; `FULL` for final wave check).
3. Baseline target re-run (at least one representative hard target).
4. Real workload stability run (no crash/hang).
5. Fallback gate pass per quantitative rules above.
6. Record delta in this doc or linked artifact note.

If regressions appear:
- Revert or isolate immediately.
- Do not start next wave until regression is explained.

## Targeted Refactor Candidate Backlog

Use this as the decision table for choosing each wave.

| Candidate ID | Area | Refactor Idea | Throughput Hypothesis | Risk | Status |
|---|---|---|---|---|---|
| C1 | TBD | TBD | TBD | TBD | Backlog |
| C2 | TBD | TBD | TBD | TBD | Backlog |
| C3 | TBD | TBD | TBD | TBD | Backlog |

## Experiment Log (Append-Only)

| Date (ET) | Wave | Hypothesis | Result | Delta | Decision |
|---|---|---|---|---|---|
| TBD | Campaign start | Establish locked baseline | Pending | TBD | Pending |

## Immediate Next Steps

1. Build and package `win98-throughputcov` validation kit + host parser/check flow.
2. Fill Baseline Matrix with the first 3 concrete high-clock targets.
3. Capture locked baseline runs with the new validation stack.
4. Fill candidate backlog and choose Wave 1 from measured data.

## Progress Snapshot (2026-04-28 ET)

Completed:
1. `tools/win98-throughputcov` scaffold created:
   - `throughputcov_win98.c`
   - `build-win98-throughputcov.sh`
   - `create-win98-throughputcov-iso.sh`
   - `iso-src/README.TXT`
   - `iso-src/SCRIPTS/RUN_SMOKE.BAT`
   - `iso-src/SCRIPTS/RUN_FULL.BAT`
   - `iso-src/SCRIPTS/RUN_STRESS.BAT`
2. Local packaging sanity complete:
   - `THROUGHCOV.EXE` builds
   - `win98-throughputcov-kit.iso` builds

Next:
1. Run the kit in Win98 guest and verify PASS/DONE markers for `SMOKE`, `FULL`, `STRESS`.
2. Add host-side parser/check script and wire it into run workflow.
3. Record first locked baseline using the new stack.

Update (2026-04-28 late):
1. Combined suite ISO added and validated for workflow use:
   - `tools/win98-throughputcov/win98-throughput-suite.iso`
   - includes `THROUGHCOV`, microstress assets, `PERF_RUN.BAT`, and bundled `D:\\DATA` payload.
2. `RUN_SUITE.BAT` fixed to use `CALL` for chained BAT execution on Win98.
3. Practical status:
   - this phase is complete for bring-up/operational readiness.
   - strict automated host parsing for in-guest `PERF_RUN` text markers remains future enhancement work.
