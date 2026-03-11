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

## Primary Evidence Used

### Local artifacts

- `Verified:` ROM file examined: `V4_4500_AGP_SD_1.18.rom`
- `Verified:` current 86Box Banshee/Voodoo3 implementation examined: `src/video/vid_voodoo_banshee.c`

### Online sources

- Linux `tdfxfb`: <https://codebrowser.dev/linux/linux/drivers/video/fbdev/tdfxfb.c.html>
- Linux `tdfx.h`: <https://codebrowser.dev/linux/linux/include/video/tdfx.h.html>
- Linux PCI IDs: <https://codebrowser.dev/linux/linux/include/linux/pci_ids.h.html>
- XFree86 4.2.0 3Dfx status page: <https://www.xfree86.org/4.2.0/Status2.html>
- XFree86 DRI hardware notes: <https://www.xfree86.org/4.1.0/DRI10.html>
- XFree86/X.org historical README.DRI path: <https://cgit.freedesktop.org/xorg/xserver/plain/hw/xfree86/doc/README.DRI>
- Mesa historical tdfx driver removal commit: <https://cgit.freedesktop.org/mesa/mesa/commit/?id=57871d7a1968190f4d903c2b50495d6390ab0af5>
- PCI Firmware Specification landing page: <https://pcisig.com/PCIConventional/Specs/Firmware/_3.3>

## Current High-Level Conclusions

- `Verified:` the ROM is a conventional x86 PCI option ROM with a standard `0x55 0xaa` header, a `PCIR` structure, and x86 entry code. It is not an EFI-only image.
- `Verified:` the ROM actively searches PCI BIOS for vendor `0x121a`, device `0x0009`.
- `Verified:` the ROM uses many offsets that directly match the Banshee/Voodoo3-era register block described by Linux `tdfx.h` and by 86Box `vid_voodoo_banshee.c`.
- `Verified:` the ROM contains standard VGA and VBE dispatch code, not just a minimal VSA-100-only bootstrap.
- `Inferred:` first bring-up should bias toward reusing the existing 86Box Banshee/Voodoo3 VGA/display path and only splitting when a reproduced VSA-100 difference is proven.
- `Inferred:` the strongest likely deltas for Voodoo 4 are PCI identity, memory-sizing/strap behavior, and some later video/input-format or 3D-family details, not a wholesale rewrite of VGA POST handling.
- `Unknown:` which parts of VSA-100 scanout, memory timing, and 3D/MMIO behavior are materially different enough to require new shared-core modeling in 86Box.

## Earlier Assumptions That Now Look Weak

- `Inferred:` “start with a thin standalone `vid_voodoo4.c`” is not supported by ROM evidence yet. The ROM more strongly supports register-path reuse than file-level separation.
- `Verified:` any claim that the Voodoo 4 ROM bypasses legacy VGA behavior is contradicted by the ROM itself.
- `Verified:` any claim that the public open-source register picture is VSA-100-complete is overstated. Linux `tdfxfb` explicitly notes incomplete public documentation and dependence on XF86 patches plus Banshee-era information.
