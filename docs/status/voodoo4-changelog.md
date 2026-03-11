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

### Reframed

- The default plan is now reuse-first over Banshee/Voodoo3.
- Earlier assumptions about needing an immediate standalone Voodoo 4 implementation are now treated as unsupported until proven.
- The old recovery plan from 2026-03-10 is now historical context, not the active plan of record.
- Earlier assumptions that the remaining work was mainly architectural are now weaker than the simpler identity/ROM/coverage gap picture.
- The next blank-screen boundary is no longer best described as "some other PCI capability mismatch"; it is now better described as a ROM-dispatch handoff that stops before the Voodoo4 entry helper reaches its first ext write.
- A simple generic ROM checksum/header rejection theory is now weaker than a dispatch/handoff failure, because the Voodoo4 image checksum and basic `PCIR` structure are valid while the first helper's mandatory ext writes are still absent at runtime.

### Corrected

- The current shared path does not have a Voodoo 4-capable PCI identity yet: it only reports `121a:0003` or `121a:0005`.
- The shared ext-register switch does not currently cover offset `0x70`, even though the Voodoo 4 ROM performs RMW there during early init.
- The shared SDRAM BIOS path still hardcodes assumptions that are not yet proven for Voodoo 4, including a `16 MB` SDRAM default and an `Init_strapInfo` value/comment that describe an `8 MB SGRAM, PCI` board.
- The first Voodoo 4 device entry used a plain filename for the ROM, which `rom_present()` treats like an absolute path; it now uses the normal `roms/video/voodoo/...` lookup path instead.
- The first concrete V3/V4 runtime divergence was not just "ROM header inspection"; it tightened to an all-zero Voodoo4 subsystem-ID block at PCI `0x2c-0x2f`.
- That all-zero subsystem-ID block is not, by itself, the root cause of the blank screen: a minimal nonzero probe did not advance execution into ext-register setup.

### Implemented

- Added a reuse-first `3dfx Voodoo4 4500` AGP device entry in the existing Banshee/Voodoo3 code path instead of creating a standalone Voodoo 4 source file.
- Wired that device to the standard `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom` lookup path and exposed PCI device ID `121a:0009`.
- Added ext-register read/write coverage for offset `0x70` as a preserved state register in the shared ext-register block.
- Rebuilt the project successfully after the minimal delta set.
- Added minimal Voodoo4-only startup tracing in `vid_voodoo_banshee.c` to log PCI identity/BAR activity, ROM reads, and first ext-register/display-state touches.
- Added `scripts/test-voodoo4-blank-boundary.sh` to reproduce the active Voodoo4 blank-screen boundary against `/Users/anthony/Library/Application Support/86Box/Virtual Machines/v4`.
- Added a minimal Voodoo4-only subsystem-ID initialization case so the device no longer falls through with `0x0000:0000` at PCI `0x2c-0x2f`.

### Open

- Why the machine BIOS inspects the ROM over the ROM BAR but does not reach the Voodoo4 entry helper's first mandatory ext write
- What exact ROM-dispatch or shadow-handoff condition keeps the Voodoo4 entry helper from running on the current `p5a` path
- Whether `0x70` or shared SDRAM/strap defaults matter only after the ROM-execution boundary is solved

## Maintenance Notes

- Add a new dated section when a meaningful milestone is reached.
- Prefer recording only decisions, proven findings, and notable reversals of prior assumptions.
- If a previous belief turns out wrong, note the correction explicitly rather than silently replacing it.

## Reference Documents

- [Executive summary](./voodoo4-executive-summary.md)
- [Tracker](./voodoo4-tracker.md)
- [Research index](../research/voodoo4-index.md)
