# Voodoo Gap And Bug Closure Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Close the highest-value correctness gaps in the Voodoo renderer and JITs without exceeding ARMv8.0 on ARM64 or the current x86-64 SSE2 baseline.

**Architecture:** Treat the C interpreter as the semantic source of truth, fix correctness there first when behavior is genuinely incomplete, then port the exact behavior into the JIT backends. Use focused verification runs and historically relevant game regressions after each subsystem change instead of trying to land broad multi-path rewrites at once.

**Tech Stack:** C, CMake, 86Box Voodoo interpreter, x86-64 JIT, ARM64 JIT, manual regression testing with 3DMark and Glide-era games

## Scope Assumptions

This plan is specifically for Voodoo emulation gaps and bugs, not the emulator as a whole. It is driven by:

- `docs/voodoo-deep-dive-findings-2026-03-12.md`
- `voodoo-arm64-port/ARM64-CODEGEN-TECHNICAL.md`
- `voodoo-arm64-port/TESTING-GUIDE.md`

## Preconditions

- Work on branch: `voodoo-dev`
- Keep ARM64 code within ARMv8.0 instruction assumptions
- Keep x86-64 code within current `-march=x86-64 -msse2`
- Do not merge performance changes with correctness changes in the same commit unless the performance change is mechanically required by the fix

## Working Task Notes

First-wave implementation scope is intentionally limited to:

- JIT cache-key split for `col_tiled` vs `aux_tiled`
- output-alpha blend completion
- targeted regression coverage for historically fragile paths

Non-goals for this phase:

- no new ISA requirements above ARMv8.0
- no opportunistic JIT optimization batches
- no broad renderer refactor in the same phase

## Current Execution Status

Status date: 2026-03-13
Branch head: `cf16e67c3` plus local uncommitted Task 5-8 notes

Completed tasks:

- Task 1 `docs: define voodoo gap-closure scope` (`70c57a2d7`)
- Task 2 `fix: split voodoo jit tiled-mode cache keys` (`cfdda4cae`)
- Task 3 `docs: capture intended voodoo output-alpha behavior` (`b31534ea2`)
- Task 4 `fix: complete voodoo interpreter output-alpha blending` (`cf16e67c3`)
- Task 5 `fix: match voodoo output-alpha blending in x64 jit` (implemented locally, uncommitted)
- Task 6 `fix: match voodoo output-alpha blending in arm64 jit` (implemented locally, uncommitted)
- Task 7 `docs: add voodoo regression checklist by scenario` (implemented locally, uncommitted)

Pending tasks:

- Task 8 final verification and handoff

Verification completed so far:

- `cmake --preset llvm-macos-aarch64-debug`
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 2
- `cmake --build out/build/llvm-macos-aarch64-debug` after Task 4
- clean rebuild: `rm -rf out/build/llvm-macos-aarch64-debug && cmake --preset llvm-macos-aarch64-debug && cmake --build out/build/llvm-macos-aarch64-debug`
- x86-64 syntax-only verification for `src/video/vid_voodoo_render.c` with `-target x86_64-apple-macos10.13 -fsyntax-only`
- ARM64 debug build after Task 6: `cmake --build out/build/llvm-macos-aarch64-debug`
- clean release build and codesign with JIT entitlements via `scripts/setup-and-build.sh build`
- manual smoke pass: `3DMark99` full demo loop appeared stable
- manual smoke pass: `3DMark2000` full demo loop appeared stable
- manual ARM64 signed release validation: user reported testing/performance restored after launching the signed release app instead of the debug build

Verification still pending:

- deeper interactive/manual regressions for `Extreme Assault`, `Lands of Lore III`, `Unreal Gold`, `3DMark99`, and `3DMark2000` beyond the initial signed-release sanity pass
- x86-64 full build/runtime verification for the Task 5 JIT work
- final commit/handoff notes for Task 8

Environment note:

- ARM64 is the active validation environment in this workspace
- x86-64 runtime testing is currently unavailable, so Task 5 should not block on x86-64 live execution evidence
- local branch state currently includes uncommitted Task 5-7 code/doc changes plus Task 8 status updates

## Test Inventory For This Plan

Primary regression set:

- `Extreme Assault`
- `Lands of Lore III`
- `Unreal Gold`
- `3DMark99`
- `3DMark2000`
- `Half-Life 1`
- `Turok demo`

Secondary set:

- `Tomb Raider`
- `Need for Speed II: SE` or `Need for Speed III`
- `Carmageddon 2`

## Task 1: Lock Down The Bug List And Non-Goals

**Files:**
- Modify: `docs/voodoo-deep-dive-findings-2026-03-12.md`
- Create: `docs/plans/2026-03-12-voodoo-gap-and-bug-closure.md`

**Step 1: Re-read the deep-dive findings and extract the first-wave fixes**

Confirm the first-wave implementation scope is exactly:

- JIT cache-key split for `col_tiled` vs `aux_tiled`
- output-alpha blend completion
- targeted regression coverage for historically fragile paths

**Step 2: Explicitly list non-goals in the plan**

Add a short note to the working task notes:

- no new ISA requirements above ARMv8.0
- no opportunistic JIT optimization batches
- no broad renderer refactor in the same phase

**Step 3: Commit**

```bash
git add docs/voodoo-deep-dive-findings-2026-03-12.md docs/plans/2026-03-12-voodoo-gap-and-bug-closure.md
git commit -m "docs: define voodoo gap-closure scope"
```

## Task 2: Fix JIT Cache-Key Aliasing For Tiled Modes

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`
- Modify: `src/include/86box/vid_voodoo_codegen_x86-64.h`
- Modify: `src/include/86box/vid_voodoo_common.h`

**Step 1: Add separate cache-key fields**

Add explicit key fields for:

- `col_tiled`
- `aux_tiled`

Example shape:

```c
int col_tiled;
int aux_tiled;
```

**Step 2: Write the fields during cache-key population**

Replace:

```c
data->is_tiled = (params->col_tiled || params->aux_tiled) ? 1 : 0;
```

with:

```c
data->col_tiled = params->col_tiled ? 1 : 0;
data->aux_tiled = params->aux_tiled ? 1 : 0;
```

**Step 3: Update cache lookup comparison**

Replace the single tiled comparison with two exact comparisons:

```c
&& (params->col_tiled ? 1 : 0) == data->col_tiled
&& (params->aux_tiled ? 1 : 0) == data->aux_tiled
```

**Step 4: Build the project**

Run:

```bash
cmake --preset llvm-macos-aarch64-debug
cmake --build out/build/llvm-macos-aarch64-debug
```

Expected:

- configure succeeds
- build succeeds with no new warnings in touched files

**Step 5: Run targeted manual regression**

Run:

- `3DMark99`
- `3DMark2000`
- `Unreal Gold`
- `Lands of Lore III`

Focus:

- no corruption after repeated scene/state transitions
- no obvious aux/depth/alpha buffer misaddressing

**Step 6: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h src/include/86box/vid_voodoo_codegen_x86-64.h src/include/86box/vid_voodoo_common.h
git commit -m "fix: split voodoo jit tiled-mode cache keys"
```

## Task 3: Document Intended Output-Alpha Behavior Before Changing Code

**Files:**
- Modify: `docs/voodoo-deep-dive-findings-2026-03-12.md`
- Modify: `src/include/86box/vid_voodoo_render.h`

**Step 1: Audit `src_aafunc` and `dest_aafunc` handling**

Read the existing logic in:

- `src/include/86box/vid_voodoo_render.h`
- `src/include/86box/vid_voodoo_codegen_x86-64.h`
- `src/include/86box/vid_voodoo_codegen_arm64.h`

and document the currently implemented cases.

**Step 2: Add a short design note near the interpreter blend path**

Add a comment block describing:

- expected role of output-alpha blend factors
- current incomplete coverage
- interpreter-first rule for future parity

**Step 3: Commit**

```bash
git add docs/voodoo-deep-dive-findings-2026-03-12.md src/include/86box/vid_voodoo_render.h
git commit -m "docs: capture intended voodoo output-alpha behavior"
```

## Task 4: Complete Output-Alpha Blending In The Interpreter

**Files:**
- Modify: `src/include/86box/vid_voodoo_render.h`
- Modify: `src/video/vid_voodoo_render.c`
- Modify: `src/video/vid_voodoo_fb.c`

**Step 1: Replace the reduced alpha writeback logic**

Replace:

```c
src_a = (((dest_aafunc == 4) ? dest_a * 256 : 0) + ((src_aafunc == 4) ? src_a * 256 : 0)) >> 8;
```

with a helper or macro that handles the full supported factor set explicitly.

Suggested shape:

```c
static __inline uint8_t
voodoo_blend_output_alpha(...)
{
    ...
}
```

**Step 2: Reuse the same helper in both triangle and LFB write paths**

Make sure:

- triangle renderer uses the helper
- LFB write path uses the helper if alpha-buffer writes are active

**Step 3: Build**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
```

Expected:

- build succeeds

**Step 4: Manual regression**

Run:

- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`

Focus:

- no regression in transparency/HUD behavior
- alpha-buffer scenes remain stable

**Step 5: Commit**

```bash
git add src/include/86box/vid_voodoo_render.h src/video/vid_voodoo_render.c src/video/vid_voodoo_fb.c
git commit -m "fix: complete voodoo interpreter output-alpha blending"
```

## Task 5: Port Output-Alpha Behavior To x86-64 JIT

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_x86-64.h`

**Step 1: Mirror the interpreter’s completed output-alpha behavior**

Update the x86-64 alpha-channel blend section so it no longer only handles the `AONE` cases.

**Step 2: Keep instruction set within current baseline**

Do not introduce instructions beyond the current x86-64/SSE2 assumptions.

**Step 3: Build**

Run an x86-64 configuration if available in your environment; otherwise build only for syntax/integration confidence where possible.

Suggested commands:

```bash
cmake --preset regular
cmake --build build/regular
```

If local x86-64 build is not available, note that explicitly in the session handoff.

**Step 4: Manual regression**

Run:

- `Extreme Assault`
- `Lands of Lore III`
- `Unreal Gold`

**Step 5: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_x86-64.h
git commit -m "fix: match voodoo output-alpha blending in x64 jit"
```

## Task 6: Port Output-Alpha Behavior To ARM64 JIT

**Files:**
- Modify: `src/include/86box/vid_voodoo_codegen_arm64.h`

**Step 1: Mirror the interpreter exactly**

Update the ARM64 alpha-channel blend section so the generated code matches the interpreter’s completed output-alpha semantics.

**Step 2: Preserve ARMv8.0 compatibility**

Use only instructions already compatible with ARMv8.0. Do not add newer ISA features.

**Step 3: Build**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
```

**Step 4: Manual regression**

Run:

- `Extreme Assault`
- `Lands of Lore III`
- `Unreal Gold`
- `3DMark99`
- `3DMark2000`

Focus:

- blend correctness
- no ARM64-only corruption
- no new hangs under repeated runs

**Step 5: Commit**

```bash
git add src/include/86box/vid_voodoo_codegen_arm64.h
git commit -m "fix: match voodoo output-alpha blending in arm64 jit"
```

## Task 7: Add A Repeatable Manual Regression Checklist

**Files:**
- Modify: `docs/voodoo-deep-dive-findings-2026-03-12.md`
- Modify: `voodoo-arm64-port/TESTING-GUIDE.md`

**Step 1: Add scenario-specific checklists**

Add short checklists for:

- alpha/blend changes
- fog/depth changes
- texture/TMU changes
- tiled-buffer/JIT-cache changes
- render-thread/concurrency changes

**Step 2: Record expected games per scenario**

Use the existing matrix and normalize it into a concise run list.

**Step 3: Commit**

```bash
git add docs/voodoo-deep-dive-findings-2026-03-12.md voodoo-arm64-port/TESTING-GUIDE.md
git commit -m "docs: add voodoo regression checklist by scenario"
```

## Task 8: Verification And Final Handoff

**Files:**
- Modify: `docs/voodoo-deep-dive-findings-2026-03-12.md`

**Step 1: Run final build verification**

Run:

```bash
cmake --build out/build/llvm-macos-aarch64-debug
```

Expected:

- successful build from clean current source state

**Step 2: Run final manual regression sweep**

Minimum final sweep:

- `Extreme Assault`
- `Lands of Lore III`
- `Unreal Gold`
- `3DMark99`
- `3DMark2000`

Optional extended sweep:

- `Half-Life 1`
- `Turok demo`
- `Tomb Raider`
- `Need for Speed II: SE` or `III`
- `Carmageddon 2`

**Step 3: Update findings doc with results**

Record:

- what was fixed
- what remains open
- what games were run
- any residual risks

**Step 4: Commit**

```bash
git add docs/voodoo-deep-dive-findings-2026-03-12.md
git commit -m "docs: record voodoo gap-closure verification results"
```

## Notes For Execution

- Use small commits exactly as outlined above.
- If any regression shows up in `Extreme Assault` or `Lands of Lore III`, stop and debug before continuing to the next task.
- If a JIT path becomes questionable, prefer temporary interpreter fallback over speculative codegen fixes.
- Do not bundle optimization work into this plan. Write a separate optimization plan afterward.
