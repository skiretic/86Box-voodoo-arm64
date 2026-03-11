# Voodoo 4 Open Questions and Risks

Date: 2026-03-11

## Highest-Priority Open Questions

1. `Verified:` the working pre-`32 MB` Voodoo4 desktop baseline was effectively an `8 MB` path, not a separately verified good `16 MB` Voodoo4 path.
2. `Verified:` the earlier common desktop `32-bit` renderer boundary is now closed for the manually tested `640x480`, `800x600`, `1024x768`, and `1280x1024` modes.
3. `Verified:` a longer live V4 Windows boot trace showed the good desktop path programming tiled `16-bit` scanout (`pixfmt=1`, `tile=1`) and landing on the existing `16bpp_tiled` renderer.
4. `Verified:` the reproduced bad `800x600` `32-bit` V4 Windows mode also programmed tiled desktop scanout (`pixfmt=3`, `tile=1`) while still selecting the generic linear `32bpp` renderer in the pre-fix trace.
5. `Verified:` adding the shared tiled `32-bit` desktop renderer resolved that mismatch for the manually tested common desktop modes above.
6. `Unknown:` whether any less common desktop timings outside that tested set expose another tiled-path mismatch.
7. `Verified:` the emulator-side Voodoo4 device config now exposes and defaults to `32 MB` SDRAM instead of inheriting the shared `16 MB` SDRAM default.
8. `Verified:` guest-visible Voodoo4 memory reporting now also shows `32 MB`; 3DMark99 reports about `31207 KB`.
9. `Verified:` the fresh bad `32 MB` desktop trace still uses `pixfmt=3`, `tile=1`, and `render=32bpp_tiled`, so the remaining mismatch is not the old missing-renderer bug.
10. `Verified:` compared with the older working pre-`32 MB` trace, the fresh bad `32 MB` trace moves the desktop start address upward into higher VRAM.
11. `Verified:` fresh high-base `2D` traces show `rectfill`, `host_to_screen`, and `screen_to_screen` activity targeting desktop base `0x00d00000`.
12. `Verified:` sampled high-base `screen_to_screen` copies are internally consistent, including later copies that faithfully move zero-filled linear source data into visible desktop tiles.
13. `Verified:` fresh linear/LFB traces now show writes landing at tiled base `0x01d00000` while desktop scanout and traced `2D` destinations remain at `0x00d00000`, an exact `0x01000000` (`16 MB`) split.
14. `Verified:` the latest manual screenshots show the remaining `32 MB` desktop failure is inconsistent rather than uniformly broken: some windows, icons, and labels render correctly while other desktop regions remain black or missing.
15. `Unknown:` which exact path leaves some higher-VRAM source surfaces zero or stale before they are copied into the visible tiled desktop.
16. `Unknown:` what exact reset-time values should the Voodoo4 device expose for memory and strap-related registers before the ROM touches them further into POST?
17. `Verified:` 86Box now provides enough of the shared path for ROM POST plus driver-enabled Windows desktop bring-up through common `16-bit` and manually tested common `32-bit` desktop modes once PCI identity, ROM wiring, the ROM-backed subsystem tuple, and the new emulator-side `32 MB` Voodoo4 default are corrected.
18. `Inferred:` another early PCI config-space mismatch is now less likely than before, because the current `p5a` path does shadow and execute the ROM and the ROM-backed subsystem tuple clears the pre-ext gate.
19. `Inferred:` the strongest live lead is now a Voodoo4 higher-half linear/LFB or source-surface population mismatch, not a generic tiled scanout failure.
20. `Unknown:` whether the exact fix is a V4-only linear/LFB higher-half alias/fold or another register/path that should reconcile `0x01d00000` and `0x00d00000`.
21. `Unknown:` which richer driver-visible Voodoo4/VSA-100 behaviors, if any, still diverge beyond the current desktop baseline.
22. `Unknown:` whether the declared ROM image size mismatch (`0xa000` declared, `0x10000` dumped) will matter to emulation or is just dump padding.
23. `Verified:` the tested `V4_4500_AGP_SD_1.18.rom` encodes subsystem pair `121a:0004` at the end of the declared image, and the ROM helper validates PCI `0x2c-0x2f` against that value.

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
- `Verified:` the bad `32-bit` path does set `tile=1`, which made the current lack of tiled `32-bit` desktop rendering a direct emulator-side suspect.
- `Impact:` the ROM-execution handoff itself is no longer the blocker; the next useful work is to identify the first mismatch on the higher-VRAM `32 MB` desktop path.
- `Verified:` the first minimal renderer-side response to that trace has now been implemented in the shared path as a tiled `32-bit` desktop renderer.
- `Verified:` manual VM retest on 2026-03-11 now shows `800x600` `32-bit` looking correct after that change, and the post-fix trace resolves it onto `32bpp_tiled`.
- `Verified:` manual VM retest on 2026-03-11 also shows `1024x768` `32-bit` looking correct after that same change, and the post-fix trace resolves it onto `32bpp_tiled`.
- `Verified:` additional manual VM retests on 2026-03-11 also show `640x480` `32-bit` and `1280x1024` `32-bit` looking correct after that same change.
- `Verified:` after the later Voodoo4-specific memory/strap changes, the guest-visible VRAM-size question is also closed: 3DMark99 now reports about `31207 KB`.
- `Verified:` the remaining symptom after that `32 MB` report change is desktop distortion, not a fallback to `16-bit` or a return to `8 MB`.
- `Verified:` two small runtime experiments were tried against that new symptom and reverted: a desktop-base alias hack and a zero-`lfbMemoryConfig` LFB guard.
- `Verified:` fresh high-base `2D` traces now show the bad path launching `rectfill`, `host_to_screen`, and `screen_to_screen` operations directly against desktop base `0x00d00000`.
- `Verified:` sampled `screen_to_screen` copies are behaving consistently with their sources, including cases where zero-filled linear sources are copied into visible desktop tiles.
- `Verified:` fresh linear/LFB traces simultaneously show writes landing at tiled base `0x01d00000`, which is `16 MB` above the visible desktop base.
- `Unknown:` whether the remaining mismatch sits in linear/LFB writes, zero/stale source-surface population, or desktop scanout interpretation of the higher VRAM region.

### Higher-half population risk

- `Verified:` the visible bad desktop uses base `0x00d00000`, while traced linear/LFB writes land at `0x01d00000`.
- `Impact:` if those two regions are supposed to alias for Voodoo4, the current emulator path may be writing a valid higher-half surface that scanout never reads.

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
- `Verified:` the failing `32-bit` path keeps `VIDPROCCFG_DESKTOP_TILE` set.
- `Verified:` the smallest next fix suggested by that evidence, a tiled `32-bit` desktop renderer, has now been implemented.
- `Verified:` that fix is sufficient for the reproduced `800x600` `32-bit` mode.
- `Verified:` that fix is also sufficient for the manually tested `1024x768` `32-bit` mode.
- `Verified:` that fix is also sufficient for the manually tested `640x480` `32-bit` and `1280x1024` `32-bit` modes.
- `Unknown:` whether any further tiled desktop assumptions still need adjustment outside the now-tested desktop modes.
- `Verified:` the next live boundary is no longer “does `32 MB` report correctly?” but “why does the higher-VRAM tiled desktop surface distort once it does?”
- `Verified:` fresh high-base `2D` traces show the visible desktop operations targeting `0x00d00000`.
- `Verified:` fresh linear/LFB traces show another active path targeting `0x01d00000`.
- `Verified:` later sampled `screen_to_screen` copies also show that at least some visible corruption comes from copying already-zero linear sources, not from a trivially broken copy loop.
- `Unknown:` whether Voodoo4 linear/LFB translation should fold that `0x01d00000` region back onto the visible `0x00d00000` desktop base.

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
