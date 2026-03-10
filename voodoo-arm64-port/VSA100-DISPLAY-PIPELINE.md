# VSA-100 Display Pipeline Technical Reference

**Author**: voodoo-arch agent
**Date**: 2026-03-10
**Sources**: 3dfx Glide H5 source code (h3defs.h, h3regs.h, h3cinit.c), Linux tdfxfb driver,
86Box vid_voodoo_banshee.c, 86Box vid_voodoo4.c, Banshee 2D Databook r1.0

---

## 1. Register Map Overview

The VSA-100 IO register space begins at BAR0 (MMIO). Registers are grouped:

| Group | Offset Range | Purpose |
|-------|-------------|---------|
| Init  | 0x00-0x3F   | Status, PCI, memory, VGA init |
| PLL   | 0x40-0x4B   | Clock generators |
| DAC   | 0x4C-0x57   | RAMDAC control |
| Video | 0x5C-0xEB   | Display pipeline, cursor, overlay |

### 1.1 Init Register Offsets

| Register | Offset | Description |
|----------|--------|-------------|
| status | 0x00 | Chip status (retrace, busy) |
| pciInit0 | 0x04 | PCI configuration |
| sipMonitor | 0x08 | SIP monitoring |
| **lfbMemoryConfig** | **0x0C** | LFB tile addressing control |
| miscInit0 | 0x10 | Y-origin swap, misc |
| miscInit1 | 0x14 | CLUT invert, 2D block disable |
| dramInit0 | 0x18 | DRAM type and config |
| dramInit1 | 0x1C | DRAM timing |
| agpInit | 0x20 | AGP configuration |
| tmuGbeInit | 0x24 | TMU/GBE init |
| **vgaInit0** | **0x28** | VGA subsystem control |
| **vgaInit1** | **0x2C** | VGA subsystem config |
| strapInfo | 0x34 | Hardware strapping |

### 1.2 PLL/DAC Register Offsets

| Register | Offset | Description |
|----------|--------|-------------|
| **pllCtrl0** | **0x40** | Video PLL (pixel clock) |
| pllCtrl1 | 0x44 | Memory PLL |
| pllCtrl2 | 0x48 | Core PLL |
| **dacMode** | **0x4C** | DAC mode (1x/2x pixel) |
| dacAddr | 0x50 | Palette write address |
| dacData | 0x54 | Palette data |

### 1.3 Video Register Offsets

| Register | Offset | Description |
|----------|--------|-------------|
| vidMaxRGBDelta | 0x58 | Screen filter threshold |
| **vidProcCfg** | **0x5C** | Video processor configuration |
| hwCurPatAddr | 0x60 | Hardware cursor pattern address |
| hwCurLoc | 0x64 | Hardware cursor location |
| hwCurC0 | 0x68 | Cursor color 0 |
| hwCurC1 | 0x6C | Cursor color 1 |
| vidInFormat | 0x70 | Video-in format |
| vidSerialParallelPort | 0x78 | DDC/I2C control |
| vidChromaMin | 0x8C | Chroma key minimum |
| vidChromaMax | 0x90 | Chroma key maximum |
| vidCurrentLine | 0x94 | Current scanline |
| **vidScreenSize** | **0x98** | Display resolution |
| vidOverlayStartCoords | 0x9C | Overlay start X,Y |
| vidOverlayEndScreenCoord | 0xA0 | Overlay end X,Y |
| vidOverlayDudx | 0xA4 | Overlay horizontal scale |
| vidOverlayDudxOffsetSrcWidth | 0xA8 | Overlay source width |
| vidOverlayDvdy | 0xAC | Overlay vertical scale |
| vidOverlayDvdyOffset | 0xE0 | Overlay V offset |
| **vidDesktopStartAddr** | **0xE4** | Desktop framebuffer start |
| **vidDesktopOverlayStride** | **0xE8** | Desktop and overlay stride |

---

## 2. vidProcCfg Register (offset 0x5C) -- COMPLETE BIT MAP

This is the master display pipeline control register.

```
Bit(s)  Name                        Description
------  ----                        -----------
  0     VIDEO_PROCESSOR_EN          Master enable for 2D/video processing engine.
                                    0 = VGA passthrough (standard VGA timing/rendering)
                                    1 = Video processor active (uses 2D engine display path)

  1     CURSOR_MODE                 Hardware cursor format.
                                    0 = Microsoft format
                                    1 = X11 format

  2     OVERLAY_STEREO_EN           Stereo overlay enable (T-buffer related)

  3     INTERLACE_EN                Interlaced display output

  4     HALF_MODE                   Scanline doubling (each line displayed twice).
                                    Used for lower resolutions to fill display.

  5     CHROMA_EN                   Desktop chroma key enable.
                                    When set, desktop pixels matching chroma range are
                                    replaced by overlay pixels.

  6     CHROMA_INVERT               Invert chroma key test (pass when NOT matching)

  7     DESKTOP_EN                  Desktop surface enable.
                                    When clear, desktop surface is not scanned out.
                                    NOTE: Our code does NOT explicitly check this bit --
                                    this may be a bug if the BIOS sets vidProcCfg without
                                    this bit and expects no desktop output.

  8     OVERLAY_EN                  Overlay surface enable

  9     VIDEOIN_AS_OVERLAY          Route video-in as overlay source

 10     DESKTOP_CLUT_BYPASS         Bypass CLUT for desktop (direct RGB passthrough).
                                    Must be set for 16bpp, 24bpp, 32bpp modes.
                                    Must be clear for 8bpp palettized mode.

 11     OVERLAY_CLUT_BYPASS         Bypass CLUT for overlay

 12     DESKTOP_CLUT_SELECT         Select upper or lower 256 CLUT entries for desktop

 13     OVERLAY_CLUT_SELECT         Select upper or lower 256 CLUT entries for overlay

 14     OVERLAY_HORIZ_SCALE_EN      Enable overlay horizontal scaling

 15     OVERLAY_VERT_SCALE_EN       Enable overlay vertical scaling

17:16   OVERLAY_FILTER_MODE         Overlay filter mode:
                                    00 = Point sampling
                                    01 = 2x2 filter
                                    10 = 4x4 filter (not in all models)
                                    11 = Bilinear

20:18   DESKTOP_PIXEL_FORMAT        Desktop surface pixel format:
                                    000 = 8-bit palettized (PAL8)
                                    001 = RGB565 (16-bit)
                                    010 = RGB24 (24-bit packed)
                                    011 = RGB32 (32-bit XRGB8888)
                                    100 = ARGB1555 (unused in most code)

23:21   OVERLAY_PIXEL_FORMAT        Overlay surface pixel format:
                                    000 = RGB1555D (dithered)
                                    001 = RGB565U (undithered)
                                    010 = RGB1555U (undithered)
                                    011 = RGB32U (undithered)
                                    100 = YUV411
                                    101 = YUYV422
                                    110 = UYVY422
                                    111 = RGB565D (dithered)

 24     DESKTOP_TILED_EN            Desktop framebuffer uses tiled memory layout.
                                    When set, scanout uses tile address calculation.
                                    When clear, scanout is linear.

 25     OVERLAY_TILED_EN            Overlay uses tiled memory layout

 26     VIDEO_2X_MODE_EN            Double pixel clock (2x mode).
                                    Used for high resolutions where the pixel clock
                                    exceeds the DAC's 1x limit. Each DAC clock outputs
                                    2 pixels.

 27     CURSOR_EN                   Hardware cursor enable

 29     OVERLAY_EACH_VSYNC          Update overlay address each vsync

 30     OVERLAY_STRIDE_ADJUST       Overlay stride adjustment

 31     OVERLAY_DEINTERLACE_EN      Overlay deinterlace enable
```

### 2.1 Key Interactions

**VIDEO_PROCESSOR_EN (bit 0) vs DESKTOP_EN (bit 7):**
- Bit 0 = master switch. When clear, the chip is in pure VGA mode.
- Bit 7 = desktop surface enable. Can be set while bit 0 is set.
- The BIOS sets bit 0 first, then configures pixel format, then sets bit 7.
- Our emulator does not distinguish these -- both the Banshee and V4 code
  check `VIDPROC_ENABLE` (bit 0) but do not independently check `DESKTOP_EN` (bit 7).
  This is likely fine because the BIOS always sets both together.

**EXTSHIFTOUT (vgaInit0 bit 12) vs VIDPROC_ENABLE (vidProcCfg bit 0):**
- `VIDPROC_ENABLE` tells the chip to use the 2D engine path for display.
- `EXTSHIFTOUT` tells the VGA shift register to use the extended pixel format
  (bypassing standard VGA 4-plane or chain4 addressing).
- For SVGA modes: BOTH must be set. The BIOS sets vidProcCfg.bit0 first, then
  vgaInit0.bit12.
- During VGA text mode: NEITHER is set. Standard VGA logic handles display.
- Our V4 code has a fallback: if `fb_only` (bit 0 set) but `extShiftOut` not set,
  we still override to read from vidDesktopStartAddr. This handles the window
  between the BIOS setting vidProcCfg and setting vgaInit0.

---

## 3. vgaInit0 Register (offset 0x28) -- KEY BITS

```
Bit(s)  Name                    Description
------  ----                    -----------
  0     VGA_DISABLE             Disable VGA core entirely
  1     EXT_TIMING              Enable extended (non-VGA) timing control
  2     CLUT_SELECT (8BIT_DAC)  RAMDAC width: 0 = 6-bit, 1 = 8-bit per channel
  6     VGA_EXTENSIONS          Enable extended VGA register access (CRTC 0x1A, 0x1B)
  8     WAKEUP_SELECT           VGA wakeup port: 0 = 0x46E8, 1 = 0x3C3
  9     LEGACY_DECODE           VGA legacy I/O decode: 0 = enabled, 1 = disabled
 10     CONFIG_READBACK          Alt readback: 0 = enable, 1 = disable
 11     FAST_BLINK              Fast cursor blink
 12     EXTSHIFTOUT             Extended shift-out mode.
                                 When set, the VGA shift register outputs pixels in the
                                 format specified by vidProcCfg.DESKTOP_PIXEL_FORMAT
                                 instead of standard VGA planar or chain4 format.
                                 THIS IS THE KEY BIT for SVGA mode display.
 13     DECODE_3C6              Enable DAC decode at 0x3C6
 22     SGRAM_HBLANK_DISABLE    Disable SGRAM refresh during hblank

23:14   VGA_BASE_ADDR           VGA memory window base address
```

### 3.1 EXTSHIFTOUT Details

When vgaInit0 bit 12 is SET:
1. The VGA shift register is bypassed
2. vidDesktopStartAddr becomes the display start address
3. vidDesktopOverlayStride provides the scanline stride
4. vidProcCfg bits 20:18 determine pixel format
5. The display scanout reads directly from VRAM at the configured address

When vgaInit0 bit 12 is CLEAR:
1. Standard VGA output path is active
2. VGA CRTC start address registers (0x0C/0x0D) determine display start
3. Standard VGA chain4 / planar addressing applies
4. This is the mode used for VGA text and standard VGA graphics modes

---

## 4. Tiled Framebuffer Architecture

### 4.1 Tile Dimensions (from Glide h3defs.h)

```c
#define SST_TILE_WIDTH_BITS   7    // 2^7 = 128 bytes wide
#define SST_TILE_WIDTH        128  // Each tile is 128 bytes wide
#define SST_TILE_HEIGHT_BITS  5    // 2^5 = 32 rows tall
#define SST_TILE_HEIGHT       32   // Each tile is 32 rows tall
#define SST_TILE_SIZE         4096 // 128 * 32 = 4096 bytes per tile
```

A tile is a 128-byte x 32-row block (4 KB). For 16-bit pixels, this is 64 pixels x 32 rows.
For 32-bit pixels, this is 32 pixels x 32 rows.

### 4.2 lfbMemoryConfig Register (offset 0x0C)

This register controls how LFB (Linear Frame Buffer) accesses are remapped to tiled memory.

```
Bit(s)   Name                    Description
------   ----                    -----------
12:0     RAW_LFB_ADDR            Tile region base address >> 12
                                 Physical base = (val & 0x1FFF) << 12
                                 This is the start of the tiled region in VRAM.

15:13    RAW_LFB_ADDR_STRIDE     Tile stride (power of 2):
                                 000 = 1024 bytes
                                 001 = 2048 bytes
                                 010 = 4096 bytes
                                 011 = 8192 bytes
                                 100 = 16384 bytes
                                 The stride is the linear pitch used to convert
                                 a linear address into tile coordinates.

22:16    RAW_LFB_TILE_STRIDE     Number of tiles per row (0-127).
                                 tile_x = val * 128 (bytes)
                                 tile_x_real = val * 128 * 32 (bytes, full tile pitch)

 29      RAW_LFB_READ_CONTROL    LFB read tile control
 30      RAW_LFB_UPDATE_CONTROL  LFB update tile control
 31      RAW_LFB_WRITE_CONTROL   LFB write tile control
```

### 4.3 lfbMemoryTileCtrl Register (NOT IN IO SPACE)

The Glide h3defs.h defines additional tile control fields in a separate register:

```
Bit(s)   Name                     Description
------   ----                     -----------
12:0     TILE_BEGIN_PAGE          Start page of tiled region (combined with bits 24:23)
15:13    ADDR_STRIDE              Same encoding as lfbMemoryConfig bits 15:13
22:16    TILE_STRIDE              Tiles per row
```

This register is NOT mapped in IO space according to the Glide header comment.
The 86Box Banshee implementation does NOT implement this register.
All tile configuration is done through lfbMemoryConfig.

### 4.4 Tile Address Remapping Formula

When an LFB access (read or write) targets an address >= tile_base, the address
is remapped from linear to tiled layout:

```
Input:  linear_addr  (byte address in LFB space)

Step 1: Subtract tile base
    addr = linear_addr - tile_base

Step 2: Decompose into x (within stride) and y (row)
    x = addr & (tile_stride - 1)          // tile_stride is power of 2
    y = addr >> tile_stride_shift          // tile_stride_shift = log2(tile_stride)

Step 3: Compute tiled address
    tiled_addr = tile_base
               + (x & 127)                // byte offset within tile (0-127)
               + ((x >> 7) * 128 * 32)    // tile column * tile size
               + ((y & 31) * 128)         // row within tile * tile width
               + (y >> 5) * tile_x_real   // tile row * row pitch in bytes

Where:
    tile_x_real = tiles_per_row * 128 * 32
```

**In plain English**: The remapping takes a linearly-addressed pixel position
and maps it to the physical VRAM location within the tiled layout. Within each
tile, bytes are stored row-major (128 bytes per row, 32 rows). Tiles themselves
are arranged in a 2D grid.

### 4.5 Display Scanout with Tiling

The tiled render function (banshee_render_16bpp_tiled / v4_render_16bpp_tiled)
reads VRAM using a simplified formula per scanline:

```
For each scanline y:
    if HALF_MODE:
        row_in_tile = (y >> 1) & 31
        tile_row    = y >> 6
    else:
        row_in_tile = y & 31
        tile_row    = y >> 5

    base_addr = desktop_addr + row_in_tile * 128 + tile_row * desktop_stride_tiled

    For each group of 64 pixels (128 bytes = 1 tile width for 16bpp):
        Read 128 bytes from base_addr
        Advance base_addr by 128 * 32 (= next tile column)
```

Where `desktop_stride_tiled = stride_tiles * 128 * 32`.

This avoids the full tile remapping formula by directly computing the position
within the tile grid.

### 4.6 vidDesktopOverlayStride Register (offset 0xE8)

```
Bit(s)   Name              Description
------   ----              -----------
13:0     DESKTOP_STRIDE    Desktop stride.
                           Linear mode: stride in bytes (14-bit, max 16383)
                           Tiled mode: stride in tiles (7-bit effectively,
                                       same field but lower bits used)

30:16    OVERLAY_STRIDE    Overlay stride (same encoding as desktop)
```

**Stride interpretation depends on vidProcCfg tiling bits:**

- If `DESKTOP_TILED_EN` is clear: stride = raw byte count (linear pitch)
  - `svga->rowoffset = (stride & 0x3FFF) >> 3`

- If `DESKTOP_TILED_EN` is set: stride = number of tiles per row
  - `svga->rowoffset = ((stride & 0x3FFF) * 128) >> 3`
  - `desktop_stride_tiled = (stride & 0x3FFF) * 128 * 32`

### 4.7 vidDesktopStartAddr Register (offset 0xE4)

```
Bit(s)   Name              Description
------   ----              -----------
23:0     START_ADDR        Byte address in VRAM where desktop surface begins.
                           This is a physical VRAM address (not a tile address).
                           For linear mode: just the starting byte.
                           For tiled mode: base of the tiled region for scanout.
```

### 4.8 vidScreenSize Register (offset 0x98)

```
Bit(s)   Name              Description
------   ----              -----------
11:0     XRES              Horizontal resolution (pixels).
                           Special: bit 11 is needed for widths >= 2048.
23:12    YRES              Vertical resolution (scanlines).
                           Formula: (yRes << 12) | (xRes & 0xFFF)
```

For HALF_MODE (scanline doubling): `(yRes << 13) | (xRes & 0xFFF)`

---

## 5. Display Scanout Pipeline Flow

### 5.1 VGA Text Mode (BIOS POST)

```
vidProcCfg bit 0 = 0 (video processor disabled)
vgaInit0 bit 12 = 0 (extShiftOut disabled)

Flow:
  VGA CRTC timing --> VGA text renderer
  Start address from CRTC registers 0x0C/0x0D
  Standard VGA text mode (attribute + character)
  Standard VGA palette (6-bit or 8-bit DAC)
```

### 5.2 SVGA Graphics Mode (Windows Desktop)

```
vidProcCfg bit 0 = 1 (video processor enabled)
vidProcCfg bit 7 = 1 (desktop enabled)
vgaInit0 bit 12 = 1 (extShiftOut enabled)

Flow:
  VGA CRTC timing (extended: crtc[0x1A], crtc[0x1B] for 10-bit values)
  -->  vidDesktopStartAddr provides VRAM start
  -->  vidDesktopOverlayStride provides pitch
  -->  vidProcCfg bits 20:18 determine pixel format
  -->  If DESKTOP_TILED_EN: tiled scanout formula
       Else: linear scanout (standard SVGA render)
  -->  vidProcCfg bits 10-13 control CLUT bypass/select
  -->  dacMode controls 1x/2x pixel clock
  -->  vidScreenSize provides resolution
```

### 5.3 Mode Transition Sequence (from Glide h3cinit.c)

The BIOS/driver sets up a new video mode in this order:

1. **Disable video output** (set vgaInit0 bit 12 to stop VGA refresh)
2. **Program VGA CRTC** registers (0x00-0x18 + extended 0x1A, 0x1B)
3. **Program pllCtrl0** with new video clock
4. **Program dacMode** (1x or 2x)
5. **Configure vidProcCfg**: set pixel format, enable flags
6. **Set vidDesktopStartAddr** and **vidDesktopOverlayStride**
7. **Set vidScreenSize**
8. **Enable vgaInit0 bit 12** (enable extended shift out)

### 5.4 Key Differences: Banshee vs VSA-100

The VSA-100 register layout is **identical to the Banshee/Voodoo3** for the display
pipeline. The differences are:

| Feature | Banshee/V3 | VSA-100 |
|---------|-----------|---------|
| PCI Device ID | 0x0005 | 0x0009 |
| Max VRAM | 16 MB | 32 MB |
| vidDesktopStartAddr mask | 24-bit | 24-bit (same) |
| Pixel formats | PAL8, RGB565, RGB24, RGB32 | Same + ARGB1555 |
| Dual pixel pipes | No | Yes (2 pipes, 1 TMU each) |
| T-buffer FSAA | No | Yes |
| 32-bit color rendering | Limited | Full |
| Register offsets | Identical | Identical |
| vidProcCfg bit layout | Identical | Identical |
| Tile dimensions | 128x32 | 128x32 (same) |
| lfbMemoryConfig encoding | Identical | Identical |

**For display pipeline emulation, the VSA-100 is essentially Banshee-compatible.**
The differences are in the 3D rendering pipeline (32-bit framebuffer, dual pipes,
larger textures, T-buffer), not in the display scanout path.

---

## 6. LFB (Linear Frame Buffer) Addressing

### 6.1 BAR Layout

| BAR | Size | Content |
|-----|------|---------|
| BAR0 | 32 MB | MMIO: Init/Video registers + 3D command space |
| BAR1 | 32 MB | Linear framebuffer (LFB) window |
| BAR2 | (optional) | I/O space (for VGA legacy ports) |

### 6.2 LFB Window Mapping

The BAR1 window provides direct byte-addressable access to VRAM.

- Addresses 0x000000 - 0x1FFFFFF map to the 32 MB VRAM
- If tiling is configured (lfbMemoryConfig), accesses to addresses >= tile_base
  are automatically remapped through the tile formula
- If the address is below tile_base, it's a direct linear access

### 6.3 LFB Read/Write Path (from 86Box Banshee code)

```c
// On each LFB access:
addr &= decode_mask;  // Mask to VRAM size

if (addr >= tile_base) {
    // Tile remap
    addr -= tile_base;
    x = addr & (tile_stride - 1);
    y = addr >> tile_stride_shift;
    addr = tile_base
         + (x & 127)
         + ((x >> 7) * 128 * 32)
         + ((y & 31) * 128)
         + (y >> 5) * tile_x_real;
}

// Now addr is the physical VRAM address
vram[addr & vram_mask] = data;
```

### 6.4 Relationship Between LFB Writes and VGA Aperture Writes

- **VGA aperture** (0xA0000-0xBFFFF): Goes through standard VGA logic
  (chain4, planar, banking). Used for VGA text mode and legacy modes.
- **LFB window** (BAR1): Direct VRAM access with optional tile remapping.
  Used by SVGA drivers and the BIOS for high-resolution modes.
- **MMIO writes** (BAR0, 3D command space): Used by the 3D engine/Glide.
  These go through the command FIFO and rendering pipeline.

The VGA aperture and LFB both write to the same physical VRAM. The difference
is the address translation path.

---

## 7. VGA Compatibility

### 7.1 VGA Core

The VSA-100 (like Banshee/V3) has a **real VGA core** -- not emulated in the 2D engine.
It includes:
- Full VGA CRTC, Sequencer, Graphics Controller, Attribute Controller
- Standard VGA memory planes
- Chain4 mode for mode 13h (320x200x256)
- Standard VGA text mode with fonts

### 7.2 Extended VGA Registers

When vgaInit0 bit 6 (VGA_EXTENSIONS) is set, additional CRTC registers are accessible:
- **CRTC 0x1A**: Extended horizontal control (htotal bit 8, hdisp bit 8, hblank bits)
- **CRTC 0x1B**: Extended vertical control (vtotal bit 10, dispend bit 10, vblank bit 10, vsync bit 10)

These allow resolutions above the standard VGA 10-bit limit (max 1023 lines).

### 7.3 VBE (VESA BIOS Extension) Mode Switching

The VSA-100 BIOS provides standard VBE 2.0 services. Mode switching:

1. Software calls INT 10h AX=4F02h BX=mode
2. BIOS programs VGA CRTC with mode timing from built-in mode table
3. BIOS programs pllCtrl0 with appropriate pixel clock
4. BIOS programs dacMode for 1x or 2x pixel rate
5. BIOS sets vidProcCfg with correct pixel format and enables
6. BIOS sets vidDesktopStartAddr and stride
7. BIOS sets vgaInit0 bit 12 to enable extended shift out

The mode table (modetabl.h) contains pre-calculated CRTC register values
and PLL settings for all supported resolutions and refresh rates.

### 7.4 Packed Chain4 vs Banking

For SVGA modes with more than 64 KB visible, the driver uses:
- **Linear framebuffer** (BAR1) for direct access
- **VGA banking** is also available but rarely used by modern drivers
- Chain4 mode is used for VGA mode 13h (320x200)

---

## 8. PLL Programming

### 8.1 pllCtrl0 Register (offset 0x40)

The PLL generates the pixel clock from a reference clock.

```
Bit(s)   Description
------   -----------
1:0      PLL k divider (post-divider)
7:2      PLL m divider (feedback divider)
         (Note: exact field widths vary by chip revision)
```

The pixel clock frequency is: `f_pixel = f_ref * (m + 2) / (2^k)`

The mode table provides pre-calculated (m, k) pairs for each resolution/refresh
combination. The BIOS programs pllCtrl0 as `(rs[19] << 8) | rs[18]` from the
mode table entry.

### 8.2 dacMode Register (offset 0x4C)

Controls the DAC pixel output rate:
- When the pixel clock exceeds the DAC's maximum 1x rate (~170 MHz for Banshee,
  ~183 MHz for V3/VSA-100), 2x mode is used.
- In 2x mode, the DAC outputs 2 pixels per clock cycle.
- vidProcCfg bit 26 (VIDEO_2X_MODE_EN) must also be set.

---

## 9. Potential Issues in vid_voodoo4.c

### 9.1 DESKTOP_EN (bit 7) Not Checked

The Glide source defines SST_DESKTOP_EN as bit 7 and uses it in
h3InitVideoDesktopSurface. Our code never checks this bit. If the BIOS
sets vidProcCfg with VIDPROC_ENABLE but without DESKTOP_EN, we would
still try to display the desktop surface. In practice this likely doesn't
cause issues because the BIOS always sets both.

### 9.2 lfbMemoryTileCtrl Not Implemented

The Glide source defines an lfbMemoryTileCtrl register with separate tile
configuration. The 86Box code only implements lfbMemoryConfig. This appears
to be fine because the h3defs.h comment says this register "doesn't exist in
IO space" -- it may be an internal register not directly programmable.

### 9.3 Tile Sync Fallback May Be Incorrect

The v4_sync_tile_params function auto-configures tile parameters from the
display state when lfbMemoryConfig is 0. This is a workaround for BIOS
init ordering. The function computes tile_base from vidDesktopStartAddr,
which assumes the tiled region starts at the desktop start address. This
may not be correct if the BIOS configures the tiled region elsewhere.

### 9.4 Missing 32-bit Tiled Render

v4_render_16bpp_tiled exists but there's no v4_render_32bpp_tiled.
When vidProcCfg sets DESKTOP_PIXEL_FORMAT to RGB32 (011) AND DESKTOP_TILED_EN,
the code falls through to svga_render_32bpp_highres which doesn't handle tiling.
This would cause garbled output in 32-bit tiled modes.

### 9.5 vidScreenSize Not Used for Display Timing

The vidScreenSize register is stored but never used to validate or override
the VGA CRTC-derived display dimensions. The Glide code programs vidScreenSize
as part of mode setup. If the CRTC timing doesn't match vidScreenSize, the
display could be wrong.

---

## 10. Diagnosing Garbled VESA Display

Based on this research, the garbled graphical display is likely caused by one
or more of:

### Hypothesis 1: extShiftOut Timing
The BIOS may set vidProcCfg before vgaInit0 bit 12. During this window,
the code tries to use the desktop pixel format but the VGA core is still
in standard mode. The V4 code has a fallback for this case (lines 554-583)
but it may not cover all transitions.

### Hypothesis 2: LFB Write Tile Mismatch
If the driver writes pixels through the LFB window with tiling enabled,
but lfbMemoryConfig is not yet configured, the tile remapping formula
uses degenerate parameters (tile_x_real = 0), scattering writes.
The v4_sync_tile_params tries to fix this but may compute wrong parameters.

### Hypothesis 3: Stride Mismatch
The desktop stride value is interpreted differently for linear vs tiled.
If the BIOS programs a tiled stride but our code interprets it as linear
(or vice versa), every scanline would be offset, causing diagonal tearing.

### Hypothesis 4: Desktop Start Address
If vidDesktopStartAddr doesn't match where the driver actually writes
pixel data, the scanout reads from the wrong VRAM region.

### Recommended Debugging Steps

1. **Log the exact BIOS register programming sequence**: vidProcCfg, vgaInit0,
   vidDesktopStartAddr, vidDesktopOverlayStride, lfbMemoryConfig, vidScreenSize
   -- in the order they are written.
2. **Compare LFB write addresses** vs **scanout read addresses**:
   Are they hitting the same VRAM locations?
3. **Check tile parameter consistency**: Does lfbMemoryConfig match the
   display tile configuration?
4. **Test with tiling disabled**: If the BIOS allows linear mode, does it work?

---

## Sources

- 3dfx Glide H5 Source: `glide3x/h5/incsrc/h3defs.h` (vidProcCfg, lfbMemoryConfig,
  tile dimensions, all bit field definitions)
- 3dfx Glide H5 Source: `glide3x/h5/incsrc/h3regs.h` (register structure layout,
  lfbMemoryTileCtrl)
- 3dfx Glide H5 Source: `glide3x/h5/cinit/h3cinit.c` (mode setup, register
  programming sequences)
- 3dfx Glide H5 Source: `glide3x/h5/cinit/modetabl.h` (VGA mode timing tables)
- Linux kernel: `include/video/tdfx.h` (register offset definitions, bit masks)
- Linux kernel: `drivers/video/fbdev/tdfxfb.c` (display pipeline programming)
- 86Box: `src/video/vid_voodoo_banshee.c` (reference Banshee implementation)
- 86Box: `src/video/vid_voodoo4.c` (current V4 implementation)
- Banshee 2D Databook r1.0 (bitsavers.org) [PDF, image-based]
- Banshee Specification r1.1 (bitsavers.org) [PDF, image-based]
