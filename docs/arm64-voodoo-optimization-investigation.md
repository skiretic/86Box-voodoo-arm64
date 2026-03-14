# ARM64 Voodoo Optimization Investigation

## Scope

This report is a static investigation of the ARM64 Voodoo JIT path, focused on the ARMv8.0 + NEON baseline and starting from `src/include/86box/vid_voodoo_codegen_arm64.h`.

The goal is not to propose exotic ISA tricks. The goal is to identify realistic optimization work that should help on Apple Silicon and other ARM64 cores without requiring features newer than baseline ARMv8.0.

Files inspected:

- `src/include/86box/vid_voodoo_codegen_arm64.h`
- `src/video/vid_voodoo_render.c`

Important limitation:

- This is based on code inspection, not hardware counter profiling. The recommendations below are prioritized by likely payoff and implementation risk, but they should still be validated with measurement.

Current branch status note:

- `cfdda4cae` already fixed the ARM64 JIT cache-key correctness bug by splitting `col_tiled` and `aux_tiled`
- `cf16e67c3` and `19b611125` completed interpreter plus ARM64/x86-64 JIT output-alpha parity work
- `696808439` records the current verification and handoff status in the Voodoo docs
- `4cab227f1` locks the optimization baseline, portability matrix, and stop conditions
- `9e70b2004` adds lightweight ARM64 optimization instrumentation and records the first signed-release baseline observations
- ARM64 signed-release sanity evidence is positive, and the March 14, 2026 signed validation matrix now covers the previously open Apple Silicon game set

## Optimization Baseline And Portability Matrix

Before optimization begins:

- the correctness-focused gap-closure work is complete and is the semantic baseline for all ARM64 optimization work
- the March 14, 2026 signed validation matrix now covers `Extreme Assault`, `Lands of Lore III`, `Unreal Gold`, `3DMark99`, and `3DMark2000` on Apple Silicon
- the newly widened output-alpha behavior remains correctness-sensitive and should not be casually optimized without targeted validation
- signed ARM64 release validation is the preferred performance reference; debug builds remain useful for diagnosis, not speed impressions

Target portability matrix:

- Apple Silicon macOS
- Linux AArch64, including Raspberry Pi-class hosts running a 64-bit OS
- Windows ARM64, including Snapdragon-class hosts

Out of scope:

- 32-bit ARM hosts

Portability rule:

- generated code must stay within an ARMv8.0 + NEON baseline across all targets
- executable-memory, W^X, and I-cache mechanics may differ by OS, but optimization work must stay inside the current platform abstractions rather than depending on Apple-only behavior

Optimization stop conditions:

- pause if interpreter and JIT behavior diverge on the core regression matrix
- pause if a candidate change requires ISA features newer than ARMv8.0 + NEON
- pause if a candidate change depends on platform-specific JIT behavior outside the current abstractions

## Executive Summary

The biggest remaining ARM64 opportunity is not “more NEON” in the abstract. The backend already uses NEON in several places, and some easy wins have already landed since this note was first drafted: `x`/`x2` are now cached in registers across the loop, delta vectors are hoisted in the prologue, the cache uses a 32-entry per-partition layout with an MRU hint, and the pointer-load setup skips zero halfwords on macOS ARM64. The next meaningful gains are still about reducing per-pixel state traffic and unnecessary scalar setup around the span loop.

The highest-value work, in order:

1. Keep more span state live in registers across the pixel loop instead of reloading/storing it every iteration.
2. Hoist dither table setup out of the loop; today the dither path rebuilds table addresses inside the per-pixel hot path.
3. Replace `alookup` / `aminuslookup` loads with synthesized NEON factors where semantics are trivially preserved.
4. Split `codegen_texture_fetch()` into more aggressively specialized fast paths for the common textured cases.
5. Align hot `voodoo_state_t` vector fields so the JIT can use aligned `LDR/STR Q` instead of `ADD + LD1/ST1`.
6. Further reduce short-span overhead from prologue/epilogue and constant materialization.
7. Improve block-cache lookup and miss behavior, but treat this as second-order compared with span execution cost and as a pure performance follow-up rather than a correctness repair.

## Current Backend Shape

The ARM64 JIT block is a full span renderer, not a per-pixel helper. `voodoo_half_triangle()` resolves a compiled block once, then calls it for the whole span via `voodoo_draw(state, params, x, real_y)` in `src/video/vid_voodoo_render.c:943-947`.

That matters because register residency is extremely valuable here:

- a value kept in a register survives many pixels
- a value spilled to memory is paid for every pixel

The generated block:

- saves a large callee-saved set in the prologue (`vid_voodoo_codegen_arm64.h:1966-1985`)
- materializes several global pointers and constants, although pointer loads now skip zero halfwords on macOS ARM64 (`vid_voodoo_codegen_arm64.h:2003-2089`)
- executes a large per-pixel loop starting at `loop_jump_pos` (`vid_voodoo_codegen_arm64.h:2140-2143`)
- updates interpolants and counters at the tail (`vid_voodoo_codegen_arm64.h:4220-4370`)

## Priority 1: Keep More Span State in Registers

### Why this is the biggest opportunity

The generated loop still reloads and stores a lot of span state every pixel:

- iterated color vector `ib/ig/ir/ia` via `ADD + LD1_V4S + ST1_V4S` (`vid_voodoo_codegen_arm64.h:4244-4254`)
- `z` (`vid_voodoo_codegen_arm64.h:4256-4266`)
- `tmu0_s/t` (`vid_voodoo_codegen_arm64.h:4268-4276`)
- `tmu0_w` (`vid_voodoo_codegen_arm64.h:4278-4286`)
- global `w` (`vid_voodoo_codegen_arm64.h:4288-4296`)
- `tmu1_s/t` and `tmu1_w` in dual-TMU mode (`vid_voodoo_codegen_arm64.h:4298-4320`)
- `pixel_count` / `texel_count` (`vid_voodoo_codegen_arm64.h:4323-4343`)

The notable exception is `x`/`x2`: those are already cached in callee-saved registers before the loop and only written back as needed at the tail. That means the next register-residency step is not “start caching loop coordinates,” but “finish the job for the remaining hot interpolants and counters.”

Because the JIT owns the entire span call, many of these values can stay register-resident across iterations and be written back only once at loop exit, or only when a later stage truly needs memory visibility.

### Recommended direction

Build a “register-resident span core”:

- keep `z`, `w`, `tmu0_w`, and counters in GPRs for the whole loop, building on the existing cached `x`/`x2` baseline
- keep `ib/ig/ir/ia`, `tmu0_s/t`, and `tmu1_s/t` in NEON registers for the whole loop
- spill back to `state` once at function exit
- only materialize temporary stores for fields that are still consumed through memory by existing helper code

### Why this is ARM64-friendly

On ARM64 there are enough integer and SIMD registers to do this, especially because the generated loop does not make calls. The backend is currently paying memory traffic where register pressure is likely still manageable.

### Expected payoff

This should be the best single improvement for textured spans, fogged spans, and dual-TMU spans alike, because it attacks work done every pixel no matter which rendering path is selected.

## Priority 2: Hoist Dither Table Setup Out of the Loop

### Evidence

In the dither path, the backend rebuilds the `dither_rb` base pointer inside the per-pixel hot path (`vid_voodoo_codegen_arm64.h:4037-4056`).

It also rebuilds the fixed `dither_g - dither_rb` offset inside the same path (`vid_voodoo_codegen_arm64.h:4113-4129`).

That means multiple `MOVZ/MOVK` instructions are emitted for every pixel when dithering is active.

### Why this matters

Dithering is already a memory-heavy path with multiple table reads. Reconstructing constant addresses inside the loop is pure overhead and competes with useful work.

### Recommended direction

When `dither` is enabled:

- hoist the chosen dither base pointer into a pinned register in the prologue
- hoist the corresponding `g_offset` into another register
- reuse those registers inside the loop

If register pressure becomes tight, this is still a good trade because it removes instructions from a path that runs once per pixel.

### Expected payoff

High for dithered workloads, probably low risk, and a good first optimization even before deeper restructuring.

## Priority 3: Replace `alookup` / `aminuslookup` Memory Loads With Synthesized NEON Factors

### Evidence

The backend still pins pointers to `alookup` and `aminuslookup` (`vid_voodoo_codegen_arm64.h:2038-2043`) because the tables remain part of the JIT support setup, but the hot-path fog and alpha blend users were the clearest first candidates for direct synthesis.

The tables themselves are simple broadcasts:

- `alookup[c] = {c, c, c, c}` (`vid_voodoo_codegen_arm64.h:4685-4693`)
- `aminuslookup[c] = {255-c, 255-c, 255-c, 255-c}` (`vid_voodoo_codegen_arm64.h:4695-4703`)

### Why this is interesting

Many of these table loads can be replaced with:

- scalar `alpha` in a W register
- `dup vN.4h, wAlpha`
- optional `sub` from the pinned `0xFF` vector for `(255 - alpha)`

That removes:

- address generation
- data-cache reads
- dependence on global lookup tables

### Where this helps

Especially in:

- fog multiplication around `vid_voodoo_codegen_arm64.h:3592-3601`
- alpha blend paths around `vid_voodoo_codegen_arm64.h:3779-3926`

### Caveat

`alookup[c + 1]` semantics are sometimes used for rounding/scale behavior, so the scalar alpha path has to preserve the exact `+1` behavior where the current code relies on it.

Because output-alpha parity was recently widened in both JITs, this optimization is no longer just a generic cleanup. The safest path is to target fog and the simpler RGB blend-factor cases first, then leave the newer output-alpha writeback path alone until broader manual regression coverage exists.

### Current Task 8 slice

The current branch now includes that narrow first step:

- non-constant fog synthesizes the `alookup[fog_a + 1]` factor with scalar `fog_a + 1` plus `dup vN.4h, wAlpha`, preserving the exact `+1` semantics
- the simple RGB blend-factor cases synthesize `src_alpha`, `dst_alpha`, `(255 - src_alpha)`, `(255 - dst_alpha)`, and saturate factors directly from the recovered 8-bit alpha values
- the newer output-alpha writeback path still consumes the same doubled `w12` / `w5` convention as before and was intentionally not reopened
- fresh `cmake --build out/build/llvm-macos-aarch64-debug` verification succeeded after this slice, and the signed-build/manual alpha-sensitive pass (`Lands of Lore III`, `Extreme Assault`, `Half-Life 1`) also looked correct

### Expected payoff

Moderate to high. This is not as universally valuable as keeping span state in registers, but it is a clean ARM64-specific simplification and may free pinned registers for other work.

## Priority 4: Specialize `codegen_texture_fetch()` More Aggressively

### Evidence

`codegen_texture_fetch()` is large and scalar-control-flow heavy:

- perspective divide path uses `SDIV`, `CLZ`, shifts, and multiple stores (`vid_voodoo_codegen_arm64.h:1264-1387`)
- bilinear mode performs clamp/wrap logic with several conditional branches (`vid_voodoo_codegen_arm64.h:1431-1685`)

The backend already specializes by feature bits, but the common-case texture fetch is still doing substantial per-fetch control flow.

### Recommended direction

Split texture fetch into more explicit fast paths, especially for:

- point sample, no perspective
- point sample, perspective
- bilinear with wrap and in-range coordinates
- single-TMU common path

Then leave edge handling, clamp cases, and rare wrap cases to smaller fallback sequences.

### Practical example

For bilinear:

- fast path when `S` and `T` are already in range and `S != edge`
- fallback only when clamp/wrap correction is actually required

Today the code emits branchy correction logic as part of the normal path.

### Expected payoff

High on textured scenes, particularly when bilinear filtering is active. This is probably the most valuable optimization after the span-loop residency work.

## Priority 5: Realign Hot `voodoo_state_t` Fields for Better Vector Loads

### Evidence

The backend explicitly called out unaligned hot fields:

- `STATE_ib = 472`, which forced `ADD + LD1/ST1` rather than `LDR/STR Q`
- `STATE_tmu1_s = 520`, which had the same problem for TMU1 `s/t`

The source struct is in `src/video/vid_voodoo_render.c`, with these fields embedded in a larger mixed-layout state object (`vid_voodoo_render.c:39-83`).

### Recommended direction

Repack or pad `voodoo_state_t` so the hottest vectorized fields become 16-byte aligned:

- `ib/ig/ir/ia`
- `tmu0_s/t` and `tmu1_s/t` groups if possible

### Why this helps

Aligned `LDR/STR Q` is simpler for the code generator and usually a better fit for ARM64 load/store hardware than `ADD + LD1/ST1` lane-structure style access.

### Caveat

This change touches layout, so it should be done carefully and validated against all code using `offsetof()`-style assumptions. Still, because the backend already depends on exact offsets, this is a reasonable optimization lever.

### Current Task 9 slice

The current branch now applies that narrow repack:

- `voodoo_state_t` inserts one explicit 8-byte alignment pad after `fb_mem` / `aux_mem`, then groups `ib/ig/ir/ia`, `tmu0_s/t`, and `tmu1_s/t` into aligned hot blocks
- `STATE_ib` moves to `480` and `STATE_tmu1_s` moves to `512`, letting the ARM64 JIT switch those hot paths over to `LDR/STR Q`
- the ARM64 offset constants were re-verified explicitly via `VOODOO_ASSERT_OFFSET(...)`
- the x86-64 header did not need source changes because it already uses `offsetof(voodoo_state_t, ...)`
- fresh ARM64 rebuild verification succeeded, and a workspace-corrected x86-64 syntax-only check also succeeded

### Expected payoff

Moderate. Less transformative than register residency, but worthwhile and mechanically clean.

### Task 10 validation state

The 2026-03-14 validation pass closes the scoped Apple Silicon handoff loop for the current branch:

- source review reconfirmed that the generic AArch64 toolchain file still targets `-march=armv8-a`, while the active macOS preset remains an Apple-local `armv8.5-a+simd` toolchain choice rather than a new codegen requirement
- the Apple Silicon executable-memory path still relies on `MAP_JIT` plus `pthread_jit_write_protect_np(...)`, and the Windows ARM64 JIT path still uses `VirtualProtect(...)` plus `FlushInstructionCache(...)`
- fresh `cmake --preset llvm-macos-aarch64-debug`, `cmake --build out/build/llvm-macos-aarch64-debug`, and `scripts/setup-and-build.sh build` runs all succeeded on the current branch
- the final signed manual matrix (`Extreme Assault`, `Lands of Lore III`, `Unreal Gold`, `3DMark99`, `3DMark2000`) looked correct on the `Windows 98 Gaming PC` VM, and the run exited with `cache hits=23,748,869`, `misses=12,791`, `generated blocks=12,791`, `single_tmu=164,030,936`, `dual_tmu=146,428,389`, and zero reject signals
- a later March 14, 2026 signed follow-up rerun also captured a shorter live-session stats sample successfully with the corrected `stderr` workflow, exiting with `cache hits=7,288,986`, `misses=193`, `generated blocks=193`, `single_tmu=107,925,850`, `dual_tmu=90,687,432`, and zero reject signals
- the remaining caveat is still external validation: this workspace has no `llvm-win32-aarch64` CMake preset, and Linux AArch64 still needs a real host or CI runner

## Priority 6: Reduce Prologue/Epilogue and Constant Materialization Cost

### Evidence

Every compiled block:

- saves/restores a wide callee-saved set (`vid_voodoo_codegen_arm64.h:1966-1985`, `4377-4396`)
- builds global pointers using repeated `MOVZ/MOVK` sequences, although the current helper already skips zero halfwords (`vid_voodoo_codegen_arm64.h:2013-2044`)
- loads NEON constants from memory after first materializing their addresses (`vid_voodoo_codegen_arm64.h:2066-2089`)

### Why this matters

This overhead is paid once per span call. It is not as important as per-pixel costs, but short spans can become prologue-bound surprisingly quickly.

### Recommended direction

Two practical options:

1. Shrink the saved register set after restructuring the loop.
2. Move constant pointers to a literal pool near the generated code and load them with PC-relative literal loads.

Literal loads are ARMv8.0-safe and can cut several setup instructions per constant.

### Expected payoff

Moderate, especially for short spans and scenes with lots of tiny triangles.

## Priority 7: Improve Block Cache Lookup and Miss Cost

### Evidence

`voodoo_get_block()` currently:

- linearly scans up to 32 slots with a multi-field key compare (`vid_voodoo_codegen_arm64.h:4530-4555`)
- linearly scans again to find the LRU victim (`vid_voodoo_codegen_arm64.h:4557-4568`)
- flips page protection and flushes I-cache on a miss (`vid_voodoo_codegen_arm64.h:4570-4600`)

That said, the cache is in better shape than older snapshots of this backend: it already has 32 slots per partition, contiguous per-partition layout, an MRU hint, and the `col_tiled` / `aux_tiled` correctness split.

### Why this is not Priority 1

The lookup is done once per block acquisition in `voodoo_half_triangle()`, not once per pixel (`vid_voodoo_render.c:792`, `943-947`). So this is important, but it will not beat span-loop work in total impact.

It is also no longer standing in for a correctness fix. After the cache-key split landed, remaining work here is about reducing hit/miss overhead, not repairing stale-block aliasing.

### Recommended direction

- add a compact fingerprint for the block key and compare that first
- keep a direct-hit “last successful block” pointer per partition before doing the scan
- consider modestly increasing cache capacity if misses are common in real traces

### Expected payoff

Moderate for state-heavy games, low for scenes where spans mostly reuse a small set of pipelines.

## Additional ARM64-Specific Notes

### The backend already does some smart ARM64-specific work

This investigation is not saying the ARM64 backend is naive. It already contains good ARM64-aware choices:

- `LDP` for paired loads where offsets allow it (`vid_voodoo_codegen_arm64.h:1280-1287`, `1457-1458`, `2132-2133`, `4325-4337`)
- `TBZ` / `CBZ` patchable branches (`vid_voodoo_codegen_arm64.h:1463-1476`, `2207-2214`, `2294-2314`)
- hoisted NEON deltas for interpolants (`vid_voodoo_codegen_arm64.h:2097-2119`)
- cached `x` / `x2` loop coordinates in callee-saved registers before the span loop
- contiguous per-partition block-cache layout with an MRU probe hint
- pointer-load helpers that skip zero halfwords instead of always emitting a full `MOVZ/MOVK` chain

So the next round of work should focus less on “replace scalar instructions with NEON everywhere” and more on “remove avoidable setup, branching, and memory traffic.”

### Correctness risk areas

Past ARM64 Voodoo debug notes already pointed at gradient increments, skip patching, and depth behavior as fragile areas. That lines up with the current code structure:

- skip targets converge right before the increment block (`vid_voodoo_codegen_arm64.h:4193-4218`)
- interpolation updates happen after skip patching (`vid_voodoo_codegen_arm64.h:4220-4370`)

Any optimization in these areas should ship with image-based regression testing, not just “it compiles.”

## Suggested Order Of Attack

### Phase 1: Low-risk / high-confidence wins

1. Hoist dither base pointers and offsets out of the loop.
2. Add measurement counters for:
   - compiled block hits/misses
   - average emitted code size
   - textured vs non-textured spans
   - dithered vs non-dithered spans
3. Replace simple `alookup` / `aminuslookup` loads with synthesized vectors only in subpaths where exact behavior is easy to prove.

### Phase 2: Main performance work

4. Build a register-resident span-loop prototype for:
   - `z`
   - `w`
   - `ib/ig/ir/ia`
   - `tmu0_s/t`
   - `tmu0_w`
5. Measure before expanding to dual-TMU and alpha-blend-heavy paths.

### Phase 3: Structural improvements

6. Repack `voodoo_state_t` for 16-byte alignment of hot vector fields.
7. Split `codegen_texture_fetch()` into fast and edge/fallback variants.
8. Revisit prologue and literal-pool design after the register plan settles.

## What I Would Do First

If the goal is “best chance of visible speedup per engineering hour,” I would start here:

1. Hoist dither table setup out of the loop.
2. Add a small amount of measurement around textured, dithered, and cache-hit-heavy workloads so later changes are easier to rank.
3. Prototype register-resident `ib/ig/ir/ia`, `z`, `tmu0_s/t`, and `tmu0_w` across the span loop while keeping the existing cached `x` / `x2` structure.
4. Only then convert a subset of `alookup` / `aminuslookup` users where the exact output can be proven cheaply.

If the goal is “largest eventual upside,” then the real target is the register-resident span loop plus texture-fetch specialization.

## Bottom Line

The ARM64 backend’s best next step is not a search for more instructions to swap in. It is a cleanup of where work happens:

- less memory traffic in the span loop
- less constant setup inside hot paths
- fewer table loads for factors that can be synthesized
- more aggressive separation of fast texture cases from edge cases

That should give the best return while staying fully compatible with an ARMv8.0 + NEON baseline.
