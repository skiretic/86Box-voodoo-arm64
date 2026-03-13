# ARM64 Voodoo Optimization Executive Summary

Date: 2026-03-13
Branch: `voodoo-dev`
Current head: `6a06e41b9`
Plan: `docs/plans/2026-03-13-arm64-voodoo-optimization-central-plan.md`

## Executive Summary

The ARM64 Voodoo optimization effort is now in the planning-ready state.

- the correctness baseline is already established on `voodoo-dev`
- the ARM64 optimization investigation has been refreshed against the current backend state
- a central optimization plan now exists with portability guardrails for Apple Silicon, Linux AArch64, and Windows ARM64
- no optimization code has been landed yet under this new plan

The safest interpretation of current status is:

- execution readiness is high because the completed correctness work, current ARM64 investigation, and central plan are now aligned
- immediate optimization targets are clear: instrumentation, dither setup hoisting, span-loop register residency, and texture fast-path specialization
- correctness-sensitive areas remain gated, especially the newly widened output-alpha path
- cross-platform ARM64 compatibility is a hard constraint, not a later cleanup item

## Progress Charts

Overall effort progress:

```text
Planning complete         [##########] 100%
Implementation started    [----------]   0%
Validation complete       [----------]   0%
```

Execution task progress:

```text
Completed  [----------] 0/10   0%
Pending    [##########] 10/10 100%
```

Effort phases:

```text
Correctness baseline locked     [##########] 100%
Investigation refresh           [##########] 100%
Central optimization plan       [##########] 100%
Instrumentation                 [----------]   0%
Low-risk hot-path work          [----------]   0%
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
- the central optimization plan is committed and ready for execution

### What has not started yet

- instrumentation for optimization ranking
- ARM64 JIT code changes for dither setup hoisting
- register-resident span-loop work
- texture-fetch fast-path specialization
- selected lookup-factor synthesis
- hot-state layout repacking
- final optimization-phase validation

## Why The Plan Starts Conservatively

The first optimization wave is intentionally conservative because the code now sits on top of recently completed correctness work. In particular:

- output-alpha behavior was only recently widened to match the interpreter more closely
- manual game coverage is still incomplete for `Extreme Assault`, `Lands of Lore III`, and `Unreal Gold`
- platform-specific executable-memory and I-cache mechanics differ across macOS, Linux, and Windows even when the generated ARM64 instructions stay portable

That makes measurement-first optimization the right approach. The plan avoids guessing where the time goes and avoids touching the most correctness-sensitive logic before easier wins are ranked.

## Highest-Value Planned Work

### 1. Instrumentation first

Add lightweight counters for block-cache behavior, emitted code size, textured spans, dithered spans, and TMU usage so optimization order is based on traces instead of intuition.

### 2. Low-risk loop setup cleanup

Hoist dither-table setup out of the per-pixel loop and trim remaining setup overhead that does not affect the generated function ABI.

### 3. Register-resident span core

Keep the hottest remaining span state in registers across the loop, building on the current cached `x` / `x2` baseline.

### 4. Higher-risk specialization only after measurement

Split texture fetch into clearer fast and fallback paths, and only then consider replacing selected `alookup` / `aminuslookup` loads where exact semantics can be proven.

## Current Risks

- manual runtime coverage is still incomplete for the games most likely to catch alpha-plane, transparency, fog, and dual-TMU regressions
- x86-64 live runtime testing is still unavailable in this workspace, so cross-backend confidence remains incomplete
- the macOS ARM64 toolchain still uses `-march=armv8.5-a+simd`, so the plan must continue treating ARMv8.0 as a policy ceiling rather than trusting the active Apple toolchain defaults
- Linux AArch64 and Windows ARM64 portability are planned for explicitly, but not yet validated through live builds or runtime testing in this workspace

## Recommended Next Step

Begin with Task 1 and Task 2 of the central plan:

1. lock the portability matrix and optimization stop conditions in the docs
2. add lightweight measurement hooks before attempting any hot-path rewrite
