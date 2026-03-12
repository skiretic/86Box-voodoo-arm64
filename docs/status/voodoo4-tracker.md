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
- [x] Add temporary watchpoint tracing for selected low linear source pages in `vid_voodoo_banshee.c` and `vid_voodoo_banshee_blitter.c`
- [x] Verify from a fresh logged desktop rerun that the known-good low linear page `0x00299e80` is populated by both decoded LFB writes (`0x12299e80 -> 0x00299e80`) and `host_to_screen` work
- [x] Verify from that same rerun that later `screen_to_screen` copies into the tiled desktop also use additional nonzero low linear sources such as `0x001ec948` and `0x001eb1cc`
- [x] Verify from that same rerun that the earlier zero-source page `0x002de9b0` did not reappear and no watched `bad` page traffic was observed
- [x] Add temporary V4-only `Init_lfbMemoryConfig` change tracing in `vid_voodoo_banshee.c` so the next run can show whether the guest explicitly programs the higher tiled base
- [x] Verify from a fresh logged V4 desktop run that the guest explicitly programs `Init_lfbMemoryConfig` to `tileBase=0x01d00000` while `vidDesktopStartAddr` remains `0x00d00000`
- [x] Reproduce a fresh failing low linear source sample at `0x001ef390` and compare it with a nearby good low source sample at `0x001edc10`
- [x] Verify from a later rerun with the new bad-vs-recovery low-linear result tracer that the session stayed on the healthy path: only `good0` (`0x00299e80`) produced low-linear result hits, while `bad0` (`0x002de000`) and `good2` (`0x002dc000`) did not reappear
- [x] Compare the latest alias-engaged run against the older bad source-population family and verify that this rerun instead repeatedly copies zeros from low-linear page `0x00133000` (`0x001332d4` / `0x00133308`) while nearby source page `0x00132000` (`0x00132400`) remains nonzero
- [x] Retarget the temporary low-linear watch pages so the next run can follow the newly reproduced `0x00132000` / `0x00133000` source family without dropping the older watched pages
- [x] Verify from a later live desktop run that the damaged band can also be blanked by a different zero low-linear source family at `0x002a0000` (`0x002a0084..0x002a0090`)
- [x] Retarget the temporary low-linear watch pages again so the next run also labels the newly verified `0x002a0000` blanking source as `bad3`
- [x] Tighten the temporary low-linear tracing so the blitter directly watches `good3` / `bad2` / `bad3` and early `good0` traffic no longer exhausts the watch counters before later bad-page families appear
- [x] Verify from a fresh rerun that the narrowed stride-packed host-row sizing change no longer waits for absurd `bad3` payload lengths (`lastByte=12`, not `65036`) and determine whether `bad3` still stays zero anyway
- [x] Compare the failing `bad3` `host_to_screen` command semantics against the healthy desktop mono overlay path and verify whether the bad command can populate an all-zero destination at all
- [x] Preserve a V4-specific research pass across the local Win9x `vs-w9x-1.04.01-beta` driver bundle and external Napalm/VSA-100 register documentation before proposing another runtime hypothesis

## Next

- [ ] Determine whether the `0x01d00000` versus `0x00d00000` split is the root Voodoo4 higher-half LFB/address-translation bug or a downstream symptom of another population path
- [ ] Capture the next logged V4 desktop run with the new `Init_lfbMemoryConfig` trace and verify whether the guest explicitly programs `tileBase=0x01d00000` while desktop start remains `0x00d00000`
- [ ] Reproduce the earlier zero-source case again under the current watchpoint instrumentation so the failing low linear source page can be compared directly against the known-good pages
- [ ] Verify from the next rerun whether `bad3` (`0x002a0000`) ever receives direct LFB or 2D destination writes before it is reused as a zero `screen_to_screen` source
- [ ] Trace which path should populate low linear source page `0x001ef390` and compare it directly against nearby good source page `0x001edc10`
- [ ] Reproduce the `0x002de9b0` blanking and nearby `0x002dcfec` recovery pair again under the new bad-vs-recovery low-linear result tracer so page-local mutation can be verified directly
- [ ] Step back into code-and-driver research before adding another runtime hypothesis, with emphasis on Voodoo4/VSA-100-specific 2D, LFB, and address-base semantics rather than assuming Voodoo3-equivalent behavior
- [ ] Compare the current 86Box `Init_lfbMemoryConfig` model against the Napalm/VSA-100 split between `lfbMemoryTileCtrl` and `lfbMemoryTileCompare`, because current code only models the tile-control half
- [ ] Compare the current CPU LFB remap threshold and tile-address derivation against the VSA driver-side `locLFBMemCfg` / `lfbTileCompare` / `tileAddress` debug model before trying any V4-only behavioral change
- [ ] Re-check whether the remaining `0x01d00000` versus `0x00d00000` split is best explained by missing `lfbMemoryTileCompare`-class behavior, by desktop-region overlap rules, or by another source-population step
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
- `Verified:` a fresh logged desktop rerun now also shows several low linear source surfaces being populated correctly, including `0x00299e80` through both decoded LFB writes and `host_to_screen`, plus later nonzero copy sources such as `0x001ec948` and `0x001eb1cc`
- `Verified:` that rerun did not reproduce the earlier `0x002de9b0` zero-source sample and did not hit any watched `bad` low linear page
- `Unknown:` whether the V4 guest explicitly reprograms `Init_lfbMemoryConfig` to the observed higher tiled base `0x01d00000`, or whether that split still reflects stale or misinterpreted device state
- `Verified:` a fresh logged V4 desktop run now shows the guest explicitly reprogramming `Init_lfbMemoryConfig` to `tileBase=0x01d00000` while `vidDesktopStartAddr` remains `0x00d00000`
- `Verified:` a fresh interaction-time failing source sample now shows low linear page `0x001ef390` still zero while nearby source page `0x001edc10` is populated and repaints correctly
- `Verified:` the latest alias-engaged run still does not revive the old `0x002de9b0` / `0x002dcfec` family; instead, repeated `screen_to_screen` samples into the damaged band pair nonzero source page `0x00132400` with zero source pages `0x001332d4` / `0x00133308`
- `Inferred:` the naive CPU/LFB `+16 MB` fold is more likely secondary than primary, because the alias engages immediately while the visible failure still tracks a separate low-linear source-population split inside the same run
- `Verified:` a later live desktop run now shows a different direct blanking event on the same damaged band: `screen_to_screen` copies from zero low-linear source page `0x002a0000` (`0x002a0084..0x002a0090`) turn previously nonzero desktop pixels at `0x00e90000` fully black
- `Inferred:` the remaining failure is not pinned to one unique bad low-linear family; multiple conditional source pages can arrive empty and blank the same visible desktop region
- `Verified:` the temporary low-linear tracing was still under-reporting the newest families because `vid_voodoo_banshee_blitter.c` had not yet promoted `good3` / `bad2` / `bad3` into the direct watch set and early `good0` traffic could exhaust the finite watch counters first
- `Verified:` the current temporary tracing now watches `good3` / `bad2` / `bad3` directly in both tracing files and biases the limited watch counters toward bad-page families so the next rerun can show whether `bad3` is ever populated before use
- `Verified:` the narrowed stride-packed host-row sizing fix is live in the latest rerun and now computes sane `bad3` row completion lengths (`lastByte=12` instead of `65036`) for the traced `srcFmt=0x00000010`, `srcStrideField=16`, `srcXY=0x0000f040` path
- `Verified:` despite that corrected row sizing, the first observed `bad3` writer in the fresh rerun is still `host_to_screen/0x22000003` with `dstBefore=0` and `dstAfter=0` at `0x002a0084..0x002a0090`
- `Verified:` the guest still provides nonzero mono upload dwords on that same `bad3` launch (`0x070080ff`, `0x030000ff`, ..., `0x7f000000`)
- `Verified:` the healthy watched desktop mono overlays use the same `srcFmt=0x00000010` family but a different command/ROP (`0xbb000003`) and do write nonzero pixels like `0xbfffffff`
- `Inferred:` the remaining `bad3` failure is no longer best explained by host-row byte sizing; the stronger lead is now an upstream source-surface population ordering bug where `bad3` is first consumed by a masked mono `0x22000003` pass before any seed/population pass has made it nonzero
- `Inferred:` under the current shared `MIX()` semantics, `0x22000003` cannot populate a zero destination by itself, so the zero `bad3` result is consistent with a missing or late prior seed pass rather than a missing host payload
- `Verified:` an added `dstAfterPlus16` row-tail trace proved that `bad3` is not uniformly zero; the traced `0x22000003` producer can leave `0x002a0084` / `0x002a0088` at zero while later words such as `0x002a008c..0x002a00a0` become nonzero in the same row
- `Verified:` a later `screen_to_screen` consumer from `srcBase=0x002a0084`, `srcFmt=0x00050080`, `srcStride=0x80`, `srcXY=0` faithfully copies that mixed row head/tail state into the desktop rather than misreading it
- `Inferred:` that result narrows the live mismatch from a generic consumer/readback bug to a producer-side staging-surface population/origin problem on the `bad3` path
- `Verified:` a larger V4-only producer-side experiment that force-normalizes masked mono `0x22000003` host uploads onto a left-aligned row origin did change the traced `bad3` launch (`lastByte=4` instead of `12`) but did not improve the visible desktop
- `Verified:` even with that larger producer-side normalization active, the row head at `0x002a0084` and `0x002a0088` still remained zero in the failing `bad3` path while later offsets became nonzero
- `Verified:` the same rerun also reproduced the earlier `bad2` zero-source family again at `0x001332d4` and `0x00133308`, so the desktop failure is still not unique to `bad3`
- `Inferred:` the larger producer-side left-alignment experiment is now a negative result; simply forcing the masked `0x22` producer to start at row head is not sufficient to fix the desktop
- `Unknown:` whether the real missing rule is a prior seeding/population pass for these low-linear staging pages, a different V4-only interpretation of the masked `0x22` path, or another richer VSA-only address/layout rule that has not been modeled yet
- `Inferred:` the active remaining bug is no longer “still only `8 MB`” or “missing tiled `32-bit` desktop rendering”; it is a higher-VRAM desktop-surface population/address-translation mismatch
- `Inferred:` low linear source-surface population is not failing universally on the `32 MB` path; the remaining bug is more likely a conditional or path-specific population miss
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
- `Verified:` watched damaged-band launches are now repeatedly reproduced around desktop page `0x00e9xxxx`.
- `Verified:` the watched damaged-band `screen_to_screen` launches use low-linear source `srcBase=0x001b8e80` with `srcFmt=0x000504b0` (32-bpp, stride `0x4b0`).
- `Verified:` the watched damaged-band `host_to_screen` launches use `srcFmt=0x00400000`, which decodes as packed `1-bpp` source data rather than a 32-bpp source path.
- `Inferred:` the remaining V4 repaint bug is at least partly on the watched-band format/packing path, not just a generic missing low-linear source population issue.
- `Verified:` watched damaged-band `1-bpp` host-to-screen overlays write nonzero pixels such as `0xbfffffff`; they are repainting details, not causing the black/zeroed regions.
- `Verified:` the blacking event itself is reproduced by low-linear `screen_to_screen` copies from zero source page `0x002de9b0`, and a nearby low-linear page around `0x002dcfec` later repopulates the same area with nonzero values.
- `Inferred:` the next narrow question is what populates bad page `0x002de000` versus nearby recovery page `0x002dc000`, since both now appear in the same failing run.
- `Verified:` a later rerun with the new bad-vs-recovery low-linear result tracer did not reproduce the `0x002de9b0`/`0x002dcfec` family; it only hit the healthy `good0` page `0x00299e80`, so the conditional failure remains reproducible but not yet stable under the newest instrumentation.
- `Verified:` the newest alias-engaged run reproduces a different conditional low-linear split: damaged-band `screen_to_screen` copies repeatedly pull white/nonzero data from `0x00132400` but zeros from nearby page `0x001332d4` / `0x00133308`.
- `Inferred:` because that split persists in a run where the naive CPU/LFB alias is already active from the first traced high-base write, the next narrow question is which path populates `0x00132000` but leaves the neighboring `0x00133000` page empty.
- `Verified:` a later live desktop run also reproduces a direct zero-source blanking event from `0x002a0084..0x002a0090`, which turns already-populated damaged-band desktop pixels at `0x00e90000` back to zero.
- `Inferred:` that newer `0x002a0000` event broadens the failure signature from “one missing neighboring page” to “conditional source-surface population can fail for more than one low-linear family.”
- `Inferred:` the current instrumentation is still useful, but the project now also needs a research pass on Voodoo4/VSA-100-specific 2D/LFB/address semantics before another round of runtime hypothesis testing.
- `Verified:` the local Win9x `3dfxvs.inf` binds `PCI\\VEN_121A&DEV_0009&SUBSYS_0004121A&REV_01` to the VSA/Voodoo path, reinforcing that the ROM-backed subsystem tuple `121a:0004` is the right Voodoo4 AGP identity to preserve.
- `Verified:` external Napalm/VSA-100 register documentation states that `vidDesktopStartAddr` is the physical start of the desktop buffer and that tiled `vidDesktopOverlayStride` encodes tile stride for the region rather than desktop width.
- `Verified:` the same Napalm/VSA-100 documentation splits `lfbMemoryConfig` into `lfbMemoryTileCtrl` and `lfbMemoryTileCompare`; `lfbMemoryTileCompare` controls the threshold used to classify CPU addresses as tiled versus linear.
- `Verified:` current 86Box code only models the tile-control half: it derives `tile_base`, `tile_stride`, and `tile_x_real` from `Init_lfbMemoryConfig`, then remaps any CPU LFB address `>= tile_base` through one shared tile formula.
- `Verified:` the local Win9x VSA driver stack contains explicit debug strings for `cfgAALfbCtrl`, `locLFBMemCfg`, `lfbTileCompare`, `strideInTiles`, `tileMark`, `tileAddress`, and `hwcCheckTarget` desktop-overlap checks, which is evidence that Voodoo4/VSA-100 address handling is richer than the current single-threshold LFB model.
- `Verified:` external register documentation also says the 2D `srcBaseAddr` / `dstBaseAddr` high bit selects tiled addressing, so the current 86Box top-bit tiled flag on those 2D base registers is source-backed and should not be discarded casually.
- `Inferred:` the strongest research-backed lead is now that the CPU LFB path may still be missing VSA-100-specific `lfbMemoryTileCompare` or adjacent address-selection semantics, while the 2D tiled-base top-bit handling itself is more likely to be fundamentally correct.
- `Verified:` the 3dfxzone Win9x Voodoo4/Voodoo5 archive adds several still-unchecked comparison targets beyond the stock `1.04.xx` line, including stock `1.03.00`, `1.01.01`, `3Dhq`, `Mikepedo`, `NuDriver`, `NuAngel`, `Iceman`, `Wipeaut`, `VIA Voodoo`, and `3dfx Wide driver 1.0`.
- `Verified:` local `Amigamerlin 2.9` comparison shows the Win9x display stack files most relevant to this desktop bug (`3dfx32vs.dll`, `3dfxvs.vxd`, `Vgartd.vxd`) are still stock `1.04.01 beta`; only `glide3x.dll` differs in that package.
- `Verified:` for reverse engineering, stock `1.04.01 beta` `glide3x.dll` remains the most informative local binary because it preserves the `cfgAALfbCtrl` / `lfbTileCompare` / `tileMark` / `aaMark` / `hwcCheckTarget` debug strings that the local `Amigamerlin 2.9` Glide build does not.
- `Verified:` after unpacking the newly downloaded archive packages, the VSA/LFB debug-string cluster in `glide3x.dll` is confirmed to exist in stock `1.00.01`, stock `1.03.00`, `Iceman`, `NuDriver1`, and `3dfx Wide driver 1.0`, so those clues are stable across multiple Win9x VSA branches.
- `Verified:` `NuDriver1` appears to be mostly a stock `1.04.01 beta` repack for the display stack, with stock-matching `3dfx32vs.dll` and `glide3x.dll` plus a customized INF.
- `Verified:` `Iceman` is a genuinely distinct Win9x branch with different `3dfx32vs.dll`, `3dfxvs.vxd`, `3dfxogl.dll`, and `glide3x.dll`, making it one of the best local comparison targets.
- `Verified:` stock `1.03.00` and stock `1.00.01` are also genuinely distinct display-stack baselines and should be treated as first-class reference branches in the reverse-engineering corpus.
- `Verified:` `3dfx Wide driver 1.0` is not a clean historical baseline; it mixes stock `1.04.01 beta` display pieces with an `Iceman`-family mini-VDD and modern 2022-era rebuilt `glide3x.dll` / OpenGL pieces.
- `Verified:` `NuDriver5.zip` actually contains `NuDriver7`, not the older `NuDriver1` branch already inspected earlier.
- `Verified:` `NuDriver7` is a hybrid branch: its `3dfx32vs.dll`, `glide3x.dll`, and `3dfxogl.dll` match `Iceman`, while its `3dfxvs.vxd` matches the stock-sized `1.04.01 beta` mini-VDD.
- `Verified:` the `NuDriver7` INF explicitly turns on `SSTH3_OVERLAYMODE` and `SSTH3_ALPHADITHERMODE` for both D3D and Glide by default, unlike the stock INF files that leave those entries commented out.
- `Verified:` compared with `Iceman`, `NuDriver7` further changes policy defaults rather than core binaries: it defaults to `32,1024,768`, sets `UseGTF=1`, reduces `OptimalRefreshLimit` to `60`, exposes AGP 4x controls, and adds stereo / guardband / geometry-assist UI and registry plumbing.
- `Inferred:` `NuDriver7` looks more like an “Iceman plus aggressive defaults and tweak exposure” branch than a wholly separate rendering stack.
- `Verified:` across stock `1.00.01`, `1.03.00`, and `1.04.01 beta`, the imported DLL sets for `glide3x.dll` and `3dfx32vs.dll` stay stable while image sizes grow, which supports treating them as one evolving VSA Win9x code line.
- `Verified:` raw disassembly of stock `glide3x.dll` around the `hwcCheckTarget` strings shows real code, not dead debug text: a branch ladder compares surface boundaries against `0x02000000` and then checks for overlap with desktop and later FIFO regions before selecting messages like “Surface starts in desktop”.
- `Verified:` that overlap ladder is structurally consistent across stock `1.00.01`, `1.03.00`, and `1.04.01 beta`, supporting the idea that desktop/fifo overlap policy is a longstanding part of the VSA Win9x stack.
- `Verified:` `Iceman` changes the shape of that `hwcCheckTarget` code path, so it is a genuine behavioral divergence candidate rather than a cosmetic rebuild.
- `Verified:` current 86Box trace-only instrumentation now also logs candidate V4 desktop-span boundaries derived from live SVGA geometry: `desktopLinearEndGuess`, `desktopTiledEndGuess`, `tileMarkGuess`, and `aaMarkGuess`.
- `Verified:` widening the traced Banshee/V4 register block to `0x1e0..0x260` still showed only early `videoDimensions=0` and `fbiInit0=0` writes before the first high-base V4 LFB traffic; no classic `colBufferAddr` / `colBufferStride` / `auxBufferAddr` / `auxBufferStride` / `leftOverlayBuf` / `swapbufferCMD` writes were observed on the idle desktop path.
- `Verified:` a first minimal V4-only CPU/LFB alias experiment engaged exactly as intended on the traced split path: `effectiveTileBase=00d00000`, `aliasApplied=1`, while the raw guest-programmed `tileBase` stayed `01d00000`.
- `Verified:` despite that alias engaging from the first traced V4 LFB write, the idle desktop still looked the same to the user.
- `Inferred:` the `0x01d00000` versus `0x00d00000` split is probably not solved by a naive V4-only CPU/LFB fold by itself; either the missing rule is subtler than a straight alias, or the visible corruption is dominated by another source-surface population path.
- `Unknown:` whether `3Dhq 1.09 beta 9` contains another distinct display-stack family or just another Glide/OpenGL repack, because the available local extraction tools still fail on its older RAR self-extractor.
