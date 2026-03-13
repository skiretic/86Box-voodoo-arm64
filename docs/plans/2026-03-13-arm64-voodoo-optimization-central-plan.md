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
Latest committed head when this plan was authored: `696808439`

Relevant state already true before optimization work starts:

- the correctness-focused Voodoo gap-closure plan is complete
- ARM64 and x86-64 JIT output-alpha parity are committed
- the ARM64 JIT cache-key correctness issue for `col_tiled` vs `aux_tiled` is fixed
- ARM64 signed-release sanity evidence is positive
- broader manual runtime coverage is still incomplete for `Extreme Assault`, `Lands of Lore III`, and `Unreal Gold`
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

- [ ] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: hoist arm64 voodoo dither setup"
```

### Task 4: Trim Remaining Constant Setup Only Where The ABI Stays Stable

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [ ] **Step 1: Audit prologue constants after the dither change**

Identify which pointer or literal loads still dominate short-span setup cost.

- [ ] **Step 2: Apply only ARMv8.0-safe setup improvements**

Allowed directions:

- literal-pool style loads
- further constant hoisting
- saved-register reduction only if the final register allocation proves it safe

Do not change the ABI contract of the generated function.

- [ ] **Step 3: Rebuild and compare code size / short-span instrumentation**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
```

Expected:

- build succeeds
- emitted code size does not grow unexpectedly for common paths

- [ ] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: reduce arm64 voodoo setup overhead"
```

## Chunk 3: Main Span-Loop Work

### Task 5: Build A Register-Resident Single-TMU Span Core

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`
- Test/Reference: `src/video/vid_voodoo_render.c`

- [ ] **Step 1: Preserve the existing `x` / `x2` cached-register design**

Do not rework loop-coordinate handling first. Build on the current `w28` / `w27` approach.

- [ ] **Step 2: Keep the hottest remaining span state in registers**

Target first:

- `ib/ig/ir/ia`
- `z`
- `tmu0_s/t`
- `tmu0_w`
- `w`

Write back only where later code truly needs memory visibility.

- [ ] **Step 3: Keep the first prototype limited**

Restrict the first pass to common single-TMU paths before extending to dual-TMU or unusual blend-heavy cases.

- [ ] **Step 4: Verify build plus core game coverage**

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

- [ ] **Step 5: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: keep arm64 voodoo single-tmu span state in registers"
```

### Task 6: Extend The Register Plan To Dual-TMU And Counter Paths

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [ ] **Step 1: Extend the register-resident model to TMU1 and counters**

Cover:

- `tmu1_s/t`
- `tmu1_w`
- `pixel_count`
- `texel_count`

- [ ] **Step 2: Reassess prologue register pressure after the extension**

If saved-register pressure becomes too high, simplify allocation before adding more features.

- [ ] **Step 3: Verify on the historically fragile matrix**

Run:

- `Unreal Gold`
- `Lands of Lore III`
- `Extreme Assault`

Focus:

- dual-TMU correctness
- transparency/HUD behavior
- alpha-plane correctness

- [ ] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: extend arm64 voodoo register-resident span core"
```

## Chunk 4: Higher-Risk Specialization

### Task 7: Split Texture Fetch Into Explicit Fast And Fallback Paths

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`
- Test/Reference: `src/include/86box/vid_voodoo_codegen_x86-64.h`

- [ ] **Step 1: Isolate the common fast paths**

Start with:

- point sample, no perspective
- point sample, perspective
- bilinear with coordinates already in range

- [ ] **Step 2: Keep edge handling in explicit fallback code**

Do not merge clamp/wrap correction into the new fast path unless measurement proves it is still profitable.

- [ ] **Step 3: Verify textured coverage**

Run:

- `Unreal Gold`
- `Turok demo`
- `3DMark2000`

Focus:

- no texture seam regressions
- no wrap/clamp edge artifacts
- no dual-TMU ordering regressions

- [ ] **Step 4: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md
git commit -m "perf: specialize arm64 voodoo texture fetch fast paths"
```

### Task 8: Replace Selected Lookup-Table Loads Only Where Semantics Are Provable

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `docs/arm64-voodoo-optimization-investigation.md`
- Modify: `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`

- [ ] **Step 1: Start with fog and simple RGB blend-factor paths**

Do not begin with the newer output-alpha writeback path.

- [ ] **Step 2: Preserve exact scale and rounding behavior**

Explicitly preserve any `alookup[c + 1]` semantics relied on by the current code.

- [ ] **Step 3: Re-run alpha-sensitive games before widening scope**

Manual minimum:

- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`

Stop if:

- alpha planes look wrong
- HUD/transparency diverges from the pre-change baseline

- [ ] **Step 4: Commit**

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

- [ ] **Step 1: Repack only the hottest vectorized state fields**

Target:

- `ib/ig/ir/ia`
- `tmu0_s/t`
- `tmu1_s/t`

- [ ] **Step 2: Update all offset assumptions explicitly**

Do not rely on incidental layout stability. Re-verify every `STATE_*` offset touched by the change.

- [ ] **Step 3: Rebuild ARM64 and syntax-check x86-64 integration if needed**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
clang -target x86_64-apple-macos10.13 -fsyntax-only -Isrc -Isrc/include -Iincludes/private -Iincludes/public src/video/vid_voodoo_render.c
```

Expected:

- ARM64 build succeeds
- x86-64 syntax integration remains clean if shared offsets or state assumptions changed

- [ ] **Step 4: Commit**

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
