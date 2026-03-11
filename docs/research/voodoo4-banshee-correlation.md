# Voodoo 4 ROM vs 86Box Banshee/Voodoo3 Correlation

Date: 2026-03-11

Primary local comparison target: `src/video/vid_voodoo_banshee.c`

Primary external register reference: Linux `tdfx.h` at <https://codebrowser.dev/linux/linux/include/video/tdfx.h.html>

## Why This Comparison Matters

- `Verified:` the local ROM uses port-based accesses to an extended register block whose offsets line up with Banshee/Voodoo3-era documentation.
- `Verified:` 86Box already models a large part of that register block in `vid_voodoo_banshee.c`.
- `Inferred:` the default engineering stance should be to prove where reuse works before creating a new standalone Voodoo 4 implementation path.

## Shared Register Map Evidence

Local 86Box defines these offsets in `vid_voodoo_banshee.c`:

- `Init_dramInit1 = 0x1c`
- `Init_vgaInit0 = 0x28`
- `Init_vgaInit1 = 0x2c`
- `PLL_pllCtrl0 = 0x40`
- `DAC_dacMode = 0x4c`
- `Video_vidProcCfg = 0x5c`
- `Video_vidScreenSize = 0x98`
- `Video_vidDesktopStartAddr = 0xe4`
- `Video_vidDesktopOverlayStride = 0xe8`

Linux `tdfx.h` defines the same offsets under the names:

- `DRAMINIT1 = 0x1c`
- `VGAINIT0 = 0x28`
- `VGAINIT1 = 0x2c`
- `PLLCTRL0 = 0x40`
- `DACMODE = 0x4c`
- `VIDPROCCFG = 0x5c`
- `VIDSCREENSIZE = 0x98`
- `VIDDESKSTART = 0xe4`
- `VIDDESKSTRIDE = 0xe8`

### Correlation table

| ROM offset | ROM behavior | 86Box Banshee/V3 meaning | Correlation |
| --- | --- | --- | --- |
| `0x1c` | RMW and bit-set during init | `Init_dramInit1` | `Verified` |
| `0x28` | RMW; bit `0x40` forced in one helper | `Init_vgaInit0` | `Verified` |
| `0x2c` | written during mode/init path | `Init_vgaInit1` | `Verified` |
| `0x40` | written during init | `PLL_pllCtrl0` | `Verified` |
| `0x4c` | zeroed during init | `DAC_dacMode` | `Verified` |
| `0x5c` | RMW during init | `Video_vidProcCfg` | `Verified` |
| `0x70` | RMW during init | Linux `VIDINFORMAT`; not modeled in 86Box ext switch today | `Verified` offset, `Unknown` semantics in 86Box |
| `0x98` | programmed during init | `Video_vidScreenSize` | `Verified` |
| `0xe4` | written during init and helper paths | `Video_vidDesktopStartAddr` | `Verified` |
| `0xe8` | written during init and helper paths | `Video_vidDesktopOverlayStride` | `Verified` |

## Behavior Clearly Shared

- `Verified:` the ROM expects the same broad extended-register layout that 86Box already uses for Banshee/Voodoo3.
- `Verified:` the ROM touches `VGAINIT0`, `VGAINIT1`, `DACMODE`, `VIDPROCCFG`, desktop start, and desktop stride, all of which are already present in 86Box Banshee/Voodoo3 handling.
- `Verified:` Linux `tdfx.h` exposes the same offset naming, which independently supports the 86Box register map.
- `Verified:` Linux `tdfxfb` groups Voodoo3/4/5 together as closely related framebuffer hardware and explicitly distinguishes them from Voodoo1/2.
- `Inferred:` ROM POST and initial framebuffer bring-up are likely much closer to Banshee/Voodoo3 than to a wholly new hardware model.

## Behavior Clearly Different

- `Verified:` the ROM wants PCI device ID `0x0009`, while the current 86Box Banshee/Voodoo3 path only reports `0x0003` or `0x0005`.
- `Verified:` the tested ROM also validates PCI subsystem tuple `121a:0004` at `0x2c-0x2f`; a merely nonzero probe there was not sufficient.
- `Verified:` Linux PCI IDs also map `0x0009` to the Voodoo4/5 family name (`PCI_DEVICE_ID_3DFX_VOODOO5` in current kernel headers).
- `Verified:` Linux `tdfxfb` uses a different memory-sizing branch for `dev_id >= PCI_DEVICE_ID_3DFX_VOODOO5`, which means Voodoo4/5 were not treated as identical to Banshee/Voodoo3 for all memory assumptions.
- `Verified:` XFree86 DRI documentation says Voodoo4/5 require different Glide library versions than Banshee/Voodoo3.
- `Inferred:` the PCI-facing identity and at least some later memory or 3D-family behavior differ enough that a pure “rename Voodoo3 to Voodoo4” approach is unlikely to hold.

## Behavior Still Unknown

- `Unknown:` whether the ROM needs different power-on/reset defaults in registers that 86Box already models.
- `Unknown:` how much of `0x70` and adjacent video-input/overlay state matters for plain VGA/VBE success.
- `Unknown:` whether VSA-100 strap, SDRAM sizing, or scanout assumptions require changes inside shared Banshee/Voodoo3 code rather than a device-identity layer.
- `Unknown:` whether any later driver-visible MMIO windows diverge enough to require new shared-core types or new register handlers.
- `Unknown:` whether the failing `32-bit` desktop mode also uses tiled scanout in the same family as the traced good `16-bit` mode.

## Phase 1 Gap Audit

This section records the pre-implementation audit result that drove the first minimal code delta. Current implementation status is tracked separately below.

### PCI identity, BARs, and ROM exposure

| Surface | Current 86Box Banshee/V3 state | Voodoo 4 ROM expectation | Audit result |
| --- | --- | --- | --- |
| PCI vendor/device ID | `Verified:` vendor `121a`; device `0003` for Banshee or `0005` for Voodoo3 in `banshee_pci_read()` | `Verified:` ROM PCI BIOS search uses `121a:0009` | `Missing:` current reusable path cannot satisfy the ROM's PCI discovery without a `0009` identity layer |
| PCI class code | `Verified:` class code reads as VGA-compatible display (`0x03/0x00/0x00`) | `Verified:` ROM `PCIR` class code is VGA-compatible display | `Already modeled:` no Voodoo 4-specific delta proven here |
| BAR0 / BAR1 / I/O BAR shape | `Verified:` BAR0 = MMIO/register window, BAR1 = linear framebuffer window, BAR2 = I/O base; AGP capability pointer is already present on AGP variants | `Inferred:` this family-level BAR shape matches the later `tdfxfb` split between register/MMIO and framebuffer resources and does not contradict ROM findings | `Already modeled:` current reusable shape is a fit until runtime evidence says otherwise |
| ROM BAR enable path | `Verified:` PCI ROM BAR enable bit and base are already modeled at `0x30-0x33`; ROM mapping is disabled until enabled | `Verified:` Voodoo 4 bring-up needs a ROM-bearing device path | `Modeled but suspect:` the mechanism exists, but there is no Voodoo 4 device wired to the local ROM yet |
| Shared reset/default assumptions | `Verified:` SDRAM BIOS variants currently reuse the shared path that hardcodes 16 MB for SDRAM cards with BIOSes; `Init_strapInfo` still returns `0x00000040` with an `8 MB SGRAM, PCI` comment | `Unknown:` no fresh evidence yet proves the correct Voodoo 4 reset/default strap picture | `Modeled but suspect:` reuse is still the right starting point, but these defaults are not source-backed for Voodoo 4 |

### ROM-touched ext-register gap table

| Offset | Current 86Box handling | Classification | Audit note |
| --- | --- | --- | --- |
| `0x1c` | `Init_dramInit1` storage/readback exists | `Modeled but suspect` | `Verified:` the ROM does RMW here. `Inferred:` current storage/readback may be enough for POST, but Voodoo 4 SDRAM defaults are not yet proven. |
| `0x28` | `Init_vgaInit0` read/write exists and affects timing/RAMDAC behavior | `Already modeled` | `Verified:` the ROM's `OR 0x40` path lines up with existing `vgaInit0`-driven timing behavior. |
| `0x2c` | `Init_vgaInit1` read/write exists and updates bank/packed VGA state | `Already modeled` | `Verified:` the ROM's paired `0x28/0x2c` programming fits the shared VGA init path. |
| `0x40` | `PLL_pllCtrl0` read/write exists and feeds display timing calculations | `Already modeled` | `Verified:` current code already consumes the stored PLL fields during timing recalculation. |
| `0x4c` | `DAC_dacMode` read/write exists and updates DPMS/timing state | `Already modeled` | `Verified:` early ROM zeroing has a direct match in the current shared handler. |
| `0x5c` | `Video_vidProcCfg` read/write exists and drives desktop/overlay/display mode state | `Already modeled` | `Verified:` this is one of the strongest reuse points in the current path. |
| `0x70` | no ext-register case in `banshee_ext_inl()` / `banshee_ext_outl()` | `Missing` | `Verified:` the ROM performs RMW on this offset; today it falls through as an unhandled ext register. |
| `0x98` | `Video_vidScreenSize` read/write exists and updates visible dimensions | `Already modeled` | `Verified:` current shared code wires this register into scanout state. |
| `0xe4` | `Video_vidDesktopStartAddr` read/write exists and updates desktop start address | `Already modeled` | `Verified:` current shared code consumes it in scanout setup. |
| `0xe8` | `Video_vidDesktopOverlayStride` read/write exists and updates desktop/overlay stride | `Already modeled` | `Verified:` current shared code consumes it in scanout setup. |

## Smallest Proven Delta Set After Phase 1

- `Verified:` a Voodoo 4 reuse-first path still needs a PCI identity layer for device ID `121a:0009`.
- `Verified:` a Voodoo 4 reuse-first path still needs ROM exposure wired to the local Voodoo 4 BIOS image.
- `Verified:` ext offset `0x70` is the first audited ROM-touched offset that is actually missing from the current shared ext-register switch.
- `Verified:` a merely nonzero subsystem-ID block is not enough; the tested ROM expects subsystem tuple `121a:0004` at PCI `0x2c-0x2f`.
- `Verified:` the four deltas above are enough for ROM POST plus baseline Windows desktop bring-up.
- `Unknown:` whether current shared SDRAM/strap defaults remain acceptable beyond that baseline.

## Current Implementation Status

- `Verified:` the shared Banshee/Voodoo3 file now exposes a reuse-first `3dfx Voodoo4 4500` AGP device entry with PCI device ID `121a:0009`.
- `Verified:` that device is wired to the local `V4_4500_AGP_SD_1.18.rom` image rather than a new standalone Voodoo 4 source file.
- `Verified:` ext offset `0x70` now has shared ext-register read/write coverage as preserved state.
- `Verified:` the Voodoo4 subsystem tuple in `vid_voodoo_banshee.c` now matches the ROM-backed value `121a:0004`.
- `Verified:` runtime tracing now reaches the first Voodoo4 ext-register writes once that subsystem tuple is corrected.
- `Verified:` manual VM verification now reaches the Windows desktop, with Windows identifying `Voodoo4 4500 AGP`, and has been manually tested through at least `800x600` `16-bit`.
- `Verified:` targeted mode-state tracing on a longer live V4 Windows boot now shows the good desktop path programming `VIDPROCCFG` with `pixfmt=1` and `VIDPROCCFG_DESKTOP_TILE=1`, ultimately selecting the existing `16bpp_tiled` renderer.
- `Verified:` the bad `800x600` `32-bit` trace later proved that the failing path also kept desktop tiling enabled while still selecting the linear `32bpp` renderer.
- `Verified:` the shared file now has a custom tiled `32-bit` desktop renderer in addition to `banshee_render_16bpp_tiled()`.
- `Verified:` manual VM retests now show the tiled `32-bit` renderer fix working at `640x480`, `800x600`, `1024x768`, and `1280x1024`.

## Corrections to Earlier Voodoo 4 Assumptions

- `Verified:` treating offsets `0x28`, `0x2c`, and `0x40` as command-FIFO meanings in the ROM is not supported by the observed access path. The ROM reaches them through its extended I/O base, so the stronger reading is `VGAINIT0`, `VGAINIT1`, and `PLLCTRL0`.
- `Verified:` the ROM does not support the idea that Voodoo 4 initialization skips the classic VGA block.
- `Inferred:` planning around a mandatory standalone `vid_voodoo4.c` before validating reuse is premature.
- `Verified:` the current shared path still contains assumptions that are too specific to earlier Banshee/Voodoo3 boards to be treated as Voodoo 4 facts, especially the SDRAM `16 MB` BIOS default and the `Init_strapInfo` comment/value that describe an `8 MB SGRAM, PCI` card.

## Source Notes

- Linux `tdfx.h`: <https://codebrowser.dev/linux/linux/include/video/tdfx.h.html>
- Linux `tdfxfb`: <https://codebrowser.dev/linux/linux/drivers/video/fbdev/tdfxfb.c.html>
- Linux PCI IDs: <https://codebrowser.dev/linux/linux/include/linux/pci_ids.h.html>
- XFree86 3Dfx support status: <https://www.xfree86.org/4.2.0/Status2.html>
- XFree86 DRI notes: <https://www.xfree86.org/4.1.0/DRI10.html>
