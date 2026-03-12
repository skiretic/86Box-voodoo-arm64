# Voodoo 4 / VSA-100 Research Index

Date: 2026-03-11

Scope: fresh Voodoo 4 4500 / VSA-100 investigation on branch `voodoo4-restart`, with the local ROM treated as primary evidence and prior implementation attempts treated as reference material only.

## Repo State

- `Verified:` branch is `voodoo4-restart`.
- `Verified:` worktree was clean except for the newly-added ROM file `V4_4500_AGP_SD_1.18.rom`.
- `Verified:` the only pre-existing Voodoo 4 planning doc in-tree was `docs/plans/2026-03-10-voodoo4-recovery-plan.md`.
- `Inferred:` that earlier plan should be treated as superseded research context, not as an implementation blueprint, because it assumes a wrapper-file implementation shape before the ROM and shared-register evidence are fully established.

## New Documents

- [ROM analysis](./voodoo4-rom-analysis.md)
- [Register and code correlation](./voodoo4-banshee-correlation.md)
- [Open questions and risks](./voodoo4-open-questions.md)
- [Rewritten recovery plan](../plans/voodoo4-recovery-plan.md)
- [Executive summary](../status/voodoo4-executive-summary.md)
- [Tracker](../status/voodoo4-tracker.md)
- [Changelog](../status/voodoo4-changelog.md)
- [Next-session prompt](../plans/voodoo4-next-session-prompt.md)

## Primary Evidence Used

### Local artifacts

- `Verified:` ROM file examined: `V4_4500_AGP_SD_1.18.rom`
- `Verified:` current 86Box Banshee/Voodoo3 implementation examined: `src/video/vid_voodoo_banshee.c`
- `Verified:` local Win9x Voodoo4/Voodoo5 driver bundle examined: `/Users/anthony/Downloads/old games/Voodoo45-10401b/vs-w9x-1.04.01-beta`
- `Verified:` local alternative Win9x Voodoo4/Voodoo5 driver bundle examined: `/Users/anthony/Downloads/3dmrk/gpu/voodoo3/am29win9x`
- `Verified:` key local driver artifacts examined: `driver9x/3dfxvs.inf`, `driver9x/3dfxvs.vxd`, `driver9x/3dfx32vs.dll`, and `driver9x/glide3x.dll`

### Online sources

- Linux `tdfxfb`: <https://codebrowser.dev/linux/linux/drivers/video/fbdev/tdfxfb.c.html>
- Linux `tdfx.h`: <https://codebrowser.dev/linux/linux/include/video/tdfx.h.html>
- Linux PCI IDs: <https://codebrowser.dev/linux/linux/include/linux/pci_ids.h.html>
- XFree86 4.2.0 3Dfx status page: <https://www.xfree86.org/4.2.0/Status2.html>
- XFree86 DRI hardware notes: <https://www.xfree86.org/4.1.0/DRI10.html>
- XFree86/X.org historical README.DRI path: <https://cgit.freedesktop.org/xorg/xserver/plain/hw/xfree86/doc/README.DRI>
- Mesa historical tdfx driver removal commit: <https://cgit.freedesktop.org/mesa/mesa/commit/?id=57871d7a1968190f4d903c2b50495d6390ab0af5>
- PCI Firmware Specification landing page: <https://pcisig.com/PCIConventional/Specs/Firmware/_3.3>
- Napalm / VSA-100 programming reference: <https://www.bitsavers.org/components/3dfx/Voodoo45_Napalm_Spec_r1.13_199910.pdf>
- 3dfxzone Win9x Voodoo4/Voodoo5 archive: <https://www.3dfxzone.it/dir/3dfx/voodoo5/drivers/voodoo45/archive/windows9x/>

## Current High-Level Conclusions

- `Verified:` the ROM is a conventional x86 PCI option ROM with a standard `0x55 0xaa` header, a `PCIR` structure, and x86 entry code. It is not an EFI-only image.
- `Verified:` the ROM actively searches PCI BIOS for vendor `0x121a`, device `0x0009`.
- `Verified:` the tested ROM also validates PCI subsystem tuple `121a:0004` against PCI `0x2c-0x2f` before the first traced ext-register writes.
- `Verified:` the ROM uses many offsets that directly match the Banshee/Voodoo3-era register block described by Linux `tdfx.h` and by 86Box `vid_voodoo_banshee.c`.
- `Verified:` the ROM contains standard VGA and VBE dispatch code, not just a minimal VSA-100-only bootstrap.
- `Inferred:` first bring-up should bias toward reusing the existing 86Box Banshee/Voodoo3 VGA/display path and only splitting when a reproduced VSA-100 difference is proven.
- `Verified:` the current reuse-first path now reaches ROM POST, manual Windows desktop bring-up, and manually tested common tiled `32-bit` desktop modes once the earlier renderer gap is closed.
- `Verified:` after the later guest-visible `32 MB` memory fixes, the live failure is no longer a generic mode-set failure but an inconsistent higher-VRAM desktop population bug.
- `Verified:` fresh runtime tracing now shows visible `2D` work targeting `0x00d00000` while a separate linear/LFB path writes to `0x01d00000`.
- `Verified:` external Napalm/VSA-100 documentation says `vidDesktopStartAddr` is the physical desktop-buffer start and that tiled `vidDesktopOverlayStride` is a tile-stride field for the region rather than a simple width field.
- `Verified:` the same Napalm/VSA-100 documentation splits `lfbMemoryConfig` into `lfbMemoryTileCtrl` and `lfbMemoryTileCompare`, where the compare half determines whether CPU LFB addresses are treated as tiled or linear.
- `Verified:` current 86Box Voodoo4 code only models the tile-control half and currently seeds no explicit V4 default for a separate `lfbMemoryTileCompare` state.
- `Verified:` the local Win9x VSA driver stack contains explicit debug/analysis strings for `cfgAALfbCtrl`, `locLFBMemCfg`, `lfbTileCompare`, `tileMark`, `aaMark`, `tileAddress`, and `hwcCheckTarget` desktop-overlap checks.
- `Verified:` the local Win9x driver also explicitly logs that `hwcInitVideo` sets `lfbMemoryConfig`, then stores `cfgAALfbCtrl`, then derives buffer offsets and overlap information from that state.
- `Verified:` the 3dfxzone Win9x archive lists additional Voodoo4/Voodoo5 comparison targets beyond stock `1.04.xx`, including stock `1.03.00` and `1.01.01`, `3Dhq 1.09 beta 9/7/5`, `Mikepedo 1.1/1.0`, `Amigamerlin 2.5 beta`, `NuDriver1`, `NuAngel's CUSTOM V5 6000`, `Iceman Driver 1.07.02`, `VIA Voodoo 5 drivers`, `Wipeaut Driver`, and `3dfx Wide driver 1.0`.
- `Verified:` local comparison against `Amigamerlin 2.9` shows `3dfx32vs.dll`, `3dfxvs.vxd`, and `Vgartd.vxd` are byte-identical to stock `1.04.01 beta`; only `glide3x.dll` differs in that package.
- `Verified:` stock `1.04.01 beta` `glide3x.dll` preserves the richest local VSA/LFB debug vocabulary seen so far (`cfgAALfbCtrl`, `lfbTileCompare`, `tileMark`, `aaMark`, `hwcCheckTarget`), while the local `Amigamerlin 2.9` `glide3x.dll` appears more stripped and does not expose those strings.
- `Verified:` newly downloaded local packages now provide three genuinely distinct historical display-stack baselines for comparison against stock `1.04.01 beta`: stock `1.00.01`, stock `1.03.00`, and `Iceman 1.07.02`.
- `Verified:` `NuDriver1` is mostly a stock `1.04.01 beta` repack at the binary level for the display stack, with stock-matching `3dfx32vs.dll` and `glide3x.dll` plus an INF customized with NuAngel branding and board names.
- `Verified:` the newly downloaded `NuDriver5.zip` actually contains a later `NuDriver7` driver tree.
- `Verified:` `NuDriver7` is a hybrid branch: its `3dfx32vs.dll`, `glide3x.dll`, and `3dfxogl.dll` match `Iceman`, while its `3dfxvs.vxd` matches the stock-sized `1.04.01 beta` mini-VDD.
- `Verified:` `NuDriver7` also differs from stock at the INF-policy level by enabling `SSTH3_OVERLAYMODE` and `SSTH3_ALPHADITHERMODE` for both D3D and Glide by default.
- `Verified:` compared with `Iceman`, `NuDriver7` pushes policy further via the INF: default mode `32,1024,768`, `UseGTF=1`, `OptimalRefreshLimit=60`, AGP 4x tweak exposure, and additional stereo / guardband / geometry-assist controls.
- `Verified:` the stock lineage `1.00.01`, `1.03.00`, and `1.04.01 beta` keeps the same imported DLL families for `glide3x.dll` and `3dfx32vs.dll`, while image sizes and relocation tables steadily grow across releases.
- `Verified:` raw disassembly of stock `glide3x.dll` around the `hwcCheckTarget` string block confirms live overlap-classification code that compares surface boundaries against `0x02000000` and branches through desktop and FIFO-region cases before pushing the matching debug string.
- `Verified:` that `hwcCheckTarget` overlap ladder remains structurally similar across stock `1.00.01`, `1.03.00`, and `1.04.01 beta`, which strengthens the case that desktop/fifo overlap handling is a stable, intended VSA address-space rule.
- `Verified:` `Iceman` rewrites the shape of that code path, which makes it a meaningful behavioral fork rather than just a string-preserving rebuild.
- `Verified:` the Win9x VSA/LFB debug-string cluster in `glide3x.dll` is present across stock `1.00.01`, stock `1.03.00`, stock `1.04.01 beta`, `Iceman`, `NuDriver1`, and even the modern `3dfx Wide driver 1.0`, so those strings reflect a longstanding code path rather than a one-off debug build.
- `Verified:` `3dfx Wide driver 1.0` is a modernized hybrid package and should not be treated as a clean period reference: it reuses stock `1.04.01 beta` `3dfx32vs.dll`, ships an `Iceman`-family `3dfxvs.vxd`, and replaces `glide3x.dll` / `3dfxogl.dll` with much newer 2022-era rebuilt binaries.
- `Unknown:` the local `3Dhq 1.09 beta 9` package still cannot be unpacked with the extraction tools currently available on this machine because its self-extractor uses an unsupported older RAR method.
- `Verified:` the latest `bad3` row-tail tracing shows the paired `screen_to_screen` consumer is faithfully copying the mixed zero/nonzero row state already present in the low-linear staging surface.
- `Verified:` a larger producer-side V4-only left-alignment experiment for masked mono `0x22000003` uploads changed the traced launch shape but did not improve the visible desktop.
- `Inferred:` the strongest remaining delta for Voodoo 4 is now a VSA-100-specific staging-surface population/layout rule adjacent to `lfbMemoryConfig`, `cfgAALfbCtrl`, or masked mono producer semantics, rather than wholesale VGA POST handling, a fundamentally broken 2D tiled-base convention, or a simple consumer-side readback bug.
- `Unknown:` which parts of VSA-100 scanout, memory timing, and 3D/MMIO behavior are materially different enough to require new shared-core modeling beyond that desktop baseline.

## Earlier Assumptions That Now Look Weak

- `Inferred:` “start with a thin standalone `vid_voodoo4.c`” is not supported by ROM evidence yet. The ROM more strongly supports register-path reuse than file-level separation.
- `Verified:` any claim that the Voodoo 4 ROM bypasses legacy VGA behavior is contradicted by the ROM itself.
- `Verified:` any claim that the public open-source register picture is VSA-100-complete is overstated. Linux `tdfxfb` explicitly notes incomplete public documentation and dependence on XF86 patches plus Banshee-era information.
