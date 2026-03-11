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

### Reframed

- The default plan is now reuse-first over Banshee/Voodoo3.
- Earlier assumptions about needing an immediate standalone Voodoo 4 implementation are now treated as unsupported until proven.
- The old recovery plan from 2026-03-10 is now historical context, not the active plan of record.
- Earlier assumptions that the remaining work was mainly architectural are now weaker than the simpler identity/ROM/coverage gap picture.
- The earlier ROM-dispatch/handoff theory is no longer the best description of the failure. The stronger description is that the ROM was already executing, but its own subsystem-ID validation was rejecting the provisional Voodoo4 PCI `0x2c-0x2f` tuple before ext-register traffic began.
- The new boundary is no longer "pre-ext dispatch"; it is now "driver-enabled `32-bit` color distortion beyond the currently verified `16-bit` desktop modes."
- The strongest current hypothesis is no longer just a possibility. The bad `800x600` `32-bit` mode has now been traced as tiled desktop scanout, so the narrower question is whether the newly added tiled `32-bit` renderer is sufficient or whether a second mismatch remains.
- The strongest live lead is no longer a generic higher-VRAM `2D` mismatch. The narrower question is whether Voodoo4 linear/LFB handling or another source-surface population path is misaddressing the higher half by exactly `16 MB`.

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

### Open

- Whether any less common desktop timings outside the now-tested `640x480`, `800x600`, `1024x768`, and `1280x1024` `32-bit` modes expose another tiled-path mismatch
- Whether `screen_to_screen` is merely copying zeros from an upstream-unpopulated linear source surface on the bad `32 MB` path
- Whether the exact remaining mismatch is a V4-only linear/LFB higher-half alias/fold problem or another source-surface population issue
- Whether that remaining mismatch is in linear/LFB writes, source-surface population, or desktop scanout interpretation rather than in guest-visible memory sizing itself

## Maintenance Notes

- Add a new dated section when a meaningful milestone is reached.
- Prefer recording only decisions, proven findings, and notable reversals of prior assumptions.
- If a previous belief turns out wrong, note the correction explicitly rather than silently replacing it.

## Reference Documents

- [Executive summary](./voodoo4-executive-summary.md)
- [Tracker](./voodoo4-tracker.md)
- [Research index](../research/voodoo4-index.md)
