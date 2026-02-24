# NVIDIA Riva 128 (NV3) — Implementation Plan

**Date**: 2026-02-22
**Branch**: TBD (new repo or clean branch off upstream 86Box)
**Feasibility study**: `86Box-voodoo-arm64/riva128-nv3-port/feasibility-study.md`

---

## Approach

Same proven sequence as the Voodoo port:
1. Software interpreter first (get it correct)
2. ARM64 JIT codegen (get it fast on Apple Silicon)
3. x86-64 JIT codegen (get it fast everywhere)

The 86Box-nv fork by starfrost013 is a potential starting point — it has 43+ files with VGA working and 2D stubs. Decision: fork from that, or build from scratch using it as reference? **Recommend: use as reference, build fresh** to keep the codebase clean and aligned with our coding style.

---

## Phase 1: Device Skeleton + VGA Boot (Weeks 1-3)

### Goal
Boot to a DOS prompt with VGA text mode output.

### Files to Create
```
src/video/nv/
  vid_nv3.c              — Main device file (device_t, init/close, PCI config)
  vid_nv3.h              — Private header (nv3_t struct, subsystem state)
src/include/86box/nv/
  vid_nv3_regs.h         — Register address defines (from envytools rnndb + nv3_ref.h)
```

### Files to Modify
```
src/video/vid_table.c    — Add device_t entries (Riva 128, Riva 128 ZX)
src/video/CMakeLists.txt — Add vid_nv3.c to vid library (nv/ section exists)
```

### Tasks
- [ ] Define `nv3_t` struct with `svga_t` as first member
- [ ] PCI config space: vendor 0x12D2, device 0x0018/0x0019, class 0x030000
- [ ] BAR0: 16MB MMIO region with `mem_mapping_add()`
- [ ] BAR1: 16MB linear framebuffer with `mem_mapping_add()`
- [ ] ROM loading: `rom_init()` at 0xC0000, 64KB, `MEM_MAPPING_EXTERNAL`
- [ ] PCI Expansion ROM BAR (0x30) handling for ROM relocation
- [ ] `svga_init()` with 4MB VRAM (NV3) or 8MB (NV3T)
- [ ] VGA I/O port intercept: `video_in()`/`video_out()` hooks
- [ ] Extended CRTC registers: 0x19 (REPAINT_0), 0x1A (REPAINT_1), 0x1D/0x1E (banks), 0x25 (vert ext), 0x28 (pixel fmt), 0x2D (horiz ext)
- [ ] Sequencer lock/unlock: index 0x06, write 0x57=unlock, 0x99=lock
- [ ] `recalctimings_ex()` hook for pixel clock from PRAMDAC VPLL
- [ ] `pci_add_card(PCI_ADD_AGP, ...)` for AGP variant, `PCI_ADD_NORMAL` for PCI
- [ ] Stub MMIO read/write handlers (log unknown accesses)
- [ ] Build + boot test: DOS with VGA text mode

### Reference Code
- `vid_voodoo_banshee.c` — BAR mapping, PCI config, ROM load pattern
- `riva_tbl.h` (rivafb) — Golden register init sequences
- `riva_hw.c` — `nv3GetConfig()`, `nv3LockUnlock()`

### Validation
- DOS boots with VGA text mode (80x25)
- VGA BIOS POST completes
- Mode 13h (320x200x256) works
- PCI config space reads back correct IDs

---

## Phase 2: Core Subsystems (Weeks 4-6)

### Goal
Windows 95/98 boots with correct SVGA resolution and color depth.

### Tasks

#### PMC (Master Control) — 0x000000
- [ ] BOOT_0 (0x000): Chip ID readback (0x30100/0x30110/0x30120)
- [ ] INTR_0 (0x100): Interrupt status (read clears, write masks)
- [ ] INTR_EN_0 (0x140): Interrupt enable mask
- [ ] ENABLE (0x200): Subsystem enable bits
- [ ] Wire PMC interrupts to PCI IRQ via `pci_set_irq()`/`pci_clear_irq()`

#### PTIMER — 0x009000
- [ ] Connect to existing `nv_rivatimer.c` infrastructure
- [ ] INTR_0 (0x9100): Timer interrupt
- [ ] NUMERATOR (0x9200) / DENOMINATOR (0x9210): Time base
- [ ] TIME_0/TIME_1 (0x9400/0x9410): Current time
- [ ] ALARM_0 (0x9420): Alarm threshold
- [ ] Actually call `rivatimer_update_all()` from poll loop

#### PFB (Framebuffer Controller) — 0x100000
- [ ] BOOT_0 (0x100000): Memory config (SGRAM/SDRAM, size, bus width)
- [ ] CONFIG_0: Framebuffer tiling, surface config
- [ ] Memory size detection based on PFB_BOOT_0

#### PEXTDEV — 0x101000
- [ ] Straps readback: crystal frequency (13.5 vs 14.318 MHz)
- [ ] Bus width, memory type detection

#### PRAMDAC — 0x680000
- [ ] VPLL (0x680508): Video pixel clock PLL
- [ ] NVPLL (0x680500): Core clock PLL
- [ ] MPLL (0x680504): Memory clock PLL
- [ ] PLL formula: `Freq = (Crystal * N) / (M * (1 << P))`
- [ ] GENERAL_CONTROL: Pixel format, sync polarity
- [ ] Cursor engine (position, shape, color)
- [ ] Color LUT (palette) access

#### PCRTC — 0x600000
- [ ] INTR (0x600100): VBlank interrupt
- [ ] START (0x600800): Display start address
- [ ] CONFIG (0x600804): Interlace, double-scan

### Reference Code
- `riva_hw.c` — `CalcVClock()`, `nv3GetConfig()`, `nv3UpdateArbitrationSettings()`
- `riva_tbl.h` — Init table values for each subsystem
- envytools rnndb XML files for register bitfields

### Validation
- Windows 95/98 driver installs and sets SVGA mode
- Multiple resolutions work (640x480, 800x600, 1024x768)
- 8bpp, 16bpp, 32bpp color depths
- Hardware cursor visible and positioned correctly
- VBlank interrupt fires at correct rate

---

## Phase 3: PFIFO + RAMIN (Weeks 7-10)

### Goal
PFIFO processes graphics commands from the Windows display driver.

This is the **hardest infrastructure phase** — NV3's object-oriented command model is fundamentally different from Voodoo's direct register writes.

### Tasks

#### RAMIN (Instance Memory) — 0x700000
- [ ] RAMIN address window at BAR0 + 0x700000 (1MB)
- [ ] VRAM↔RAMIN address translation: `addr XOR (vram_size - 16)`
- [ ] DMA object structure parsing

#### RAMHT (Hash Table)
- [ ] Configurable location in first 64KB of RAMIN
- [ ] Size: 4KB-32KB (configurable via PFIFO register)
- [ ] Hash function: XOR of object handle bytes + channel ID
- [ ] Lookup: object name → (engine, class, RAMIN offset)

#### RAMFC (FIFO Context)
- [ ] 128 channel contexts, 8 bytes each, 0x1000 bytes total
- [ ] Save/restore channel state on context switch
- [ ] DMA_PUT, DMA_GET, REF_CNT per channel

#### RAMRO (Runout Buffer)
- [ ] Error queue: 0x200 or 0x2000 bytes (configurable)
- [ ] Log invalid method/object errors

#### PFIFO Engine — 0x002000
- [ ] CACHES (0x2500): Master FIFO enable
- [ ] CACHE0: 1-entry notifier injection cache
  - PUSH0 (0x3000): Push enable/access
  - PULL0 (0x3040): Pull enable
- [ ] CACHE1: 32 entries (NV3) / 64 entries (NV3T)
  - PUSH0/PUSH1 (0x3200/0x3204): Push enable, channel select
  - PUT (0x3210): Write pointer
  - PULL0/PULL1 (0x3240/0x3250): Pull enable, engine select
  - GET (0x3270): Read pointer
- [ ] Pusher state machine: reads USER MMIO writes → CACHE1
- [ ] Puller state machine: CACHE1 → PGRAPH dispatch
- [ ] DMA mode (NV3): kernel-assisted batch DMA
  - DMA_STATE (0x3018), DMA_CTRL (0x3020), DMA_COUNT (0x3024), DMA_GET (0x3028)

#### USER Space — 0x800000
- [ ] 8 channels × 8 subchannels × 8KB = user submission area
- [ ] PIO writes to USER → PFIFO pusher
- [ ] DMA_PUT/DMA_GET per channel (0x800040+i*0x8000)

### Reference Code
- envytools `nv1_pfifo.xml` — Register definitions
- envytools [NV1:NV4 PFIFO docs](https://envytools.readthedocs.io/en/latest/hw/fifo/nv1-pfifo.html)
- `nouveau_reg.h` — NV03_PFIFO_* register addresses
- `riva_tbl.h` — nv3TablePFIFO (26 init entries), nv3TablePRAMIN (61 entries)
- 86Box-nv `nv3_pfifo.c`, `nv3_pramin.c`, `nv3_pramin_ramht.c`

### Validation
- PFIFO initializes from driver init sequence
- RAMHT hash lookup resolves object names to classes
- PIO command submission works (write to USER → CACHE1 → PGRAPH)
- No RAMRO errors during normal operation
- Channel context save/restore works

---

## Phase 4: 2D Acceleration (Weeks 11-14)

### Goal
Accelerated Windows desktop with correct 2D rendering.

### Object Classes to Implement

| Priority | Class | Name | Methods |
|----------|-------|------|---------|
| HIGH | 0x10 | Blit | Screen-to-screen blit |
| HIGH | 0x07 | Rectangle | Filled rectangles |
| HIGH | 0x11 | Image | Image from CPU |
| HIGH | 0x0C | GDI Text | Windows 95 text acceleration |
| HIGH | 0x05 | Clip | Clipping rectangle |
| HIGH | 0x02 | ROP | Raster operation |
| MED | 0x12 | Bitmap | Monochrome bitmap |
| MED | 0x06 | Pattern | Blit pattern (8x8 mono/color) |
| MED | 0x01 | Beta | Alpha blending factor |
| MED | 0x03 | Chroma | Color key transparency |
| MED | 0x0E | Scaled | Scaled image from memory |
| MED | 0x15 | Stretch | Stretched image from CPU |
| LOW | 0x04 | Plane | Bitplane mask |
| LOW | 0x08 | Point | Point primitives |
| LOW | 0x09 | Line | Line primitives |
| LOW | 0x0A | Lin | Lines without endpoints |
| LOW | 0x0B | Triangle | 2D triangles |
| LOW | 0x0D | M2MF | Memory-to-memory format convert |
| LOW | 0x14 | ToMem | Transfer to memory (screen capture) |
| LOW | 0x1C | InMem | Surface configuration |

### Tasks
- [ ] PGRAPH method dispatch: class+method → handler function
- [ ] PGRAPH context: current bound objects (ROP, clip, pattern, surface)
- [ ] Object binding: method 0x0180-0x01FC binds context objects
- [ ] Surface state: offset, pitch, format per surface (4 surfaces)
- [ ] ROP3 lookup table (256 entries)
- [ ] Implement high-priority classes first (blit, rect, image, GDI text)
- [ ] Clipping rectangle intersection
- [ ] Pattern application (8x8 mono/color)
- [ ] Chroma key comparison
- [ ] Color format conversion between surfaces

### Reference Code
- envytools `nv3_gdi.xml` — GDI class method definitions
- envytools [2D pipeline docs](https://envytools.readthedocs.io/en/latest/hw/graph/2d/intro.html)
- `nouveau nvkm/engine/gr/nv04.c` — Method dispatch for NV03 classes
- 86Box-nv `classes/` directory — 24 class implementation files
- `riva_xaa.c` — XAA 2D acceleration (FIFO-based rect/blit/line)

### Validation
- Windows 95 desktop renders correctly (background, icons, taskbar)
- Window move/resize uses accelerated blit (no tearing/corruption)
- Text rendering is correct (GDI text acceleration)
- Paint/WordPad drawing operations work
- No visual corruption in 2D mode

---

## Phase 5: 3D Interpreter (Weeks 15-20)

### Goal
Direct3D 5 games render correctly.

### Class 0x17 — D3D5 Textured Triangles with Z-Buffer

#### Vertex Format (TLVERTEX, 32 bytes each)
```
Offset 0x00: Specular color (BGRA) + fog alpha in high byte
Offset 0x04: Diffuse color (BGRA)
Offset 0x08: Screen X (float, pre-transformed)
Offset 0x0C: Screen Y (float, pre-transformed)
Offset 0x10: Screen Z (float, 0.0-1.0)
Offset 0x14: RHW (1/W, for perspective correction)
Offset 0x18: Texture U (float)
Offset 0x1C: Texture V (float)
```

#### Pipeline Stages
1. **Vertex buffer**: 128 slots, fill via TLVERTEX methods at 0x1000-0x1FFC
2. **Triangle kick**: Write to FOG_TRI method triggers triangle with 3 vertex indices
3. **Setup**: Compute edge equations, gradients for all interpolants
4. **Rasterization**: Edge walking + span generation
5. **Texture coordinate interpolation**: Perspective-correct (divide by W)
6. **Texture fetch**: From DMA_TEXTURE memory region
7. **Texture filtering**: Point sample or bilinear (4-tap)
8. **Fog**: Per-vertex fog color from specular alpha
9. **Alpha test**: Compare alpha vs reference (8 functions)
10. **Z-buffer test**: Compare Z vs framebuffer Z (8 functions)
11. **Alpha blending**: Source/dest factor selection (limited to 8 modes)
12. **Color write**: 16-bit RGB565 to color surface
13. **Z write**: 16-bit Z to zeta surface

#### Configuration Registers
```
TEXTURE_OFFSET (0x0304): Texture base address in VRAM
TEXTURE_FORMAT (0x0308): Color key enable, format (4 types), min/max mip size
FILTER (0x030C): Spread X/Y, size adjust
FOG_COLOR (0x0310): RGB fog color
CONFIG (0x0314): Interpolation, wrap U/V, source color, culling, Z persp, blend
ALPHA (0x0318): Alpha reference value + comparison function
```

#### D3D_CONFIG Bit Layout (0x0314)
```
Bits  1-0:  INTERPOLATOR (zero-order, MS variant, full-order)
Bits  3-2:  WRAP_U (cylindrical, wrap, mirror, clamp)
Bits  5-4:  WRAP_V
Bit   6:    SOURCE_COLOR (normal, inverse)
Bits  9-8:  CULLING (none, CW, CCW, both)
Bit  12:    Z_PERSPECTIVE_ENABLE
Bits 16-19: Z_FUNC (comparison)
Bits 20-22: ZETA_WRITE_ENABLE
Bits 24-26: COLOR_WRITE_ENABLE
Bit  28:    ROP (blend=0, add=1)
Bit  29:    BETA (srcalpha=0, dstcolor=1)
Bit  30:    DST_BLEND (invbeta=0, zero=1)
Bit  31:    SRC_BLEND (beta=0, zero=1)
```

#### Texture Formats
| Code | Format | Bits |
|------|--------|------|
| 0 | A1R5G5B5 | 16 |
| 1 | X1R5G5B5 | 16 |
| 2 | A4R4G4B4 | 16 |
| 3 | R5G6B5 | 16 |

### Tasks
- [ ] PGRAPH class 0x17 method dispatch
- [ ] Vertex buffer (128 slots × 32 bytes)
- [ ] Triangle kick via FOG_TRI method
- [ ] Edge equation setup (floating-point)
- [ ] Span rasterizer (edge walking)
- [ ] Perspective-correct texture coordinate interpolation
- [ ] Texture fetch (4 formats, power-of-two, up to 256x256)
- [ ] Point sampling
- [ ] Bilinear filtering (4-tap)
- [ ] Mipmapping (per-polygon LOD selection)
- [ ] Wrap modes: cylindrical, wrap, mirror, clamp
- [ ] Per-vertex fog interpolation and application
- [ ] Alpha test (8 comparison functions)
- [ ] Z-buffer test (8 comparison functions)
- [ ] Alpha blending (8 supported mode combinations)
- [ ] Dithering to 16-bit RGB565
- [ ] Color surface write
- [ ] Zeta surface write (16-bit Z + 8-bit stencil)
- [ ] Class 0x18 (ZPOINT) — Z-buffered points (simpler, good warm-up)
- [ ] Culling (CW/CCW/none/both)
- [ ] Color key (from TEXTURE_FORMAT)

### Reference Code
- envytools `nv3_3d.xml` — Method offsets, vertex layout, config bits
- envytools `nv3_pgraph.xml` — D3D state registers
- `vid_voodoo_render.c` — Similar pipeline structure (texture, Z, alpha, fog, blend)
- D3D5 SDK documentation — TLVERTEX format, render state definitions
- 86Box-nv `nv3_class_017_d3d5_tri_zeta_buffer.c` — Stub (method dispatch only)

### Test Games (D3D5 era)
- 3DMark 99 (D3D5 benchmark)
- Incoming (simple D3D5)
- MechWarrior 2: Mercenaries
- Need for Speed III
- Expendable
- G-Police
- Forsaken

### Validation
- 3DMark 99 renders all scenes
- Test games boot and display 3D content
- Texture mapping is correct (no warping)
- Z-buffering works (correct occlusion)
- Alpha transparency works where supported
- Fog fades correctly
- No pixel-level corruption

---

## Phase 6: ARM64 JIT Codegen (Weeks 21-28)

### Goal
JIT-compile the pixel pipeline for significant speedup on Apple Silicon.

### Approach
Same architecture as the Voodoo ARM64 JIT:
- Compile pixel pipeline configuration → native ARM64 code
- Cache compiled blocks keyed by pipeline state
- Fall back to interpreter for rare/complex states

### Pipeline Stages to JIT
```
1. Texture coordinate perspective divide (1/W multiply)
2. Texture fetch (address calculation + memory read)
3. Texture filter (point or bilinear)
4. Fog application (per-vertex interpolated)
5. Alpha test (compare + conditional skip)
6. Z-buffer test (compare + conditional skip)
7. Alpha blending (src/dst factor multiply + add)
8. Dither (ordered dither to RGB565)
9. Color surface write
10. Zeta surface write
11. Per-pixel iterator increments
```

### NV3 vs Voodoo JIT Comparison
| Feature | Voodoo JIT | NV3 JIT |
|---------|-----------|---------|
| Texture units | 1-2 TMUs | 1 TMU |
| Trilinear | Yes | No |
| Per-pixel fog | Table-based | No (per-vertex only) |
| Blend modes | ~20 | 8 |
| Color combine | Complex cc/cca | Simple src*blend |
| Texture formats | 6+ | 4 |
| **Estimated complexity** | **~4,000 lines** | **~2,000-2,500 lines** |

The NV3 JIT should be roughly **half the complexity** of the Voodoo JIT.

### Tasks
- [ ] Define pipeline state key (CONFIG + ALPHA + TEXTURE_FORMAT bits)
- [ ] Block cache (same design as Voodoo: hash table of compiled blocks)
- [ ] Codegen framework: prologue/epilogue, register allocation
- [ ] Texture coordinate interpolation + perspective divide
- [ ] Texture address calculation (wrap modes)
- [ ] Texture fetch (4 formats → BGRA)
- [ ] Bilinear filter (4-tap interpolation)
- [ ] Per-vertex fog blend
- [ ] Alpha test codegen (8 comparison functions)
- [ ] Z-buffer test codegen (8 comparison functions)
- [ ] Alpha blend codegen (8 mode combinations)
- [ ] Dither to RGB565
- [ ] Surface write (color + zeta)
- [ ] Per-pixel increment (X step for all interpolants)
- [ ] Verify mode (JIT vs interpreter comparison, same as Voodoo)
- [ ] NEON optimization pass (texture filter, blend, dither)

### Reference Code
- `vid_voodoo_codegen_arm64.h` — Direct template for NV3 JIT
- Voodoo JIT optimization batches — apply same patterns

---

## Phase 7: x86-64 JIT Codegen (Weeks 29-34)

### Goal
Performance parity on Intel/AMD hosts.

### Tasks
- [ ] Port ARM64 codegen to x86-64 (SSE2 baseline)
- [ ] Same pipeline stages, same cache design
- [ ] SSE2 for texture filter, blend, dither
- [ ] Verify mode against interpreter

### Reference Code
- `vid_voodoo_codegen_x86-64.h` — Direct template

---

## Phase 8: Variants + Polish (Weeks 35-38)

### Tasks
- [ ] Riva 128 ZX (NV3T): 8MB VRAM, AGP 2x, CACHE1=64, RAMDAC 250MHz
- [ ] PVIDEO overlay engine (YUV→RGB conversion, scaling)
- [ ] PME (Mediaport Engine) — stub or basic
- [ ] Memory arbitration fine-tuning (`nv3UpdateArbitrationSettings`)
- [ ] DMA push buffer mode (beyond PIO)
- [ ] I2C (DDC2) for monitor detection
- [ ] Multiple ROM images (Diamond Viper V330, STB Velocity 128, etc.)

---

## Custom Agent Definitions

Create in `.claude/agents/` when work begins:

### `nv3-lead`
- **Scope**: Device skeleton, VGA, PCI config, build/test, coordination
- **Phases**: 1, 2 (partial)
- **Tools**: Full read/write/build access

### `nv3-subsys`
- **Scope**: Core subsystems (PMC, PTIMER, PFB, PEXTDEV, PRAMDAC, PCRTC)
- **Phases**: 2
- **Tools**: Full read/write access

### `nv3-fifo`
- **Scope**: PFIFO + RAMIN (RAMHT/RAMFC/RAMRO, pusher/puller)
- **Phases**: 3
- **Tools**: Full read/write access

### `nv3-2d`
- **Scope**: 2D object classes, ROP3, blit, GDI text
- **Phases**: 4
- **Tools**: Full read/write access

### `nv3-3d`
- **Scope**: 3D interpreter (class 0x17 rasterizer, texture, blending)
- **Phases**: 5
- **Tools**: Full read/write access

### `nv3-codegen`
- **Scope**: ARM64 and x86-64 JIT codegen for pixel pipeline
- **Phases**: 6, 7
- **Tools**: Full read/write access

### `nv3-debug`
- **Scope**: Validation, debugging, encoding verification
- **Phases**: All (support role)
- **Tools**: Read-only + build

### `nv3-arch`
- **Scope**: Architecture research, spec validation, envytools reference
- **Phases**: All (research role)
- **Tools**: Read-only + web search

---

## Key Reference Materials

### Must-Read Before Starting
1. envytools NV3 rnndb XML files (register definitions)
2. `riva_tbl.h` from Linux rivafb (golden init tables)
3. `riva_hw.c` from xf86-video-nv (PLL calc, memory detect, arbitration)
4. [86Box blog part 1](https://86box.net/2025/02/25/riva128-part-1.html) (architecture overview)
5. envytools [PFIFO docs](https://envytools.readthedocs.io/en/latest/hw/fifo/nv1-pfifo.html)
6. [PCBox/86Box-nv](https://github.com/PCBox/86Box-nv) source code (reference, not fork)

### D3D5 Reference (for 3D pipeline)
- Microsoft D3D5 SDK documentation (TLVERTEX format)
- D3D5 render state reference (blend modes, Z functions, alpha test)
- D3D5 texture stage state machine

### Envytools rnndb XML Files
```
rnndb/graph/nv3_pgraph.xml    — PGRAPH registers
rnndb/graph/nv3_3d.xml        — 3D object class methods
rnndb/graph/nv3_gdi.xml       — GDI 2D methods
rnndb/graph/nv3_pdma.xml      — PDMA registers
rnndb/memory/nv3_pfb.xml      — Framebuffer controller
rnndb/display/nv3_pcrtc.xml   — CRTC scanout
rnndb/display/nv3_pramdac.xml — RAMDAC/PLL/cursor
rnndb/fifo/nv1_pfifo.xml      — PFIFO (NV1:NV4 shared)
```

---

## ROM Images Needed

- NVIDIA Riva 128 BIOS (32KB, mapped at 0xC0000)
- Available from TechPowerUp VGA BIOS Collection
- Multiple board variants exist — start with Diamond Viper V330 or generic

---

## Test Environment

- Same 86Box Windows 98 VM as Voodoo testing
- Install NVIDIA Riva 128 driver (Windows 98 built-in or NVIDIA reference)
- Test games: 3DMark 99, Incoming, Need for Speed III
- Verify mode: JIT vs interpreter pixel comparison (same infra as Voodoo)

---

## Timeline Summary

| Phase | Weeks | Description | Milestone |
|-------|-------|-------------|-----------|
| 1 | 1-3 | Device + VGA | DOS boots with VGA text |
| 2 | 4-6 | Subsystems | Win98 boots with SVGA |
| 3 | 7-10 | PFIFO + RAMIN | Command submission works |
| 4 | 11-14 | 2D Accel | Accelerated Windows desktop |
| 5 | 15-20 | 3D Interpreter | D3D5 games render correctly |
| 6 | 21-28 | ARM64 JIT | Fast 3D on Apple Silicon |
| 7 | 29-34 | x86-64 JIT | Fast 3D on Intel/AMD |
| 8 | 35-38 | Polish | ZX variant, overlay, DMA |
| **Total** | **~38 weeks** | | **Full NV3 with JIT** |
