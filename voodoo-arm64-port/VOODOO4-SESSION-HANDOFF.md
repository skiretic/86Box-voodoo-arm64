# Voodoo 4 Session Handoff — 2026-03-10 (Session 2)

## Branch: `voodoo4`
## File: `src/video/vid_voodoo4.c` (~2300 lines, standalone VSA-100 driver)

## Current State

**VGA text mode**: WORKS perfectly.
**VESA graphical mode (800x600x8bpp)**: Partially working — pixel data visible, mouse cursor visible and correctly positioned, but display garbled due to stride/addressing mismatch. Clicking in the Windows desktop causes individual lines to redraw correctly (one at a time), confirming the VGA write path IS working for new writes.

## What's Working

1. BIOS POST succeeds, ROM executes fully
2. PCI config is correct (device 0x0009, vendor 0x121A)
3. BAR0 (registers) = 0xe2000000, BAR1 (LFB) = 0xe0000000, BAR2 (I/O) = 0xD800
4. Windows 98 boots, detects "Voodoo 4 4500"
5. VGA text mode displays correctly (VGA passthrough, fb_only=0)
6. CLUT palette loads correctly (makecol32 fix applied — colors visible)
7. Hardware cursor visible and correctly positioned in VESA mode
8. LFB (BAR1) partially working — cursor data writes go through v4_write_linear
9. VGA aperture writes reach VRAM with `fast=1` path (confirmed by clicking redraw behavior)

## Root Cause Analysis (THOROUGHLY INVESTIGATED)

### Problem Chain (3 interconnected issues)

**Issue 1: BIOS writes vgaInit1 to wrong I/O port**

During ROM init, the BIOS writes `packed_chain4=1` and VBE banking config via vgaInit1 register (I/O offset 0x2C). However, the write goes to port **0x202C** (I/O base 0x2000) instead of **0xD82C** (I/O base 0xD800). This is because the ROM reads BAR2 before PCI BAR assignment is finalized. The write is effectively lost.

Evidence:
- One non-zero vgaInit1 write at port=0x202C val=0x1fe00000 (early init)
- 96 vgaInit1 accesses at correct port 0xD82C, ALL val=0x00000000
- CRTC[0x1C] = 0xD8 (set later, after the critical write was lost)

**Issue 2: VGA core in wrong mode for VESA**

Because vgaInit1 was lost, the VGA core stays in text/planar mode:
- chain4=0 (should be 1 for packed 8bpp addressing)
- packed_chain4=0 (should be 1, from vgaInit1 bit 20)
- gdcreg6=0x0e → memory map B8000-BFFFF (text mode, should be A0000-AFFFF for graphics)
- gdcreg5=0x10 → planar write mode
- write_bank=0, read_bank=0 (VBE banking never activated)

Without chain4/packed_chain4, VGA aperture writes use planar addressing (`addr <<= 2`), scattering data to 4x the expected address spacing. Without banking, only the first 64KB of VRAM is accessible.

**Issue 3: Display reads from wrong VRAM location**

The BIOS programs `vidDesktopStartAddr=0x760000` (7.375MB into VRAM), but VGA aperture writes land at VRAM[0] (because write_bank=0). Scanout reads from 0x760000 → empty VRAM.

### Fixes Applied (this session)

1. **memaddr_latch=0 hack** (from last session) — forces scanout to read from VRAM[0]
2. **Force packed_chain4=1** — enables packed byte addressing in svga_write_common
3. **Force GDC memory map to mode 1** (A0000-AFFFF) — enables banking in svga_decode_addr
4. **Force chain4=1 + GDC pass-through** — makes `svga->fast=1` for direct VRAM writes
5. **v4_out post-fixup** — re-forces `svga->fast=1` after every svga_out call when fb_only=1

These fixes are in v4_recalctimings (lines ~552-584) and v4_out (lines ~376-381).

### Current Result

With all fixes applied:
- `fast=1` confirmed — VGA writes bypass all plane logic
- Mouse cursor visible and correctly positioned
- Clicking in Windows desktop causes individual lines to redraw (correct pixel data)
- BUT: existing data from before fixes took effect is garbled
- "Windows" text from desktop still shows garbled/noisy patterns
- Bottom of screen is black (VBE banking not working, only first 64KB accessible)

## Remaining Issues (IN ORDER OF PRIORITY)

### 1. VBE Banking Not Working (PRIMARY BLOCKER)

VBE function 4F05h (Set Display Window) never fires with a non-zero bank value. Without banking, only the first 64KB of the 480KB framebuffer (800x600x8bpp) is accessible through the VGA aperture. This means only ~81 lines of the 600-line display can receive pixel data.

**Root cause**: The BIOS VBE handler reads vgaInit1, modifies bank bits, writes it back. But since vgaInit1 is always 0 at the correct port, the BIOS may compute bank=0 always. OR: the BIOS's VBE 4F05h handler is never called by Windows (Windows may be trying to use LFB instead).

**Investigation needed**:
- Add logging in v4_ext_outl for vgaInit1 writes showing the CALLER context (what triggered the write)
- Check if Windows is calling INT 10h 4F05h at all
- Check if Windows is using LFB mode (BAR1) instead of banking
- If Windows uses LFB: fix LFB addressing so writes go to vidDesktopStartAddr
- If Windows uses banking: debug why bank values stay 0

### 2. Stride/Address Mismatch

Even with `fast=1` and direct VRAM writes, the display is garbled. Possible causes:
- The VGA CRTC offset (40) vs vidDesktopOverlayStride (800 bytes) — these don't match
- The scanout uses rowoffset=100 (800 bytes/line) but VGA data may use a different stride
- The data written before the fixes took effect used planar addressing (4x spacing)
- After fix: new writes should be packed, but old data is still garbled

**Key diagnostic**: Do a full VRAM dump after Windows redraws (e.g., after moving a window). Check:
- Is the data packed 8bpp at 800 bytes/line? Or is it some other format/stride?
- Is there valid pixel data beyond 64KB in VRAM?

### 3. vidDesktopStartAddr Mismatch

The BIOS sets vidDesktopStartAddr=0x760000 but data lives at VRAM[0]. The memaddr_latch=0 hack works for now, but proper fix is either:
a. Fix VBE banking so data goes to VRAM[0x760000], OR
b. Set vidDesktopStartAddr=0 (matching where data actually is)

### 4. LFB (BAR1) Investigation

LFB writes DO arrive (cursor data at VRAM[0x97efc+]). But the main framebuffer doesn't appear to use LFB. Questions:
- What is the LFB physical address in the VBE mode info block? (Is it BAR1 base?)
- Is Windows using LFB for the main framebuffer? If so, where does data land?
- The cursor writes go to VRAM ~0x98000 — is this the cursor pattern area?
- BAR1 appears to be remapped to 0x12000000 range (log shows addr=0x12097efc)

## Key Register State (VESA 800x600x8bpp, with fixes)

```
vidProcCfg      = 0x08000081 (enable=1, pixfmt=0=8bpp, cursor=1, tile=0)
vgaInit0        = 0x00001140 (extShiftOut=1)
vgaInit1        = 0x00000000 (no banking, no packed_chain4 from hardware)
vidDesktopStartAddr = 0x760000 (hacked to memaddr_latch=0)
vidDesktopOverlayStride = 0x320 (desktop=800 bytes)
lfbMemoryConfig = 0x00080800 (tile_base=8MB, tile_stride=1024)
CRTC offset     = 40 (0x28)
CRTC[0x1a]      = 0xa0 (extension bits)
chain4          = 1 (FORCED by recalctimings)
packed_chain4   = 1 (FORCED by recalctimings)
gdcreg5         = 0x10 (from BIOS, not overridden)
gdcreg6         = 0x06 (FORCED from 0x0e to mode 1: A0000-AFFFF)
write_bank=0, read_bank=0
fast            = 1 (FORCED: chain4+packed_chain4+gdcreg_passthrough+fb_only)
rowoffset       = 100 (from vidDesktopOverlayStride: 800/8)
memaddr_latch   = 0x0 (HACKED from vidDesktopStartAddr>>2)
```

## Architecture Research (completed this session)

### How VBE Banking Works on Banshee/VSA-100

**Register**: vgaInit1 at I/O offset 0x2C from BAR2

| Bits | Field | Purpose |
|------|-------|---------|
| 9:0 | Write bank | 10-bit write bank (32KB granularity) |
| 19:10 | Read bank | 10-bit read bank (32KB granularity) |
| 20 | packed_chain4 | Packed Chain4 mode enable |
| 30:29 | Upper write bank | Extended write bank bits |

VRAM offset = bank_field_value * 32768 (32KB per unit).
VBE exposes 64KB granularity → BIOS doubles the bank number before storing.

**VBE 4F05h handler flow**:
1. Read CRTC[0x1C] to get I/O base high byte
2. Construct I/O base (e.g., 0xD800)
3. Read vgaInit1 at ioBase+0x2C
4. Modify bank bits, write back

**V4 handler already correct**: v4_ext_outl case V4_Init_vgaInit1 (line 761) correctly extracts write_bank and read_bank, identical to Banshee code.

### SVGA Write Path Analysis

**fast=1 path**: svga_writeb_linear does `svga->vram[addr] = val` directly. No plane logic.

**fast=0 path (packed_chain4)**: `writemask2 = 1 << (addr & 3); addr &= ~3` — still writes to only 1 of 4 planes. NOT fully packed.

**fast flag computation**:
```c
svga->fast = (gdcreg[8]==0xFF && !(gdcreg[3]&0x18) && !gdcreg[1]) &&
             ((chain4 && (packed_chain4 || force_old_addr)) || fb_only);
```

Requires BOTH: GDC pass-through AND (chain4+packed_chain4 or fb_only). We force all of these.

**fast recalculation**: Happens in svga_out when GDC or Sequencer registers are written. Guest writes can clear fast. Our v4_out post-fixup re-forces it.

## Research Docs (written this session and previous)

- `voodoo-arm64-port/VSA100-DISPLAY-PIPELINE.md` — VSA-100 hardware architecture, register map, tile format, LFB addressing, VGA compatibility, key differences from Banshee
- `voodoo-arm64-port/VSA100-REGISTER-REFERENCE.md` — tdfx/Mesa/Glide driver register definitions, init sequences, bit fields, PCI extended config
- `voodoo-arm64-port/VOODOO4-PLAN.md` — Phase plan with checklists
- `voodoo-arm64-port/VOODOO4-CHANGELOG.md` — Detailed change log

## Diagnostic Code in Tree (DO NOT COMMIT as final — review before committing)

All diagnostic code is in the tree and will be committed with the functional changes. Review and strip before any production merge.

- fprintf logging in all I/O handlers (v4_in/out, v4_ext_in/out/inl/outl)
- fprintf in v4_recalctimings (entry + DONE state, now includes packed_chain4)
- VRAM content probe (one-shot, fires 3 times when fb_only=1)
- LFB write counter (first 32 writes logged)
- v4_updatemapping logging
- memaddr_latch=0 hack (in v4_recalctimings fb_only block)
- chain4/packed_chain4/GDC force (in v4_recalctimings fb_only+extShiftOut block)
- v4_out fast flag fixup (after svga_out)

## Changes Made This Session (Session 2, 2026-03-10)

1. **Force packed_chain4=1** — in v4_recalctimings when extShiftOut=1 && fb_only=1
2. **Force GDC memory map to mode 1** (gdcreg6 bits 3:2 = 01) — A0000-AFFFF, 64KB window
3. **Update banked_mask and mem_mapping** for new memory map mode
4. **Force chain4=1** — enables chain4 addressing
5. **Force GDC pass-through** (gdcreg[8]=0xFF, gdcreg[3]&=~0x18, gdcreg[1]=0) — makes fast=1
6. **Force svga->fast=1** unconditionally when fb_only+extShiftOut
7. **v4_out post-fixup** — re-forces fast=1 after svga_out to prevent guest GDC writes from clearing it
8. **Added packed_chain4 to DONE log line** for diagnostic visibility

## Build

```bash
cd /Users/anthony/projects/code/86Box-voodoo-arm64
./scripts/build-and-sign.sh
```

Binary: `build/src/86Box.app/Contents/MacOS/86Box`
Test VM: `/Users/anthony/Library/Application Support/86Box/Virtual Machines/v4`
Launch: `build/src/86Box.app/Contents/MacOS/86Box -P "<vm_path>" 2>~/v4_stderr.log`

## Test Logs

- `~/v4_stderr14.log` — latest (chain4+packed_chain4+fast=1 forced, partial display)
- `~/v4_stderr13.log` — packed_chain4 only (without fast=1 force)
- `~/v4_stderr12.log` — memaddr_latch=0 hack only (5x repetition)
- `~/v4_stderr11.log` — VRAM probe results showing data at VRAM[0]
- Earlier logs: v4_stderr1 through v4_stderr10 (various diagnostic iterations)

## Suggested Next Steps for New Session

1. **Debug VBE banking**: Add targeted logging to confirm whether Windows calls INT 10h 4F05h (VBE bank switch). If not, Windows may be using LFB mode. If so, focus on LFB.

2. **Debug LFB addressing**: The LFB (BAR1) IS receiving writes (cursor data). Check if Windows is writing the main framebuffer through LFB. If so, the issue is that writes go to VRAM[~0x98000] instead of VRAM[0x760000]. Check the VBE mode info block's PhysBasePtr field.

3. **Try setting vidDesktopStartAddr=0 on the hardware side**: Instead of the memaddr_latch=0 hack, actually write vidDesktopStartAddr=0 when the VGA aperture is in use. This aligns the scanout with where data actually lands.

4. **VRAM dump diagnostic**: Add a one-shot VRAM dump (first 4KB at offset 0, and 4KB at offset 0x760000) after Windows has been running for a few seconds. Compare packed vs planar data layout. This will definitively show what format the data is in.

5. **Compare with Banshee runtime**: Boot a Banshee VM with the same Windows 98 image (Standard VGA driver, 800x600x8bpp). Log the vgaInit1, chain4, gdcreg, write_bank values. This gives the reference for what "correct" looks like.
