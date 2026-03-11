# Voodoo 4 Tracker

Date: 2026-03-11

Purpose: keep a running status of what is complete, what is next, and what remains blocked or unknown for the Voodoo 4 restart.

## Status Snapshot

- Overall phase: `Phase 1 audit complete; ROM shadow/dispatch and early init are proven; manual VM verification reaches the Windows desktop with the Voodoo4 driver installed, common desktop 32-bit tiled scanout is proven, and the live boundary is now inconsistent higher-VRAM desktop population on the 32 MB path`
- Primary strategy: `Reuse-first over Banshee/Voodoo3 until disproven`
- Evidence baseline: `ROM analysis + source-backed register correlation + Phase 1 code audit + current high-base 2D/LFB runtime tracing`

## Done

- [x] Ground branch `voodoo4-restart` and repo state
- [x] Verify local ROM placement and inspect ROM size/header
- [x] Identify ROM header strings and `PCIR` location
- [x] Decode `PCIR` vendor/device/class/image metadata
- [x] Map ROM entry path from header jump to early init routines
- [x] Identify ROM `INT 10h` / VBE dispatch behavior
- [x] Identify likely PCI BIOS discovery routines in the ROM
- [x] Extract likely register offsets and access patterns from ROM disassembly
- [x] Correlate ROM register usage against `src/video/vid_voodoo_banshee.c`
- [x] Collect online primary-source references and historical open-source driver context
- [x] Write fresh research index
- [x] Write ROM analysis document
- [x] Write register/code correlation document
- [x] Write open questions and risks document
- [x] Rewrite recovery plan from scratch
- [x] Add executive summary
- [x] Add changelog
- [x] Add tracker
- [x] Audit current Banshee/Voodoo3 PCI identity against the Voodoo 4 ROM's `121a:0009` expectation
- [x] Audit current BAR shape and ROM BAR handling against the reuse-first baseline
- [x] Build a concrete gap table for ROM-touched offsets `0x1c`, `0x28`, `0x2c`, `0x40`, `0x4c`, `0x5c`, `0x70`, `0x98`, `0xe4`, `0xe8`
- [x] Confirm that `0x70` is the first audited ROM-touched ext offset that is actually missing from the shared handler
- [x] Call out unsupported shared assumptions that should not be promoted to Voodoo 4 fact (`16 MB` SDRAM BIOS default, `Init_strapInfo` comment/value)
- [x] Add a reuse-first Voodoo 4 4500 AGP device entry on top of `vid_voodoo_banshee.c`
- [x] Wire the new device to the local `V4_4500_AGP_SD_1.18.rom`
- [x] Add ext-register read/write coverage for offset `0x70`
- [x] Verify the branch still builds successfully after the minimal delta set
- [x] Identify the active runtime target as `/Users/anthony/Library/Application Support/86Box/Virtual Machines/v4/86box.cfg`
- [x] Reproduce the blank-screen setup against that `v4` VM on machine `p5a`
- [x] Add minimal Voodoo4-only startup tracing for PCI discovery, ROM mapping, ROM reads, and early ext-register/display-state boundaries
- [x] Verify at runtime that the guest reads PCI ID `121a:0009`, assigns the I/O BAR, and toggles the ROM BAR
- [x] Verify at runtime that the ROM header is readable through the ROM BAR at `0xe7ef0000`
- [x] Verify at runtime that no Voodoo4 ext-register touches were observed after ROM header inspection
- [x] Verify at runtime that no legacy sub-`1 MB` ROM fetches were observed in the traced boot window
- [x] Compare the Voodoo4 blank-screen trace against the known-good `Voodoo3 3000 AGP` bring-up on the same `p5a` machine family
- [x] Verify the first concrete V3/V4 config-space divergence is that Voodoo4 exposes `0x0000:0000` at PCI `0x2c-0x2f` where the working Voodoo3 exposes a nonzero subsystem ID block
- [x] Add a VM-driven trace test for the Voodoo4 blank-screen boundary at PCI `0x2c-0x2f`
- [x] Verify that a minimal nonzero Voodoo4 subsystem-ID probe (`121a:0009`) removes the all-zero block but still does not unlock early ext-register traffic
- [x] Compare the current full Voodoo3/Voodoo4 AGP PCI config/capability layout in `banshee_pci_read()` and verify that the shared shell is identical across `0x04-0x67` except for the device ID and subsystem-device bytes
- [x] Verify on the `p5a` boot path that, after the subsystem-ID probe, the BIOS still reads the same later PCI bytes (`0x3c-0x3f`, `0x38-0x3b`, `0x34-0x37`, `0x28-0x2b`) on both Voodoo3 and Voodoo4, but only Voodoo3 transitions into ext-register activity
- [x] Verify the Voodoo4 ROM's declared `0xa000` image has a correct zero checksum and standard `PCIR` metadata, weakening a simple generic ROM-header/checksum rejection theory
- [x] Disassemble the Voodoo4 ROM entry helper at `0x865c` and verify that it would write ext `+0x28` on both success and failure paths, and ext `+0x70` on the normal path, before later init work
- [x] Determine the next pre-ext evidence boundary after subsystem-ID reads: ROM dispatch/handoff is stopping before the Voodoo4 entry helper reaches its first traced ext write
- [x] Trace the generic `p5a` option-ROM handoff closely enough to prove that the BIOS shadows the Voodoo4 ROM from the PCI ROM BAR into `0xc0000` and executes it at `c000:0003`
- [x] Trace the shadowed execution path far enough to verify that the ROM reaches `0x3e6e`, enters helper `0x865c`, and runs the PCI config-validation loop before any ext write is seen
- [x] Verify that helper `0x3db2` compares PCI `0x2c-0x2f` against the dword stored at the end of the declared ROM image rather than merely checking for a nonzero subsystem block
- [x] Verify that the tested Voodoo4 ROM encodes subsystem pair `121a:0004` at declared-image offset `0x9ff8`, while the working Voodoo3 ROM encodes `121a:003a` at its analogous location
- [x] Replace the provisional Voodoo4 subsystem-ID probe with the ROM-matched tuple `121a:0004`
- [x] Verify that the ROM-matched subsystem tuple unlocks the first Voodoo4 ext-register traffic, including writes to ext `+0x28`, ext `+0x70`, `DACMODE`, desktop start, screen size, and stride
- [x] Verify by manual VM boot that the current Voodoo4 path reaches the Windows desktop in `640x480` `16-color` mode and that Windows identifies the adapter as `Voodoo4 4500 AGP`
- [x] Verify by manual VM testing that the current Voodoo4 path also reaches `800x600` at `16-bit` color
- [x] Verify by manual VM testing that the current Voodoo4 path reaches `1024x768` at `16-bit` color
- [x] Verify by user report that a Voodoo4 driver is installed and active in the working configuration
- [x] Add targeted mode-state tracing in `vid_voodoo_banshee.c` for `DACMODE`, `VIDPROCCFG`, `VIDINFORMAT`, screen size, desktop start, desktop stride, and selected renderer
- [x] Rebuild successfully after the targeted tracing delta
- [x] Re-run `bash scripts/test-voodoo4-blank-boundary.sh` successfully after the tracing delta
- [x] Capture a longer live V4 Windows boot trace at `/tmp/voodoo4-mode-boundary.log`
- [x] Verify from that live V4 Windows trace that the working driver-enabled desktop path programs tiled `16-bit` scanout (`pixfmt=1`, `tile=1`) and lands on the existing `16bpp_tiled` renderer
- [x] Verify locally that the shared code still has no custom tiled desktop renderer for `24-bit` or `32-bit`; only the `16-bit` tiled path exists today
- [x] Reconfirm that the active VM was returned to `16-bit` color before pausing the investigation
- [x] Reproduce the distorted `800x600` `32-bit` mode with tracing still enabled on 2026-03-11
- [x] Verify from the new bad-mode trace that the failing `800x600` `32-bit` path programs tiled desktop scanout (`pixfmt=3`, `tile=1`) rather than a separate non-tiled path
- [x] Verify from the same bad-mode trace that the failing path was still landing on the generic linear `32bpp` renderer before the next code change
- [x] Implement the smallest renderer-side code delta suggested by that trace: a tiled `32-bit` desktop renderer in the shared Banshee/Voodoo3 path
- [x] Rebuild successfully after the tiled `32-bit` renderer delta
- [x] Re-run `bash scripts/test-voodoo4-blank-boundary.sh` successfully after the tiled `32-bit` renderer delta
- [x] Verify by manual VM retest on 2026-03-11 that `800x600` `32-bit` now looks correct after the tiled `32-bit` renderer change
- [x] Verify from the post-fix trace that the working `800x600` `32-bit` mode still programs tiled scanout and now resolves onto `32bpp_tiled`
- [x] Verify by manual VM retest on 2026-03-11 that `1024x768` `32-bit` now also looks correct after the tiled `32-bit` renderer change
- [x] Verify from the post-fix trace that the working `1024x768` `32-bit` mode resolves onto `32bpp_tiled`
- [x] Verify by manual VM retest on 2026-03-11 that `640x480` `32-bit` also looks correct after the tiled `32-bit` renderer change
- [x] Verify by manual VM retest on 2026-03-11 that `1280x1024` `32-bit` also looks correct after the tiled `32-bit` renderer change
- [x] Remove the temporary Voodoo4 mode-state tracing after the `32-bit` desktop boundary was verified
- [x] Rebuild successfully after removing the temporary mode-state tracing
- [x] Re-run `bash scripts/test-voodoo4-blank-boundary.sh` successfully after removing the temporary mode-state tracing
- [x] Add a Voodoo4-specific `32 MB` SDRAM device config and stop the reuse-first Voodoo4 path from inheriting the shared `16 MB` SDRAM default
- [x] Rebuild successfully after the Voodoo4 `32 MB` memory-config change
- [x] Re-run `bash scripts/test-voodoo4-blank-boundary.sh` successfully after the Voodoo4 `32 MB` memory-config change
- [x] Prove from a fresh log and 3DMark99 that guest-visible Voodoo4 VRAM now reports `32 MB`
- [x] Reproduce the new post-`32 MB` symptom: desktop distortion once the driver uses the higher-VRAM desktop surface
- [x] Re-enable narrow mode-state tracing to capture the `32 MB` desktop path
- [x] Correct the working-history baseline: the older good Voodoo4 desktop state before the guest-visible memory fixes was effectively an `8 MB` path, not a verified good `16 MB` Voodoo4 path
- [x] Compare the bad `32 MB` mode-state trace against the older working pre-`32 MB` trace and confirm that the desktop start address moves upward while the mode still lands on `32bpp_tiled`
- [x] Try and revert a V4-only desktop-base alias experiment that made the desktop worse
- [x] Try and revert a V4-only zero-`lfbMemoryConfig` LFB guard that did not change the distortion
- [x] Add V4-only high-base `2D` tracing in `vid_voodoo_banshee_blitter.c` for `rectfill`, `host_to_screen`, and `screen_to_screen`
- [x] Verify from fresh traces that the high-base `2D` desktop path targets `dstBase=00d00000`
- [x] Sample high-base `screen_to_screen` copies and verify they are internally consistent, including cases that copy zero-filled linear source data into visible desktop tiles
- [x] Add V4-only high-base linear/LFB tracing in `vid_voodoo_banshee.c`
- [x] Verify from fresh traces that the linear/LFB path writes to tiled base `0x01d00000` while desktop scanout and traced `2D` destinations remain at `0x00d00000`
- [x] Verify from fresh manual screenshots that the remaining failure is inconsistent population: some windows/icons/labels render correctly while other desktop regions remain black or missing

## Next

- [ ] Determine whether the `0x01d00000` versus `0x00d00000` split is the root Voodoo4 higher-half LFB/address-translation bug or a downstream symptom of another population path
- [ ] Trace which path leaves some linear source surfaces zero or stale before `screen_to_screen` copies move them into the visible tiled desktop
- [ ] Keep the current narrow high-base `2D` and LFB tracing only until the higher-VRAM desktop mismatch is localized

## Open Questions

- `Verified:` the earlier concrete `32-bit` desktop distortion boundary is now closed for the manually tested common modes `640x480`, `800x600`, `1024x768`, and `1280x1024`
- `Verified:` the current Banshee/Voodoo3 reuse path is now sufficient for at least ROM POST plus Windows desktop bring-up through `1024x768` `16-bit`
- `Verified:` the traced good V4 Windows desktop path uses tiled `16-bit` scanout
- `Verified:` the traced bad `800x600` `32-bit` V4 Windows path also uses tiled desktop scanout (`pixfmt=3`, `tile=1`)
- `Verified:` before the latest code change, that traced bad tiled `32-bit` path still selected the generic linear `32bpp` renderer
- `Verified:` after the latest code change, manual VM retest shows `800x600` `32-bit` looking correct and the post-fix trace resolves that mode onto `32bpp_tiled`
- `Verified:` after the same code change, manual VM retest also shows `1024x768` `32-bit` looking correct and the post-fix trace resolves that mode onto `32bpp_tiled`
- `Verified:` additional manual VM retests now also show `640x480` `32-bit` and `1280x1024` `32-bit` looking correct
- `Verified:` the strongest current emulator-side explanation for the reproduced `800x600` `32-bit` distortion was the missing tiled `32-bit` desktop renderer, not a broad generic `32-bit` or ROM-dispatch theory
- `Verified:` the emulator-side Voodoo4 device config now exposes and defaults to `32 MB` SDRAM instead of inheriting the shared `16 MB` SDRAM default
- `Verified:` after the Voodoo4-specific guest-visible memory/strap changes, 3DMark99 now reports roughly `31207 KB`, so the guest-visible VRAM sizing boundary is closed
- `Verified:` the fresh bad `32 MB` desktop trace still uses `pixfmt=3`, `tile=1`, and `render=32bpp_tiled`
- `Verified:` the older working pre-`32 MB` desktop baseline was effectively an `8 MB` path, not a verified good `16 MB` Voodoo4 path
- `Verified:` compared with that older working pre-`32 MB` trace, the fresh bad `32 MB` desktop trace moves the desktop start address upward into higher VRAM
- `Verified:` fresh high-base `2D` traces show `rectfill`, `host_to_screen`, and `screen_to_screen` activity targeting `dstBase=00d00000`
- `Verified:` sampled `screen_to_screen` copies are internally consistent, including later copies that faithfully move zero-filled linear source data into visible desktop tiles
- `Verified:` fresh linear/LFB traces now show writes landing at tiled base `0x01d00000` while desktop scanout and traced `2D` destinations remain at `0x00d00000`
- `Verified:` the latest user screenshots show inconsistent rendering: some windows, icons, and labels render correctly while other desktop regions remain black or missing
- `Inferred:` the active remaining bug is no longer “still only `8 MB`” or “missing tiled `32-bit` desktop rendering”; it is a higher-VRAM desktop-surface population/address-translation mismatch
- `Unknown:` does Voodoo 4 require different reset defaults for existing modeled registers such as `Init_dramInit1`?
- `Inferred:` another early PCI shell mismatch is now less likely than before, because the ROM-dispatch boundary was cleared by matching the subsystem tuple that the ROM itself validates.
- `Unknown:` does the shared `16 MB` SDRAM BIOS default misstate Voodoo 4 memory sizing early enough to matter during POST?
- `Unknown:` whether any less common desktop timings outside the now-tested `640x480`, `800x600`, `1024x768`, and `1280x1024` `32-bit` modes expose another tiled-path mismatch
- `Unknown:` whether the exact fix is a V4-only linear/LFB higher-half alias/fold or another register/path that should reconcile `0x01d00000` and `0x00d00000`
- `Unknown:` whether the higher-VRAM desktop surface is being corrupted by zero/stale source surfaces, LFB writes, or desktop scanout interpretation
- `Unknown:` what additional Voodoo4/VSA-100 differences matter for `32-bit` color modes and beyond, now that the protected-mode driver path is known to be active?

## Blockers

- No hard blocker at research level.
- `Verified:` the earlier ROM-execution / pre-ext blocker is resolved.
- `Verified:` the earlier blank-screen blocker is resolved through desktop bring-up, higher-resolution `16-bit` mode validation, and a driver-enabled configuration.
- `Verified:` the earlier common tiled `32-bit` desktop boundary is also resolved for the manually tested modes.
- `Verified:` the active blocker is now the inconsistent higher-VRAM desktop path after `32 MB` becomes guest-visible.

## Working Rules

- `Verified:` no implementation should be driven by unsupported old-branch assumptions.
- `Verified:` major conclusions stay labeled `Verified`, `Inferred`, or `Unknown`.
- `Inferred:` when unsure, prefer proving reuse before creating a new Voodoo 4-specific code path.

## Reference Documents

- [Executive summary](./voodoo4-executive-summary.md)
- [Changelog](./voodoo4-changelog.md)
- [Research index](../research/voodoo4-index.md)
- [Recovery plan](../plans/voodoo4-recovery-plan.md)
