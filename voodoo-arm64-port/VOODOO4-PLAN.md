# Voodoo 4 (VSA-100) Emulation Plan

**Branch**: `voodoo4`
**ROM**: `V4_4500_AGP_SD_1.18.rom` (Voodoo 4 4500 AGP, BIOS v1.18, 64 KB)
**Baseline**: Existing Voodoo 3 / Banshee emulation in `src/video/vid_voodoo_banshee.c`

---

## Overview

The 3dfx VSA-100 chip (codenamed "Napalm") powers the Voodoo 4 4500 and Voodoo 5 5500/6000. It is architecturally a direct evolution of the Voodoo 3 (Avenger), sharing the same PCI vendor ID (`0x121A`), BAR layout, command FIFO, 2D blitter, VGA core, and register base. The key additions are a 32-bit render pipeline, 32-bit depth+stencil, larger textures, and hardware FSAA via the T-buffer.

This plan targets **Voodoo 4 4500 single-chip** only. Voodoo 5 dual-chip SLI is explicitly out of scope.

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

## Codebase Map

Files that need modification, grouped by phase:

| File | Phase | Change |
|------|-------|--------|
| `src/include/86box/vid_voodoo_common.h` | 1 | Add `VOODOO_4` enum value, `TYPE_V4_4500` |
| `src/video/vid_voodoo_banshee.c` | 1 | Add init, PCI config, device_t entry |
| `src/video/video.c` | 1 | Register card in video card list |
| `src/video/vid_voodoo_render.c` | 2 | Dual 16/32-bit framebuffer write path |
| `src/video/vid_voodoo_fb.c` | 2 | 32-bit LFB read/write |
| `src/video/vid_voodoo_display.c` | 2 | 32-bit scanout |
| `src/video/vid_voodoo_texture.c` | 2 | LOD_MAX=11, 32-bit texture formats |
| `src/include/86box/vid_voodoo_codegen_arm64.h` | 3 | JIT Phase 6 dual-path |
| `src/include/86box/vid_voodoo_codegen_x86-64.h` | 3 | JIT Phase 6 dual-path |
| `src/video/vid_voodoo_render.c` | 4 | Stencil test/write |

---

## Phase 1 — Minimal Stub (16-bit mode)

**Goal**: Voodoo 4 4500 appears in card list, POSTs, and is recognized by Windows. Runs on the existing Voodoo 3 render pipeline (16-bit color, all V3 limits). This is effectively a Voodoo 3 with different PCI IDs and 32 MB RAM.

**Complexity**: Low
**Reuse**: ~95% of Banshee/V3 code

### Checklist

- [ ] Copy ROM to `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom`
- [ ] `vid_voodoo_common.h`: Add `VOODOO_4 = 4` to the voodoo type enum
- [ ] `vid_voodoo_common.h`: Document `TYPE_V4_4500` constant (to be added in banshee.c)
- [ ] `vid_voodoo_banshee.c`: Add `#define ROM_VOODOO4_4500 "roms/video/voodoo/V4_4500_AGP_SD_1.18.rom"`
- [ ] `vid_voodoo_banshee.c`: Add `TYPE_V4_4500` to the type enum
- [ ] `vid_voodoo_banshee.c`: Return device ID `0x0009` in `banshee_pci_read()` when type is V4
- [ ] `vid_voodoo_banshee.c`: Add `v4_4500_agp_init()` calling `banshee_init_common()` with 32 MB, SDRAM
- [ ] `vid_voodoo_banshee.c`: Add `const device_t voodoo4_4500_agp_device` definition
- [ ] `video.c` (or equivalent registry): Add Voodoo 4 4500 AGP to the selectable card list
- [ ] Build and confirm card appears in 86Box card picker
- [ ] Boot a VM, confirm BIOS POSTs and Windows recognizes the card
- [ ] Install 3dfx drivers (Win9x or Win2K), confirm basic 2D/VGA works

### Risk

Low. The only risk is that VSA-100 drivers probe for registers that respond incorrectly. If so, stub those registers to return safe values (0 or V3-compatible).

---

## Phase 2 — 32-bit Render Pipeline (Software)

**Goal**: Full 32-bit RGBA rendering via the software rasterizer. No JIT changes yet — JIT will fall back to interpreter for 32-bit mode. This unlocks true-color rendering and 32-bit desktop on Windows.

**Complexity**: Medium-High
**Prerequisite**: Phase 1 complete

### The Core Problem

The entire rendering stack is hardwired to `uint16_t *fb_mem`. The strategy is **conditional dual-path**: check `voodoo->type >= VOODOO_4` (or a `is_32bit` flag) at the relevant write/read points.

### Sub-tasks

#### 2a — Framebuffer infrastructure
- [ ] `vid_voodoo_common.h`: Add `fb_32bit` flag to the voodoo state struct
- [ ] `vid_voodoo_common.h`: Add `uint32_t *fb_mem_32, *aux_mem_32` pointers (or union)
- [ ] `vid_voodoo_banshee.c`: Allocate 32-bit framebuffer when `VOODOO_4`; set `row_width` accordingly
- [ ] Define `fb_write_pixel()` / `fb_read_pixel()` inline helpers that dispatch on `fb_32bit`

#### 2b — LFB access (`vid_voodoo_fb.c`)
- [ ] LFB write: add 32-bit ARGB8888 write path (bypass RGB565 pack)
- [ ] LFB read: add 32-bit read path
- [ ] Handle `lfbMode` pixel format field for V4 formats

#### 2c — Rasterizer output (`vid_voodoo_render.c`)
- [ ] Pixel write at end of render loop: if `fb_32bit`, write `uint32_t` instead of `uint16_t`
- [ ] Skip dither table lookup in 32-bit mode (no precision loss)
- [ ] Preserve alpha channel in 32-bit output

#### 2d — Depth + stencil (`vid_voodoo_render.c`, `vid_voodoo_fb.c`)
- [ ] Change aux_mem to 32-bit for V4 (24-bit Z packed with 8-bit stencil in high byte)
- [ ] Update Z read/write to mask correctly: `Z = value & 0x00FFFFFF`, `S = (value >> 24) & 0xFF`
- [ ] Add stencil test: compare stencil ref vs stored, using zaMode stencil bits
- [ ] Add stencil write ops: keep/replace/increment/decrement/invert

#### 2e — Texture changes (`vid_voodoo_texture.c`)
- [ ] Increase `LOD_MAX` from 8 to 11 (2048×2048)
- [ ] Widen texture dimension mask arrays for LOD 9/10/11
- [ ] Add ARGB8888 texture decode path in the texel fetch function
- [ ] Add RGBA8888 decode path

#### 2f — Display scanout (`vid_voodoo_display.c`)
- [ ] When `fb_32bit`: read `uint32_t` per pixel, extract R/G/B directly (no RGB565 decomposition)
- [ ] Handle 32-bit → 24-bit conversion for display output

#### 2g — Testing
- [ ] Run a 32-bit color 3D application (e.g., Quake III demo) under Win9x/Win2K
- [ ] Confirm correct colors, no corruption
- [ ] Confirm Z-buffer works (no Z-fighting or transparency glitches)
- [ ] Run GLQuake (16-bit mode) to confirm no regression

---

## Phase 3 — JIT Backend (ARM64 + x86-64)

**Goal**: Extend the JIT codegen to support 32-bit framebuffer writes, restoring JIT performance for 32-bit mode.

**Complexity**: High
**Prerequisite**: Phase 2 complete and validated

### ARM64 (`vid_voodoo_codegen_arm64.h`)

The existing Phase 6 (framebuffer write) emits:
1. Dither lookup → 16-bit packed RGB565
2. `STRH` to `fb_mem`

For V4 32-bit mode, emit instead:
1. No dither (bypass)
2. Pack R/G/B/A into 32-bit `ARGB8888`
3. `STR` (32-bit) to `fb_mem_32`

- [ ] Add `fb_32bit` flag check in JIT compile path
- [ ] Emit 32-bit framebuffer write block (Phase 6 alternate)
- [ ] Emit 32-bit depth+stencil write (Phase 6 aux_mem)
- [ ] Emit stencil test sequence if stencil enabled
- [ ] Validate codegen output with `voodoo-debug` agent

### x86-64 (`vid_voodoo_codegen_x86-64.h`)

Same changes as ARM64, adapted for x86-64 register conventions and instruction encoding.

- [ ] Add dual-path Phase 6 for 32-bit mode
- [ ] Validate codegen

---

## Phase 4 — Stencil Operations (Stretch)

**Goal**: Full stencil pipeline for OpenGL stencil-dependent effects (shadows, portal rendering).

**Complexity**: Medium
**Prerequisite**: Phase 2 stencil infrastructure

- [ ] Implement all stencil ops: KEEP, ZERO, REPLACE, INCR, DECR, INVERT, INCR_WRAP, DECR_WRAP
- [ ] Implement separate front/back stencil (if VSA-100 supports it — verify from datasheet)
- [ ] Add stencil clear to fast-clear path
- [ ] Validate with a stencil-shadow demo

---

## Out of Scope (this implementation)

The following VSA-100 features are explicitly deferred:

- **T-buffer / FSAA**: Hardware accumulation buffer for anti-aliasing. Architecturally complex, few games used it. Skip.
- **FXT1 texture compression**: 3dfx proprietary format. Can be added later if needed.
- **DXTC / S3TC texture compression**: DXT1-5. Useful but not blocking. Defer.
- **Dual pixel pipelines**: VSA-100 has 2 pixel pipes with 1 TMU each vs V3's 1 pipe with 2 TMUs. The render loop restructuring is significant. Defer.
- **Voodoo 5 dual-chip SLI**: Two VSA-100 chips with interleaved scan lines. Out of scope entirely.

---

## File Placement

| Asset | Path |
|-------|------|
| ROM file | `roms/video/voodoo/V4_4500_AGP_SD_1.18.rom` |
| Main driver | `src/video/vid_voodoo_banshee.c` (extend) |
| Common state | `src/include/86box/vid_voodoo_common.h` (extend) |
| Render pipeline | `src/video/vid_voodoo_render.c` (extend) |
| LFB access | `src/video/vid_voodoo_fb.c` (extend) |
| Scanout | `src/video/vid_voodoo_display.c` (extend) |
| Texture fetch | `src/video/vid_voodoo_texture.c` (extend) |
| ARM64 JIT | `src/include/86box/vid_voodoo_codegen_arm64.h` (extend) |
| x86-64 JIT | `src/include/86box/vid_voodoo_codegen_x86-64.h` (extend) |

---

## Driver Notes

- Windows 9x: Use the final official 3dfx unified driver (1.04.00). Also supports OpenGL via the `3dfxvgl.dll` ICD.
- Windows 2000/XP: Use `win2k-1.00.01` or the leaked WinXP beta drivers.
- Glide: Glide 3.x SDK supports VSA-100. Glide 2.x apps should work via the compatibility shim.
- The BIOS (`V4_4500_AGP_SD_1.18.rom`) handles memory detection at POST; 32 MB SDRAM is the standard V4 4500 configuration.

---

## Success Criteria

| Milestone | Criteria |
|-----------|----------|
| Phase 1 | Card visible in 86Box picker; BIOS POSTs; Windows detects "Voodoo 4 4500" |
| Phase 2 | 32-bit desktop color; 3D app renders correctly in 32-bit; no 16-bit regressions |
| Phase 3 | JIT path active in 32-bit mode; frame rate comparable to V3 JIT in 16-bit |
| Phase 4 | Stencil shadows render correctly in a test app |
