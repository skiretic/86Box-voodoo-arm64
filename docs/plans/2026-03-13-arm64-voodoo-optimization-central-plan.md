# ARM64 Voodoo Optimization Central Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve ARM64 Voodoo JIT performance without regressing correctness, without exceeding an ARMv8.0 + NEON baseline, and without baking in Apple-only assumptions.

**Architecture:** Start from the completed correctness baseline on `voodoo-dev`, treat interpreter parity as the semantic source of truth, and use measurement-first ARM64 JIT optimization rather than mixing optimization guesses with behavior changes. Keep platform-specific executable-memory and I-cache handling inside the existing platform abstractions so the same codegen strategy remains viable on Apple Silicon, Linux AArch64 systems including Raspberry Pi-class hosts, and Windows ARM64 systems.

**Tech Stack:** C, CMake, ARM64 JIT codegen, macOS/Linux/Windows executable-memory platform layer, manual Voodoo regression testing, signed ARM64 release builds

---

## Scope Inputs

This central plan is driven by:

- `docs/arm64-voodoo-optimization-investigation.md`
- `docs/plans/2026-03-12-voodoo-gap-and-bug-closure.md`
- `docs/2026-03-12-voodoo-gap-closure-executive-summary.md`
- `docs/voodoo-deep-dive-findings-2026-03-12.md`
- `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`
- `voodoo-arm64-port/TESTING-GUIDE.md`
- `src/include/86box/vid_voodoo_codegen_arm64.h`
- `src/video/vid_voodoo_render.c`
- `src/unix/unix.c`
- `src/qt/qt_platform.cpp`
- `cmake/flags-gcc-aarch64.cmake`
- `cmake/llvm-macos-aarch64.cmake`
- `cmake/llvm-win32-aarch64.cmake`

## Current Starting Point

Status date: 2026-03-13
Branch: `voodoo-dev`
Latest committed head when this plan was last updated: `9072af755`

Relevant state already true before optimization work starts:

- the correctness-focused Voodoo gap-closure plan is complete
- ARM64 and x86-64 JIT output-alpha parity are committed
- the ARM64 JIT cache-key correctness issue for `col_tiled` vs `aux_tiled` is fixed
- ARM64 signed-release sanity evidence is positive
- broader manual runtime coverage is still incomplete for `Extreme Assault` and `Lands of Lore III`; `Unreal Gold` and `Turok` now have fresh signed-run coverage
- x86-64 live runtime validation remains unavailable in this workspace

## Guardrails

- Keep generated ARM64 instructions within an ARMv8.0 + NEON baseline.
- Treat Apple Silicon, Linux AArch64, and Windows ARM64 as first-class portability targets for codegen decisions.
- Do not introduce platform-specific fast paths in the JIT unless they stay behind the existing platform abstraction layer.
- Do not use optimization work to silently alter interpreter or x86-64-visible behavior.
- Do not optimize the newly widened output-alpha writeback path until measurement exists and regression coverage is improved.
- Keep x86-64 work out of scope unless a mechanical reference update is required to preserve shared documentation or invariants.
- Land optimizations in small commits that remain understandable when bisected.

## Success Criteria

- ARM64 JIT code remains semantically aligned with the interpreter on the active validation workloads.
- The optimization sequence preserves the current ARM64 signed-release sanity behavior.
- The codegen strategy remains compatible with:
  - Apple Silicon via the existing macOS ARM64 toolchain and JIT entitlement flow
  - Linux AArch64 via the generic `-march=armv8-a` configuration
  - Windows ARM64 via the existing Clang/MSVC-targeted toolchain and `VirtualProtect` / `FlushInstructionCache` path
- Performance work is backed by measurement, not intuition alone.

## Non-Goals

- no ISA requirements above ARMv8.0
- no Apple-only or Windows-only algorithm forks in the JIT
- no combined ARM64 optimization plus broad correctness rewrite in the same commit
- no reopening of the completed correctness plan except where optimization uncovers a real regression
- no assumption that one ARM64 core family represents all ARM64 cores

## Chunk 1: Baseline And Instrumentation

### Task 1: Lock The Optimization Baseline And Portability Matrix

**Files:**
- Modify: `docs/arm64-voodoo-optimization-investigation.md`
- Modify: `voodoo-arm64-port/TESTING-GUIDE.md`
- Modify: `docs/2026-03-12-voodoo-gap-closure-executive-summary.md`

- [x] **Step 1: Record the optimization starting assumptions**

Document in the relevant docs:

- correctness work is complete before optimization begins
- remaining manual game gaps still exist
- ARM64 portability target is broader than Apple Silicon alone
- Windows ARM64 and Linux AArch64 rely on the same ARMv8.0 codegen discipline, but not identical OS runtime mechanics

- [x] **Step 2: Define the platform matrix explicitly**

Record three target environments:

- Apple Silicon macOS
- Linux AArch64 / Raspberry Pi-class hosts running a 64-bit OS
- Windows ARM64 / Snapdragon-class hosts

Also record that 32-bit ARM environments are out of scope.

- [x] **Step 3: Define optimization stop conditions**

State that optimization work pauses if:

- interpreter/JIT behavior diverges on the core game matrix
- a change requires ISA features above ARMv8.0
- a change depends on platform-specific JIT behavior outside current abstractions

- [x] **Step 4: Commit**

```bash
git add docs/arm64-voodoo-optimization-investigation.md voodoo-arm64-port/TESTING-GUIDE.md docs/2026-03-12-voodoo-gap-closure-executive-summary.md
git commit -m "docs: define arm64 voodoo optimization baseline"
```

### Task 2: Add Measurement Hooks Before Optimization

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `src/video/vid_voodoo_render.c`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [x] **Step 1: Add lightweight measurement counters**

Add counters or trace points for:

- block-cache hits and misses
- emitted code size per block
- textured vs non-textured spans
- dithered vs non-dithered spans
- single-TMU vs dual-TMU span usage

Keep them behind an existing debug flag or a clearly disabled-by-default instrumentation switch.

- [x] **Step 2: Surface the counters in a developer-usable way**

Add a logging path or summary dump that can be inspected after targeted runs without changing normal release behavior.

- [x] **Step 3: Build and capture a baseline**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
scripts/setup-and-build.sh build
```

Expected:

- both commands succeed
- the instrumentation is inert by default
- enabling the measurement path produces stable, readable summaries

- [x] **Step 4: Record baseline observations**

Baseline captured on 2026-03-13 from the first few minutes of the signed-release `3DMark99` demo on `Windows 98 Gaming PC`:

- cache hits=`5,292,377`, misses=`49`, rejected=`0`
- generated blocks=`49`, code bytes total=`60,884`, avg=`1242.5`, min=`644`, max=`1852`
- spans textured=`92,185,570`, untextured=`21,129,932`
- spans dithered=`113,315,502`, non-dithered=`0`
- single-TMU spans=`42,205,969`, dual-TMU spans=`49,979,601`
- W^X rejects=`0`, emit overflow rejects=`0`

Capture at least:

- whether dithered paths appear common enough to optimize first
- whether block misses look meaningfully frequent
- whether dual-TMU spans are common in the active workloads

- [x] **Step 5: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h src/video/vid_voodoo_render.c voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: add arm64 voodoo optimization instrumentation"
```

## Chunk 2: Low-Risk Hot-Path Work

### Task 3: Hoist Dither Setup Out Of The Pixel Loop

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [x] **Step 1: Hoist dither table pointer setup into the prologue**

Pin the chosen `dither_rb` base pointer when dithering is enabled. A broader attempt that also hoisted the green-table offset and changed `real_y` register residency regressed signed-release rendering on 2026-03-13, so the validated Task 3 slice keeps those pieces on the original known-good path.

- [x] **Step 2: Remove redundant per-pixel constant materialization**

Replace the loop-local `dither_rb` base rebuild with use of the pinned register, while preserving the original per-pixel green-table offset materialization.

- [x] **Step 3: Verify build and focused runtime coverage**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
```

Then manually re-check:

- `3DMark99`
- `3DMark2000`
- one fog-heavy or gradient-heavy scene from `Unreal Gold`

Focus:

- no new dither artifacts
- no obvious RGB565 packing regression
- no signed-release performance drop

Build verification completed on 2026-03-13:

- `cmake --build out/build/llvm-macos-aarch64-debug` succeeded after the Task 3 JIT change
- `scripts/setup-and-build.sh build` succeeded and re-signed `build/src/86Box.app`

Signed-release rerun completed on 2026-03-13 against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`:

- user reported the rerun looked visually correct through boot and the sampled `3DMark99` demo segment
- cache hits=`6,628,949`, misses=`74`, rejected=`0`
- generated blocks=`74`, code bytes total=`88,884`, avg=`1201.1`, min=`648`, max=`1856`
- spans textured=`116,971,083`, untextured=`27,648,911`
- spans dithered=`144,619,994`, non-dithered=`0`
- single-TMU spans=`60,981,381`, dual-TMU spans=`55,989,702`
- W^X rejects=`0`, emit overflow rejects=`0`

Interpretation constraints:

- this rerun covered a longer session than the Task 2 baseline, so the counters are not a direct performance A/B
- the useful signal is that the narrowed hoist preserved visual correctness and introduced no new reject signals

Additional manual validation completed on 2026-03-13:

- user reported a full `3DMark2000` demo run looked correct on the same signed build
- user reported `Unreal Gold timedemo 1` also looked correct, covering a fog/gradient-heavy follow-up scene
- follow-up run stats ended at `cache hits=16,145,549`, `misses=146`, `generated blocks=146`, `dithered spans=343,935,094`, `dual_tmu=179,954,012`, with zero W^X or emit-overflow rejects

Task 3 validation status:

- focused build verification complete
- focused signed-release runtime coverage complete for `3DMark99`, `3DMark2000`, and `Unreal Gold timedemo 1`

- [x] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: hoist arm64 voodoo dither base"
```

### Task 4: Trim Remaining Constant Setup Only Where The ABI Stays Stable

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [x] **Step 1: Audit prologue constants after the dither change**

Identify which pointer or literal loads still dominate short-span setup cost.

- [x] **Step 2: Apply only ARMv8.0-safe setup improvements**

Allowed directions:

- literal-pool style loads
- further constant hoisting
- saved-register reduction only if the final register allocation proves it safe

Do not change the ABI contract of the generated function.

- [x] **Step 3: Rebuild and compare code size / short-span instrumentation**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
```

Expected:

- build succeeds
- emitted code size does not grow unexpectedly for common paths

Task 4 slice implemented on 2026-03-13:

- audited the prologue helper loads after Task 3 and identified `x22` (`neon_00_ff_w`), `x23` (`i_00_ff_w`), and `x25` (`bilinear_lookup`) as the clearest feature-gated candidates
- kept the register map and generated-function ABI unchanged
- made those helper loads conditional on the block actually using bilinear fetch or dual-TMU trilinear reverse-blend paths

Build verification completed on 2026-03-13:

- `cmake --build out/build/llvm-macos-aarch64-debug` succeeded after the Task 4 JIT change
- `scripts/setup-and-build.sh build` succeeded and re-signed `build/src/86Box.app`

Signed-release runtime validation completed on 2026-03-13 against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`:

- user reported the full `3DMark99` demo looked correct
- user reported the full `3DMark2000` demo looked correct
- user reported `Unreal Gold timedemo 1` looked correct
- cache hits=`24,421,694`, misses=`234`, rejected=`0`
- generated blocks=`234`, code bytes total=`289,608`, avg=`1237.6`, min=`612`, max=`1856`
- spans textured=`508,741,370`, untextured=`38,734,178`
- spans dithered=`547,475,548`, non-dithered=`0`
- single-TMU spans=`212,742,674`, dual-TMU spans=`295,998,696`
- W^X rejects=`0`, emit overflow rejects=`0`

- [x] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h docs/plans/2026-03-13-arm64-voodoo-optimization-central-plan.md docs/2026-03-13-arm64-voodoo-optimization-executive-summary.md docs/2026-03-13-arm64-voodoo-optimization-changelog.md voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: gate arm64 voodoo setup helper loads"
```

## Chunk 3: Main Span-Loop Work

### Task 5: Build A Register-Resident Single-TMU Span Core

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`
- Test/Reference: `src/video/vid_voodoo_render.c`

Preparation note recorded on 2026-03-13:

- correct `D0` / `D1` 64-bit GPR<->SIMD transfer helpers now exist in `vid_voodoo_codegen_arm64.h` as a build-verified prep step for the resident-state work
- this prep is intentionally limited to encoding correctness and does not yet change any active generated code path

- [x] **Step 1: Preserve the existing `x` / `x2` cached-register design**

Do not rework loop-coordinate handling first. Build on the current `w28` / `w27` approach.

- [x] **Step 2: Keep the hottest remaining span state in registers**

Target first:

- `ib/ig/ir/ia`
- `z`
- `tmu0_s/t`
- `tmu0_w`
- `w`

Write back only where later code truly needs memory visibility.

- [x] **Step 3: Keep the first prototype limited**

Restrict the first pass to common single-TMU paths before extending to dual-TMU or unusual blend-heavy cases.

- [x] **Step 4: Verify build plus core game coverage**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
scripts/setup-and-build.sh build
```

Manual minimum:

- `3DMark99`
- `3DMark2000`
- `Unreal Gold`

Focus:

- no lost interpolant updates
- no depth/fog drift
- stable performance compared with the signed-release baseline

Task 5 slice implemented on 2026-03-13:

- added a gated single-TMU resident-state path that keeps `ib/ig/ir/ia`, `z`, `tmu0_s/t`, `tmu0_w`, and `w` live in registers across the loop
- preserved the existing cached `w28` / `w27` `x` / `x2` design and left the dual-TMU path on the original memory-backed flow
- switched the single-TMU texture fetch, alpha/fog users, and loop tail to consume the resident copies instead of reloading from `state` every pixel
- spillback of the resident state now happens once on loop exit so generated-function ABI and post-block state visibility remain unchanged

Build verification completed on 2026-03-13:

- `cmake --build out/build/llvm-macos-aarch64-debug` succeeded after the Task 5 JIT change
- `scripts/setup-and-build.sh build` succeeded and re-signed `build/src/86Box.app`

Signed-release runtime validation completed on 2026-03-13 against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`:

- user reported `3DMark99`, `3DMark2000`, and `Unreal Gold` all looked visually correct on the signed build
- the expected Windows boot log line `Illegal instruction 00008B55 (FF)` remained present and is not a regression signal
- the logfile footer did not capture a fresh optimization-stats summary for this run, so strict quantitative comparison remains pending

- [x] **Step 5: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: keep arm64 voodoo single-tmu span state in registers"
```

### Task 6: Extend The Register Plan To Dual-TMU And Counter Paths

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [x] **Step 1: Extend the register-resident model to TMU1 and counters**

Cover:

- `tmu1_s/t`
- `tmu1_w`
- `pixel_count`
- `texel_count`

- dual-TMU textured blocks now preload `tmu1_s/t` into caller-saved `v20`, `tmu1_w` into `v23.d[0]`, `pixel_count` / `texel_count` into `v21.2S`, and the dual-TMU counter delta into `v22.2S`
- `codegen_texture_fetch()` now takes a resident-TMU bitmask so TMU1 can source its resident `s/t/w` values without disturbing the validated TMU0 single-TMU path
- the dual-TMU loop tail increments and spills those values once at loop exit instead of round-tripping them through `state` every pixel

- [x] **Step 2: Reassess prologue register pressure after the extension**

Result:

- prologue stack size and the generated-function ABI stayed unchanged
- the new dual-TMU resident slice uses caller-saved `v20`-`v24`, so no additional callee-saved NEON or GPR save/restore traffic was introduced

- [x] **Step 3: Verify on the current signed-release validation set**

Run:

- `3DMark99`
- `3DMark2000`
- `Unreal Gold`
- `Turok` demo

Focus:

- dual-TMU correctness
- fog-heavy correctness
- no visual regression on the previously validated single-TMU path

Build verification completed on 2026-03-13:

- `cmake --build out/build/llvm-macos-aarch64-debug` succeeded
- `scripts/setup-and-build.sh build` succeeded and re-signed `build/src/86Box.app`

Signed-release runtime validation completed on 2026-03-13 against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`:

- user reported full-run `3DMark99`, `3DMark2000`, `Unreal Gold`, and the fog-heavy `Turok` demo all looked correct
- the expected Windows boot log line `Illegal instruction 00008B55 (FF)` remained present and is not a regression signal
- the run exited with a fresh stats footer:
  - cache hits=`29,427,145` misses=`356` rejected=`0` hit_rate=`100.00%`
  - generated blocks=`356` code_bytes total=`438,876` avg=`1232.8` min=`608` max=`1888`
  - spans textured=`609,650,807` untextured=`51,519,041`
  - spans dithered=`661,054,648` non_dithered=`115,200`
  - single-TMU=`317,023,661` dual-TMU=`292,627,146`
  - rejects wx_write=`0` wx_exec=`0` emit_overflow=`0`

- [x] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: extend arm64 voodoo resident dual-tmu state"
```

## Chunk 4: Higher-Risk Specialization

### Task 7: Split Texture Fetch Into Explicit Fast And Fallback Paths

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`
- Test/Reference: `src/include/86box/vid_voodoo_codegen_x86-64.h`

- [x] **Step 1: Isolate the common fast paths**

Start with:

- point sample, no perspective
- point sample, perspective
- bilinear with coordinates already in range

- [x] **Step 2: Keep edge handling in explicit fallback code**

Do not merge clamp/wrap correction into the new fast path unless measurement proves it is still profitable.

Task 7 slice landed on 2026-03-14 in `3a1dbd08c`:

- wrap-mode point sampling now branches to a direct fast path when the mirrored coordinates are already in range, covering both the perspective and non-perspective point-sample setups
- wrap-mode bilinear fetch now branches to a direct adjacent-texel fast path when `S` / `T` are already in range and not about to cross the bilinear edge sample
- the validated clamp, mixed clamp/wrap, and wrap-edge correction logic remains on the fallback path instead of being folded into the new fast path
- `codegen_texture_fetch()` keeps the same generated-function ABI and the existing resident-TMU mask contract from Task 6

Build verification completed on 2026-03-14:

- `cmake --build out/build/llvm-macos-aarch64-debug` succeeded after the Task 7 JIT change
- `scripts/setup-and-build.sh build` succeeded and re-signed `build/src/86Box.app`

- [x] **Step 3: Verify textured coverage**

Run:

- `Unreal Gold`
- `Turok demo`
- `3DMark2000`

Focus:

- no texture seam regressions
- no wrap/clamp edge artifacts
- no dual-TMU ordering regressions

Signed-release runtime validation completed on 2026-03-14 against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`:

- user reported `Unreal Gold`, `Turok demo`, and `3DMark2000` all looked visually correct on the signed build
- the VM logfile still contains the expected Windows boot line `Illegal instruction 00008B55 (FF)`, which remains non-regression noise
- the signed app exited with a fresh stats footer:
  - cache hits=`24,154,831` misses=`206` rejected=`0` hit_rate=`100.00%`
  - generated blocks=`206` code_bytes total=`273,268` avg=`1326.5` min=`608` max=`2032`
  - spans textured=`499,117,920` untextured=`38,361,664`
  - spans dithered=`537,479,584` non_dithered=`0`
  - single-TMU=`211,834,339` dual-TMU=`287,283,581`
  - rejects wx_write=`0` wx_exec=`0` emit_overflow=`0`

- [x] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: specialize arm64 voodoo texture fetch fast paths"
```

### Task 8: Replace Selected Lookup-Table Loads Only Where Semantics Are Provable

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `docs/arm64-voodoo-optimization-investigation.md`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [x] **Step 1: Start with fog and simple RGB blend-factor paths**

Do not begin with the newer output-alpha writeback path.

- [x] **Step 2: Preserve exact scale and rounding behavior**

Explicitly preserve any `alookup[c + 1]` semantics relied on by the current code.

Task 8 slice landed on 2026-03-14 in `3a1dbd08c`:

- non-constant fog now synthesizes the `alookup[fog_a + 1]` broadcast factor with `ADD #1` plus `DUP`, preserving the existing `+1` scale behavior instead of loading the table entry from memory
- the simple RGB alpha-factor cases now synthesize `src_alpha`, `dst_alpha`, `255 - src_alpha`, `255 - dst_alpha`, and saturate factors directly from the recovered 8-bit scalar alpha values instead of loading `alookup[]` / `aminuslookup[]`
- the newer output-alpha writeback path, generated-function ABI, resident-TMU mask contract, and existing instrumentation remain unchanged
- `cmake --build out/build/llvm-macos-aarch64-debug` succeeded after the Task 8 JIT change

- [x] **Step 3: Re-run alpha-sensitive games before widening scope**

Manual minimum:

- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`

Stop if:

- alpha planes look wrong
- HUD/transparency diverges from the pre-change baseline

Current signed-run evidence from 2026-03-14:

- the signed app launched successfully against `Windows 98 Gaming PC` with `86BOX_VOODOO_ARM64_OPT_STATS=1`
- `/tmp/task8_manual_86box.log` again contained the expected Windows boot line `Illegal instruction 00008B55 (FF)`
- the run exited with a fresh stats footer:
  - cache hits=`8,823,365` misses=`52` rejected=`0` hit_rate=`100.00%`
  - generated blocks=`52` code_bytes total=`75,656` avg=`1454.9` min=`608` max=`2036`
  - spans textured=`125,921,769` untextured=`739,325`
  - spans dithered=`126,661,094` non_dithered=`0`
  - single-TMU=`8,262,860` dual-TMU=`117,658,909`
  - rejects wx_write=`0` wx_exec=`0` emit_overflow=`0`
- user then reported that `Lands of Lore III`, `Extreme Assault`, and `Half-Life 1` all looked fine on the signed build

- [x] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h docs/arm64-voodoo-optimization-investigation.md voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: synthesize selected arm64 voodoo blend factors"
```

## Chunk 5: Structural Follow-Ups And Validation

### Task 9: Repack Hot State Layout Only After The JIT Register Plan Settles

**Files:**
- Modify: `src/video/vid_voodoo_render.c`
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `src/include/86box/vid_voodoo_codegen_x86-64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [x] **Step 1: Repack only the hottest vectorized state fields**

Target:

- `ib/ig/ir/ia`
- `tmu0_s/t`
- `tmu1_s/t`

- [x] **Step 2: Update all offset assumptions explicitly**

Do not rely on incidental layout stability. Re-verify every `STATE_*` offset touched by the change.

- [x] **Step 3: Rebuild ARM64 and syntax-check x86-64 integration if needed**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
clang -target x86_64-apple-macos10.13 -fsyntax-only -Isrc -Isrc/include -Iincludes/private -Iincludes/public src/video/vid_voodoo_render.c
```

Expected:

- ARM64 build succeeds
- x86-64 syntax integration remains clean if shared offsets or state assumptions changed

Task 9 slice implemented on 2026-03-14:

- `voodoo_state_t` now inserts one explicit 8-byte alignment pad after `fb_mem` / `aux_mem`, then groups `ib/ig/ir/ia`, `tmu0_s/t`, and `tmu1_s/t` so those hot vectorized blocks land on 16-byte boundaries
- the ARM64 JIT now switches the affected `ib` and `tmu1_s` hot paths from `ADD + LD1/ST1` to aligned `LDR/STR Q`
- the shared offset constants in `vid_voodoo_codegen_arm64.h` were updated explicitly and remain guarded by `VOODOO_ASSERT_OFFSET(...)`
- `src/include/86box/vid_voodoo_codegen_x86-64.h` required no source edit because it already uses `offsetof(voodoo_state_t, ...)` instead of copied offset constants

Verification completed on 2026-03-14:

- `cmake --build out/build/llvm-macos-aarch64-debug` succeeded after the Task 9 layout change
- the exact plan syntax-check command failed in this workspace before parsing the shared layout because it does not include the local header search paths needed for `cpu.h`
- a minimally corrected x86-64 syntax-only check succeeded:
  - `clang -target x86_64-apple-macos10.13 -fsyntax-only -Iout/build/llvm-macos-aarch64-debug/src/include -Isrc -Isrc/include -Isrc/cpu -Isrc/codegen_new -Iincludes/private -Iincludes/public -I/opt/homebrew/include/freetype2 -I/opt/homebrew/include src/video/vid_voodoo_render.c`

- [x] **Step 4: Commit**

```bash
git add src/video/vid_voodoo_render.c src/include/86box/vid_voodoo_codegen_arm64.h src/include/86box/vid_voodoo_codegen_x86-64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: align hot voodoo state fields for arm64"
```

### Task 10: Cross-Platform ARM64 Build Validation And Final Handoff

**Files:**
- Modify: `docs/arm64-voodoo-optimization-investigation.md`
- Modify: `docs/2026-03-12-voodoo-gap-closure-executive-summary.md`
- Modify: `voodoo-arm64-port/TESTING-GUIDE.md`

- [ ] **Step 1: Validate platform assumptions explicitly**

Confirm the final optimization branch still respects:

- `-march=armv8-a` generic AArch64 expectations
- Apple Silicon JIT entitlement / W^X flow
- Windows ARM64 `VirtualProtect` / `FlushInstructionCache` path

- [ ] **Step 2: Run the available build matrix**

Minimum:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
```

If toolchains are available in the environment, also run:

```bash
cmake --preset llvm-win32-aarch64
cmake --build out/build/llvm-win32-aarch64
```

Linux AArch64 should be validated on an appropriate host or CI runner rather than guessed from macOS alone.

- [ ] **Step 3: Run the final manual ARM64 matrix**

Minimum final matrix:

- `Extreme Assault`
- `Lands of Lore III`
- `Unreal Gold`
- `3DMark99`
- `3DMark2000`

Record separately:

- build verification
- signed-release sanity evidence
- remaining missing manual game coverage
- any remaining Windows ARM64 or Linux AArch64 validation caveats

- [ ] **Step 4: Commit**

```bash
git add docs/arm64-voodoo-optimization-investigation.md docs/2026-03-12-voodoo-gap-closure-executive-summary.md voodoo-arm64-port/TESTING-GUIDE.md
git commit -m "docs: record arm64 voodoo optimization validation results"
```

## Recommended Execution Notes

- Start with measurement and low-risk loop setup work before touching the more fragile blend or texture-specialization code.
- Prefer signed ARM64 release validation over debug-app performance impressions.
- Treat Apple Silicon as the active execution environment, not the only target environment.
- Treat Linux AArch64 and Windows ARM64 as portability constraints throughout the work, even when live runtime validation is unavailable.
- If a change makes the JIT harder to reason about than the speedup justifies, stop and re-scope before compounding it with more optimization work.
