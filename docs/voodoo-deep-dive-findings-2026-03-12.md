# Voodoo Deep Dive Findings

Date: 2026-03-13
Branch: `voodoo-dev`
Scope: `src/video/vid_voodoo_*`, `src/include/86box/vid_voodoo_*`, and selected 86Box git history related to interpreter/JIT correctness, compatibility, and performance

## Executive Summary

The Voodoo renderer is built around one canonical C interpreter in `src/video/vid_voodoo_render.c` and two architecture-specific JIT backends in:

- `src/include/86box/vid_voodoo_codegen_x86-64.h`
- `src/include/86box/vid_voodoo_codegen_arm64.h`

The highest-confidence current correctness issue is not a single instruction bug in the ARM64 JIT. It is JIT cache key aliasing: both JITs collapse `col_tiled` and `aux_tiled` into one boolean cache key even though code generation emits distinct paths for color-buffer and aux-buffer addressing. If those modes change independently, a stale compiled block can be reused with the wrong framebuffer/depth-buffer access pattern.

The second major issue is incomplete alpha-plane output-alpha blending. The RGB blend logic is implemented, but the final alpha writeback logic in both the interpreter and JITs only meaningfully handles the `AONE` alpha-alpha factors. That leaves a known hole in a part of the pipeline that has already caused compatibility issues for games relying on alpha buffers and transparency.

The history also shows a repeated pattern: tiny codegen mistakes have produced visible game bugs. Recent fixes include:

- wrong shift in `FOG_Z`
- wrong instruction (`MOV` vs `ADD`)
- incorrect fog alpha behavior
- TMU1 negate ordering mismatch
- depth-bias clamp mismatch
- render-thread contention on ARM64
- HUD transparency bugs in Extreme Assault

That pattern strongly suggests future improvements should be staged behind verification, with interpreter parity treated as the primary constraint and performance work only landing after mode-by-mode validation.

For the first wave of gap-closure work, the scope should stay narrow:

- split the JIT cache key for `col_tiled` versus `aux_tiled`
- complete output-alpha blending behavior with interpreter-first parity
- run targeted regression coverage for historically fragile fog, transparency, TMU ordering, depth-bias, and render-thread timing paths

Non-goals for the same phase:

- no new ISA assumptions above ARMv8.0
- no opportunistic JIT optimization batches mixed into correctness fixes
- no broad renderer refactor while the correctness holes above are still open

## Current Status Snapshot

Status date: 2026-03-13
Current branch head: `19b611125`

Implemented so far:

- scope and non-goals locked into the working docs
- JIT cache-key aliasing fixed by separating `col_tiled` and `aux_tiled` in both active JIT cache keys
- output-alpha behavior documented before code changes
- interpreter and LFB output-alpha writeback moved from `AONE`-only handling to shared factor-based helper logic
- x86-64 JIT output-alpha writeback now mirrors the interpreter factor set on the current branch head
- ARM64 JIT output-alpha writeback now mirrors the interpreter factor set on the current branch head
- regression-checklist docs are refreshed for alpha/blend, fog/depth, texture/TMU, tiled-buffer/JIT-cache, and render-thread scenarios
- final verification and handoff status are recorded in the current docs

Not implemented yet:

- no further implementation work in this scoped plan

Verified so far:

- successful ARM64 debug configure
- successful ARM64 debug build after the cache-key fix
- successful ARM64 debug build after the interpreter output-alpha fix
- successful x86-64 syntax-only compile of `src/video/vid_voodoo_render.c` after the Task 5 local JIT change
- successful ARM64 debug build after the Task 6 local JIT change
- successful clean ARM64 debug rebuild from a deleted build directory
- successful signed ARM64 release rebuild via `scripts/setup-and-build.sh build`
- successful final Task 8 ARM64 debug build verification on current source state (`ninja: no work to do.`)
- `3DMark99` full demo smoke loop looked stable in this session
- `3DMark2000` full demo smoke loop looked stable in this session
- manual signed-release sanity check restored expected performance after launching the release app instead of the debug app

Still unverified:

- full x86-64 build/runtime behavior of the new x86-64 JIT alpha path
- game-specific regression coverage outside the `3DMark99` / `3DMark2000` smoke loops and the signed-release sanity pass
- runtime behavior of the new interpreter and JIT alpha-factor coverage outside the historically exercised subset

## Final Verification Status

### Build verification

- fresh `cmake --build out/build/llvm-macos-aarch64-debug` from the current branch state succeeded with `ninja: no work to do.`
- prior clean ARM64 debug rebuild and signed release rebuild remain the strongest build and integration evidence for the touched code paths

### ARM64 signed-release sanity evidence

- `scripts/setup-and-build.sh build` previously produced the signed ARM64 release app with JIT entitlements
- manual sanity checking reported expected performance once the signed release app was launched instead of the debug build
- the existing `3DMark99` and `3DMark2000` smoke loops did not reveal obvious regressions

### Remaining missing manual game coverage

- dedicated post-parity runs are still missing for `Extreme Assault`, `Lands of Lore III`, and `Unreal Gold`
- optional extended coverage is still missing for `Half-Life 1`, `Turok demo`, `Tomb Raider`, `Need for Speed II: SE` or `III`, and `Carmageddon 2`
- because those runs are unrecorded, alpha-plane, transparency/HUD, fog, and dual-TMU edge cases remain only partially covered

### Remaining x86-64 runtime caveat

- x86-64 output-alpha parity is committed on `voodoo-dev`, but this workspace still lacks x86-64 live runtime validation
- local confidence for x86-64 remains limited to syntax/build integration rather than real cached-block execution
- treat x86-64 as runtime-unverified until a suitable host is available

## Scenario Checklists

Use these as the repeatable first-pass manual checklist before calling a Voodoo correctness change done.

### Alpha / Blend changes

- Run `Lands of Lore III`, `Extreme Assault`, and `Half-Life 1`
- Compare JIT ON versus OFF for transparency, HUD overlays, alpha-plane writes, and masked effects
- If visual corruption appears only with JIT ON, repeat with `jit_debug=2` long enough to capture the problematic scene

### Fog / Depth changes

- Run `Unreal Gold`, `3DMark99`, and `3DMark2000`
- Look for fog intensity mismatches, wrong `FOG_Z` behavior, depth-fighting, and missing occlusion
- Re-check with more than one camera transition or scene loop before ruling a bug out

### Texture / TMU changes

- Run `Unreal Gold` and `Turok demo`
- Check dual-TMU ordering, bilinear filtering, texture edge seams, and negate/reverse path behavior
- Toggle JIT ON versus OFF if the issue looks texture-state dependent

### Tiled-buffer / JIT-cache changes

- Run `3DMark99`, `3DMark2000`, `Unreal Gold`, and `Lands of Lore III`
- Focus on repeated scene transitions, aux/depth/alpha correctness, and one-frame corruption after state changes
- Prefer longer loops over single launches because cache aliasing bugs are state-transition sensitive

### Render-thread / Concurrency changes

- Run repeated demo loops with `render_threads = 1` first, then try `2` only if investigating contention-sensitive behavior
- Watch for hangs, guest slowdowns, frame pacing issues, or corruption that appears only after longer runs
- Keep `jit_debug=1` available for log capture if a long-run issue appears

## Architecture Overview

### Interpreter

The interpreter lives in `src/video/vid_voodoo_render.c`.

Main flow:

- `voodoo_triangle()`
- `voodoo_half_triangle()`
- per-pixel inner loop in `voodoo_half_triangle()`

The interpreter computes:

- triangle setup and LOD estimation
- scanline clipping and SLI routing
- depth/W conversion
- TMU fetch and TMU combine
- color combine and alpha combine
- fog
- alpha test and alpha blend
- RGB565 writeback and aux/depth/alpha writeback

### JITs

The JIT is injected into the scanline path from `voodoo_half_triangle()`:

- if `voodoo->use_recompiler` is enabled, `voodoo_get_block()` returns a compiled scanline-span routine
- otherwise execution falls back to the interpreter

ARM64 JIT:

- file: `src/include/86box/vid_voodoo_codegen_arm64.h`
- cache: 32-entry LRU per partition
- emits a specialized block keyed from selected render state
- supports alpha planes, fog, TMU combine, and per-pixel writeback

x86-64 JIT:

- file: `src/include/86box/vid_voodoo_codegen_x86-64.h`
- older, more ad hoc cache logic
- still serves as the behavioral reference for much of the ARM64 port

### Current architecture constraints

Current build settings already matter here:

- generic AArch64 flags use `-march=armv8-a`
- x86-64 flags use `-march=x86-64 -msse2`
- macOS LLVM AArch64 toolchain currently uses `-march=armv8.5-a+simd`

If future work must stay at or below ARMv8.0, the macOS ARM64 toolchain settings should be treated as a policy mismatch and kept out of any new optimization assumptions.

Relevant files:

- `cmake/flags-gcc-aarch64.cmake`
- `cmake/flags-gcc-x86_64.cmake`
- `cmake/llvm-macos-aarch64.cmake`

## High-Confidence Current Findings

### 1. JIT cache key aliasing between `col_tiled` and `aux_tiled`

Confidence: High
Impact: Correctness bug, likely intermittent and state-dependent
Affected code:

- `src/include/86box/vid_voodoo_codegen_arm64.h`
- `src/include/86box/vid_voodoo_codegen_x86-64.h`
- `src/video/vid_voodoo_reg.c`

Problem:

Both JITs store and compare a single cache-key bit:

- `data->is_tiled = (params->col_tiled || params->aux_tiled) ? 1 : 0;`

But the generated code contains separate compile-time branches for:

- `params->col_tiled`
- `params->aux_tiled`

That means these distinct states alias to the same cached block:

- color tiled, aux linear
- color linear, aux tiled
- color tiled, aux tiled

This is not theoretical. `SST_colBufferStride` and `SST_auxBufferStride` are programmed independently in `src/video/vid_voodoo_reg.c`, and each sets its own tiled mode and row width independently.

Why this matters:

- the JIT emits different read/write addressing for color and aux buffers
- stale block reuse can produce wrong depth reads, wrong alpha-plane reads, or wrong framebuffer writes
- because the failure depends on state transitions and cache reuse, it can present as game-specific corruption or one-frame glitches rather than deterministic failure

Recommendation:

- split the cache key into separate `col_tiled` and `aux_tiled` fields in both JITs
- include `row_width` vs `aux_row_width` only if generation actually depends on them beyond base pointers
- add a targeted verify scenario that toggles one tiled mode at a time

### 2. Output-alpha blending remains only partially implemented

Confidence: High
Impact: Compatibility risk in alpha-plane paths
Affected code:

- `src/include/86box/vid_voodoo_render.h`
- `src/video/vid_voodoo_render.c`
- `src/include/86box/vid_voodoo_codegen_x86-64.h`
- `src/include/86box/vid_voodoo_codegen_arm64.h`

Problem:

The interpreter still contains a live comment:

- `TODO: Implement proper alpha blending support here for alpha values.`

After RGB blending, the interpreter computes output alpha using a reduced formula that only meaningfully handles `dest_aafunc == AFUNC_AONE` and `src_aafunc == AFUNC_AONE`.

The JITs mirror this reduced behavior:

- x86-64 explicitly accumulates alpha only for `dest_aafunc == 4` and `src_aafunc == 4`
- ARM64 does the same

Current implementation detail:

- RGB blending already evaluates the broader `AFUNC_*` factor set for `dest_afunc` and `src_afunc`
- final output-alpha writeback does not mirror that coverage
- interpreter output alpha currently only preserves contributions when `dest_aafunc == AFUNC_AONE` and/or `src_aafunc == AFUNC_AONE`
- x86-64 and ARM64 JIT output-alpha writeback intentionally mirror that same reduced `AONE`-only behavior today

Why this matters:

- alpha planes were important enough to add dedicated support
- commit `601155fdd` explicitly mentions Lands of Lore III relying on alpha buffer functionality
- the codebase has already seen transparency/HUD regressions tied to subtle pipeline behavior

Current interpretation:

- RGB blending is implemented
- output-alpha blend-factor coverage is incomplete
- current JIT/interpreter parity may be preserved, but parity is incomplete with respect to the intended hardware model

Recommendation:

- document the intended hardware behavior for `src_aafunc` and `dest_aafunc`
- expand interpreter implementation first
- then update both JITs to match
- validate against alpha-plane users before any optimization work in this area

### 3. JIT correctness remains fragile in small codegen details

Confidence: High
Impact: Regression risk for any optimization pass

This is not one bug, but a strong pattern in the history:

- `5b4b486af`: x86-64 JIT fixed a `MOV` that should have been an `ADD`
- `74824bbac`: x86-64 JIT fixed wrong shift in `FOG_Z`
- `ad136e140`: ARM64 JIT fixed fog alpha computation and corrected `FOG_Z`
- `be05fefe8`: ARM64 JIT fixed TMU1 negate ordering and depth-bias clamp
- `7aeaabeee`: older x86/x86-64 dynarec fix corrected a missing jump
- `f39c3491b`: transparency/HUD fix for Extreme Assault changed both interpreter and x86-64 recompiler behavior

Takeaway:

The Voodoo JIT is vulnerable to:

- wrong shift counts
- wrong branch patch offsets
- wrong sign/negate ordering
- wrong one-instruction substitutions
- behavior copied from older x86-64 code that was itself imperfect

Recommendation:

- treat interpreter parity as the source of truth, not x86-64 JIT parity
- require a verification pass for every functional JIT change
- prefer small, isolated commits over broad optimization batches

### 4. ARM64 render-thread fixes indicate concurrency is part of correctness now

Confidence: High
Impact: Performance and possible timing-sensitive correctness failures

Commit `0922ad784` added:

- cache-line padding for `params_read_idx`, `params_write_idx`, and `render_voodoo_busy`
- ARM64-specific short spin-wait before sleeping render threads

This means the ARM64 JIT is no longer just a codegen problem. It also changes the timing and contention profile between:

- CPU/FIFO thread
- render thread(s)
- JIT cache activity

Why this matters:

- state bugs can be amplified by timing
- a “correct” codegen change can still surface only under contention
- regressions may show up as hangs, livelocks, or status-register busy behavior rather than image corruption

Recommendation:

- preserve concurrency-aware validation in any deep Voodoo rework
- keep multi-threaded rendering and FIFO pressure in the test matrix

## Medium-Confidence or Structural Risk Areas

### 5. The interpreter/JIT contract is too implicit

Confidence: Medium
Impact: Long-term maintainability and hidden regressions

The JIT headers rely on outer-scope locals and macros from the enclosing interpreter function. The ARM64 header explicitly documents that many variables are not declared inside the header and are expected to exist in the including function.

This makes the code harder to reason about because:

- the interpreter is the semantic definition
- the JIT is a textual include with shared local context
- subtle local-variable or macro changes can alter code generation semantics

Recommendation:

- document the exact JIT contract more explicitly
- gradually move cache-key derivation and mode decoding into shared helpers
- keep the generated-code stages aligned with named interpreter stages

### 6. x86-64 JIT cache structure looks under-evolved compared with ARM64

Confidence: Medium
Impact: Performance and code complexity, not an urgent correctness issue

The ARM64 JIT uses a 32-entry LRU per partition. The x86-64 JIT still:

- allocates `BLOCK_NUM * 4`
- probes only 8 entries in its hot path
- rotates through 8 write positions

This suggests legacy technical debt:

- the cache shape is inconsistent across JITs
- memory allocation and actual cache behavior are out of sync
- x86-64 remains a behavioral reference despite older infrastructure

Recommendation:

- do not “improve” ARM64 by copying x86-64 cache strategy
- if x86-64 remains important, give it its own cleanup pass after correctness work

### 7. Toolchain policy needs to be pinned before ISA-level optimization work

Confidence: High
Impact: Architectural compliance risk

Current generic ARM64 flags are fine for the stated constraint, but `cmake/llvm-macos-aarch64.cmake` currently uses `-march=armv8.5-a+simd`.

If the project rule is “no code changes can target higher than armv8.0,” then:

- future ARM64 work should assume only ARMv8.0 instructions
- toolchain defaults should be audited so test builds do not accidentally validate newer instructions

Recommendation:

- standardize ARM64 policy before any micro-architecture tuning
- avoid introducing LSE, RCpc, or newer ISA assumptions
- keep x86-64 work within the current `x86-64 + SSE2` baseline

## Historical Commits That Matter for Future Work

These commits should shape any redesign or cleanup effort:

### Game- or scenario-driven fixes

- `f39c3491b`: Extreme Assault HUD transparency fix
- `601155fdd`: alpha planes and alpha mask support, with interpreter fallback for some x86-64 cases
- `2b3be4140`: x86-64 alpha-plane dynarec implementation
- `89ae64e53`: revert of texture precalc rework for Voodoo 1/2, fixing compatibility issue #1137

### JIT correctness fixes

- `5b4b486af`: x86-64 `MOV` vs `ADD`
- `74824bbac`: x86-64 `FOG_Z` shift correction
- `ad136e140`: ARM64 fog alpha and `FOG_Z` correction
- `be05fefe8`: ARM64 TMU1 negate ordering and depth-bias clamp correction
- `7aeaabeee`: dynarec jump patching fix

### Concurrency and runtime-behavior fixes

- `0922ad784`: ARM64 render-thread contention fix
- `ab16bbf5e`: move ARM64 `jit_generation[]` to per-instance state to avoid SLI/shared-state corruption
- `c1be59bcc`: texture cache dirty-range masking/wrap fix

General lesson:

Most “mysterious Voodoo bugs” in history have not come from large conceptual misunderstandings. They have come from:

- state aliasing
- ordering mistakes
- off-by-one or shift-count mistakes
- cache invalidation mistakes
- synchronization details

## Improvement Directions

### Immediate correctness work

1. Fix JIT cache key aliasing for `col_tiled` vs `aux_tiled`.
2. Define and implement full output-alpha blend behavior in the interpreter.
3. Port that exact alpha behavior into both JITs.
4. Add focused verify/debug scenarios for:
   - alpha planes
   - tiled color-only vs aux-only transitions
   - fog modes
   - dual-TMU and trilinear paths

### Medium-term code health work

1. Reduce duplication between interpreter mode decoding and JIT key derivation.
2. Build a clearer “render state fingerprint” helper used by all JITs.
3. Separate correctness-oriented commits from performance-oriented commits.
4. Keep ARM64 and x86-64 parity notes explicit when one backend is intentionally more correct than the other.

### Performance work that still fits the stated architecture limits

Safe directions under ARMv8.0 / current x86-64 baseline:

- reduce redundant state loads and stores
- reduce cache-key scanning cost
- tighten writeback/increment paths
- reduce branch patching complexity
- batch or defer low-value counters when debug/verify is off

Avoid for now:

- any optimization that changes blend/fog/depth ordering before parity is locked down
- any optimization that assumes ARMv8.1+ or x86-64 features above the current SSE2 baseline

## Suggested Validation Matrix

Any future interpreter/JIT work should be validated across:

- Voodoo 1
- Voodoo 2
- Banshee
- Voodoo 3
- single-TMU and dual-TMU paths
- point-sampled and bilinear paths
- fog off / fog Z / fog W / fog alpha
- alpha planes on and off
- tiled color only
- tiled aux only
- tiled color + aux
- SLI and non-SLI
- 1, 2, and 4 render-thread configurations where relevant

Known historically relevant scenarios:

- Extreme Assault HUD transparency
- Lands of Lore III alpha-buffer usage
- Voodoo 1/2 trilinear and texture-precalc-sensitive paths

## Recommended Game Matrix

This section maps practical regression targets to the kinds of Voodoo changes most likely to break them.

### Core benchmark and smoke tests

- `3DMark99`
  Use for: broad smoke test after any render-pipeline change
  Good at catching: obvious corruption, depth/fog mistakes, general JIT/interpreter divergence

- `3DMark2000`
  Use for: later-era broad regression pass after texture, blending, or threading changes
  Good at catching: wider state coverage than a single game, especially after optimization batches

### Transparency, alpha, and HUD-sensitive tests

- `Extreme Assault`
  Use for: any alpha-blend, transparency, color-before-fog, or HUD-related change
  Good at catching: known historical HUD transparency regressions
  Priority: mandatory after blend-path edits

- `Lands of Lore III`
  Use for: alpha-plane and aux-buffer behavior
  Good at catching: alpha-buffer read/write issues and incomplete alpha-path handling
  Priority: mandatory after `FBZ_ALPHA_ENABLE`, aux writeback, or output-alpha changes

- `Half-Life 1`
  Use for: real-world transparency and later Glide-era sanity
  Good at catching: blend-path regressions that do not show up in synthetic tests

- `Unreal Gold`
  Use for: fog, detail textures, multitexture, and world rendering
  Good at catching: fog-path mismatches, texture-combine mistakes, and dual-TMU regressions
  Priority: mandatory after fog or TMU-combine changes

### Texture, TMU, and trilinear tests

- `Turok demo`
  Use for: older Glide path and texture behavior
  Good at catching: early-era texture sampling or state bugs

- `Unreal Gold`
  Use for: dual-TMU, detail textures, and texture stage interactions
  Good at catching: TMU combine regressions, LOD issues, texture-state mismatches

- `Tomb Raider`
  Use for: early Voodoo 1-era texture and transparency sanity
  Good at catching: classic compatibility regressions that newer engines may hide

- `Quake 3`
  Use for: later-era texture and frame-to-frame sanity
  Good at catching: broad rendering breakage, though it is less Voodoo-specific than Unreal or early Glide titles

### Fog and depth tests

- `Unreal Gold`
  Use for: fog-heavy scenes and general depth stability
  Good at catching: `FOG_Z`, `FOG_W`, and texture/fog ordering mistakes

- `Need for Speed II: SE` or `Need for Speed III`
  Use for: racing scenes with distance fog and fast scene changes
  Good at catching: fog, depth bias, and pacing-sensitive visual issues

- `3DMark99` and `3DMark2000`
  Use for: depth/fog sanity after low-level arithmetic changes
  Good at catching: wrong shifts, clamp mistakes, and sign bugs

### Tiled-buffer, framebuffer, and aux-buffer tests

- `Lands of Lore III`
  Use for: aux-buffer and alpha-plane paths
  Good at catching: wrong aux addressing and alpha write/read behavior

- `Unreal Gold`
  Use for: sustained real-game rendering after framebuffer-path changes
  Good at catching: corruption that only appears after multiple state transitions

- `3DMark99` / `3DMark2000`
  Use for: state churn after changes to JIT cache keys, tiled addressing, or writeback paths
  Good at catching: aliasing bugs such as wrong reuse of compiled blocks

### Multi-threading and contention tests

- `3DMark2000`
  Use for: repeated runs with different render-thread counts
  Good at catching: contention-related hangs, timing-sensitive corruption, and JIT cache churn

- `Unreal Gold`
  Use for: longer gameplay/session stability checks with different render-thread settings
  Good at catching: state timing issues that short benchmarks miss

### Suggested per-change checklist

- After alpha/blend changes:
  `Extreme Assault`, `Lands of Lore III`, `Half-Life 1`, `Unreal Gold`

- After fog/depth changes:
  `Unreal Gold`, `Need for Speed II: SE` or `III`, `3DMark99`, `3DMark2000`

- After texture/TMU/trilinear changes:
  `Unreal Gold`, `Turok demo`, `Tomb Raider`, `3DMark99`

- After framebuffer/aux/tiled/JIT-cache-key changes:
  `Lands of Lore III`, `Unreal Gold`, `3DMark99`, `3DMark2000`

- After render-thread/JIT-cache/concurrency changes:
  `3DMark2000`, `Unreal Gold`, plus at least one long-ish session in a real game

### Priority order from your current collection

If time is limited, the most valuable recurring set from what you already have is:

1. `Extreme Assault`
2. `Lands of Lore III`
3. `Unreal Gold`
4. `3DMark99`
5. `3DMark2000`
6. `Half-Life 1`
7. `Turok demo`

Then rotate in newly acquired titles by subsystem:

- add `Tomb Raider` for early Voodoo compatibility
- add `Need for Speed II: SE` or `III` for fog/depth/racing coverage
- add `Carmageddon 2` for odd real-game blend/texture behavior

## Bottom Line

The current Voodoo implementation is not suffering from one obvious ARM64-only disaster. The more important picture is:

- the interpreter is still the true behavioral spec
- the JITs are accurate enough to be useful, but still structurally fragile
- the most actionable current bug is tiled-mode cache-key aliasing
- the most important unfinished behavior is output-alpha blending
- the git history shows that compatibility often turns on tiny details, not broad architecture

That means the safest path forward is:

1. lock down correctness holes first
2. preserve historical game-driven fixes
3. only then do targeted performance work within ARMv8.0 and current x86-64 limits
