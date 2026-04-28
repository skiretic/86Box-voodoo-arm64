# arm64-and-pacing Upstream Sync and Cleanup Plan

## Goal

Prepare a PR-ready branch based on `upstream/master` that contains only the real functional/code changes, with temporary instrumentation removed before final submission.

## Preferred Strategy

Use a rebase-first workflow (instead of broad cherry-picking) to preserve commit context while removing branch noise.

## Phase 0: Safety and Baseline

1. Confirm current state is clean and reproducible.
2. Create safety references:
   - backup branch from current `arm64-and-pacing`
   - optional tag before history rewrite
3. Record current known-good build command:
   - `./scripts/setup-and-build.sh build`

## Phase 1: History Cleanup Since Split from `master`

1. Identify fork point with your `master`.
2. Run interactive rebase from that fork point.
3. During rebase, classify commits:
   - keep: functional emulator changes
   - squash: small fixups into parent functional commit
   - drop: docs-only, experiment-only logging/telemetry scaffolding (if clearly non-functional)
   - defer-drop: instrumentation needed temporarily for validation
4. Produce a cleaned commit stack that still builds.

## Phase 2: Rebase Clean Stack Onto `upstream/master`

1. Fetch latest upstream.
2. Rebase cleaned branch onto `upstream/master`.
3. Resolve conflicts while preserving cleaned intent.
4. Push to a dedicated branch name for this PR prep flow.

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
2. Keep any instrumentation that is required for stable runtime behavior or user-facing diagnostics.
3. Rebuild and rerun workload validation.

## Phase 5: Final PR Preparation

1. Final pass for dead code/macros/includes left by removals.
2. Ensure minimal, reviewable commit structure.
3. Push final branch and prepare PR summary focused on functional changes only.

## Acceptance Criteria

- Branch is based on latest `upstream/master`.
- Branch contains only intended functional changes plus any intentionally retained necessary diagnostics.
- All temporary/experiment-only instrumentation is removed.
- Clean build succeeds.
- Workload validation passes.
- Diff is PR-reviewable and scoped.

## Fallback Option

If rebase cleanup becomes too tangled, switch to selective cherry-pick onto a fresh `upstream/master` branch using a curated commit list.
