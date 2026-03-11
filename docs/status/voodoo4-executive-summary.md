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
- `Verified:` a reproduced bad `800x600` `32-bit` mode on 2026-03-11 also programs tiled desktop scanout (`pixfmt=3`, `tile=1`), but before the latest code change it still resolved onto the generic linear `32bpp` renderer.
- `Verified:` after adding the smallest matching tiled `32-bit` renderer, manual VM retest on 2026-03-11 shows `800x600` `32-bit` looking correct, and the post-fix trace resolves that mode onto `32bpp_tiled`.
- `Verified:` the same tiled `32-bit` renderer change also restores manually tested `1024x768` `32-bit`, and the post-fix trace resolves that mode onto `32bpp_tiled`.
- `Verified:` additional manual checks now also show `640x480` `32-bit` and `1280x1024` `32-bit` working correctly after the same change.
- `Verified:` the strongest current emulator-side explanation for the reproduced `800x600` `32-bit` distortion is no longer hypothetical: the shared path was missing tiled `32-bit` desktop scanout even though tiled `16-bit` scanout already existed.
- `Verified:` the emulator-side Voodoo4 device config now exposes and defaults to `32 MB` SDRAM instead of inheriting the shared `16 MB` SDRAM default.
- `Verified:` the guest now sees `32 MB` VRAM after the Voodoo4-specific memory/strap fixes; 3DMark99 reports roughly `31207 KB`.
- `Verified:` the pre-`32 MB` working Voodoo4 desktop baseline was effectively an `8 MB` path; there was no separately verified good `16 MB` Voodoo4 desktop path.
- `Verified:` the current remaining symptom is not “still only `8 MB`.” It is an inconsistent `2D` desktop once the Voodoo4 driver places the tiled `32-bit` desktop surface in higher VRAM.
- `Verified:` fresh mode-state tracing on the `32 MB` path still lands on `32bpp_tiled`, but the desktop start address moves upward relative to the older working pre-`32 MB` trace.
- `Verified:` recent manual screenshots show the failure is inconsistent rather than uniformly broken: some windows, icons, and labels render correctly while other desktop regions remain black or missing.
- `Verified:` current high-base `2D` tracing shows `rectfill`, `host_to_screen`, and `screen_to_screen` activity targeting desktop base `0x00d00000`.
- `Verified:` sampled high-base `screen_to_screen` copies are internally consistent, including later copies that faithfully move zero-filled linear source data into visible desktop tiles.
- `Verified:` fresh linear/LFB traces now show writes landing at tiled base `0x01d00000` while desktop scanout and traced `2D` destinations remain at `0x00d00000`, an exact `0x01000000` (`16 MB`) split.
- `Inferred:` the strongest current lead is no longer a generic tiled-renderer or scanout bug. It is a higher-half desktop-surface population/address-translation mismatch, most likely around Voodoo4 linear/LFB handling or the surfaces that later get copied into the visible tiled desktop.
- `Inferred:` the most likely successful bring-up path is reuse-first, not a clean-sheet Voodoo 4 rewrite.

## Strategic Recommendation

The effort should continue from the new post-desktop boundary by proving the next smallest Voodoo 4-specific delta on top of the current Banshee/Voodoo3 path:

1. preserve the current working PCI identity, ROM exposure, and ROM-backed subsystem tuple
2. treat the earlier common tiled `32-bit` renderer boundary as closed for the manually tested modes `640x480`, `800x600`, `1024x768`, and `1280x1024`
3. treat the new `32 MB` bring-up result as a fresh reproduced post-desktop symptom: inconsistent desktop population after the driver moves the surface into higher VRAM
4. target the next smallest shared-path mismatch in higher-VRAM desktop-surface population or address translation before widening into unrelated VSA-100 or 3D work
5. keep proven dead ends closed: do not reopen ROM-dispatch theory, and do not reuse the reverted desktop-base alias or zero-`lfbMemoryConfig` LFB-guard experiments

This recommendation is driven by both the ROM and the current runtime result. The BIOS and Windows desktop path already prove substantial shared VGA-era behavior. That makes a premature standalone architecture high-risk and weakly supported.

## What Is Already Done

- `Verified:` repo state and branch state were grounded locally.
- `Verified:` the ROM header, `PCIR` block, entry path, and likely init routines were disassembled and documented.
- `Verified:` the ROM’s observed register accesses were correlated against current 86Box Banshee/Voodoo3 code.
- `Verified:` online source references were collected for Linux `tdfxfb`, Linux `tdfx.h`, Linux PCI IDs, XFree86 DRI notes, and historical Mesa/X.org context.
- `Verified:` a fresh research set and a rewritten recovery plan were added to the repo.

## Key Risks

- `Inferred:` the main technical risk is overcommitting to a standalone Voodoo 4 implementation path before exhausting proven reuse.
- `Verified:` the first meaningful remaining desktop gap now appears on the driver-enabled higher-VRAM `32-bit` path, where rendering is inconsistent rather than uniformly broken.
- `Verified:` the leading local-code candidate, the tiled `32-bit` desktop render path, was sufficient for the reproduced `800x600` `32-bit` failure and the manually tested `1024x768` `32-bit` mode.
- `Verified:` the same fix also holds for manually tested `640x480` `32-bit` and `1280x1024` `32-bit`.
- `Unknown:` whether any less common desktop timings outside the now-tested modes expose another mismatch.
- `Unknown:` which exact shared path leaves some higher-VRAM source surfaces zero or stale before they are copied into the visible tiled desktop.
- `Unknown:` whether the remaining mismatch is in linear/LFB writes, source-surface population, or desktop scanout interpretation of that higher VRAM region.

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
