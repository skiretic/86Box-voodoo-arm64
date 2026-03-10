# Voodoo 4 (VSA-100) Emulation Plan

**Branch**: `voodoo4`
**ROM**: `V4_4500_AGP_SD_1.18.rom` (Voodoo 4 4500 AGP, BIOS v1.18, 64 KB)
**Architecture**: Standalone `vid_voodoo4.c` — NOT grafted onto Banshee

---

## Overview

The 3dfx VSA-100 chip (codenamed "Napalm") powers the Voodoo 4 4500 and Voodoo 5 5500/6000.
It is architecturally related to the Voodoo 3 (Avenger) but is a distinct chip with its own
register map, initialization sequence, and capabilities. It is **not a Banshee card** and must
not be implemented as a Banshee extension.

The VSA-100 shares the PCI vendor ID (`0x121A`), the general BAR layout concept, and the
3D rendering pipeline philosophy with Voodoo 3 — but the chip-level register handling,
memory controller initialization, PCI config responses, and BIOS initialization sequence
differ enough that reusing Banshee code wholesale causes silent init failures.

This plan targets **Voodoo 4 4500 single-chip** only. Voodoo 5 dual-chip SLI is explicitly
out of scope.

---

## What We Learned (First Attempt)

The first attempt grafted V4 onto `vid_voodoo_banshee.c` as a new type. Results:

- Card appeared in the 86Box picker ✓
- `banshee_init_common()` returned a valid pointer ✓
- **Zero PCI enumeration activity** — the system BIOS never enumerated the card or ran the ROM
- **Zero register activity** — no MMIO reads or writes ever logged
- Root cause: the VSA-100 BIOS init sequence writes to VSA-100-specific registers; Banshee
  register handling either responds incorrectly or silently fails, preventing POST

**Conclusion**: The V4 needs its own driver file with its own PCI config, register map, and
init handling. The Banshee code is useful as a *reference* but must not be the *host*.

---

## ROM Analysis

- **File**: `V4_4500_AGP_SD_1.18.rom` — 64 KB, valid PCI Option ROM
- **Signature**: `55 AA` at offset 0
- **PCI Vendor**: `0x121A` (3dfx Interactive)
- **PCI Device**: `0x0009` (VSA-100)
- **Entry point chain**: `0xC003 → JMP 0xC052 → JMP 0xC0EB`
- **BIOS init sequence** (at `0xC0EB`):
  1. `call 0xFE6E` — PCI device identification / chip probe
  2. `call 0x465C` — main chip initialization; `JC` on failure → skip display init
  3. `call 0x43B2` — VGA mode setup
  4. `call 0x4891` — additional setup
  5. `RETF` — return to system BIOS
- **ROM destination**: `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom` ✓ (already in place)

---

## Hardware Reference

| Property | Voodoo 3 (Avenger) | Voodoo 4 (VSA-100) |
|----------|-------------------|---------------------|
| PCI Vendor ID | 0x121A | 0x121A |
| PCI Device ID | 0x0005 | 0x0009 |
| Max texture size | 256×256 (LOD 8) | 2048×2048 (LOD 11) |
| Framebuffer depth | 16-bit RGB565 | 16-bit or 32-bit RGBA |
| Z/stencil buffer | 16-bit Z | 24-bit Z + 8-bit stencil |
| Texture formats | RGB565, ARGB1555, etc. | + ARGB8888, FXT1, DXTC |
| Pixel pipes | 1 pipe, 2 TMUs | 2 pipes, 1 TMU each |
| Max VRAM | 16 MB | 32 MB |
| FSAA | No | Yes (T-buffer) |

---

## File Structure

The V4 implementation lives in its own files. It uses the shared Voodoo infrastructure
(render, texture, display, LFB, FIFO) via the existing `voodoo_t` state, but owns its
own PCI config, register dispatch, and init.

| Asset | Path | Notes |
|-------|------|-------|
| ROM | `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom` | Already in place |
| Main driver | `src/video/vid_voodoo4.c` | New standalone file |
| Header | `src/include/86box/vid_voodoo4.h` | Device extern + register defines |
| Common state | `src/include/86box/vid_voodoo_common.h` | Add `VOODOO_4` enum only |
| Render pipeline | `src/video/vid_voodoo_render.c` | Phase 2: 32-bit path |
| LFB access | `src/video/vid_voodoo_fb.c` | Phase 2: 32-bit LFB |
| Scanout | `src/video/vid_voodoo_display.c` | Phase 2: 32-bit scanout |
| Texture fetch | `src/video/vid_voodoo_texture.c` | Phase 2: larger LOD, 32-bit formats |
| ARM64 JIT | `src/include/86box/vid_voodoo_codegen_arm64.h` | Phase 3: 32-bit FB write |
| x86-64 JIT | `src/include/86box/vid_voodoo_codegen_x86-64.h` | Phase 3: 32-bit FB write |
| Build | `src/video/CMakeLists.txt` | Add vid_voodoo4.c |
| Card table | `src/video/vid_table.c` | Add device entry |
| Card extern | `src/include/86box/video.h` | Add extern declaration |

---

## Phase 1 — Standalone VSA-100 Driver (POST + VGA)

**Goal**: `vid_voodoo4.c` brings up the VSA-100 correctly through its own PCI config and
register handling so the BIOS POSTs and Windows recognizes the card. The 3D render path
delegates to the existing Voodoo shared pipeline (16-bit mode initially).

**Complexity**: Medium (more than a stub — needs correct PCI + register stubs for BIOS)
**Key insight**: The BIOS at `0xFE6E` probes the chip identity; we must respond correctly
to those reads or the `JC` at `0xC0F4` will skip all display initialization.

### Strategy

1. Create `vid_voodoo4.c` copied from Banshee as structural reference
2. Add comprehensive `fprintf(stderr)` logging on ALL PCI config reads/writes and ALL
   register reads/writes (VSA-100 register space) so we can observe the BIOS init sequence
3. Identify exactly which registers the BIOS probes (from the log)
4. Stub those registers to return correct VSA-100 responses
5. Iterate until POST succeeds and the card is visible to the OS

### Checklist

- [x] `src/include/86box/vid_voodoo_common.h`: Add `VOODOO_4 = 4` to voodoo type enum
- [x] `src/video/vid_voodoo4.c`: Create standalone file with:
  - [x] PCI config read/write handlers (vendor=0x121A, device=0x0009)
  - [x] VSA-100 register read/write dispatch with full logging
  - [x] Init function: SVGA core, memory mapping, ROM loading, PCI registration
  - [x] `available()` checking ROM presence
  - [x] `device_t voodoo4_4500_agp_device` definition
- [x] `src/include/86box/vid_voodoo4.h`: Header with extern and register defines
- [x] `src/video/CMakeLists.txt`: Add `vid_voodoo4.c`
- [x] `src/video/vid_table.c`: Add device entry
- [x] `src/include/86box/video.h`: Add extern
- [x] Build, boot VM, collect BIOS init log
- [x] Analyze log: identify chip probe register(s) and expected response
- [x] Stub probe registers with correct VSA-100 responses
- [x] Iterate until BIOS POST succeeds (no black screen)
- [x] Confirm Windows detects "Voodoo 4 4500"
- [x] Confirm basic VGA works (VGA text mode works perfectly)
- [x] Add I/O handler diagnostic logging
- [x] Fix BAR2 write handler leak (cases 0x1a/0x1b)
- [x] Fix CLUT palette (makecol32 for alpha byte)
- [x] Identify display corruption root cause (3-layer: lost vgaInit1, planar mode, fast flag)
- [x] Force packed_chain4 + chain4 + GDC pass-through for VESA mode
- [x] Force svga->fast=1 in v4_out post-fixup
- [x] Hardware cursor visible and correctly positioned
- [ ] Fix VBE banking (write_bank stays 0 — primary blocker for full framebuffer)
- [ ] Fix stride/address alignment for correct VESA display
- [ ] Remove memaddr_latch=0 hack (proper vidDesktopStartAddr handling)
- [ ] Remove diagnostic logging for clean commit

---

## Phase 2 — 32-bit Render Pipeline (Software)

**Goal**: Full 32-bit RGBA rendering via the software rasterizer.

**Complexity**: Medium-High
**Prerequisite**: Phase 1 complete

### The Core Problem

The shared rendering stack (`vid_voodoo_render.c`, `vid_voodoo_fb.c`, `vid_voodoo_display.c`)
is hardwired to `uint16_t *fb_mem`. The strategy is **conditional dual-path** gated on
`voodoo->type == VOODOO_4`.

### Sub-tasks

#### 2a — Framebuffer infrastructure
- [ ] `vid_voodoo_common.h`: Add `fb_32bit` flag to `voodoo_t`
- [ ] `vid_voodoo_common.h`: Add `uint32_t *fb_mem_32, *aux_mem_32` pointers
- [ ] `vid_voodoo4.c`: Allocate 32-bit framebuffer; set `row_width` accordingly
- [ ] Define `fb_write_pixel()` / `fb_read_pixel()` inline helpers dispatching on `fb_32bit`

#### 2b — LFB access (`vid_voodoo_fb.c`)
- [ ] 32-bit ARGB8888 write path (bypass RGB565 pack)
- [ ] 32-bit read path
- [ ] Handle `lfbMode` pixel format field for V4 formats

#### 2c — Rasterizer output (`vid_voodoo_render.c`)
- [ ] Pixel write: if `fb_32bit`, write `uint32_t` instead of `uint16_t`
- [ ] Skip dither table lookup in 32-bit mode
- [ ] Preserve alpha channel in 32-bit output

#### 2d — Depth + stencil
- [ ] aux_mem as 32-bit: 24-bit Z packed with 8-bit stencil in high byte
- [ ] Z read/write: `Z = value & 0x00FFFFFF`, `S = (value >> 24) & 0xFF`
- [ ] Stencil test and write ops (keep/replace/increment/decrement/invert)

#### 2e — Texture changes (`vid_voodoo_texture.c`)
- [ ] `LOD_MAX` from 8 to 11 (2048×2048) for V4
- [ ] Widen dimension mask arrays for LOD 9/10/11
- [ ] ARGB8888 and RGBA8888 texture decode paths

#### 2f — Display scanout (`vid_voodoo_display.c`)
- [ ] When `fb_32bit`: read `uint32_t` per pixel, extract R/G/B directly
- [ ] 32-bit → 24-bit conversion for display output

#### 2g — Testing
- [ ] 32-bit color 3D app (e.g., Quake III) under Win9x/Win2K
- [ ] Correct colors, no corruption, Z-buffer correct
- [ ] 16-bit mode regression (GLQuake) passes

---

## Phase 3 — JIT Backend (ARM64 + x86-64)

**Goal**: Extend JIT codegen for 32-bit framebuffer writes.

**Complexity**: High
**Prerequisite**: Phase 2 complete

- [ ] ARM64 Phase 6 alternate: bypass dither, pack ARGB8888, `STR` (32-bit) to `fb_mem_32`
- [ ] ARM64 Phase 6: 32-bit depth+stencil write
- [ ] ARM64: stencil test sequence when stencil enabled
- [ ] x86-64: same dual-path Phase 6
- [ ] Validate both JIT backends

---

## Phase 4 — Stencil Operations (Stretch)

- [ ] All stencil ops: KEEP, ZERO, REPLACE, INCR, DECR, INVERT, INCR_WRAP, DECR_WRAP
- [ ] Stencil clear in fast-clear path
- [ ] Validate with stencil-shadow demo

---

## Out of Scope

- **T-buffer / FSAA**: Skip
- **FXT1 texture compression**: Defer
- **DXTC / S3TC**: Defer
- **Dual pixel pipelines**: Defer
- **Voodoo 5 SLI**: Out of scope

---

## Driver Notes

- Windows 9x: 3dfx unified driver 1.04.00 + `3dfxvgl.dll` ICD
- Windows 2000/XP: `win2k-1.00.01` driver
- Glide: Glide 3.x SDK supports VSA-100
- BIOS handles memory detection; 32 MB SDRAM is the standard V4 4500 config

---

## Success Criteria

| Milestone | Criteria |
|-----------|----------|
| Phase 1 | Card visible in picker; BIOS POSTs without black screen; Windows sees "Voodoo 4 4500" |
| Phase 2 | 32-bit desktop; 3D app renders correctly in 32-bit; no 16-bit regressions |
| Phase 3 | JIT active in 32-bit mode; frame rate comparable to V3 JIT |
| Phase 4 | Stencil shadows render correctly |
