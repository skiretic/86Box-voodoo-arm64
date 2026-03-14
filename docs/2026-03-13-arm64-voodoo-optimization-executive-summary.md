# ARM64 Voodoo Optimization Executive Summary

Date: 2026-03-13
Branch: `voodoo-dev`
Current baseline head before this update: `3d1d48141`
Plan: `docs/plans/2026-03-13-arm64-voodoo-optimization-central-plan.md`

## Executive Summary

The ARM64 Voodoo optimization effort is now at final handoff state for the scoped Apple Silicon workspace plan.

- the correctness baseline is already established on `voodoo-dev`
- the ARM64 optimization investigation has been refreshed against the current backend state
- a central optimization plan now exists with portability guardrails for Apple Silicon, Linux AArch64, and Windows ARM64
- the optimization baseline docs are now locked
- lightweight ARM64 optimization instrumentation is now landed and baseline data has been captured from a signed-release run
- Task 3's ARM64 dither-setup hoist is now committed as a validated minimal slice
- Task 4's prologue helper-load gating is now committed and manually revalidated
- Task 5's first real single-TMU resident-state slice is now committed and visually revalidated
- Task 6's dual-TMU resident-state extension is now committed and manually revalidated
- Task 7's first texture-fetch fast/fallback split is now committed and manually revalidated on the signed build
- Task 8's first lookup-factor synthesis slice is now committed, build-verified, and manually validated on the signed build
- Task 9's hot-state layout repack is now committed and build-verified, with x86-64 syntax integration rechecked against the shared layout
- Task 10's final validation and handoff evidence is now recorded from fresh macOS builds plus a signed five-title manual matrix

The safest interpretation of current status is:

- the scoped optimization plan is fully executed on this branch
- Apple Silicon build and signed manual validation evidence is now in place for the full planned handoff set
- correctness-sensitive areas remain gated, especially the newly widened output-alpha path
- cross-platform ARM64 compatibility remains a hard constraint, with live Windows ARM64 and Linux AArch64 validation still outside this workspace

## Progress Charts

Overall effort progress:

```text
Planning complete         [##########] 100%
Implementation complete   [##########] 100%
Validation complete       [########--]  80%
```

Execution task progress:

```text
Completed  [##########] 10/10 100%
Pending    [----------] 0/10   0%
```

Effort phases:

```text
Correctness baseline locked     [##########] 100%
Investigation refresh           [##########] 100%
Central optimization plan       [##########] 100%
Instrumentation                 [##########] 100%
Low-risk hot-path work          [##########] 100%
Main span-loop work             [##########] 100%
Higher-risk specialization      [##########] 100%
Cross-platform validation       [######----]  60%
```

Portability readiness:

```text
Apple Silicon assumptions       [##########] 100%
Linux AArch64 constraints       [#########-]  90%
Windows ARM64 constraints       [#########-]  90%
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
- Task 3's fresh debug and signed-release rebuilds succeeded, and signed-release reruns of `3DMark99`, full-demo `3DMark2000`, and `Unreal Gold timedemo 1` all looked visually correct before commit
- Task 4 now gates the `x22`/`x23` trilinear helper loads and the `x25` bilinear lookup load so blocks that cannot use those paths stop paying the prologue setup cost, while keeping the generated-function ABI unchanged
- Task 4's fresh debug and signed-release rebuilds succeeded, and the same signed-release validation trio still looked visually correct before commit
- Task 5 prep corrected the ARM64 `D0` / `D1` 64-bit GPR<->SIMD transfer helper encodings and added the missing low-lane helpers so the resident-state work could proceed without guessing at scalar-vector lane moves
- Task 5's first single-TMU resident-state slice now keeps `ib/ig/ir/ia`, `z`, `w`, `tmu0_s/t`, and `tmu0_w` in registers across the common textured single-TMU loop while leaving dual-TMU on the original path
- Task 6 now extends that resident model into the dual-TMU path by keeping `tmu1_s/t`, `tmu1_w`, and the `pixel_count` / `texel_count` counters in caller-saved NEON registers across the loop, then spilling them back once on exit
- Task 6 kept the generated-function ABI and prologue stack shape unchanged by using caller-saved `v20`-`v24` instead of adding more callee-saved pressure
- fresh debug and signed-release rebuilds succeeded from the Task 6 working tree, and you reported full-run `3DMark99`, `3DMark2000`, `Unreal Gold`, and the fog-heavy `Turok` demo all looked visually correct on the signed build
- the fresh Task 6 signed-run stats footer was captured on exit, showing `cache hits=29,427,145`, `misses=356`, `generated blocks=356`, `dithered spans=661,054,648`, `single_tmu=317,023,661`, `dual_tmu=292,627,146`, and zero reject signals
- Task 7 now adds explicit wrap-mode fast paths inside `codegen_texture_fetch()` for point-sampled and bilinear fetches when coordinates are already in range, while leaving clamp, mixed clamp/wrap, and wrap-edge correction on explicit fallback code
- the Task 7 slice keeps the generated-function ABI stable, preserves the resident-TMU mask contract from Task 6, and does not reopen the correctness-sensitive output-alpha path
- fresh `cmake --build out/build/llvm-macos-aarch64-debug` and `scripts/setup-and-build.sh build` runs both succeeded after the Task 7 change
- you then manually revalidated the signed build on March 14, 2026 with `Unreal Gold`, `Turok demo`, and `3DMark2000`; all looked correct, the expected boot log line `Illegal instruction 00008B55 (FF)` remained non-regression noise, and the exit footer reported `cache hits=24,154,831`, `misses=206`, `generated blocks=206`, `spans textured=499,117,920`, `single_tmu=211,834,339`, `dual_tmu=287,283,581`, and zero reject signals
- Task 8 now replaces the fog `alookup[fog_a + 1]` load and the simple RGB alpha-factor table loads with synthesized NEON broadcasts while preserving exact `+1` semantics where required
- the Task 8 slice deliberately leaves the newer output-alpha writeback path, generated-function ABI, resident-TMU mask contract, and instrumentation unchanged
- fresh `cmake --build out/build/llvm-macos-aarch64-debug` and `scripts/setup-and-build.sh build` both succeeded after the Task 8 change
- a fresh signed run against `Windows 98 Gaming PC` exited with `cache hits=8,823,365`, `misses=52`, `generated blocks=52`, `spans textured=125,921,769`, `single_tmu=8,262,860`, `dual_tmu=117,658,909`, and zero reject signals; the expected boot log line `Illegal instruction 00008B55 (FF)` remained non-regression noise
- you then reported that `Lands of Lore III`, `Extreme Assault`, and `Half-Life 1` all looked fine, so the narrow Task 8 slice is ready for handoff
- Task 9 now repacks `voodoo_state_t` so `ib/ig/ir/ia`, `tmu0_s/t`, and `tmu1_s/t` land on 16-byte boundaries, and the ARM64 JIT now uses aligned `LDR/STR Q` on the affected hot paths
- fresh `cmake --build out/build/llvm-macos-aarch64-debug` succeeded after the Task 9 layout change
- the exact plan x86-64 syntax command needed extra local include paths in this workspace, but the corrected x86-64 syntax-only check succeeded without requiring any x86-64 source change because that header already uses `offsetof(...)`
- Task 10 then revalidated the portability assumptions in source, reran `cmake --preset llvm-macos-aarch64-debug`, reran `cmake --build out/build/llvm-macos-aarch64-debug`, and completed a fresh signed `scripts/setup-and-build.sh build`
- the final signed manual matrix on March 14, 2026 covered `Extreme Assault`, `Lands of Lore III`, `Unreal Gold`, `3DMark99`, and `3DMark2000`; you reported that all looked okay
- `/tmp/task10_manual_86box.log` again contained the expected boot line `Illegal instruction 00008B55 (FF)`, and the run exited with `cache hits=23,748,869`, `misses=12,791`, `generated blocks=12,791`, `single_tmu=164,030,936`, `dual_tmu=146,428,389`, and zero reject signals
- a later March 14, 2026 signed follow-up rerun also confirmed the corrected live-`stderr` capture path: after a short proof run, a shorter multi-title sanity pass exited with `cache hits=7,288,986`, `misses=193`, `generated blocks=193`, `single_tmu=107,925,850`, `dual_tmu=90,687,432`, and zero reject signals
- the optional `cmake --preset llvm-win32-aarch64` configure step remains unavailable in this workspace because that preset does not exist here, so Windows ARM64 and Linux AArch64 live validation remain explicit external caveats

### What has not started yet

- no further in-workspace tasks remain in the scoped central plan
- live Linux AArch64 and Windows ARM64 build/runtime validation still need an external host or CI environment

## Why The Plan Starts Conservatively

The first optimization wave is intentionally conservative because the code now sits on top of recently completed correctness work. In particular:

- output-alpha behavior was only recently widened to match the interpreter more closely
- the final Apple Silicon manual matrix now covers `Extreme Assault`, `Lands of Lore III`, `Unreal Gold`, `3DMark99`, and `3DMark2000`, while non-Apple ARM64 validation still remains external
- platform-specific executable-memory and I-cache mechanics differ across macOS, Linux, and Windows even when the generated ARM64 instructions stay portable

That makes measurement-first optimization the right approach. The plan avoids guessing where the time goes and avoids touching the most correctness-sensitive logic before easier wins are ranked.

## Highest-Value Planned Work

### 1. Instrumentation first

This is now complete. The current baseline from the first few minutes of the signed-release `3DMark99` demo shows:

- cache misses are negligible relative to hits
- dithered spans dominate the sampled workload
- dual-TMU textured spans are common

### 2. Low-risk loop setup cleanup

Task 3 is now committed as a deliberately narrow change: the generated function preserves `real_y` in `x24`, hoists the chosen `dither_rb` base into otherwise-dead `x2`, and leaves the green-table offset on the original path because the broader hoist caused visible green/distorted output during signed-release VM testing.

### 3. Small prologue gating wins

Task 4’s first safe slice is now committed: helper-pointer materialization for `bilinear_lookup`, `neon_00_ff_w`, and `i_00_ff_w` is no longer unconditional, and the same signed-release validation trio still looked correct.

### 4. Register-resident span core

The first slice of this is now in place: the common textured single-TMU loop keeps the hottest remaining span state in registers across the loop while preserving the cached `x` / `x2` baseline and leaving dual-TMU on the original path.

The second slice is now in place as well: the dual-TMU path now keeps TMU1 state plus the span counters resident without growing the prologue or changing the generated-function ABI.

The scoped plan has now been carried through the final Apple Silicon validation pass.

## Current Risks

- x86-64 live runtime testing is still unavailable in this workspace, so cross-backend confidence remains incomplete
- the macOS ARM64 toolchain still uses `-march=armv8.5-a+simd`, so the plan must continue treating ARMv8.0 as a policy ceiling rather than trusting the active Apple toolchain defaults
- the final manual matrix is Apple Silicon-only; Linux AArch64 and Windows ARM64 still are not validated through live builds or runtime testing in this workspace
- the current baseline comes from one representative workload, and the recorded post-plan follow-up sample is shorter than the earlier long-form validation passes, so optimization ranking is better informed now but not universal across all games

## Recommended Next Step

The scoped optimization plan is complete on `voodoo-dev`. The next safe move is to hand it off as-is unless you want to open a separate follow-up for live Linux AArch64 or Windows ARM64 validation.

The final validated boundaries are:

1. keep the completed correctness baseline closed unless a real regression appears
2. treat Apple Silicon validation as complete for this branch, but leave live Linux AArch64 and Windows ARM64 validation as separate follow-up work
3. keep future ARM64 JIT work inside ARMv8.0 + NEON and the existing platform JIT abstractions

This preserves the final branch as a validated Apple Silicon handoff rather than reopening scope after the central plan is already complete.
