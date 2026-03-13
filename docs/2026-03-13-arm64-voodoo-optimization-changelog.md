# ARM64 Voodoo Optimization Changelog

Date: 2026-03-13
Branch: `voodoo-dev`
Current head: `6a06e41b9`
Plan: `docs/plans/2026-03-13-arm64-voodoo-optimization-central-plan.md`

## Purpose

This changelog tracks the preparation and execution milestones for the ARM64 Voodoo optimization effort.

It starts from the point where the correctness-focused Voodoo gap-closure work had already established a usable baseline for optimization planning.

## Changelog

### 2026-03-12 21:49 - Correctness scope locked down

Commit: `70c57a2d7` `docs: define voodoo gap-closure scope`

- captured the first-wave Voodoo correctness scope
- recorded non-goals and guardrails
- established the interpreter-first approach that still constrains optimization work

### 2026-03-12 21:53 - Tiled-mode cache-key correctness fixed

Commit: `cfdda4cae` `fix: split voodoo jit tiled-mode cache keys`

- split `col_tiled` and `aux_tiled` cache-key handling
- removed a correctness blocker that would have made later block-cache performance work hard to reason about

### 2026-03-12 21:55 - Output-alpha intent documented

Commit: `b31534ea2` `docs: capture intended voodoo output-alpha behavior`

- documented the interpreter-first rule for alpha-path changes
- clarified the previous reduced output-alpha behavior
- created a clearer semantic reference for later optimization safety checks

### 2026-03-12 22:01 - Interpreter output-alpha behavior completed

Commit: `cf16e67c3` `fix: complete voodoo interpreter output-alpha blending`

- widened interpreter output-alpha handling
- moved the shared behavior closer to a stable semantic baseline
- increased the need for caution around any later ARM64 optimization touching alpha-sensitive logic

### 2026-03-13 - JIT output-alpha parity completed

Commit: `19b611125` `fix: complete voodoo jit output-alpha parity`

- ported the updated output-alpha behavior into both the ARM64 and x86-64 JITs
- made the ARM64 JIT more correct
- also made optimization work around blend-factor handling more sensitive to regression risk

### 2026-03-13 - Validation and handoff recorded

Commit: `696808439` `docs: record voodoo gap-closure verification results`

- recorded the final gap-closure verification and handoff state
- separated build verification, signed-release sanity evidence, missing manual game coverage, and x86-64 runtime caveats
- established the documented starting point for the next optimization phase

### 2026-03-13 - ARM64 optimization investigation refreshed

Commit coverage: included in `6a06e41b9`

- updated `docs/arm64-voodoo-optimization-investigation.md` to match current backend reality
- called out that `x` / `x2` are already cached in registers
- downgraded block-cache work from implied correctness repair to explicit performance follow-up
- added caution around optimizing the newly widened output-alpha path
- made the portability target explicit across Apple Silicon, Linux AArch64, and Windows ARM64

### 2026-03-13 - Central optimization plan added

Commit: `6a06e41b9` `docs: add arm64 voodoo optimization plan`

- created the first central planning doc for the ARM64 optimization effort
- sequenced the work into instrumentation, low-risk hot-path cleanup, span-loop residency, higher-risk specialization, and final validation
- encoded ARMv8.0 + NEON as a hard ISA guardrail
- encoded Apple Silicon, Linux AArch64, and Windows ARM64 as portability targets rather than afterthoughts

## Current Effort Status

Completed so far:

- correctness baseline established
- optimization investigation refreshed
- central optimization plan committed

Not yet started:

- optimization instrumentation
- ARM64 JIT hot-path code changes
- optimization-phase build validation beyond the existing correctness baseline
- optimization-phase manual regression runs

## Notes

- This changelog does not replace the completed correctness summary in `docs/2026-03-12-voodoo-gap-closure-executive-summary.md`.
- It is the optimization-effort companion log that starts where the correctness plan left off.
