# ARM64 Voodoo Optimization Changelog

Date: 2026-03-13
Branch: `voodoo-dev`
Current baseline head before this update: `3d1d48141`
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
- Task 5 single-TMU resident state committed and manually revalidated
- Task 6 dual-TMU resident state committed and manually revalidated
- Task 7 texture-fetch fast/fallback split committed and manually revalidated
- Task 8 selected lookup-factor synthesis committed and build-verified
- Task 9 hot-state layout repack committed and build-verified

### 2026-03-13 - Task 5 single-TMU resident state landed

Commit: `9cf474e32` `perf: keep arm64 voodoo single-tmu state in registers`

- added a gated resident-state path for the common textured single-TMU loop, keeping `ib/ig/ir/ia`, `z`, `w`, `tmu0_s/t`, and `tmu0_w` in registers across pixels
- preserved the cached `w28` / `w27` coordinate handling and left dual-TMU on the original memory-backed path
- taught the single-TMU texture fetch plus the alpha/fog users of `ia`, `z`, and `w` to read the resident copies instead of reloading from `state`
- verified the change through fresh ARM64 debug and signed-release rebuilds
- reran the signed app against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`; the user reported `3DMark99`, `3DMark2000`, and `Unreal Gold` all looked correct
- the logfile did not capture a fresh optimization-stats footer for this run, so quantitative comparison remains pending even though the visual validation passed

### 2026-03-13 - Task 6 dual-TMU resident state landed

Commit: `3d1d48141` `perf: extend arm64 voodoo resident dual-tmu state`

- extended the resident-state design into the dual-TMU path by keeping `tmu1_s/t` in `v20`, `tmu1_w` in `v23.d[0]`, and the `pixel_count` / `texel_count` pair in `v21.2S`
- changed `codegen_texture_fetch()` to accept a resident-TMU bitmask so TMU1 can source resident `s/t/w` values without disturbing the already validated TMU0 single-TMU path
- kept the prologue and generated-function ABI stable by using caller-saved `v20`-`v24` instead of adding new callee-saved pressure
- verified the change through fresh ARM64 debug and signed-release rebuilds
- reran the signed app against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`; the user reported full-run `3DMark99`, `3DMark2000`, `Unreal Gold`, and the fog-heavy `Turok` demo all looked correct
- the run exited with `cache hits=29,427,145`, `misses=356`, `generated blocks=356`, `dithered spans=661,054,648`, `single_tmu=317,023,661`, `dual_tmu=292,627,146`, and zero reject signals

### 2026-03-14 - Task 7 texture-fetch fast/fallback split landed

Commit: `3a1dbd08c` `perf: synthesize selected arm64 voodoo blend factors`

- split the ARM64 `codegen_texture_fetch()` wrap-mode path into explicit fast and fallback sequences instead of always emitting the correction work inline
- added direct point-sample fast paths for both the perspective and non-perspective setups when mirrored `S` / `T` coordinates are already in range
- added a bilinear fast path that only takes the direct adjacent-texel load when `S` / `T` are already in range and not about to cross the edge sample
- kept clamp handling, mixed clamp/wrap handling, and wrap-edge correction on the validated fallback path rather than widening the new fast path
- preserved the existing optimization instrumentation, generated-function ABI, resident-TMU mask contract, and correctness-sensitive output-alpha work
- reran `cmake --build out/build/llvm-macos-aarch64-debug`; it succeeded with only the existing linker/deployment warnings
- reran `scripts/setup-and-build.sh build`; it completed a clean rebuild and re-signed `build/src/86Box.app`
- reran the signed app against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`; you reported `Unreal Gold`, `Turok demo`, and `3DMark2000` all looked correct
- `/tmp/task7_manual_86box.log` still contains the expected Windows boot line `Illegal instruction 00008B55 (FF)`, which remains non-regression noise
- the signed app exited with `cache hits=24,154,831`, `misses=206`, `generated blocks=206`, `code_bytes total=273,268`, `spans textured=499,117,920`, `single_tmu=211,834,339`, `dual_tmu=287,283,581`, and zero reject signals

### 2026-03-14 - Task 8 selected lookup-factor synthesis landed

Commit: `3a1dbd08c` `perf: synthesize selected arm64 voodoo blend factors`

- replaced the non-constant fog `alookup[fog_a + 1]` table load with a synthesized `fog_a + 1` NEON broadcast so the existing `+1` scale behavior remains exact
- replaced the simple RGB alpha-factor table loads with synthesized `src_alpha`, `dst_alpha`, `(255 - src_alpha)`, `(255 - dst_alpha)`, and saturate broadcasts recovered from the same doubled `w12` / `w5` convention already used by the RGB blend path
- deliberately left the newer output-alpha writeback path, generated-function ABI, resident-TMU mask contract, and instrumentation untouched
- reran `cmake --build out/build/llvm-macos-aarch64-debug`; it succeeded with only the existing linker/deployment warnings
- reran `scripts/setup-and-build.sh build`; it completed a clean rebuild and re-signed `build/src/86Box.app`
- relaunched the signed app against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`; `/tmp/task8_manual_86box.log` again showed the expected boot line `Illegal instruction 00008B55 (FF)`
- the run exited with `cache hits=8,823,365`, `misses=52`, `generated blocks=52`, `code_bytes total=75,656`, `spans textured=125,921,769`, `single_tmu=8,262,860`, `dual_tmu=117,658,909`, and zero reject signals
- you then reported that the alpha-sensitive manual set (`Lands of Lore III`, `Extreme Assault`, `Half-Life 1`) all looked fine, so this slice is now ready for handoff

### 2026-03-14 - Task 9 hot-state layout repack landed

Commit: `perf: align hot voodoo state fields for arm64`

- repacked `voodoo_state_t` with one explicit 8-byte alignment pad after `fb_mem` / `aux_mem`, then grouped `ib/ig/ir/ia`, `tmu0_s/t`, and `tmu1_s/t` into 16-byte-aligned hot blocks
- updated the ARM64 `STATE_*` offset constants explicitly and kept them guarded with `VOODOO_ASSERT_OFFSET(...)`
- switched the affected ARM64 hot paths from `ADD + LD1/ST1` to aligned `LDR/STR Q` for `ib` and `tmu1_s`
- reran `cmake --build out/build/llvm-macos-aarch64-debug`; it succeeded with only the existing linker/deployment warnings
- the exact plan x86-64 syntax-only command failed in this workspace before reaching the shared-layout check because it omitted local include paths for headers such as `cpu.h`
- reran a minimally corrected x86-64 syntax-only command with the required local include paths; it succeeded, and `src/include/86box/vid_voodoo_codegen_x86-64.h` required no source edit because it already uses `offsetof(voodoo_state_t, ...)`

Not yet started:

- broader optimization-phase manual regression runs beyond Task 6
- stricter like-for-like signed-release VM timing comparison against the Task 2 baseline

## Notes

- This changelog does not replace the completed correctness summary in `docs/2026-03-12-voodoo-gap-closure-executive-summary.md`.
- It is the optimization-effort companion log that starts where the correctness plan left off.
