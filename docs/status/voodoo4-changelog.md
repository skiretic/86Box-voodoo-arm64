# Voodoo 4 Restart Changelog

Date: 2026-03-11

Purpose: record the meaningful research and planning milestones for the `voodoo4-restart` effort. This is a project changelog, not a git log.

## 2026-03-11

### Added

- Fresh Voodoo 4 / VSA-100 research index
- Fresh ROM analysis for `V4_4500_AGP_SD_1.18.rom`
- Fresh register/code correlation against current 86Box Banshee/Voodoo3 code
- Fresh open questions and risks document
- Rewritten evidence-first recovery plan
- Executive summary for the restart
- Tracker for completed work and next steps

### Verified

- The local ROM is a standard x86 VGA option ROM with VBE handling.
- The `PCIR` block identifies the board as `121a:0009`.
- The ROM entry path uses classic VGA/VBE BIOS structure, not a VSA-100-only private init path.
- The ROM touches multiple offsets that map directly onto the current 86Box Banshee/Voodoo3 extended register block.
- The current shared PCI path already matches the family-level VGA class code and BAR shape closely enough to keep reuse-first as the baseline.
- The first audited ROM-touched ext offset that is actually missing from the shared handler is `0x70`.
- Runtime tracing on `/Users/anthony/Library/Application Support/86Box/Virtual Machines/v4/86box.cfg` shows the guest discovers the card as `121a:0009`, assigns the I/O BAR, and toggles the ROM BAR.
- Runtime tracing shows the guest reads the Voodoo4 ROM header through the ROM BAR at `0xe7ef0000`.
- Runtime tracing did not show any Voodoo4 ext-register traffic or any traced legacy sub-`1 MB` ROM reads after header inspection in the blank-screen boot window.
- Comparison against a working `Voodoo3 3000 AGP` boot on the same `p5a` machine family shows the first concrete config-space divergence is at PCI `0x2c-0x2f`: Voodoo3 exposes a nonzero subsystem ID block before ext-register traffic begins, while Voodoo4 originally exposed `0x0000:0000`.
- A VM-driven trace test now reproduces that Voodoo4 blank-screen boundary directly.
- A minimal Voodoo4 subsystem-ID probe changed PCI `0x2c-0x2f` from `0x0000:0000` to `121a:0009`, but the blank-screen boot still did not reach any Voodoo4 ext-register traffic.
- The current AGP Voodoo3/Voodoo4 PCI shell in `banshee_pci_read()` is otherwise the same across the returned `0x04-0x67` config/capability region except for device ID and subsystem-device bytes.
- Runtime comparison on the same `p5a` boot path shows that, after the subsystem-ID probe, both cards still reach the same later PCI reads (`0x3c-0x3f`, `0x38-0x3b`, `0x34-0x37`, `0x28-0x2b`), but only Voodoo3 transitions into ext-register writes.
- The Voodoo4 ROM's declared `0xa000` image checksum sums to zero, and its `PCIR` metadata remains structurally valid in both the repo copy and the standard 86Box ROM lookup copy.
- The Voodoo4 ROM entry helper called from offset `0x00f1` reaches ext `+0x28` on both its success and failure paths, and ext `+0x70` on the normal path, before any later init routines.
- Generic `p5a` runtime tracing now shows that the BIOS shadows the Voodoo4 ROM from the PCI ROM BAR into `0xc0000` and executes it at `c000:0003`.
- Shadowed execution reaches `0x3e6e`, enters helper `0x865c`, and passes through the ROM's PCI config-validation loop before any ext write appears.
- Helper `0x3db2` compares PCI `0x2c-0x2f` against the dword stored at the end of the declared ROM image.
- The tested Voodoo4 ROM encodes subsystem pair `121a:0004` at declared-image offset `0x9ff8`, while the working Voodoo3 ROM encodes `121a:003a` at the corresponding end-of-image location.
- Replacing the provisional Voodoo4 subsystem tuple with the ROM-matched `121a:0004` unlocks the first Voodoo4 ext-register writes at runtime, including ext `+0x28`, ext `+0x70`, and later early-init writes to `DACMODE`, screen size, desktop start, and stride.
- Manual VM verification now shows the current Voodoo4 path booting Windows to the desktop in `640x480` `16-color` mode, with Windows identifying the card as `Voodoo4 4500 AGP`.
- Manual VM verification now also shows the current Voodoo4 path reaching `800x600` at `16-bit` color.
- User manual testing now shows the Voodoo4 driver path is active, `1024x768` at `16-bit` works, and `32-bit` color produces distortion.
- A longer live V4 Windows boot trace now shows the working driver-enabled desktop path programming tiled `16-bit` scanout (`pixfmt=1`, `tile=1`) and reaching the existing `16bpp_tiled` renderer.
- The current shared code still has no custom tiled desktop renderer for `24-bit` or `32-bit`; only the `16-bit` tiled path exists today.
- A reproduced `800x600` `32-bit` manual retest on 2026-03-11 now shows the bad V4 mode also programming tiled desktop scanout (`pixfmt=3`, `tile=1`), while still landing on the generic linear `32bpp` renderer before the latest code change.
- The working pre-`32 MB` Voodoo4 desktop baseline was effectively an `8 MB` path, not a separately verified good `16 MB` Voodoo4 path.
- Fresh high-base `2D` tracing now shows `rectfill`, `host_to_screen`, and `screen_to_screen` activity targeting desktop base `0x00d00000` on the bad `32 MB` path.
- Sampled high-base `screen_to_screen` copies are internally consistent, including later copies that faithfully move zero-filled linear source data into visible desktop tiles.
- Fresh linear/LFB tracing now shows writes landing at tiled base `0x01d00000` while desktop scanout and traced `2D` destinations remain at `0x00d00000`, an exact `0x01000000` (`16 MB`) split.
- Latest manual screenshots now show the remaining `32 MB` desktop failure is inconsistent rather than uniformly broken: some windows, icons, and labels render correctly while other desktop regions remain black or missing.
- A fresh logged desktop rerun now shows the known-good low linear page `0x00299e80` being populated by both decoded LFB writes (`0x12299e80 -> 0x00299e80`) and `host_to_screen` work.
- That same rerun also shows later `screen_to_screen` copies into the tiled desktop using additional nonzero low linear sources such as `0x001ec948` and `0x001eb1cc`.
- That same rerun did not reproduce the earlier `0x002de9b0` zero-source sample and did not hit any watched `bad` low linear page.
- A later alias-engaged rerun still leaves the desktop visibly wrong even though the V4-only CPU/LFB fold engages from the first traced high-base write.
- That same alias-engaged rerun now reproduces a different low-linear source split in the damaged band: repeated `screen_to_screen` copies pair nonzero page `0x00132400` with zero pages `0x001332d4` and `0x00133308`.
- The temporary low-linear watch pages are now retargeted to keep tracking that newly reproduced `0x00132000` / `0x00133000` source family alongside the older watchpoints.
- A later live desktop run now reproduces another direct blanking source family: `screen_to_screen` copies from zero page `0x002a0000` (`0x002a0084..0x002a0090`) drive the damaged-band desktop at `0x00e90000` from nonzero pixels back to zero.
- The temporary low-linear watch pages are now widened again so the next run labels that new `0x002a0000` blanking source as `bad3`.
- The temporary low-linear tracing now also watches `good3` / `bad2` / `bad3` directly inside `vid_voodoo_banshee_blitter.c`, and the limited watch counters are now biased toward bad-page families so early `good0` traffic does not hide later bad-page population attempts.
- A fresh rerun with the narrowed stride-packed host-row sizing change now computes sane `bad3` row completion lengths (`lastByte=12` rather than `65036`) for the traced `srcFmt=0x00000010`, `srcStrideField=16`, `srcXY=0x0000f040` host upload path.
- That sizing correction still does not populate `bad3`: the first observed `bad3` writer remains `host_to_screen/0x22000003`, and it leaves `0x002a0084..0x002a0090` all zero even while the guest supplies nonzero mono upload dwords.
- A healthy watched desktop mono overlay path in the same run uses the same `srcFmt=0x00000010` family but a different command/ROP (`0xbb000003`) and still writes nonzero pixels like `0xbfffffff`.
- The strongest lead is now upstream source-surface population ordering rather than host payload sizing: under the current shared `MIX()` semantics, the traced `0x22000003` masked mono pass cannot create a nonzero `bad3` surface from an all-zero destination by itself.
- A later rerun with added `dstAfterPlus16` tracing proved that `bad3` is only partially empty: the same `0x22000003` producer can leave `0x002a0084` / `0x002a0088` at zero while writing nonzero data into later row offsets such as `0x002a008c..0x002a00a0`.
- That same rerun also proved the paired `screen_to_screen` consumer is faithful rather than broken: when it reads `srcBase=0x002a0084` with `srcXY=0`, it copies the zero row head and nonzero tail exactly as stored in `bad3`.
- A larger producer-side V4-only experiment then forced masked mono `0x22000003` uploads to a normalized left-aligned row origin; it changed the traced `bad3` launch (`lastByte=4` instead of `12`) but did not improve the desktop.
- With that larger experiment active, `bad3` still kept the row head zero while later offsets became nonzero, so the change is now recorded as a negative result rather than a fix.
- The same final rerun also reproduced the earlier `bad2` zero-source family at `0x001332d4` and `0x00133308`, reinforcing that the remaining desktop failure is not unique to one watched low-linear page family.

### Reframed

- The default plan is now reuse-first over Banshee/Voodoo3.
- Earlier assumptions about needing an immediate standalone Voodoo 4 implementation are now treated as unsupported until proven.
- The old recovery plan from 2026-03-10 is now historical context, not the active plan of record.
- Earlier assumptions that the remaining work was mainly architectural are now weaker than the simpler identity/ROM/coverage gap picture.
- The earlier ROM-dispatch/handoff theory is no longer the best description of the failure. The stronger description is that the ROM was already executing, but its own subsystem-ID validation was rejecting the provisional Voodoo4 PCI `0x2c-0x2f` tuple before ext-register traffic began.
- The new boundary is no longer "pre-ext dispatch"; it is now "driver-enabled `32-bit` color distortion beyond the currently verified `16-bit` desktop modes."
- The strongest current hypothesis is no longer just a possibility. The bad `800x600` `32-bit` mode has now been traced as tiled desktop scanout, so the narrower question is whether the newly added tiled `32-bit` renderer is sufficient or whether a second mismatch remains.
- The strongest live lead is no longer a generic higher-VRAM `2D` mismatch. The narrower question is whether Voodoo4 linear/LFB handling or another source-surface population path is misaddressing the higher half by exactly `16 MB`.
- The strongest live lead is now narrower still: low linear source-surface population can work on the `32 MB` path, so the remaining bug appears conditional or path-specific rather than universal.
- The strongest live lead is now narrower again: the naive CPU/LFB alias can engage successfully while the failure still tracks a distinct low-linear source-population split, which makes aliasing look secondary unless a subtler V4 rule ties the two paths together.
- The strongest live lead is now broader in one specific way: the same damaged desktop region can be blanked by more than one conditional zero-source family, so the missing rule likely sits upstream of any single watched page identity.
- One concrete instrumentation gap is now closed: the latest bad-page families were previously only visible indirectly once they hit watched desktop destinations, because the blitter watch set and finite counters still favored the older `good0` path.

### Corrected

- The current shared path does not have a Voodoo 4-capable PCI identity yet: it only reports `121a:0003` or `121a:0005`.
- The shared ext-register switch does not currently cover offset `0x70`, even though the Voodoo 4 ROM performs RMW there during early init.
- The shared SDRAM BIOS path still hardcodes assumptions that are not yet proven for Voodoo 4, including a `16 MB` SDRAM default and an `Init_strapInfo` value/comment that describe an `8 MB SGRAM, PCI` board.
- The first Voodoo 4 device entry used a plain filename for the ROM, which `rom_present()` treats like an absolute path; it now uses the normal `roms/video/voodoo/...` lookup path instead.
- The first concrete V3/V4 runtime divergence was not just "ROM header inspection"; it tightened to an all-zero Voodoo4 subsystem-ID block at PCI `0x2c-0x2f`.
- That all-zero subsystem-ID block is not, by itself, the root cause of the blank screen: a minimal nonzero probe did not advance execution into ext-register setup.
- The provisional Voodoo4 subsystem-ID probe `121a:0009` was not the ROM-backed board tuple. The tested 1.18 ROM expects `121a:0004` at PCI `0x2c-0x2f`.
- The BIOS was not refusing to dispatch the Voodoo4 ROM. The ROM was being shadowed and executed; the failure was earlier inside the ROM's own config validation.
- Earlier references to an "earlier good `16 MB` trace" were inaccurate. The working pre-`32 MB` Voodoo4 desktop state was effectively an `8 MB` path.

### Implemented

- Added a reuse-first `3dfx Voodoo4 4500` AGP device entry in the existing Banshee/Voodoo3 code path instead of creating a standalone Voodoo 4 source file.
- Wired that device to the standard `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom` lookup path and exposed PCI device ID `121a:0009`.
- Added ext-register read/write coverage for offset `0x70` as a preserved state register in the shared ext-register block.
- Rebuilt the project successfully after the minimal delta set.
- Added minimal Voodoo4-only startup tracing in `vid_voodoo_banshee.c` to log PCI identity/BAR activity, ROM reads, and first ext-register/display-state touches.
- Added `scripts/test-voodoo4-blank-boundary.sh` to reproduce the active Voodoo4 blank-screen boundary against `/Users/anthony/Library/Application Support/86Box/Virtual Machines/v4`.
- Added a minimal Voodoo4-only subsystem-ID initialization case so the device no longer falls through with `0x0000:0000` at PCI `0x2c-0x2f`.
- Used temporary targeted ROM-shadow tracing in `mem.c` and `ali1541.c` to log `0xc0000` shadow writes, first shadowed execution PCs, and `p5a` C000 shadow-state transitions; that tracing was later removed after the root cause was confirmed.
- Updated the Voodoo4 subsystem tuple in `vid_voodoo_banshee.c` from the provisional `121a:0009` probe to the ROM-backed `121a:0004`.
- Added targeted mode-state tracing in `vid_voodoo_banshee.c` for `DACMODE`, `VIDPROCCFG`, `VIDINFORMAT`, screen size, desktop start, desktop stride, and selected renderer.
- Rebuilt successfully and re-ran `bash scripts/test-voodoo4-blank-boundary.sh` successfully after the targeted tracing delta.
- Captured a longer live V4 Windows boot trace at `/tmp/voodoo4-mode-boundary.log`.
- Added a shared tiled `32-bit` desktop renderer in `vid_voodoo_banshee.c` and routed tiled `PIX_FORMAT_RGB32` desktop scanout to it.
- Added a Voodoo4-specific SDRAM device config that exposes `32 MB` and switched the reuse-first Voodoo4 path away from the inherited shared `16 MB` SDRAM default.
- Rebuilt successfully and re-ran `bash scripts/test-voodoo4-blank-boundary.sh` successfully after the tiled `32-bit` renderer delta.
- Rebuilt successfully and re-ran `bash scripts/test-voodoo4-blank-boundary.sh` successfully after the Voodoo4 `32 MB` memory-config change.
- Manual VM retest on 2026-03-11 now shows `800x600` `32-bit` looking correct after that delta, and the post-fix trace confirms the working mode resolves onto `32bpp_tiled`.
- Manual VM retest on 2026-03-11 also shows `1024x768` `32-bit` looking correct after that same delta, and the post-fix trace confirms that higher mode also resolves onto `32bpp_tiled`.
- Additional manual VM retests on 2026-03-11 show `640x480` `32-bit` and `1280x1024` `32-bit` also looking correct after the same delta.
- Removed the temporary Voodoo4 mode-state tracing after the tiled `32-bit` desktop investigation was complete, then rebuilt and re-ran `bash scripts/test-voodoo4-blank-boundary.sh` successfully.
- Added a Voodoo4-specific `32 MB` guest-visible memory report path so 3DMark99 now reports about `31207 KB` instead of `8 MB`.
- Re-enabled narrow mode-state tracing after the `32 MB` work reproduced a new desktop-only symptom: distortion once the driver uses a higher-VRAM tiled desktop surface.
- Fresh `32 MB` traces now show the bad mode still landing on `32bpp_tiled` while shifting the desktop start address upward relative to the older working pre-`32 MB` trace.
- Tried a V4-only desktop-base alias experiment and reverted it after it made the desktop worse.
- Tried a V4-only zero-`lfbMemoryConfig` LFB guard and reverted it after it did not change the distortion.
- Added temporary V4-only high-base `2D` tracing and sampled copy logging in `vid_voodoo_banshee_blitter.c`.
- Added temporary V4-only high-base linear/LFB tracing in `vid_voodoo_banshee.c`.
- Added temporary watchpoint tracing for selected low linear source pages in both Voodoo4 tracing files.
- Added temporary V4-only `Init_lfbMemoryConfig` change tracing in `vid_voodoo_banshee.c` so the next run can prove whether the guest explicitly programs the higher `tile_base` that currently diverges from desktop start.
- Added a fresh logged V4 desktop run proving the guest explicitly programs `lfbMemoryConfig` to `tileBase=0x01d00000` while `vidDesktopStartAddr` remains `0x00d00000`.
- Captured a fresh interaction-time failing low linear source sample at `0x001ef390` and a nearby good low source sample at `0x001edc10`.

### Open

- Whether any less common desktop timings outside the now-tested `640x480`, `800x600`, `1024x768`, and `1280x1024` `32-bit` modes expose another tiled-path mismatch
- Whether `screen_to_screen` is merely copying zeros from an upstream-unpopulated linear source surface on the bad `32 MB` path
- Whether the earlier `0x002de9b0` zero-source case requires a longer or more specific desktop interaction sequence to reproduce under the new watchpoint tracing
- Whether the exact remaining mismatch is a V4-only linear/LFB higher-half alias/fold problem or another source-surface population issue
- Whether that remaining mismatch is in linear/LFB writes, source-surface population, or desktop scanout interpretation rather than in guest-visible memory sizing itself
- Whether the next logged V4 run shows `Init_lfbMemoryConfig` explicitly programming `tileBase=0x01d00000` while `vidDesktopStartAddr` remains `0x00d00000`
- Which path should populate low linear source page `0x001ef390`, and why that page stays zero when nearby low linear source `0x001edc10` is populated correctly

## Maintenance Notes

- Add a new dated section when a meaningful milestone is reached.
- Prefer recording only decisions, proven findings, and notable reversals of prior assumptions.
- If a previous belief turns out wrong, note the correction explicitly rather than silently replacing it.

## Reference Documents

- [Executive summary](./voodoo4-executive-summary.md)
- [Tracker](./voodoo4-tracker.md)
- [Research index](../research/voodoo4-index.md)
- Added watched damaged-band launch tracing for tiled 32-bpp desktop destinations around `0x00e9xxxx` and used it to confirm repeated `screen_to_screen`, `host_to_screen`, and `rectfill` activity inside the bad region.
- Verified that watched-band `screen_to_screen` launches use `srcBase=0x001b8e80`, `srcFmt=0x000504b0` (32-bpp, stride `0x4b0`), while watched-band `host_to_screen` launches use `srcFmt=0x00400000` (packed `1-bpp` host source).
- Fixed a temporary tracing gap so watched-band result logging also runs through the color-conversion path used by the `1-bpp` host-to-screen overlays.
- Verified with watched-band result tracing that the `1-bpp` host-to-screen overlays write valid nonzero pixels into the damaged band, so the overlay path is not the primary source of the black/missing regions.
- Reproduced the original blanking path again: `screen_to_screen` copies from zero source page `0x002de9b0` blank visible desktop tiles, and a nearby page around `0x002dcfec` later restores them.
- Added new low-linear result tracing focus for bad page `0x002de000` and nearby recovery page `0x002dc000` so the next run can show whether those pages are actually being populated or left unchanged.
- Verified from the next rerun that the new bad-vs-recovery low-linear result tracer itself is working, but that particular session stayed on the healthy path: only `good0` (`0x00299e80`) produced low-linear result hits, while `bad0` (`0x002de000`) and `good2` (`0x002dc000`) did not reappear.
- Reframed the next step from “keep dragging until the same bad page reappears” to “preserve the current evidence, then do a broader Voodoo4/VSA-100 research pass on 2D, LFB, and address-base semantics before the next runtime hypothesis.”
- Added a broader Voodoo4/VSA-100 research pass using the local Win9x `vs-w9x-1.04.01-beta` driver bundle plus external Napalm register documentation.
- Verified from the local Win9x INF that `PCI\\VEN_121A&DEV_0009&SUBSYS_0004121A&REV_01` is the Voodoo4 AGP path in this driver set, matching the ROM-backed `121a:0004` subsystem tuple already required by the ROM.
- Verified from external Napalm/VSA-100 documentation that `vidDesktopStartAddr` is the physical desktop-buffer start and that tiled `vidDesktopOverlayStride` is a tile-stride field for the region, not merely desktop width in bytes.
- Verified from the same documentation that `lfbMemoryConfig` is split between `lfbMemoryTileCtrl` and `lfbMemoryTileCompare`, where the compare half controls the CPU-address threshold between tiled and linear interpretation.
- Verified locally that current 86Box only models the tile-control half of `Init_lfbMemoryConfig` and remaps any CPU LFB address `>= tile_base` through one shared threshold and tile formula.
- Verified from the local Win9x VSA driver strings that the shipping driver stack contains explicit logic and debugging around `cfgAALfbCtrl`, `locLFBMemCfg`, `lfbTileCompare`, `strideInTiles`, `tileAddress`, and desktop-overlap checks (`hwcCheckTarget`), supporting the idea that current LFB/address handling is still under-modeled for VSA-100.
- Verified from external register documentation that the 2D `srcBaseAddr` / `dstBaseAddr` high bit selecting tiled layout is source-backed, so that particular shared-Banshee convention is not the best current suspect.
- Added an external-driver inventory pass against the 3dfxzone Voodoo4/Voodoo5 Win9x archive and recorded the main additional families worth comparing: stock `1.03.00` and `1.01.01`, `3Dhq`, `Mikepedo`, `Amigamerlin 2.5`, `NuDriver`, `NuAngel`, `Iceman`, `VIA Voodoo`, `Wipeaut`, and `3dfx Wide driver 1.0`.
- Verified locally that `Amigamerlin 2.9` is not a wholly different Win9x Voodoo4/Voodoo5 display stack: `3dfx32vs.dll`, `3dfxvs.vxd`, and `Vgartd.vxd` match stock `1.04.01 beta` byte-for-byte, while `glide3x.dll` is the only changed binary in that package.
- Verified that the stock `1.04.01 beta` `glide3x.dll` currently remains the richest single local reverse-engineering binary because it keeps the `cfgAALfbCtrl` / `lfbTileCompare` / `hwcCheckTarget` debug strings that are absent from the local `Amigamerlin 2.9` Glide build.
- Added a new comparison pass over freshly downloaded archive packages in `/Users/anthony/Downloads/old games/drivers`, including stock `1.03.00`, stock `1.00.01`, `NuDriver1`, `Iceman`, `3dfx Wide driver 1.0`, `Mikepedo 1.1`, and the `Voodoo45` / `Xpentor` package.
- Verified that the VSA/LFB debug-string cluster in `glide3x.dll` (`cfgAALfbCtrl`, `lfbTileCompare`, `tileMark`, `aaMark`, `hwcCheckTarget`) is already present in stock `1.00.01`, stock `1.03.00`, `Iceman`, `NuDriver1`, and `3dfx Wide driver 1.0`; it is not unique to stock `1.04.01 beta`.
- Verified that `NuDriver1` reuses stock `1.04.01 beta` binaries for at least `3dfx32vs.dll` and `glide3x.dll`, while customizing the INF branding and board names around them.
- Verified that `Iceman` ships a genuinely different Win9x display stack from stock `1.04.01 beta`, including distinct `3dfx32vs.dll`, `3dfxvs.vxd`, `3dfxogl.dll`, and `glide3x.dll`.
- Verified that stock `1.03.00` and stock `1.00.01` each also carry distinct `3dfx32vs.dll`, `3dfxvs.vxd`, and `glide3x.dll` families, making them useful true-reference baselines rather than mere packaging variants.
- Verified that `3dfx Wide driver 1.0` is a modernized hybrid package: it reuses stock `1.04.01 beta` `3dfx32vs.dll`, ships an `Iceman`-family `3dfxvs.vxd`, and replaces `glide3x.dll` / `3dfxogl.dll` with much newer 2022-era rebuilt binaries.
- Verified that the locally downloaded `3Dhq 1.09 beta 9` self-extractor still has not been unpacked here because the available local tools reject its older RAR method.
- Added direct local analysis of the newly downloaded `NuDriver5.zip`, which actually contains a later `NuDriver7` Win9x driver tree.
- Verified that `NuDriver7` shares its core user-mode display binaries with `Iceman` rather than with stock `1.04.01 beta`: `3dfx32vs.dll`, `glide3x.dll`, and `3dfxogl.dll` match `Iceman` exactly.
- Verified that `NuDriver7` keeps a stock-sized `3dfxvs.vxd` matching stock `1.04.01 beta`, making it a hybrid branch: `Iceman`-family user-mode display stack plus stock-size mini-VDD plus a heavily customized INF.
- Verified from the `NuDriver7` INF that it explicitly enables `SSTH3_OVERLAYMODE` and `SSTH3_ALPHADITHERMODE` for both D3D and Glide by default, which is more aggressive than the stock reference INF files that leave those lines commented out.
- Verified by direct INF comparison that `NuDriver7` is also policy-heavier than `Iceman`: it switches the default desktop mode to `32,1024,768`, turns `UseGTF` on, lowers `OptimalRefreshLimit` from `75` to `60`, exposes AGP 4x options in the tweak UI, and adds stereo / guardband / geometry-assist controls beyond the simpler stock layout.
- Verified that the stock lineage `1.00.01 -> 1.03.00 -> 1.04.01 beta` keeps the same imported DLL families for `glide3x.dll` and `3dfx32vs.dll` while steadily growing code/data size, consistent with one evolving codebase rather than abrupt interface rewrites.
- Added raw disassembly around the stock `glide3x.dll` `hwcCheckTarget` message references and verified a real overlap-classification branch ladder in code, not just inert debug strings.
- Verified from stock `1.00.01`, `1.03.00`, and `1.04.01 beta` `glide3x.dll` that the `hwcCheckTarget` code explicitly compares a surface boundary against `0x02000000` (`32 MB`) and then branches through separate cases for “surface starts/ends in desktop” and later FIFO-related regions before choosing the corresponding debug string.
- Verified that the stock `hwcCheckTarget` ladder keeps the same basic structure across `1.00.01`, `1.03.00`, and `1.04.01 beta`, while `Iceman` rewrites that logic into a different control-flow shape.
- Added trace-only V4 buffer-mark instrumentation in `vid_voodoo_banshee.c` to log `desktopLinearEndGuess`, `desktopTiledEndGuess`, `tileMarkGuess`, and `aaMarkGuess` from live SVGA geometry beside the existing `tileBase` / `desktopStart` / compare-threshold trace state.
- Tightened the same V4 trace to also log current shared-buffer placement state from the Voodoo core: `colBuf`, `colStride`, `fbWrite`, `fbRead`, `auxBuf`, `auxStride`, `front`, `back`, `swap`, and `fbMask`.
- Added a second trace pass in `vid_voodoo_reg.c` to log the V4/Banshee-family shared-buffer register sequence itself (`colBufferAddr`, `colBufferStride`, `auxBufferAddr`, `auxBufferStride`, `leftOverlayBuf`, `swapbufferCMD`) so the next run can show when those placements first become nonzero.
- Added a first minimal V4-only CPU/LFB behavior experiment in `vid_voodoo_banshee.c`: when the guest programs the exact traced `tileBase = desktopStart + 0x01000000` split with no explicit compare register, the CPU/LFB tiled helpers temporarily fold to `desktopStart` and log `effectiveTileBase` / `aliasApplied`.
- Verified from the widened `0x1e0..0x260` V4 register-block trace that the idle desktop boot still only showed early `videoDimensions=0` and `fbiInit0=0` writes before the first high-base V4 LFB access; the classic Banshee-family `colBufferAddr` / `colBufferStride` / `auxBufferAddr` / `auxBufferStride` / `leftOverlayBuf` / `swapbufferCMD` path did not appear there.
- Verified from the first alias-experiment boot that the V4-only CPU/LFB fold actually engaged immediately (`effectiveTileBase=00d00000`, `aliasApplied=1`) while the raw guest register state still reported `tileBase=01d00000`.
- Verified from the same boot that the idle desktop still looked unchanged to the user, so that naive V4-only fold is not sufficient by itself.
