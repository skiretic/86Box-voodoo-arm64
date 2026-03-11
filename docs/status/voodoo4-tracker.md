# Voodoo 4 Tracker

Date: 2026-03-11

Purpose: keep a running status of what is complete, what is next, and what remains blocked or unknown for the Voodoo 4 restart.

## Status Snapshot

- Overall phase: `Phase 1 audit complete; ROM shadow/dispatch and early init are proven; manual VM verification now reaches the Windows desktop through at least 1024x768 16-bit with the Voodoo4 driver installed`
- Primary strategy: `Reuse-first over Banshee/Voodoo3 until disproven`
- Evidence baseline: `ROM analysis + source-backed register correlation + Phase 1 code audit + first runtime boundary trace`

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

## Next

- [ ] Investigate the newly identified `32-bit` color distortion boundary with the Voodoo4 driver installed
- [ ] Reproduce the distortion again with the new mode-state tracing still enabled and capture the bad-mode register state in `/tmp/voodoo4-mode-boundary.log`
- [ ] Determine whether the same distortion appears first at `800x600` `32-bit`, `1024x768` `32-bit`, or both
- [ ] Compare the traced good tiled `16-bit` mode against the traced bad `32-bit` mode at `DACMODE`, `VIDPROCCFG`, `VIDINFORMAT`, screen size, desktop start, and stride
- [ ] Decide whether the smallest evidence-backed code delta is a tiled `32-bit` desktop renderer or some other narrower scanout/state fix
- [ ] Re-evaluate shared reset/default assumptions such as `Init_dramInit1`, SDRAM sizing, and `Init_strapInfo` only if the next post-desktop boundary points back to them
- [ ] Decide the next smallest probe from the new baseline-desktop boundary rather than widening broadly into unrelated VSA-100 behavior

## Open Questions

- `Verified:` the first concrete runtime failure boundary now observed is `32-bit` color, which produces distortion under the installed Voodoo4 driver
- `Verified:` the current Banshee/Voodoo3 reuse path is now sufficient for at least ROM POST plus Windows desktop bring-up through `1024x768` `16-bit`
- `Verified:` the traced good V4 Windows desktop path uses tiled `16-bit` scanout
- `Inferred:` the strongest current emulator-side hypothesis is that the shared path is missing tiled `24/32-bit` desktop handling, because only `16bpp_tiled` exists today
- `Unknown:` does Voodoo 4 require different reset defaults for existing modeled registers such as `Init_dramInit1`?
- `Inferred:` another early PCI shell mismatch is now less likely than before, because the ROM-dispatch boundary was cleared by matching the subsystem tuple that the ROM itself validates.
- `Unknown:` does the shared `16 MB` SDRAM BIOS default misstate Voodoo 4 memory sizing early enough to matter during POST?
- `Unknown:` whether the failing `32-bit` mode also programs `tile=1` in the same family as the traced good `16-bit` mode, or diverges earlier in another display register
- `Unknown:` what additional Voodoo4/VSA-100 differences matter for `32-bit` color modes and beyond, now that the protected-mode driver path is known to be active?

## Blockers

- No hard blocker at research level.
- `Verified:` the earlier ROM-execution / pre-ext blocker is resolved.
- `Verified:` the earlier blank-screen blocker is resolved through desktop bring-up, higher-resolution `16-bit` mode validation, and a driver-enabled configuration.
- `Verified:` further work should now target the observed `32-bit` color distortion boundary rather than revisiting the solved pre-ext path.

## Working Rules

- `Verified:` no implementation should be driven by unsupported old-branch assumptions.
- `Verified:` major conclusions stay labeled `Verified`, `Inferred`, or `Unknown`.
- `Inferred:` when unsure, prefer proving reuse before creating a new Voodoo 4-specific code path.

## Reference Documents

- [Executive summary](./voodoo4-executive-summary.md)
- [Changelog](./voodoo4-changelog.md)
- [Research index](../research/voodoo4-index.md)
- [Recovery plan](../plans/voodoo4-recovery-plan.md)
