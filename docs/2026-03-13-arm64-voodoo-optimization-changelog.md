# ARM64 Voodoo Optimization Changelog

Date: 2026-03-13
Branch: `voodoo-dev`
Current baseline head before this update: `b4fb0303d`
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

### 2026-03-13 - Optimization baseline locked

Commit: `4cab227f1` `docs: define arm64 voodoo optimization baseline`

- recorded that correctness work is complete before optimization
- made the Apple Silicon / Linux AArch64 / Windows ARM64 portability matrix explicit in the optimization docs
- recorded optimization stop conditions and kept the widened output-alpha path flagged as correctness-sensitive

### 2026-03-13 - Optimization instrumentation landed and baselined

Commit: `9e70b2004` `perf: add arm64 voodoo optimization instrumentation`

- added disabled-by-default ARM64 optimization stats for cache hits/misses, emitted code size, span texture mix, dither usage, and TMU usage
- verified the instrumentation through fresh ARM64 debug and signed-release builds
- captured a signed-release `3DMark99` baseline showing negligible cache misses, dither-heavy rendering, and common dual-TMU usage

### 2026-03-13 - Task 3 dither-setup hoist landed

Commit: `f79452a07` `perf: hoist arm64 voodoo dither base`
Docs synced in: `8b58eba10` `docs: mark task3 arm64 dither hoist complete`

- initial broader hoisting of `real_y` and the green-table path regressed live signed-release rendering with obvious green/distorted output
- the regression investigation narrowed Task 3 to a safer slice: hoist only the `dither_rb` base pointer, keep `real_y` in `x24`, and leave the green-table offset on the original path
- pinned `dither_rb` into otherwise-dead `x2` instead of consuming a hotter register
- preserved the existing optimization instrumentation and verified the narrowed change through fresh ARM64 debug and signed-release rebuilds
- reran the signed app against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`; the user reported correct visuals and the run ended with `cache hits=6,628,949`, `misses=74`, `generated blocks=74`, `dithered spans=144,619,994`, `dual_tmu=55,989,702`, and zero reject signals
- completed follow-up manual validation with a full `3DMark2000` demo run and `Unreal Gold timedemo 1`; user reported both looked correct
- the follow-up validation run ended with `cache hits=16,145,549`, `misses=146`, `generated blocks=146`, `dithered spans=343,935,094`, `dual_tmu=179,954,012`, and zero reject signals

### 2026-03-13 - Task 4 prologue helper loads gated

Commit: `9072af755` `perf: gate arm64 voodoo setup helper loads`

- audited the remaining unconditional prologue helper loads after Task 3
- gated `x25` (`bilinear_lookup`) behind bilinear-capable textured blocks
- gated `x22` (`neon_00_ff_w`) and `x23` (`i_00_ff_w`) behind the dual-TMU trilinear reverse-blend paths that actually consume them
- preserved the existing register layout and generated-function ABI rather than reworking the hot-path register plan
- verified the change through fresh ARM64 debug and signed-release rebuilds
- reran the signed app against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`; the user reported full-demo `3DMark99`, full-demo `3DMark2000`, and `Unreal Gold timedemo 1` all looked correct, and the run ended with `cache hits=24,421,694`, `misses=234`, `generated blocks=234`, `dithered spans=547,475,548`, `dual_tmu=295,998,696`, and zero reject signals

### 2026-03-13 - Task 5 resident-state prep corrected lane-move helpers

Commit: `b4fb0303d` `refactor: prep arm64 voodoo resident-state helpers`

- corrected the existing `ARM64_FMOV_X_D1` / `ARM64_FMOV_D1_X` helper encodings so they match real `D[1]` transfers instead of aliasing `D[0]`
- added the missing `ARM64_FMOV_X_D0` / `ARM64_FMOV_D0_X` helpers so the backend has both low-lane and high-lane 64-bit scalar/SIMD transfer forms available
- verified the encodings against a locally assembled ARM64 probe object before updating the header macros
- reran `cmake --build out/build/llvm-macos-aarch64-debug` and `scripts/setup-and-build.sh build`; both succeeded with only the existing linker/deployment warnings
- kept the change intentionally preparatory only: no active generated code path consumes the corrected helpers yet, so no new signed-runtime validation was required at this checkpoint

## Current Effort Status

Completed so far:

- correctness baseline established
- optimization investigation refreshed
- central optimization plan committed
- optimization baseline locked
- optimization instrumentation committed and baseline-captured
- Task 3 minimal dither-base hoist committed and manually revalidated
- Task 4 gated prologue helper loads committed and manually revalidated
- Task 5 helper-macro prep committed and build-verified

### 2026-03-13 - Task 5 single-TMU resident state landed

Commit: `9cf474e32` `perf: keep arm64 voodoo single-tmu state in registers`

- added a gated resident-state path for the common textured single-TMU loop, keeping `ib/ig/ir/ia`, `z`, `w`, `tmu0_s/t`, and `tmu0_w` in registers across pixels
- preserved the cached `w28` / `w27` coordinate handling and left dual-TMU on the original memory-backed path
- taught the single-TMU texture fetch plus the alpha/fog users of `ia`, `z`, and `w` to read the resident copies instead of reloading from `state`
- verified the change through fresh ARM64 debug and signed-release rebuilds
- reran the signed app against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`; the user reported `3DMark99`, `3DMark2000`, and `Unreal Gold` all looked correct
- the logfile did not capture a fresh optimization-stats footer for this run, so quantitative comparison remains pending even though the visual validation passed

Not yet started:

- broader optimization-phase manual regression runs beyond Task 4
- dual-TMU resident-loop extension
- stricter like-for-like signed-release VM timing comparison against the Task 2 baseline

## Notes

- This changelog does not replace the completed correctness summary in `docs/2026-03-12-voodoo-gap-closure-executive-summary.md`.
- It is the optimization-effort companion log that starts where the correctness plan left off.
