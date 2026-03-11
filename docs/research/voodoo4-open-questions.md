# Voodoo 4 Open Questions and Risks

Date: 2026-03-11

## Highest-Priority Open Questions

1. `Verified:` the first concrete runtime failure beyond the currently verified desktop modes is `32-bit` color distortion under the installed Voodoo4 driver.
2. `Unknown:` what exact runtime or register mismatch causes that `32-bit` color distortion when `1024x768` `16-bit` already works?
3. `Verified:` a longer live V4 Windows boot trace now shows the good desktop path programming tiled `16-bit` scanout (`pixfmt=1`, `tile=1`) and landing on the existing `16bpp_tiled` renderer.
4. `Inferred:` the strongest current code-side hypothesis is a missing tiled `24/32-bit` desktop render path, because the shared file has custom tiled handling only for `16-bit`.
5. `Unknown:` whether the failing `32-bit` mode also programs tiled desktop scanout in the same family as the traced good `16-bit` mode.
6. `Unknown:` what exact reset-time values should the Voodoo4 device expose for memory and strap-related registers before the ROM touches them further into POST?
7. `Verified:` 86Box now provides enough of the shared path for ROM POST plus driver-enabled Windows desktop bring-up through `1024x768` `16-bit` once PCI identity, ROM wiring, and the ROM-backed subsystem tuple are corrected.
8. `Inferred:` another early PCI config-space mismatch is now less likely than before, because the current `p5a` path does shadow and execute the ROM and the ROM-backed subsystem tuple clears the pre-ext gate.
9. `Unknown:` how much of the Voodoo4/Voodoo5 distinction matters to later protected-mode driver paths such as `32-bit` color modes?
10. `Unknown:` whether the declared ROM image size mismatch (`0xa000` declared, `0x10000` dumped) will matter to emulation or is just dump padding.
11. `Verified:` the tested `V4_4500_AGP_SD_1.18.rom` encodes subsystem pair `121a:0004` at the end of the declared image, and the ROM helper validates PCI `0x2c-0x2f` against that value.

## Specific Risks

### Reuse risk

- `Inferred:` the biggest planning risk is over-correcting into a full standalone Voodoo 4 model before exhausting Banshee/Voodoo3 reuse.
- `Impact:` duplicated code, duplicated bugs, and slower validation.

### Under-modeling risk

- `Inferred:` the opposite risk is assuming the current Banshee/Voodoo3 path is “close enough” without checking VSA-100-specific PCI identity, memory sizing, and unimplemented ext registers such as `0x70`.
- `Impact:` ROM POST may fail for reasons that look like VGA issues but are really device-identity or register-coverage gaps.

### ROM-dispatch risk

- `Verified:` the current runtime trace reaches PCI discovery and ROM BAR header reads, shadows the image into `0xc0000`, and executes the ROM at `c000:0003`.
- `Verified:` the Voodoo4 ROM's declared `0xa000` image checksum sums to zero, and its `PCIR` structure remains syntactically valid.
- `Verified:` the Voodoo4 ROM entry helper reached from header offset `0x00f1` would write ext `+0x28` even on its failure path, and ext `+0x70` on its normal path, before later init routines.
- `Verified:` helper `0x3db2` compares PCI `0x2c-0x2f` against the dword stored at the end of the declared ROM image, and the tested Voodoo4 ROM encodes `121a:0004` there.
- `Verified:` replacing the provisional `121a:0009` subsystem tuple with the ROM-backed `121a:0004` unlocks the first Voodoo4 ext-register writes.
- `Verified:` manual VM verification now shows Windows reaching the desktop in `640x480` `16-color` mode, identifying the adapter as `Voodoo4 4500 AGP`, successfully switching to `800x600` `16-bit`, and reaching `1024x768` `16-bit`.
- `Verified:` user testing now reports an installed Voodoo4 driver and distortion when switching into `32-bit` color.
- `Verified:` targeted mode-state tracing captured the good V4 Windows desktop path using tiled `16-bit` scanout.
- `Inferred:` if the bad `32-bit` path also sets `tile=1`, the current lack of tiled `24/32-bit` desktop rendering becomes a direct emulator-side suspect.
- `Impact:` the ROM-execution handoff itself is no longer the blocker; the next useful work is to identify the first mismatch on the `32-bit` color path.

### Shared-defaults risk

- `Verified:` the current shared SDRAM BIOS path still carries assumptions that are not validated for Voodoo 4, including a `16 MB` default and an `Init_strapInfo` value/comment that describe an `8 MB SGRAM, PCI` board.
- `Impact:` even a successful reuse-first device shell may fail later because a shared default is wrong, not because the reuse architecture is wrong.

### Documentation risk

- `Verified:` Linux `tdfxfb` itself warns that public documentation was incomplete and that some practical knowledge came from XF86 patches rather than a complete public spec.
- `Impact:` any plan that labels VSA-100 register semantics as settled would overstate the evidence.

### Historical-branch contamination risk

- `Inferred:` old Voodoo 4 implementation attempts may contain useful symptoms or traces, but they should not be allowed to define the architecture of the restart.
- `Impact:` unsupported assumptions can become “facts” by repetition.

## Questions to Answer Before Coding

### Verified-first questions

- `Verified:` does a minimal `121a:0009` PCI identity plus ROM exposure let the ROM reach the same ext-register touches already observed in disassembly?
- `Verified:` does a merely nonzero Voodoo4 subsystem-ID block at PCI `0x2c-0x2f` unlock any early ext-register traffic?
- `Verified:` does the ROM-backed subsystem tuple `121a:0004` unlock the first early ext-register traffic? Yes.
- `Verified:` does the current AGP Voodoo3/Voodoo4 shell already match for the PCI config/capability bytes that the `p5a` BIOS actually probes after subsystem IDs?
- `Verified:` which of the ROM-touched offsets already have matching behavior in `vid_voodoo_banshee.c`, and which currently fall through or behave differently?
- `Verified:` is `0x70` the first missing register required for ROM progress, or do earlier failures happen sooner?
- `Verified:` which current shared defaults are merely inherited Banshee/Voodoo3 behavior rather than source-backed Voodoo 4 facts?
- `Verified:` does the runtime trace show actual ROM execution, or only ROM header inspection through the ROM BAR?
- `Verified:` does the generic `p5a` path shadow the Voodoo4 ROM into `0xc0000` before execution? Yes.
- `Verified:` would the Voodoo4 ROM entry helper touch ext registers before any later VSA-100-specific setup if it were dispatched?
- `Verified:` does the current path reach a usable Windows desktop after the subsystem tuple correction? Yes, and manual testing now reaches at least `1024x768` `16-bit`.
- `Verified:` does the traced good V4 Windows desktop path use tiled desktop scanout? Yes, the captured `16-bit` path programs `pixfmt=1`, `tile=1`, and reaches `16bpp_tiled`.

### Inference-check questions

- `Inferred:` does Voodoo 4 share enough of Banshee/Voodoo3 scanout to reuse current SVGA/display timing logic through VBE mode set?
- `Inferred:` does Voodoo 4 need only a new identity/config shell plus a few register deltas for milestone-one VGA/VBE bring-up?
- `Inferred:` the next useful instrumentation point is now the first failing `32-bit` color mode-set path rather than the generic option-ROM dispatch/shadow path.
- `Inferred:` if the failing `32-bit` path also keeps `VIDPROCCFG_DESKTOP_TILE` set, then the smallest next fix is likely a tiled `32-bit` desktop renderer rather than a broader architecture change.

## Useful External Clues, With Limits

- Linux `tdfxfb` source: <https://codebrowser.dev/linux/linux/drivers/video/fbdev/tdfxfb.c.html>
  - `Verified:` Voodoo1/2 are treated as a different family from Voodoo3/4/5.
  - `Verified:` Voodoo4/5 share a later-family memory-sizing path.
- Linux `tdfx.h`: <https://codebrowser.dev/linux/linux/include/video/tdfx.h.html>
  - `Verified:` public register names exist for many offsets the ROM touches.
- XFree86 DRI notes: <https://www.xfree86.org/4.1.0/DRI10.html>
  - `Verified:` Banshee/Voodoo3 and Voodoo4/5 use different Glide library families.
- Mesa historical tdfx driver removal commit: <https://cgit.freedesktop.org/mesa/mesa/commit/?id=57871d7a1968190f4d903c2b50495d6390ab0af5>
  - `Verified:` Mesa historically carried a `tdfx` driver family.
  - `Unknown:` how much VSA-100-specific bring-up detail remains recoverable from that code without deeper archival digging.

## Working Rule Set for the Restart

- `Verified:` do not treat unverified old branch behavior as hardware fact.
- `Verified:` label every meaningful conclusion as `Verified`, `Inferred`, or `Unknown`.
- `Inferred:` treat Banshee/Voodoo3 reuse as the baseline until a concrete ROM trace or source-backed contradiction says otherwise.
