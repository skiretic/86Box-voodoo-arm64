# NV3 Implementation Checklist

## Phase 1: Device Skeleton + VGA Boot
- [x] `nv3_t` struct definition with `svga_t` first member
- [x] `device_t` registration (Riva 128 + Riva 128 ZX)
- [x] PCI config space (vendor 0x12D2, device 0x0018/0x0019)
- [x] BAR0 MMIO mapping (16MB)
- [x] BAR1 linear framebuffer mapping (16MB)
- [x] ROM/BIOS loading (`rom_init()`)
- [x] PCI Expansion ROM BAR handling
- [x] `svga_init()` with 4MB/8MB VRAM
- [x] VGA I/O intercept (`video_in`/`video_out`)
- [x] Extended CRTC registers (0x19, 0x1A, 0x1D, 0x1E, 0x25, 0x28, 0x2D)
- [x] Sequencer lock/unlock (index 0x06)
- [x] `recalctimings_ex()` hook
- [x] Stub MMIO handlers (log unknowns)
- [x] CMakeLists.txt + vid_table.c integration
- [x] Build succeeds
- [x] DOS boots with VGA text mode

## Phase 2: Core Subsystems
### PMC (0x000000)
- [x] BOOT_0 chip ID readback
- [x] INTR_0 interrupt status (read-only aggregation from subsystems)
- [x] INTR_EN_0 interrupt enable (2-bit enum: disabled/hardware/software)
- [x] ENABLE subsystem enable
- [x] PCI IRQ routing (pci_set_irq/pci_clear_irq based on INTR_0 & INTR_EN_0)

### PTIMER (0x009000)
- [ ] Connect to nv_rivatimer.c
- [x] NUMERATOR / DENOMINATOR
- [x] TIME_0 / TIME_1
- [x] ALARM_0 + interrupt

### PFB (0x100000)
- [x] BOOT_0 memory config (read-only, hardware-determined)
- [x] Memory size detection
- [x] Register bank for unhandled PFB offsets (DELAY, DEBUG_0, GREEN_0, etc.)

### PEXTDEV (0x101000)
- [x] Crystal frequency straps (14.318 MHz / 13.5 MHz from straps bit 6)
- [x] Register bank for unhandled PEXTDEV offsets (0x101114, 0x101200, etc.)

### PRAMDAC (0x680000)
- [x] VPLL (pixel clock) — M/N/P coefficients, PLL formula
- [x] NVPLL (core clock)
- [x] MPLL (memory clock)
- [x] GENERAL_CONTROL (DAC bit depth, cursor mode)
- [x] CURSOR_START register
- [ ] Cursor engine rendering (hwcursor_draw callback)
- [x] Color LUT (palette passthrough working via SVGA core)

### PROM (0x110000)
- [x] BIOS ROM read through MMIO window

### MMIO Infrastructure
- [x] Unmapped MMIO returns 0x00000000 (was mmio_fallback[], now removed)
- [x] PRAMDAC/PCRTC/PGRAPH/PFIFO register bank sync with dedicated fields
- [x] MMIO trace logging (ENABLE_NV3_MMIO_TRACE_LOG compile flag)

### PCRTC (0x600000)
- [x] VBlank interrupt (via svga->vblank_start callback)
- [x] Display start address (START register)
- [x] CONFIG register
- [ ] Win98 boots with SVGA (needs driver testing)

## Phase 3: PFIFO + RAMIN
### RAMIN
- [x] Address window (0x700000, 1MB)
- [x] VRAM<->RAMIN translation
- [ ] DMA object parsing

### RAMHT
- [x] Hash table allocation
- [x] XOR hash function
- [x] Object name -> class lookup

### RAMFC
- [x] Channel context save/restore
- [x] DMA_PUT/DMA_GET per channel

### RAMRO
- [x] Error runout buffer

### PFIFO
- [x] Master FIFO enable (CACHES)
- [x] CACHE0 (1 entry)
- [x] CACHE1 (32/64 entries)
- [x] Pusher state machine
- [x] Puller state machine
- [x] USER space PIO dispatch
- [ ] DMA mode (NV3)
- [ ] Command submission validated

## Phase 4: 2D Acceleration
### High Priority Classes
- [ ] 0x10 Blit (screen-to-screen)
- [ ] 0x07 Rectangle (filled)
- [ ] 0x11 Image (from CPU)
- [ ] 0x0C GDI Text
- [ ] 0x05 Clip
- [ ] 0x02 ROP

### Medium Priority Classes
- [ ] 0x12 Bitmap (monochrome)
- [ ] 0x06 Pattern
- [ ] 0x01 Beta
- [ ] 0x03 Chroma
- [ ] 0x0E Scaled image
- [ ] 0x15 Stretched image

### Low Priority Classes
- [ ] 0x04 Plane mask
- [ ] 0x08 Point
- [ ] 0x09 Line
- [ ] 0x0A Lin
- [ ] 0x0B Triangle (2D)
- [ ] 0x0D M2MF
- [ ] 0x14 Transfer to memory
- [ ] 0x1C Surface config

### Infrastructure
- [ ] PGRAPH method dispatch
- [ ] Object binding (context objects)
- [ ] Surface state management
- [ ] ROP3 lookup table
- [ ] Accelerated Windows desktop

## Phase 5: 3D Interpreter
### Class 0x17 -- D3D5 Triangles
- [ ] Method dispatch
- [ ] Vertex buffer (128 x 32 bytes)
- [ ] Triangle kick (FOG_TRI)
- [ ] Edge equation setup
- [ ] Span rasterizer
- [ ] Perspective-correct tex coords

### Texture
- [ ] Texture fetch (4 formats)
- [ ] Point sampling
- [ ] Bilinear filtering
- [ ] Mipmapping (per-polygon)
- [ ] Wrap: cylindrical/wrap/mirror/clamp
- [ ] Color key

### Fragment Operations
- [ ] Per-vertex fog
- [ ] Alpha test (8 functions)
- [ ] Z-buffer test (8 functions)
- [ ] Alpha blending (8 modes)
- [ ] Dither to RGB565
- [ ] Color surface write
- [ ] Zeta surface write
- [ ] Culling (CW/CCW/none/both)

### Class 0x18 -- ZPOINT
- [ ] Z-buffered point rendering

### Validation
- [ ] 3DMark 99 renders
- [ ] Test game(s) display 3D

## Phase 6: ARM64 JIT Codegen
- [ ] Pipeline state key definition
- [ ] Block cache
- [ ] Codegen prologue/epilogue
- [ ] Texture coord interpolation
- [ ] Texture address + fetch
- [ ] Bilinear filter
- [ ] Fog blend
- [ ] Alpha test
- [ ] Z-buffer test
- [ ] Alpha blend
- [ ] Dither
- [ ] Surface write
- [ ] Per-pixel increment
- [ ] Verify mode
- [ ] NEON optimization pass

## Phase 7: x86-64 JIT Codegen
- [ ] Port ARM64 -> x86-64
- [ ] SSE2 texture filter
- [ ] SSE2 blend + dither
- [ ] Verify mode

## Phase 8: Variants + Polish
- [ ] Riva 128 ZX (NV3T) -- 8MB, AGP 2x, CACHE1=64
- [ ] PVIDEO overlay engine
- [x] PME (Mediaport) stub — interrupt registers + register bank
- [ ] Memory arbitration
- [ ] DMA push buffer mode
- [ ] I2C / DDC2
- [ ] Multiple ROM images
