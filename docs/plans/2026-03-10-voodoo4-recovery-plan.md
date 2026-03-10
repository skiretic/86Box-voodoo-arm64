# Voodoo 4 Recovery Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bring up a clean Voodoo 4 4500 implementation from `master` that reaches BIOS POST and stable VGA/VBE framebuffer output without speculative hacks, while preserving a path to later VSA-100 3D work.

**Architecture:** Start from a fresh branch and treat the existing `voodoo4` branch as research only. Add a thin standalone Voodoo 4 wrapper that owns PCI/ROM identity and only confirmed VSA-100-specific deltas, while reusing proven Banshee/Voodoo3 display and common code until runtime evidence forces divergence.

**Tech Stack:** C, CMake, 86Box video/PCI/SVGA device infrastructure, local ROM-based boot verification.

## Summary

The previous branch proved that a full standalone `vid_voodoo4.c` can quickly accumulate duplicated logic, diagnostics, and state-forcing hacks before the hardware model is trustworthy. This restart narrows the first milestone to one job: get a Voodoo 4 device to POST and present correct VGA/VBE output using only verified behavior.

The clean implementation should not modify shared render, texture, framebuffer, or display subsystems during bring-up unless a reproduced incompatibility proves the existing Banshee/Voodoo3 path is wrong for VSA-100. The current `voodoo4` branch remains useful as a comparison artifact for ROM choice, observed probe activity, and possible register candidates, but not as code to merge forward.

## Important Interfaces

- Add one new video device entry for Voodoo 4 4500 AGP.
- Add one new driver source file for the Voodoo 4 wrapper layer.
- Add one extern declaration in the global video device list.
- Do not add shared-core `VOODOO_4` behavior flags, 32-bit framebuffer ownership changes, or Voodoo 5 abstractions in this first milestone unless validation proves they are necessary for POST/VBE.

## Task 1: Establish the clean baseline and capture the reusable surface

**Files:**
- Review: `src/video/vid_voodoo_banshee.c`
- Review: `src/include/86box/vid_voodoo_common.h`
- Review: `src/video/vid_table.c`
- Review: `src/include/86box/video.h`
- Reference only: `voodoo-arm64-port/VOODOO4-PLAN.md`
- Reference only: `voodoo-arm64-port/VOODOO4-SESSION-HANDOFF.md`

**Step 1: Identify the minimum code surface needed for a new Voodoo 4 device**

Document which parts are strictly device identity and setup:
- PCI vendor/device/revision responses
- ROM loading and availability
- BAR exposure and bus type
- device registration and picker entry

**Step 2: Record the current evidence boundaries**

Carry forward only facts already validated by local branch inspection:
- the Voodoo 4 ROM asset is needed
- the new device should identify as VSA-100
- the old branch relies on forced display/VGA state and should not be copied forward as implementation

**Step 3: Define explicit non-goals for milestone 1**

Out of scope:
- 32-bit framebuffer rework
- new render pipeline branches
- stencil or FSAA
- Voodoo 5 multi-chip support

**Step 4: Commit**

```bash
git add docs/plans/2026-03-10-voodoo4-recovery-plan.md
git commit -m "docs: add voodoo4 recovery plan"
```

## Task 2: Add the minimal Voodoo 4 device shell

**Files:**
- Create: `src/video/vid_voodoo4.c`
- Modify: `src/video/CMakeLists.txt`
- Modify: `src/video/vid_table.c`
- Modify: `src/include/86box/video.h`

**Step 1: Write the smallest possible device registration implementation**

The new file should:
- expose `voodoo_4_4500_agp_device`
- load the Voodoo 4 ROM if present
- register on AGP
- return correct PCI identity for VSA-100
- reuse existing common initialization patterns instead of cloning full Banshee logic

**Step 2: Reuse Banshee/Voodoo3 setup wherever behavior is not yet proven different**

The wrapper should delegate or mirror only the established setup flow for:
- SVGA initialization
- memory mappings
- common Voodoo core setup
- standard display timing behavior

**Step 3: Keep the file intentionally thin**

Do not port forward:
- broad diagnostic logging
- custom scanout overrides
- forced VGA register writes
- display address redirection hacks

**Step 4: Build and verify the repo still compiles**

Use the project’s normal build command and confirm the new device is selectable.

**Step 5: Commit**

```bash
git add src/video/vid_voodoo4.c src/video/CMakeLists.txt src/video/vid_table.c src/include/86box/video.h
git commit -m "video: add minimal voodoo4 device shell"
```

## Task 3: Add evidence-first bring-up instrumentation

**Files:**
- Modify: `src/video/vid_voodoo4.c`

**Step 1: Add focused temporary logging only at Voodoo 4-specific boundaries**

Allowed instrumentation:
- PCI config reads/writes
- ROM enable and BAR programming
- init/MMIO register accesses whose values are needed to understand VSA-100-specific probe behavior

Avoid logging shared SVGA internals unless a specific failure points there.

**Step 2: Reproduce BIOS bring-up and classify each failure**

For every failed boot or black-screen issue, answer in this order:
1. Is the wrapper miswired while existing Banshee/V3 behavior would already be correct?
2. Is a register interaction clearly different for VSA-100?
3. Is the current theory based only on branch docs and not on reproduced evidence?

**Step 3: Only add VSA-100-specific behavior when the observed evidence demands it**

A behavior change is allowed only when both are true:
- it is tied to a reproduced local runtime observation
- it is either source-backed or demonstrably required for correct ROM/guest progression

**Step 4: Commit**

```bash
git add src/video/vid_voodoo4.c
git commit -m "video: add targeted voodoo4 bring-up instrumentation"
```

## Task 4: Reach the first milestone without hacks

**Files:**
- Modify: `src/video/vid_voodoo4.c`
- Modify shared files only if a reproduced incompatibility requires it

**Step 1: Achieve BIOS POST and ROM execution**

Acceptance requirement:
- no forced `chain4`
- no forced `packed_chain4`
- no forced `fast=1`
- no `memaddr_latch=0` or equivalent address redirection hack

**Step 2: Reach correct VGA and VBE behavior**

Acceptance requirement:
- text mode remains correct
- banked or LFB graphics modes display correctly because guest-programmed state is modeled correctly
- framebuffer content is read from the correct guest-selected address, not a guessed replacement

**Step 3: Verify Windows reaches a stable desktop in VGA/VBE mode**

Acceptance requirement:
- no garbled persistent framebuffer
- redraw behavior is consistent
- no branch-local diagnostic workaround is required to maintain output

**Step 4: Remove or reduce temporary logging to the minimum useful level**

Leave only instrumentation that is still needed for future VSA-100 validation.

**Step 5: Commit**

```bash
git add src/video/vid_voodoo4.c
git commit -m "video: bring up voodoo4 post and vbe output"
```

## Test Plan

- Build test: the repo builds successfully after each task.
- Registration test: Voodoo 4 appears in the video device picker.
- PCI test: the device reports the expected vendor/device identity and sane BAR behavior.
- ROM test: BIOS POST reaches ROM execution and progresses without wrapper-side hacks.
- VGA test: text mode output remains correct.
- VBE test: a standard graphics mode renders correctly with proper banking or LFB handling.
- OS test: Windows detects the card and reaches a stable desktop in standard VGA/VBE usage.
- Regression test: Banshee and Voodoo3 devices still initialize and render as before.

## Assumptions and Defaults

- Branch name: `voodoo4-restart`
- Base branch: `master`
- The old `voodoo4` branch is retained only as research/reference.
- Milestone 1 stops before VSA-100-specific 3D enablement.
- Shared Banshee/Voodoo3 behavior is the default baseline unless hard evidence proves a VSA-100 difference.
