# Voodoo 4 Executive Summary

Date: 2026-03-11

Scope: restart of Voodoo 4 4500 / VSA-100 work on branch `voodoo4-restart`, using fresh local ROM analysis and primary-source driver references.

## Why This Restart Exists

Previous Voodoo 4 work in this repo accumulated implementation assumptions before the hardware evidence was solid. This restart resets the effort around a stricter rule: local ROM behavior and source-backed facts come first, and older Voodoo 4 work is reference material only unless it survives re-verification.

## Current Position

- `Verified:` the local BIOS image `V4_4500_AGP_SD_1.18.rom` is a standard x86 VGA option ROM with VBE services.
- `Verified:` its `PCIR` data identifies the device as `121a:0009`.
- `Verified:` the tested 1.18 ROM also validates PCI `0x2c-0x2f` against subsystem tuple `121a:0004`, not against the device ID.
- `Verified:` the ROM executes classic VGA/VBE service code and uses a Banshee/Voodoo3-style extended register block.
- `Verified:` the ROM touches offsets that already exist in 86Box’s Banshee/Voodoo3 implementation, including `0x1c`, `0x28`, `0x2c`, `0x40`, `0x4c`, `0x5c`, `0x98`, `0xe4`, and `0xe8`.
- `Verified:` after matching the ROM-backed subsystem tuple, the current reuse-first path reaches early ext-register traffic, manually boots Windows to the desktop, and has now been manually verified through `1024x768` `16-bit`, with Windows identifying `Voodoo4 4500 AGP`.
- `Verified:` targeted mode-state tracing on the live V4 Windows path now shows the working driver-enabled desktop programming a tiled `16-bit` mode (`pixfmt=1`, `tile=1`) that resolves onto the existing `16bpp_tiled` renderer.
- `Inferred:` the strongest current emulator-side hypothesis is no longer a generic scanout mismatch; it is that `24/32-bit` tiled desktop scanout is not modeled in the shared path even though `16-bit` tiled scanout is.
- `Inferred:` the most likely successful bring-up path is reuse-first, not a clean-sheet Voodoo 4 rewrite.

## Strategic Recommendation

The effort should continue from the new post-desktop boundary by proving the next smallest Voodoo 4-specific delta on top of the current Banshee/Voodoo3 path:

1. preserve the current working PCI identity, ROM exposure, and ROM-backed subsystem tuple
2. capture the first failing `32-bit` mode-set with the new mode-state tracing still enabled, so the bad path can be compared directly against the now-traced good tiled `16-bit` path
3. only if that bad trace confirms a concrete divergence such as tiled `32-bit` scanout should the next code delta implement the smallest matching renderer/state fix
4. only then revisit any proven VSA-100-specific reset/default differences that line up with that failure

This recommendation is driven by both the ROM and the current runtime result. The BIOS and Windows desktop path already prove substantial shared VGA-era behavior. That makes a premature standalone architecture high-risk and weakly supported.

## What Is Already Done

- `Verified:` repo state and branch state were grounded locally.
- `Verified:` the ROM header, `PCIR` block, entry path, and likely init routines were disassembled and documented.
- `Verified:` the ROM’s observed register accesses were correlated against current 86Box Banshee/Voodoo3 code.
- `Verified:` online source references were collected for Linux `tdfxfb`, Linux `tdfx.h`, Linux PCI IDs, XFree86 DRI notes, and historical Mesa/X.org context.
- `Verified:` a fresh research set and a rewritten recovery plan were added to the repo.

## Key Risks

- `Inferred:` the main technical risk is overcommitting to a standalone Voodoo 4 implementation path before exhausting proven reuse.
- `Verified:` the first meaningful hardware gap now appears on the driver-enabled `32-bit` color path, where the user reports distortion while `1024x768` `16-bit` works.
- `Inferred:` the leading local-code candidate is a missing tiled `24/32-bit` desktop render path, because the traced good `16-bit` Windows mode is tiled and the shared code only has a custom tiled renderer for `16-bit`.
- `Unknown:` whether the failing `32-bit` mode programs the same `tile=1` shape at runtime or diverges somewhere earlier in pixel format, stride, desktop start, DAC mode, or another display register.
- `Unknown:` VSA-100-specific memory-sizing or reset-default behavior may still require shared-core changes once the first post-desktop failure is captured.

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
