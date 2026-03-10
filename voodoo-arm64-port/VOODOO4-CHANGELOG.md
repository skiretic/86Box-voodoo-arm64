# Voodoo 4 (VSA-100) Emulation — Changelog

All changes and progress for the Voodoo 4 4500 AGP emulation effort.
Branch: `voodoo4`

---

## Display Pipeline Investigation + VGA Write Path Fixes (2026-03-10, Session 2)

### Root Cause Analysis: Complete

Identified a 3-layer root cause for VESA graphical mode corruption:

**Layer 1 — Lost vgaInit1 write (BIOS I/O port timing)**:
The BIOS ROM writes packed_chain4=1 and banking config to vgaInit1 (I/O offset 0x2C) during
early init. However, BAR2 hasn't been assigned its final address (0xD800) yet, so the write
goes to port 0x202C (I/O base 0x2000) instead of 0xD82C. The V4 ext I/O handler isn't
registered at 0x2000, so the write is lost. All subsequent vgaInit1 accesses at the correct
port (0xD82C) read/write 0x00000000.

Evidence: 1 write at port=0x202C val=0x1fe00000 (early init), 96 accesses at port=0xD82C
all val=0x00000000. CRTC[0x1C]=0xD8 confirmed correct but set AFTER the critical write.

**Layer 2 — VGA core in wrong addressing mode**:
Without packed_chain4 from vgaInit1, the VGA core stays in planar mode:
- chain4=0 → svga_write_common uses `addr <<= 2` (planar scatter, 4x address spacing)
- gdcreg6=0x0e → memory map B8000-BFFFF (text mode, no banking)
- write_bank=0 → no VBE bank switching

VGA aperture writes scatter each byte to 4 VRAM locations. The 8bpp scanout reads packed
data → 5x repetition pattern (800/160=5).

**Layer 3 — svga->fast flag interaction**:
Even with packed_chain4 forced, `svga->fast=0` because GDC registers aren't in pass-through.
When fast=0, the packed_chain4 path in svga_write_common still writes to only 1 of 4 planes
(`writemask2 = 1 << (addr & 3)`), causing garbled output. Guest writes to GDC registers
(through VGA I/O ports 3CE/3CF) continuously recalculate fast, resetting it to 0.

### Fixes Applied

**1. Force packed_chain4=1 in v4_recalctimings** (lines ~556-559):
When extShiftOut=1 && fb_only=1, force `svga->packed_chain4 = 1` to enable packed byte
addressing in the SVGA write path.

**2. Force GDC memory map to mode 1** (lines ~560-568):
Change gdcreg6 bits [3:2] from 11 (B8000-BFFFF) to 01 (A0000-AFFFF). This enables VBE
banking in svga_decode_addr (banking only applies in memory map modes 0 and 1). Also
updates banked_mask and re-maps the SVGA memory mapping to match.

**3. Force chain4 + GDC pass-through** (lines ~577-583):
Set `svga->chain4=1`, `gdcreg[8]=0xFF`, `gdcreg[3]&=~0x18`, `gdcreg[1]=0`, and
`svga->fast=1`. This ensures the fast write path is active, bypassing all VGA plane logic.

**4. v4_out post-fixup** (lines ~376-381):
After every `svga_out()` call in the V4 VGA output handler, re-force `svga->fast=1` when
`svga->fb_only` is set. This prevents guest GDC/Sequencer register writes from clearing
the fast flag through svga_out's internal recalculation.

### Results

- Hardware cursor: **VISIBLE and correctly positioned** ✓
- Clicking in Windows desktop: individual lines redraw correctly (new writes use fast path) ✓
- Overall display: still garbled — old data from before fixes (planar format) persists
- VBE banking: still not working (write_bank=0 always)
- Only first 64KB of 480KB framebuffer accessible without banking

### Architecture Research Completed

Created comprehensive research documents:
- `VSA100-DISPLAY-PIPELINE.md` — Full hardware architecture, register map, tile format,
  LFB addressing, VGA compatibility, mode transition sequences, Banshee vs VSA-100 differences
- `VSA100-REGISTER-REFERENCE.md` — Complete register definitions from tdfx/Mesa/Glide source,
  vidProcCfg bit map, vgaInit0/vgaInit1 bits, dacMode, stride encoding, tile addressing,
  PCI extended config registers, 2D/3D engine register offsets, DDC/I2C bit definitions

### SVGA Write Path Analysis

Documented the complete svga_write_common code path:
- fast=1: direct VRAM write, no plane logic
- fast=0 + packed_chain4: `writemask2 = 1 << (addr & 3)`, `addr &= ~3` — single plane write
- fast=0 + chain4: address bit-shuffle with plane select
- fast=0 + neither: `addr <<= 2` — planar scatter (4x spacing)
- fast flag: requires GDC pass-through AND (chain4+packed_chain4 OR fb_only)

### VBE Banking Analysis

The banking register is vgaInit1 at I/O offset 0x2C:
- Bits 9:0 = write bank (32KB granularity, 10-bit)
- Bits 19:10 = read bank (32KB granularity, 10-bit)
- Bit 20 = packed_chain4 flag
- VRAM offset = bank_field * 32768
- VBE exposes 64KB granularity (BIOS doubles bank number)
- V4 handler (line 761) is correct, identical to Banshee

The banking code is present and correct — the issue is it's never called with non-zero values.
BIOS CRTC[0x1C]=0xD8 is correct (stored I/O base high byte), but the initial vgaInit1
configuration was lost due to the port timing issue.

### Diagnostic Logging Enhanced

- Added packed_chain4 to recalctimings DONE log line
- All I/O handlers have comprehensive fprintf(stderr) logging
- LFB write logging active (first 32 writes)
- VRAM content probe (one-shot, 3 fires)

### Build: PASS

---

## Display Corruption Investigation (2026-03-10, Session 1)

### Display in VESA 800x600x8bpp: Pixel Data Visible

After extensive investigation of the display pipeline, identified and fixed several issues
that were preventing pixel data from appearing:

**1. CLUT palette fix — makecol32():**
The dacData handler was storing raw RGB bytes without conversion. On the host display,
palette entries need alpha=0xFF. Changed to use `makecol32(r, g, b)` which packs
`0xFF000000 | (r << 16) | (g << 8) | b`. Colors now correct.

**2. VRAM address mismatch identified:**
VGA aperture writes land at VRAM[0] (write_bank=0, never changes). BIOS programs
vidDesktopStartAddr=0x760000. Scanout reads from 0x760000 → empty VRAM → black screen.

**3. memaddr_latch=0 hack:**
Temporary fix: override memaddr_latch to 0 so scanout reads from VRAM[0] where data
actually lives. Result: pixel data visible with correct colors, but stride mismatch.

**4. Stride mismatch diagnosed:**
Display rowoffset=100 (800 bytes/line from vidDesktopOverlayStride). VGA data uses
CRTC offset=40 → effective stride ~160 bytes. 800/160=5 repetitions visible.

### Other fixes this session

- **BAR2 write handler leak fix** — cases 0x1a/0x1b in v4_pci_write now properly
  remove/set I/O handler when updating ioBaseAddr
- **v4_sync_tile_params()** — auto-configures tile parameters from display state
- **v4_render_16bpp_tiled()** — new render function for tiled 16bpp mode
- **fb_only override path** — handles fb_only without extShiftOut
- **tile_base init** — initialized to 32MB (vram_size) instead of 0

### Build: PASS

---

## Investigation & Architecture Decision (2026-03-09)

### ROM Analysis

- `V4_4500_AGP_SD_1.18.rom` confirmed valid: 64 KB PCI Option ROM, `55 AA` header
- PCI vendor `0x121A`, device `0x0009` (VSA-100)
- ROM contains combined Voodoo 4 4500 / Voodoo 5 5500 BIOS (shared BIOS image)
- Entry point chain: `0xC003 → JMP 0xC052 → JMP 0xC0EB`
- Init sequence at `0xC0EB`: chip probe (`call 0xFE6E`) → main init (`call 0x465C`, with
  carry-flag abort on failure) → VGA setup (`call 0x43B2`) → additional setup (`call 0x4891`)
- ROM copied to `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom` ✓

### First Attempt: Banshee-based Stub (reverted)

Implemented `TYPE_V4_4500` inside `vid_voodoo_banshee.c`, returning PCI device ID `0x0009`
and allocating 32 MB SDRAM. Card appeared in the picker and `banshee_init_common()` succeeded.

**Result**: Black screen on boot. Diagnostic logging revealed **zero PCI enumeration activity
and zero register activity** — the system BIOS never enumerated the card and the ROM never
executed.

**Root cause**: The VSA-100 BIOS probes VSA-100-specific chip identity registers early in
its init sequence (`call 0xFE6E`). If those reads return wrong values (or the card isn't
properly enumerated), the BIOS sets carry flag and skips all display initialization, causing
the black screen. Banshee register handling does not respond correctly to VSA-100 probes.

**Decision**: The VSA-100 is not a Banshee card and must not be implemented as one. All
Banshee-based V4 code was reverted. The correct approach is a standalone `vid_voodoo4.c`.

### Commit History

- `38104f05a` — `video: add Voodoo 4 4500 AGP stub running on Voodoo 3 pipeline` (approach failed)
- `e093d91fe` — `revert: remove Voodoo 4 banshee-based stub — starting fresh with standalone vid_voodoo4.c`

---

## PCI Config Fixes + Diagnostic Logging (2026-03-09)

### v4_pci_read fixes

Compared `v4_pci_read` against `banshee_pci_read` and identified several missing/incorrect
PCI config register responses.

**Critical fix — PCI Status register (0x06-0x07):**
- Offset 0x06 was NOT handled at all — returned 0x00. This meant the **"Capabilities List"
  bit (bit 4)** was not set, so the BIOS/OS could never discover AGP or PM capabilities
  via the standard PCI capability walk.
- Fixed: offset 0x06 now returns 0x10 (Capabilities List bit set).
- Offset 0x07 now OR's in 0x02 (medium DEVSEL timing) regardless of pci_regs state.

**Additional registers added:**
- 0x05 (Command high byte) — returns 0x00 explicitly
- 0x0c (Cache Line Size) — readable/writable via pci_regs
- 0x0e (Header Type) — returns 0x00 (standard type 0 header)
- 0x57 (AGP version high byte) — returns 0x00
- 0x61 (PM next capability pointer) — returns 0x00 (end of chain)
- 0x63 (PM caps high byte) — returns 0x00

### v4_pci_write fixes

- Added `has_bios` check on ROM base address writes (0x30/0x32/0x33), matching Banshee
- Added 0x06, 0x0e to read-only register list
- Added 0x0c (Cache Line Size) write handler
- Fixed offset 0x5f write: was writing to `pci_regs[0x5e]` (wrong register), now writes
  to `pci_regs[0x5f]` correctly
- Added UNHANDLED log for unrecognized write offsets

### Diagnostic logging added

- `v4_pci_read`: logs every register read with offset and value
- `v4_pci_write`: logs every register write with offset and value
- Additional detail logs for: Command register (IO/MEM enable state), BAR0/BAR1 addresses,
  I/O base address, ROM enable/disable

### Build: PASS

---

## BIOS POST SUCCESS + ROM Trace Analysis (2026-03-09)

### BIOS POST: WORKING

The PCI Status register fix (0x06 = 0x10, Capabilities List bit) was the key blocker.
After applying the PCI config fixes above, the ROM POST now **succeeds fully**:

- PCI Find Device (INT 1Ah B102): vendor=0x121A, device=0x0009 — found ✓
- Subsystem ID check (PCI config 0x2C): 0x0004121A — matches ✓
- Command register check (PCI config 0x04): bits 0+1 set (IO+MEM) — passes ✓
- BAR2 read (PCI config 0x18): returns 0xD800 — I/O base assigned ✓
- Hardware init (0x865C): reads strapInfo, writes pciInit0/vgaInit0/offset 0x70,
  reads miscInit1 bit 28 = 0 → returns CLC (success) ✓
- VGA mode setup (0x83B2) and additional init (0x8891) both execute ✓

### Boot Test Results

- **BIOS POST**: Card identified, ROM executes, VGA text mode works
- **Windows 98**: Boots fully, detects "Voodoo 4 4500", prompts for driver install
- **Display after reboot**: VGA text works; graphical mode shows corrupted framebuffer

### Build: PASS

---

## Current State Summary

- ROM in place: `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom` ✓
- Standalone `vid_voodoo4.c` exists (~2300 lines) ✓
- PCI config: capabilities, status register, header type all correct ✓
- **BIOS POST: WORKING** ✓
- **Windows detection: WORKING** ✓
- **VGA text mode: WORKING** ✓
- **Diagnostic logging: COMPLETE** ✓ (all I/O handlers)
- **BAR2 handler leak: FIXED** ✓
- **CLUT palette: FIXED** ✓
- **Hardware cursor: VISIBLE** ✓
- **VGA write path: FAST PATH WORKING** ✓ (new writes reach VRAM correctly)
- Display in VESA mode: partially working — VBE banking and stride still need fixing
