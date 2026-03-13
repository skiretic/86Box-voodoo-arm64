# Voodoo Gap-Closure Executive Summary

Date: 2026-03-13
Branch: `voodoo-dev`
Current head: `cf16e67c3` plus local uncommitted Task 5-7 changes
Plan: `docs/plans/2026-03-12-voodoo-gap-and-bug-closure.md`

## Executive Summary

Work is still in progress, but local implementation now extends through the JIT parity and checklist-documentation phase.

- 7 of 8 planned tasks are complete in the local working tree
- correctness work now covers the interpreter, x86-64 JIT, and ARM64 JIT output-alpha path
- regression checklist docs are now in place for the main fragile scenarios
- fresh ARM64 builds succeed, and the signed release build restored expected performance in manual use, but broader runtime regression evidence is still incomplete

The safest interpretation of current status is:

- structural correctness risk from tiled-mode cache-key aliasing is reduced
- interpreter/JIT output-alpha parity is implemented locally
- compatibility evidence is improved by the signed-release sanity pass, but still incomplete until broader game coverage is recorded

## Progress Charts

Task progress:

```text
Completed  [#######-] 7/8  87%
Pending    [-------#] 1/8  13%
```

Implementation phases:

```text
Docs / scope lock-in           [##########] 100%
JIT cache-key correctness      [##########] 100%
Interpreter alpha correctness  [##########] 100%
x86-64 JIT alpha parity        [##########] 100%
ARM64 JIT alpha parity         [##########] 100%
Regression checklist docs      [##########] 100%
Final verification / handoff   [----------]   0%
```

Verification progress:

```text
Configure/build checks         [##########] 100%
Interactive regression runs    [####------]  40%
Cross-backend parity checks    [######----]  60%
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

### 5. x86-64 JIT output-alpha parity implemented locally

Outcome:

- replaced the old `AFUNC_AONE`-only x86-64 alpha writeback block
- mirrored the interpreter factor set in the x86-64 JIT output-alpha path
- kept the change within the existing SSE2/x86-64 baseline

File changed:

- `src/include/86box/vid_voodoo_codegen_x86-64.h`

### 6. ARM64 JIT output-alpha parity implemented locally

Outcome:

- replaced the old `AFUNC_AONE`-only ARM64 alpha writeback block
- mirrored the interpreter factor set in scalar ARM64 code using ARMv8.0-safe integer multiply/divide
- kept the new writeback result in the existing `w12` output-alpha register path

File changed:

- `src/include/86box/vid_voodoo_codegen_arm64.h`

### 7. Regression checklist docs refreshed locally

Outcome:

- added scenario-based manual regression checklists to the findings doc
- refreshed the ARM64 testing guide with a focused checklist for the current output-alpha and JIT work

Files changed:

- `docs/voodoo-deep-dive-findings-2026-03-12.md`
- `voodoo-arm64-port/TESTING-GUIDE.md`

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

### 2026-03-13 - JIT parity and doc refresh

- ported output-alpha factor handling to the x86-64 JIT in the local working tree
- ported the same behavior to the ARM64 JIT in the local working tree
- refreshed the manual regression checklist docs ahead of fresh-build testing

### 2026-03-13 - Signed release validation

- rebuilt the release app with `scripts/setup-and-build.sh build`
- confirmed the app was re-signed with JIT entitlements
- manual user validation reported normal performance after launching the signed release app instead of the debug app

## Verification Log

Completed:

- `cmake --preset llvm-macos-aarch64-debug`
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 2
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 4
- x86-64 syntax-only verification of `src/video/vid_voodoo_render.c` after the Task 5 local change
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 6
- clean rebuild from an empty `out/build/llvm-macos-aarch64-debug`
- clean release build plus codesign via `scripts/setup-and-build.sh build`
- `3DMark99` full demo loop smoke pass
- `3DMark2000` full demo loop smoke pass
- signed ARM64 release app launched with expected performance according to manual user validation

Observed build notes:

- no new warnings were introduced in the touched Voodoo files during the successful builds
- unrelated existing warnings remain in the broader project, including macOS/Homebrew link-target warnings

Not yet completed:

- `Unreal Gold`
- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`
- full x86-64 build/runtime validation of the local Task 5 change
- refreshed ARM64 runtime/manual coverage for the local Task 6 change beyond the initial signed-release sanity check

## Open Risks

- The new interpreter alpha-factor coverage is broader than the historically exercised `AONE`-only path, so runtime validation is still required.
- manual regression evidence is still incomplete for the refreshed JIT output-alpha paths
- the local Task 5 and Task 6 changes are still uncommitted while awaiting validation

Related note:

- see `docs/2026-03-12-voodoo-known-bug-candidates.md` for the current ranked shortlist of likely remaining defects
- ARM64 is the active validation environment for follow-up work; x86-64 runtime testing is currently unavailable in this workspace

## Recommended Next Step

Use the fresh ARM64 rebuild as the validation candidate, then finish Task 8:

1. record the signed-release sanity result plus any additional game-by-game notes
2. commit the Task 5-7 work and the doc refresh if no new issues appear
3. finalize the handoff with remaining x86-64 caveats called out explicitly
