# Voodoo 4 Executive Summary

Date: 2026-03-11

Scope: restart of Voodoo 4 4500 / VSA-100 work on branch `voodoo4-restart`, using fresh local ROM analysis and primary-source driver references.

## Why This Restart Exists

Previous Voodoo 4 work in this repo accumulated implementation assumptions before the hardware evidence was solid. This restart resets the effort around a stricter rule: local ROM behavior and source-backed facts come first, and older Voodoo 4 work is reference material only unless it survives re-verification.

## Current Position

- `Verified:` the local BIOS image `V4_4500_AGP_SD_1.18.rom` is a standard x86 VGA option ROM with VBE services.
- `Verified:` its `PCIR` data identifies the device as `121a:0009`.
- `Verified:` the ROM executes classic VGA/VBE service code and uses a Banshee/Voodoo3-style extended register block.
- `Verified:` the ROM touches offsets that already exist in 86Box’s Banshee/Voodoo3 implementation, including `0x1c`, `0x28`, `0x2c`, `0x40`, `0x4c`, `0x5c`, `0x98`, `0xe4`, and `0xe8`.
- `Inferred:` the most likely successful bring-up path is reuse-first, not a clean-sheet Voodoo 4 rewrite.

## Strategic Recommendation

The effort should begin by proving the smallest set of Voodoo 4-specific deltas on top of the current Banshee/Voodoo3 path:

1. PCI identity and ROM exposure
2. ext-register coverage required by the ROM
3. only then any proven VSA-100-specific reset/default differences

This recommendation is driven by the ROM itself. The BIOS clearly expects shared VGA-era behavior. That makes a premature standalone architecture high-risk and weakly supported.

## What Is Already Done

- `Verified:` repo state and branch state were grounded locally.
- `Verified:` the ROM header, `PCIR` block, entry path, and likely init routines were disassembled and documented.
- `Verified:` the ROM’s observed register accesses were correlated against current 86Box Banshee/Voodoo3 code.
- `Verified:` online source references were collected for Linux `tdfxfb`, Linux `tdfx.h`, Linux PCI IDs, XFree86 DRI notes, and historical Mesa/X.org context.
- `Verified:` a fresh research set and a rewritten recovery plan were added to the repo.

## Key Risks

- `Inferred:` the main technical risk is overcommitting to a standalone Voodoo 4 implementation path before exhausting proven reuse.
- `Unknown:` the first meaningful hardware gap may be around `0x70` or other later-family ext-register semantics.
- `Unknown:` VSA-100-specific PCI config, memory-sizing, or reset-default behavior may still require shared-core changes.

## Decision Rule Going Forward

- `Verified:` every major conclusion should remain labeled `Verified`, `Inferred`, or `Unknown`.
- `Verified:` no implementation change should be justified by old branch lore alone.
- `Inferred:` if reuse breaks, the breakage should be documented as a specific proven delta, not as a general feeling that “Voodoo 4 is separate.”

## Reference Documents

- [Research index](../research/voodoo4-index.md)
- [ROM analysis](../research/voodoo4-rom-analysis.md)
- [Register and code correlation](../research/voodoo4-banshee-correlation.md)
- [Open questions and risks](../research/voodoo4-open-questions.md)
- [Recovery plan](../plans/voodoo4-recovery-plan.md)
