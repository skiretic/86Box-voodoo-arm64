# Voodoo Gap-Closure Executive Summary

Date: 2026-03-13
Branch: `voodoo-dev`
Current head: `19b611125`
Plan: `docs/plans/2026-03-12-voodoo-gap-and-bug-closure.md`

## Executive Summary

Implementation work for the scoped gap-closure plan is complete, and Task 8 now records the final verification and handoff state.

- all 8 planned tasks are complete for the scoped gap-closure work
- correctness work now covers the interpreter, x86-64 JIT, and ARM64 JIT output-alpha path
- regression checklist docs are now in place for the main fragile scenarios
- fresh ARM64 build verification still passes, and the signed release build restored expected performance in manual use, but broader runtime regression evidence is still incomplete

The safest interpretation of current status is:

- structural correctness risk from tiled-mode cache-key aliasing is reduced
- interpreter/JIT output-alpha parity is committed on `voodoo-dev`
- compatibility evidence is improved by the signed-release sanity pass, but still incomplete until broader game coverage is recorded

## Progress Charts

Task progress:

```text
Completed  [########] 8/8 100%
Pending    [--------] 0/8   0%
```

Implementation phases:

```text
Docs / scope lock-in           [##########] 100%
JIT cache-key correctness      [##########] 100%
Interpreter alpha correctness  [##########] 100%
x86-64 JIT alpha parity        [##########] 100%
ARM64 JIT alpha parity         [##########] 100%
Regression checklist docs      [##########] 100%
Final verification / handoff   [##########] 100%
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

### 5. x86-64 JIT output-alpha parity committed

Outcome:

- replaced the old `AFUNC_AONE`-only x86-64 alpha writeback block
- mirrored the interpreter factor set in the x86-64 JIT output-alpha path
- kept the change within the existing SSE2/x86-64 baseline

File changed:

- `src/include/86box/vid_voodoo_codegen_x86-64.h`

Commit coverage:

- `19b611125` `fix: complete voodoo jit output-alpha parity`

### 6. ARM64 JIT output-alpha parity committed

Outcome:

- replaced the old `AFUNC_AONE`-only ARM64 alpha writeback block
- mirrored the interpreter factor set in scalar ARM64 code using ARMv8.0-safe integer multiply/divide
- kept the new writeback result in the existing `w12` output-alpha register path

File changed:

- `src/include/86box/vid_voodoo_codegen_arm64.h`

Commit coverage:

- `19b611125` `fix: complete voodoo jit output-alpha parity`

### 7. Regression checklist docs refreshed

Outcome:

- added scenario-based manual regression checklists to the findings doc
- refreshed the ARM64 testing guide with a focused checklist for the current output-alpha and JIT work

Files changed:

- `docs/voodoo-deep-dive-findings-2026-03-12.md`
- `voodoo-arm64-port/TESTING-GUIDE.md`

Commit coverage:

- `3485dda0c` `docs: record voodoo progress and bug candidates`

### 8. Final verification and handoff recorded

Outcome:

- re-ran the final ARM64 debug build verification from the current source state
- recorded the signed-release sanity evidence separately from broader manual coverage
- left the remaining manual game gaps and x86-64 runtime caveat explicit in the handoff docs

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

- ported output-alpha factor handling to the x86-64 JIT
- ported the same behavior to the ARM64 JIT
- refreshed the manual regression checklist docs ahead of fresh-build testing

### 2026-03-13 - Signed release validation

- rebuilt the release app with `scripts/setup-and-build.sh build`
- confirmed the app was re-signed with JIT entitlements
- manual user validation reported normal performance after launching the signed release app instead of the debug app

### 2026-03-13 - Final Task 8 verification

- re-ran `cmake --build out/build/llvm-macos-aarch64-debug`
- observed `ninja: no work to do.`
- finalized the handoff with build evidence, signed-release sanity evidence, remaining manual game gaps, and the x86-64 runtime caveat split out explicitly

## Verification Log

Completed:

- `cmake --preset llvm-macos-aarch64-debug`
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 2
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 4
- x86-64 syntax-only verification of `src/video/vid_voodoo_render.c` after the Task 5 local change
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 6
- final Task 8 build verification: `cmake --build out/build/llvm-macos-aarch64-debug`
- clean rebuild from an empty `out/build/llvm-macos-aarch64-debug`
- clean release build plus codesign via `scripts/setup-and-build.sh build`
- `3DMark99` full demo loop smoke pass
- `3DMark2000` full demo loop smoke pass
- signed ARM64 release app launched with expected performance according to manual user validation

Observed build notes:

- no new warnings were introduced in the touched Voodoo files during the successful builds
- unrelated existing warnings remain in the broader project, including macOS/Homebrew link-target warnings

Final handoff status:

- build verification: fresh ARM64 debug build check succeeded on current source state (`ninja: no work to do.`)
- ARM64 signed-release sanity evidence: prior signed release rebuild plus manual sanity check reported expected performance
- remaining missing manual game coverage: `Extreme Assault`, `Lands of Lore III`, `Unreal Gold`, and any extended optional sweep remain unrecorded after the parity commits
- remaining x86-64 runtime caveat: no x86-64 live runtime verification is available in this workspace

Not yet completed:

- `Unreal Gold`
- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`
- full x86-64 runtime validation of the committed JIT parity change
- refreshed ARM64 runtime/manual coverage beyond the initial signed-release sanity check and `3DMark99` / `3DMark2000` smoke loops

## Open Risks

- The new interpreter alpha-factor coverage is broader than the historically exercised `AONE`-only path, so runtime validation is still required.
- manual regression evidence is still incomplete for the refreshed JIT output-alpha paths
- broader manual runtime coverage is still pending for the parity changes

Related note:

- see `docs/2026-03-12-voodoo-known-bug-candidates.md` for the current ranked shortlist of likely remaining defects
- ARM64 is the active validation environment for follow-up work; x86-64 runtime testing is currently unavailable in this workspace

## Recommended Next Step

Recommended next step is outside this scoped plan: when an ARM64/manual test window is available, add dedicated runs for `Extreme Assault`, `Lands of Lore III`, and `Unreal Gold`, then pursue x86-64 live validation on a suitable host.
