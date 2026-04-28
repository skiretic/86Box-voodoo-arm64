# arm64-and-pacing Upstream Sync and Cleanup Plan

## Goal

Prepare a PR-ready branch based on `upstream/master` that contains only the real functional/code changes, with temporary instrumentation removed before final submission.

## Preferred Strategy

Use a fresh branch from `upstream/master` and transplant only code paths (`src/`, `cmake/`, and top-level CMake files) from `arm64-and-pacing`. Avoid replaying history.

## Phase 0: Safety and Baseline

1. Confirm current state is clean and reproducible.
2. Create safety references:
   - backup branch from current `arm64-and-pacing`
   - optional tag before history rewrite
3. Record current known-good build command:
   - `./scripts/setup-and-build.sh build`

## Phase 1: Create Fresh Upstream Branch

1. Fetch latest upstream refs.
2. Create a new branch from `upstream/master` (for example `arm64-codeonly-pr`).
3. Keep this branch history clean (no cherry-pick/rebase replay from branch history).

## Phase 2: Transplant Code-Only Changes

1. Copy only code-bearing paths from `arm64-and-pacing` into the fresh branch:
   - `src/`
   - `cmake/`
   - `CMakeLists.txt`
   - `CMakePresets.json` (if needed)
2. Do not copy `docs/`, `scripts/`, `tools/`, `research/`, perf artifacts, or other workflow scaffolding.
3. Review diff and prune any accidental non-code files.

## Phase 3: Validation (Normal Workload)

1. Run clean build.
2. Run standard workload validation flow (3 runs, host-check + launch workflow where VM is driven manually).
3. Capture pass/fail notes per run.
4. If failures appear, fix functional regressions first; do not strip additional code yet.

## Phase 4: Strip Remaining Instrumentation

1. Remove only branch-added instrumentation that is not required for function:
   - extra logging
   - telemetry counters
   - temporary timing/reporting hooks
   - experiment/debug toggles without runtime product value
2. Keep only instrumentation required for stable runtime behavior or intentional product diagnostics.
3. Rebuild and rerun workload validation.

## Phase 5: Final PR Preparation

1. Final pass for dead code/macros/includes left by removals.
2. Prefer a minimal commit structure (single code-only commit or small logical split).
3. Push final branch and prepare PR summary focused on functional changes only.

## Acceptance Criteria

- Branch is based on latest `upstream/master`.
- Branch contains only intended functional changes plus any intentionally retained necessary diagnostics.
- All temporary/experiment-only instrumentation is removed.
- Clean build succeeds.
- Workload validation passes.
- Diff is PR-reviewable and scoped.

## Fallback Option

If direct path transplant pulls too much, narrow scope further to subpaths (`src/codegen_new`, specific dynarec files) and build incrementally.
