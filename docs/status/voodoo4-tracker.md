# Voodoo 4 Tracker

Date: 2026-03-11

Purpose: keep a running status of what is complete, what is next, and what remains blocked or unknown for the Voodoo 4 restart.

## Status Snapshot

- Overall phase: `Phase 1 audit complete; runtime blank-screen boundary narrowed to ROM-dispatch handoff before V4 entry-helper ext writes`
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

## Next

- [ ] Trace the generic option-ROM dispatch/shadow handoff closely enough to prove why the Voodoo4 entry helper is not reaching its first ext write
- [ ] Decide whether the next minimal probe should target BIOS ROM-dispatch criteria or shadowed `0xc0000` execution rather than any more Voodoo4 PCI-layout edits
- [ ] Only after ROM execution is proven, revisit whether ext offset `0x70` or shared SDRAM/strap defaults block later POST progress
- [ ] Capture the first runtime failure after ROM execution begins, before widening the implementation

## Open Questions

- `Unknown:` what exact BIOS ROM-dispatch or shadow-handoff condition prevents the Voodoo4 entry helper from reaching its first ext write after the ROM header is inspected?
- `Unknown:` does the current Banshee/Voodoo3 path already cover enough ext-register behavior for ROM POST once ROM execution actually begins?
- `Unknown:` does Voodoo 4 require different reset defaults for existing modeled registers such as `Init_dramInit1`?
- `Inferred:` PCI config space likely does not need another early layout change on this path, because the current Voodoo3/Voodoo4 AGP shell already matches for the bytes the `p5a` BIOS actually probes after the subsystem block.
- `Unknown:` does the shared `16 MB` SDRAM BIOS default misstate Voodoo 4 memory sizing early enough to matter during POST?
- `Unknown:` how much of Voodoo4/Voodoo5-family behavior matters before protected-mode drivers load?

## Blockers

- No hard blocker at research level.
- `Verified:` later ext-register and scanout debugging are blocked behind the earlier ROM-execution boundary found in runtime tracing.

## Working Rules

- `Verified:` no implementation should be driven by unsupported old-branch assumptions.
- `Verified:` major conclusions stay labeled `Verified`, `Inferred`, or `Unknown`.
- `Inferred:` when unsure, prefer proving reuse before creating a new Voodoo 4-specific code path.

## Reference Documents

- [Executive summary](./voodoo4-executive-summary.md)
- [Changelog](./voodoo4-changelog.md)
- [Research index](../research/voodoo4-index.md)
- [Recovery plan](../plans/voodoo4-recovery-plan.md)
