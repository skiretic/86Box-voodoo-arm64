# ARM64 Voodoo Optimization Executive Summary

Date: 2026-03-13
Branch: `voodoo-dev`
Current head: `050b7640f`
Plan: `docs/plans/2026-03-13-arm64-voodoo-optimization-central-plan.md`

## Executive Summary

The ARM64 Voodoo optimization effort is now in the early execution state.

- the correctness baseline is already established on `voodoo-dev`
- the ARM64 optimization investigation has been refreshed against the current backend state
- a central optimization plan now exists with portability guardrails for Apple Silicon, Linux AArch64, and Windows ARM64
- the optimization baseline docs are now locked
- lightweight ARM64 optimization instrumentation is now landed and baseline data has been captured from a signed-release run
- Task 3's ARM64 dither-setup hoist is now implemented in the working tree as a validated minimal slice and fresh-build verified

The safest interpretation of current status is:

- execution readiness remains high because the completed correctness work, current ARM64 investigation, central plan, and first-wave measurements are now aligned
- immediate optimization targets are clearer now: the safe `dither_rb` base hoist first, then span-loop register residency, then texture fast-path specialization
- correctness-sensitive areas remain gated, especially the newly widened output-alpha path
- cross-platform ARM64 compatibility is a hard constraint, not a later cleanup item

## Progress Charts

Overall effort progress:

```text
Planning complete         [##########] 100%
Implementation started    [###-------]  30%
Validation complete       [##--------]  20%
```

Execution task progress:

```text
Completed  [###-------] 3/10  30%
Pending    [#######---] 7/10  70%
```

Effort phases:

```text
Correctness baseline locked     [##########] 100%
Investigation refresh           [##########] 100%
Central optimization plan       [##########] 100%
Instrumentation                 [##########] 100%
Low-risk hot-path work          [#####-----]  50%
Main span-loop work             [----------]   0%
Higher-risk specialization      [----------]   0%
Cross-platform validation       [----------]   0%
```

Portability readiness:

```text
Apple Silicon assumptions       [##########] 100%
Linux AArch64 constraints       [########--]  80%
Windows ARM64 constraints       [########--]  80%
Live multi-platform validation  [----------]   0%
```

## Current Position

### What is already done

- the Voodoo gap-closure correctness plan is complete
- ARM64 and x86-64 output-alpha parity work is committed
- the ARM64 JIT cache-key correctness fix for `col_tiled` vs `aux_tiled` is committed
- the ARM64 optimization investigation has been updated to reflect current backend reality rather than older assumptions
- the central optimization plan is committed and now has Task 1 and Task 2 completed
- the ARM64 optimization baseline docs are locked around the portability matrix and stop conditions
- lightweight ARM64 measurement hooks are committed and validated through fresh debug and signed-release builds
- a signed-release `3DMark99` baseline now shows negligible block-cache misses, all-dithered spans in the sampled run, and common dual-TMU usage
- Task 3 now hoists only the `dither_rb` table base out of the per-pixel loop by pinning it in otherwise-dead `x2`; `real_y` remains preserved in `x24` and the green-table offset stays on the original path after broader hoisting attempts regressed live rendering
- fresh debug and signed-release rebuilds both succeed from the Task 3 working tree, and signed-release reruns of `3DMark99`, full-demo `3DMark2000`, and `Unreal Gold timedemo 1` all looked visually correct

### What has not started yet

- register-resident span-loop work
- texture-fetch fast-path specialization
- selected lookup-factor synthesis
- hot-state layout repacking
- the broader optimization-phase manual runtime validation pass beyond Task 3

## Why The Plan Starts Conservatively

The first optimization wave is intentionally conservative because the code now sits on top of recently completed correctness work. In particular:

- output-alpha behavior was only recently widened to match the interpreter more closely
- manual game coverage is still incomplete for `Extreme Assault`, `Lands of Lore III`, and `Unreal Gold`
- platform-specific executable-memory and I-cache mechanics differ across macOS, Linux, and Windows even when the generated ARM64 instructions stay portable

That makes measurement-first optimization the right approach. The plan avoids guessing where the time goes and avoids touching the most correctness-sensitive logic before easier wins are ranked.

## Highest-Value Planned Work

### 1. Instrumentation first

This is now complete. The current baseline from the first few minutes of the signed-release `3DMark99` demo shows:

- cache misses are negligible relative to hits
- dithered spans dominate the sampled workload
- dual-TMU textured spans are common

### 2. Low-risk loop setup cleanup

Task 3 implementation is now in the working tree as a deliberately narrow change: the generated function preserves `real_y` in `x24`, hoists the chosen `dither_rb` base into otherwise-dead `x2`, and leaves the green-table offset on the original path because the broader hoist caused visible green/distorted output during signed-release VM testing.

### 3. Register-resident span core

Keep the hottest remaining span state in registers across the loop, building on the current cached `x` / `x2` baseline.

### 4. Higher-risk specialization only after measurement

Split texture fetch into clearer fast and fallback paths, and only then consider replacing selected `alookup` / `aminuslookup` loads where exact semantics can be proven.

## Current Risks

- manual runtime coverage is still incomplete for the games most likely to catch alpha-plane, transparency, fog, and dual-TMU regressions
- x86-64 live runtime testing is still unavailable in this workspace, so cross-backend confidence remains incomplete
- the macOS ARM64 toolchain still uses `-march=armv8.5-a+simd`, so the plan must continue treating ARMv8.0 as a policy ceiling rather than trusting the active Apple toolchain defaults
- Linux AArch64 and Windows ARM64 portability are planned for explicitly, but not yet validated through live builds or runtime testing in this workspace
- the current baseline comes from one representative workload, so optimization ranking is better informed now but not universal across all games

## Recommended Next Step

Finish Task 3 validation from the current working tree:

1. decide whether to capture a stricter like-for-like timed `3DMark99` comparison against the Task 2 baseline, since the current reruns are strongest as correctness/signal checks rather than a performance A/B
2. move on to the next low-risk optimization task from the central plan
