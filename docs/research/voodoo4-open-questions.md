# Voodoo 4 Open Questions and Risks

Date: 2026-03-11

## Highest-Priority Open Questions

1. `Unknown:` what exact BIOS ROM-dispatch or shadow-handoff condition prevents the Voodoo4 entry helper from reaching its first mandatory ext write after the ROM header is inspected?
2. `Unknown:` what exact reset-time values should `121a:0009` expose for memory and strap-related registers before the ROM touches them, once ROM execution begins?
3. `Unknown:` does 86Box already provide enough of the extended register block for the ROM to POST once PCI identity and ROM wiring are corrected, now that Phase 1 has narrowed the first confirmed coverage gap to offset `0x70`?
4. `Inferred:` does the Voodoo 4 ROM depend on a later dispatch-side check rather than another early PCI config-space capability mismatch, now that the current AGP Voodoo3/Voodoo4 shell matches for the bytes the `p5a` BIOS actually probes?
5. `Unknown:` how much of the Voodoo4/Voodoo5 distinction matters to BIOS POST versus only to later protected-mode drivers?
6. `Unknown:` whether the declared ROM image size mismatch (`0xa000` declared, `0x10000` dumped) will matter to emulation or is just dump padding.
7. `Unknown:` what exact subsystem vendor/device pair should a retail `Voodoo4 4500 AGP` expose at PCI `0x2c-0x2f`, now that the current `121a:0009` value is only a minimal hypothesis probe?

## Specific Risks

### Reuse risk

- `Inferred:` the biggest planning risk is over-correcting into a full standalone Voodoo 4 model before exhausting Banshee/Voodoo3 reuse.
- `Impact:` duplicated code, duplicated bugs, and slower validation.

### Under-modeling risk

- `Inferred:` the opposite risk is assuming the current Banshee/Voodoo3 path is “close enough” without checking VSA-100-specific PCI identity, memory sizing, and unimplemented ext registers such as `0x70`.
- `Impact:` ROM POST may fail for reasons that look like VGA issues but are really device-identity or register-coverage gaps.

### ROM-dispatch risk

- `Verified:` the current runtime trace reaches PCI discovery and ROM BAR header reads, and after a minimal subsystem-ID probe it also reaches a nonzero PCI `0x2c-0x2f` block, but still not any Voodoo4-specific register activity.
- `Verified:` the Voodoo4 ROM's declared `0xa000` image checksum sums to zero, and its `PCIR` structure remains syntactically valid.
- `Verified:` the Voodoo4 ROM entry helper reached from header offset `0x00f1` would write ext `+0x28` even on its failure path, and ext `+0x70` on its normal path, before later init routines.
- `Inferred:` because no Voodoo4 ext write is observed at all, the current `p5a` boot is stopping before that helper meaningfully executes.
- `Impact:` deeper register/default work may be irrelevant until the ROM-execution handoff itself is understood.

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
- `Verified:` does a nonzero Voodoo4 subsystem-ID block at PCI `0x2c-0x2f` unlock any early ext-register traffic?
- `Verified:` does the current AGP Voodoo3/Voodoo4 shell already match for the PCI config/capability bytes that the `p5a` BIOS actually probes after subsystem IDs?
- `Verified:` which of the ROM-touched offsets already have matching behavior in `vid_voodoo_banshee.c`, and which currently fall through or behave differently?
- `Verified:` is `0x70` the first missing register required for ROM progress, or do earlier failures happen sooner?
- `Verified:` which current shared defaults are merely inherited Banshee/Voodoo3 behavior rather than source-backed Voodoo 4 facts?
- `Verified:` does the runtime trace show actual ROM execution, or only ROM header inspection through the ROM BAR?
- `Verified:` would the Voodoo4 ROM entry helper touch ext registers before any later VSA-100-specific setup if it were dispatched?

### Inference-check questions

- `Inferred:` does Voodoo 4 share enough of Banshee/Voodoo3 scanout to reuse current SVGA/display timing logic through VBE mode set?
- `Inferred:` does Voodoo 4 need only a new identity/config shell plus a few register deltas for milestone-one VGA/VBE bring-up?
- `Inferred:` is the next useful instrumentation point the generic BIOS option-ROM dispatch/shadow path rather than another Voodoo4 PCI config tweak?

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
