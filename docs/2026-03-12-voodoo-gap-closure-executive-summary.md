# Voodoo Gap-Closure Executive Summary

Date: 2026-03-12
Branch: `voodoo-dev`
Current head: `cf16e67c3`
Plan: `docs/plans/2026-03-12-voodoo-gap-and-bug-closure.md`

## Executive Summary

Work is partially complete and currently stopped after the interpreter-side correctness phase.

- 4 of 8 planned tasks are complete
- correctness work landed for the JIT tiled-mode cache key and the interpreter/LFB output-alpha path
- JIT parity work for output-alpha is still pending on both x86-64 and ARM64
- clean rebuild succeeded and `3DMark99` / `3DMark2000` smoke loops looked stable

The safest interpretation of current status is:

- structural correctness risk from tiled-mode cache-key aliasing is reduced
- interpreter behavior for output-alpha is more complete than before
- backend parity and compatibility evidence are still incomplete

## Progress Charts

Task progress:

```text
Completed  [####----] 4/8  50%
Pending    [----####] 4/8  50%
```

Implementation phases:

```text
Docs / scope lock-in           [##########] 100%
JIT cache-key correctness      [##########] 100%
Interpreter alpha correctness  [##########] 100%
x86-64 JIT alpha parity        [----------]   0%
ARM64 JIT alpha parity         [----------]   0%
Regression checklist docs      [----------]   0%
Final verification / handoff   [----------]   0%
```

Verification progress:

```text
Configure/build checks         [########--]  80%
Interactive regression runs    [###-------]  30%
Cross-backend parity checks    [##--------]  20%
```

## Completed Work

### 1. Scope and non-goals locked down

Commit: `70c57a2d7` `docs: define voodoo gap-closure scope`

Outcome:

- created the working implementation plan
- created the deep-dive findings document
- made first-wave scope and non-goals explicit

### 2. JIT tiled-mode cache-key aliasing fixed

Commit: `cfdda4cae` `fix: split voodoo jit tiled-mode cache keys`

Outcome:

- split x86-64 JIT cache keys into exact `col_tiled` and `aux_tiled` bits
- split ARM64 JIT cache keys into exact `col_tiled` and `aux_tiled` bits
- removed the previous aliasing caused by a single combined tiled flag

Files changed:

- `src/include/86box/vid_voodoo_codegen_x86-64.h`
- `src/include/86box/vid_voodoo_codegen_arm64.h`

### 3. Output-alpha intent documented

Commit: `b31534ea2` `docs: capture intended voodoo output-alpha behavior`

Outcome:

- documented that RGB blending already covers a broader AFUNC set
- documented that final alpha writeback had only been meaningfully handling `AFUNC_AONE`
- added an interpreter-first design note near the shared render macros

### 4. Interpreter output-alpha blending completed

Commit: `cf16e67c3` `fix: complete voodoo interpreter output-alpha blending`

Outcome:

- added shared helper logic for output-alpha factor evaluation
- switched triangle rendering to use the shared helper
- switched LFB writes to use the same helper
- wired alpha-plane aux writes through the updated LFB path when alpha writes are active

Files changed:

- `src/include/86box/vid_voodoo_render.h`
- `src/video/vid_voodoo_render.c`
- `src/video/vid_voodoo_fb.c`

## Changelog

### 2026-03-12 21:49 - Scope lock-in

- added the saved execution plan
- added the deep-dive findings doc
- recorded first-wave scope and non-goals

### 2026-03-12 21:53 - Cache-key fix

- replaced the combined tiled cache-key bit in both JITs
- now distinguish color-tiling from aux-tiling during cache lookup and key storage

### 2026-03-12 21:55 - Alpha behavior documentation

- recorded the current output-alpha gap in the findings doc
- added the interpreter-first design note for future parity work

### 2026-03-12 22:01 - Interpreter alpha implementation

- replaced the old `AONE`-only alpha writeback path
- introduced shared output-alpha helper logic
- reused that logic in triangle and LFB write paths

## Verification Log

Completed:

- `cmake --preset llvm-macos-aarch64-debug`
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 2
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 4
- clean rebuild from an empty `out/build/llvm-macos-aarch64-debug`
- `3DMark99` full demo loop smoke pass
- `3DMark2000` full demo loop smoke pass

Observed build notes:

- no new warnings were introduced in the touched Voodoo files during the successful builds
- unrelated existing warnings remain in the broader project, including macOS/Homebrew link-target warnings

Not yet completed:

- `Unreal Gold`
- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`
- any x86-64 build for upcoming Task 5 work

## Open Risks

- The new interpreter alpha-factor coverage is broader than the historically exercised `AONE`-only path, so runtime validation is still required.
- JIT parity is incomplete until Tasks 5 and 6 land.
- Manual regression evidence is still missing for the historically fragile games and scenes listed in the plan.

Related note:

- see `docs/2026-03-12-voodoo-known-bug-candidates.md` for the current ranked shortlist of likely remaining defects
- ARM64 is the active validation environment for follow-up work; x86-64 runtime testing is currently unavailable in this workspace

## Recommended Next Step

Resume at Task 5 and stop after a single checkpoint:

1. implement x86-64 JIT output-alpha parity
2. build only the required target(s)
3. record status before moving to ARM64 parity
