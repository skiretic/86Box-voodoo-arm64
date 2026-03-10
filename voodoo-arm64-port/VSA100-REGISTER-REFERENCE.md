# VSA-100 / Voodoo 4/5 Register Programming Reference

Compiled from open-source Linux tdfxfb driver, X.org xf86-video-tdfx driver,
Glide3x H5 (Napalm) source code, and 86Box vid_voodoo_banshee.c.

## Sources

- **Linux kernel**: `drivers/video/fbdev/tdfxfb.c` + `include/video/tdfx.h` (v6.2)
- **X.org**: `xf86-video-tdfx/src/tdfxdefs.h`
- **Glide3x H5**: `sezero/glide` — `glide3x/h5/incsrc/h3defs.h`, `minihwc/linhwc.c`, `minihwc/minihwc.c`
- **86Box**: `src/video/vid_voodoo_banshee.c`

---

## 1. PCI Identification

| Card | Vendor ID | Device ID | Source |
|------|-----------|-----------|--------|
| Voodoo Graphics | 0x121A | 0x0001 | xf86-video-tdfx |
| Voodoo 2 | 0x121A | 0x0002 | xf86-video-tdfx |
| Banshee | 0x121A | 0x0003 | xf86-video-tdfx |
| Velocity (Banshee variant) | 0x121A | 0x0004 | xf86-video-tdfx |
| Voodoo 3 | 0x121A | 0x0005 | xf86-video-tdfx |
| Voodoo 4 | 0x121A | 0x0007 | xf86-video-tdfx |
| **Voodoo 4 4500 / Voodoo 5** | **0x121A** | **0x0009** | xf86-video-tdfx |

---

## 2. I/O Register Map (Init Space, memBaseAddr0 offset 0x000)

All offsets are relative to the I/O register base (BAR0 + 0x000000 for init registers).

| Register | Offset | Description | Source |
|----------|--------|-------------|--------|
| STATUS | 0x00 | Chip status, FIFO depth, busy flags | tdfx.h |
| PCIINIT0 | 0x04 | PCI stall/threshold/interrupt control | tdfx.h |
| SIPMONITOR | 0x08 | Silicon performance monitor | tdfx.h |
| LFBMEMORYCONFIG | 0x0C | LFB memory configuration (tile config) | tdfx.h |
| MISCINIT0 | 0x10 | Misc init 0 (Y-origin swap, byte swizzle, resets) | tdfx.h |
| MISCINIT1 | 0x14 | Misc init 1 (CLUT invert, 2D block disable) | tdfx.h |
| DRAMINIT0 | 0x18 | DRAM timing, chip count, SGRAM type | tdfx.h |
| DRAMINIT1 | 0x1C | DRAM refresh, triple buffer, SDRAM flag | tdfx.h |
| AGPINIT | 0x20 | AGP configuration | tdfx.h |
| TMUGBEINIT | 0x24 | TMU/GBE initialization | tdfx.h |
| VGAINIT0 | 0x28 | VGA init 0 (8-bit DAC, ext enable, ext shift out) | tdfx.h |
| VGAINIT1 | 0x2C | VGA init 1 (lower 21 bits preserved) | tdfx.h |
| DRAMCOMMAND | 0x30 | DRAM command register | tdfx.h |
| DRAMDATA | 0x34 | DRAM data register | tdfx.h |
| PLLCTRL0 | 0x40 | Video PLL control | tdfx.h |
| PLLCTRL1 | 0x44 | Graphics PLL control | tdfx.h |
| PLLCTRL2 | 0x48 | Memory PLL control | tdfx.h |
| DACMODE | 0x4C | DAC mode (2x, DPMS, sync control) | tdfx.h |
| DACADDR | 0x50 | DAC palette address | tdfx.h |
| DACDATA | 0x54 | DAC palette data | tdfx.h |
| RGBMAXDELTA | 0x58 | RGB max delta | tdfx.h |
| **VIDPROCCFG** | **0x5C** | **Video processor config (pixel format, desktop enable, tiling)** | tdfx.h |
| HWCURPATADDR | 0x60 | HW cursor pattern address | tdfx.h |
| HWCURLOC | 0x64 | HW cursor location | tdfx.h |
| HWCURC0 | 0x68 | HW cursor color 0 | tdfx.h |
| HWCURC1 | 0x6C | HW cursor color 1 | tdfx.h |
| VIDINFORMAT | 0x70 | Video input format | tdfx.h |
| VIDINSTATUS | 0x74 | Video input status | tdfx.h |
| VIDSERPARPORT | 0x78 | Video serial/parallel port (DDC/I2C) | tdfx.h |
| VIDINXDELTA | 0x7C | Video input X delta | tdfx.h |
| VIDININITERR | 0x80 | Video input Y init error | tdfx.h |
| VIDINYDELTA | 0x84 | Video input Y delta | tdfx.h |
| VIDPIXBUFTHOLD | 0x88 | Video pixel buffer threshold | tdfx.h |
| VIDCHRMIN | 0x8C | Video chroma key minimum | tdfx.h |
| VIDCHRMAX | 0x90 | Video chroma key maximum | tdfx.h |
| VIDCURLIN | 0x94 | Video current line | tdfx.h |
| **VIDSCREENSIZE** | **0x98** | **Screen dimensions (width + height)** | tdfx.h |
| VIDOVRSTARTCRD | 0x9C | Overlay start coordinates | tdfx.h |
| VIDOVRENDCRD | 0xA0 | Overlay end coordinates | tdfx.h |
| VIDOVRDUDX | 0xA4 | Overlay dU/dX | tdfx.h |
| VIDOVRDUDXOFF | 0xA8 | Overlay dU/dX offset + src width | tdfx.h |
| VIDOVRDVDY | 0xAC | Overlay dV/dY | tdfx.h |
| VIDOVRDVDYOFF | 0xE0 | Overlay dV/dY offset | tdfx.h |
| **VIDDESKSTART** | **0xE4** | **Desktop start address** | tdfx.h |
| **VIDDESKSTRIDE** | **0xE8** | **Desktop + overlay stride** | tdfx.h |
| VIDINADDR0 | 0xEC | Video input address 0 | tdfx.h |
| VIDINADDR1 | 0xF0 | Video input address 1 | tdfx.h |
| VIDINADDR2 | 0xF4 | Video input address 2 | tdfx.h |
| VIDINSTRIDE | 0xF8 | Video input stride | tdfx.h |
| VIDCUROVRSTART | 0xFC | Video current overlay start | tdfx.h |

---

## 3. vidProcCfg (0x5C) — Video Processor Configuration

This is the MOST CRITICAL register for display output.

### Bit Field Layout

| Bit(s) | Name | Description | Source |
|--------|------|-------------|--------|
| 0 | VIDEO_PROCESSOR_EN | Enable video processor output | h3defs.h, tdfx.h |
| 1-2 | CURSOR_MODE | HW cursor mode (bit 1 = X11 mode) | h3defs.h, tdfx.h |
| 2 | OVERLAY_STEREO_EN | Enable stereo overlay | h3defs.h |
| 3 | INTERLACE | Enable interlaced mode | h3defs.h, tdfx.h |
| 4 | HALF_MODE | Half scanline mode (double-height) | h3defs.h, tdfx.h |
| 5 | CHROMA_EN | Enable chroma key | h3defs.h |
| 6 | CHROMA_INVERT | Invert chroma key | h3defs.h |
| **7** | **DESKTOP_EN** | **Enable desktop surface display** | h3defs.h, tdfx.h |
| 8 | OVERLAY_EN | Enable overlay surface display | h3defs.h |
| 9 | VIDEOIN_AS_OVERLAY | Route video input to overlay path | h3defs.h |
| **10** | **DESKTOP_CLUT_BYPASS** | **Bypass CLUT for desktop (true color)** | h3defs.h, tdfx.h |
| 11 | OVERLAY_CLUT_BYPASS | Bypass CLUT for overlay | h3defs.h |
| 12 | DESKTOP_CLUT_SELECT | Desktop CLUT bank select | h3defs.h |
| 13 | OVERLAY_CLUT_SELECT | Overlay CLUT bank select | h3defs.h |
| 14 | OVERLAY_HORIZ_SCALE_EN | Enable overlay horizontal scaling | h3defs.h |
| 15 | OVERLAY_VERT_SCALE_EN | Enable overlay vertical scaling | h3defs.h |
| 16-17 | OVERLAY_FILTER_MODE | Overlay filter: 0=point, 1=2x2 dither, 2=4x4 dither, 3=bilinear | h3defs.h |
| **18-20** | **DESKTOP_PIXEL_FORMAT** | **Desktop pixel format (see table below)** | h3defs.h, tdfx.h |
| 21-23 | OVERLAY_PIXEL_FORMAT | Overlay pixel format (see table below) | h3defs.h |
| **24** | **DESKTOP_TILED_EN** | **Desktop uses tiled memory addressing** | h3defs.h |
| 25 | OVERLAY_TILED_EN | Overlay uses tiled memory addressing | h3defs.h |
| **26** | **VIDEO_2X_MODE_EN** | **2X pixel clock mode (for high resolutions)** | h3defs.h, tdfx.h |
| **27** | **CURSOR_EN** | **Enable hardware cursor** | h3defs.h, tdfx.h |
| 29 | OVERLAY_EACH_VSYNC | Update overlay address each vsync | h3defs.h |
| 30 | OVERLAY_STRIDE_ADJUST | Overlay stride adjustment | h3defs.h |
| 31 | OVERLAY_DEINTERLACE_EN | Enable overlay deinterlacing | h3defs.h |

### Desktop Pixel Format Values (bits 18-20)

| Value | Format | BPP | Linux Encoding | Source |
|-------|--------|-----|----------------|--------|
| 0 | Palettized 8-bit | 8 | (cpp-1)<<18 = 0<<18 | tdfxfb.c, h3defs.h |
| 1 | RGB565 | 16 | (cpp-1)<<18 = 1<<18 | tdfxfb.c, h3defs.h |
| 2 | RGB24 (packed) | 24 | (cpp-1)<<18 = 2<<18 | tdfxfb.c, h3defs.h |
| 3 | ARGB8888 / RGB32 | 32 | (cpp-1)<<18 = 3<<18 | tdfxfb.c, h3defs.h |
| 4 | RGB1555 (unpacked) | 16 | | h3defs.h only |

**CRITICAL**: The Linux tdfxfb driver computes the pixel format field as:
```c
((cpp - 1) << VIDCFG_PIXFMT_SHIFT)   // where cpp = bytes_per_pixel
```
This means: cpp=1 -> format 0 (8bpp), cpp=2 -> format 1 (RGB565), cpp=3 -> format 2 (RGB24), cpp=4 -> format 3 (ARGB8888).

### Overlay Pixel Format Values (bits 21-23)

| Value | Format | Source |
|-------|--------|--------|
| 0 | RGB1555 (dithered) | h3defs.h |
| 1 | RGB565 (undithered) | h3defs.h |
| 2 | RGB1555 (undithered) | h3defs.h |
| 3 | ARGB8888 (undithered) | h3defs.h |
| 4 | YUV411 | h3defs.h |
| 5 | YUYV422 | h3defs.h |
| 6 | UYVY422 | h3defs.h |
| 7 | RGB565 (dithered) | h3defs.h |

### Standard vidProcCfg Value for Desktop Mode

From the Linux tdfxfb driver `tdfxfb_set_par`:
```c
reg.vidcfg = VIDCFG_VIDPROC_ENABLE    // bit 0
           | VIDCFG_DESK_ENABLE       // bit 7
           | VIDCFG_CURS_X11          // bit 1
           | ((cpp - 1) << VIDCFG_PIXFMT_SHIFT)  // bits 18-20
           | (cpp != 1 ? VIDCFG_CLUT_BYPASS : 0); // bit 10 for >8bpp
```

For 32-bit desktop: `vidProcCfg = 0x000C0483` (enable + desktop + X11 cursor + 32bpp + CLUT bypass)

---

## 4. vgaInit0 (0x28) — VGA Initialization Register 0

| Bit | Name | Value | Description | Source |
|-----|------|-------|-------------|--------|
| 0 | VGA_DISABLE | BIT(0) | Disable VGA core | h3defs.h |
| 1 | EXT_TIMING | BIT(1) | Use extended timing registers | tdfx.h |
| **2** | **8BIT_DAC** | **BIT(2)** | **8-bit RAMDAC (vs 6-bit)** | tdfx.h, h3defs.h |
| **6** | **EXT_ENABLE** | **BIT(6)** | **Enable extended register access** | tdfx.h |
| 8 | WAKEUP_3C3 | BIT(8) | VGA wakeup via port 3C3 | tdfx.h |
| 9 | LEGACY_DISABLE | BIT(9) | Disable VGA legacy decode | tdfx.h |
| 10 | ALT_READBACK | BIT(10) | Alternate readback mode | tdfx.h |
| 11 | FAST_BLINK | BIT(11) | Fast cursor blink | tdfx.h |
| **12** | **EXTSHIFTOUT** | **BIT(12)** | **Extended shift-out mode (use vidProcCfg pixel format)** | tdfx.h, 86Box |
| 13 | DECODE_3C6 | BIT(13) | Decode port 3C6 | tdfx.h |
| 22 | SGRAM_HBLANK_DISABLE | BIT(22) | Disable SGRAM during horizontal blank | tdfx.h |

**CRITICAL**: `VGAINIT0_EXTSHIFTOUT` (bit 12) is the switch between standard VGA output and
accelerated desktop mode. When set, the hardware uses `vidProcCfg` pixel format instead of
standard VGA shift register. The 86Box Banshee code checks `vgaInit0 & 0x40` (bit 6, EXT_ENABLE)
AND `VGAINIT0_EXTENDED_SHIFT_OUT` (bit 12) to determine desktop rendering mode.

Standard value for desktop mode:
```c
reg.vgainit0 = VGAINIT0_8BIT_DAC       // bit 2
             | VGAINIT0_EXT_ENABLE      // bit 6
             | VGAINIT0_WAKEUP_3C3      // bit 8
             | VGAINIT0_ALT_READBACK    // bit 10
             | VGAINIT0_EXTSHIFTOUT;    // bit 12
// = 0x1544
```

### vgaInit1 (0x2C)

Lower 21 bits are preserved on write (mask = 0x001FFFFF). Rest is read-only.

---

## 5. dacMode (0x4C) — DAC Mode Register

| Bit | Name | Value | Description | Source |
|-----|------|-------|-------------|--------|
| **0** | **DAC_MODE_2X** | **BIT(0)** | **2X pixel clock (for high-res modes)** | h3defs.h, tdfx.h |
| 1 | DPMS_ON_VSYNC | BIT(1) | DPMS: control VSYNC output | h3defs.h |
| 2 | FORCE_VSYNC | BIT(2) | Force VSYNC active | h3defs.h |
| 3 | DPMS_ON_HSYNC | BIT(3) | DPMS: control HSYNC output | h3defs.h |
| 4 | FORCE_HSYNC | BIT(4) | Force HSYNC active | h3defs.h |

**For high-res modes** (pixel clock > max_pixclock/2):
```c
reg.dacmode |= DACMODE_2X;  // bit 0
reg.vidcfg  |= VIDCFG_2X;   // vidProcCfg bit 26
// Also halve all horizontal timing values
```

---

## 6. vidDesktopOverlayStride (0xE8) — Combined Stride Register

This register packs BOTH the desktop stride and the overlay stride.

### Bit Field Layout

| Bits | Field | Tiled | Linear | Source |
|------|-------|-------|--------|--------|
| 0-13 | Desktop stride | Stride in tiles (7-bit max, 0x7F) | Stride in bytes (14-bit, 0x3FFF) | h3defs.h |
| 15 | Desktop memory type | 1 = tiled | 0 = linear | h3defs.h (SST_BUFFER_MEMORY_TILED) |
| 16-29 | Overlay stride | Stride in tiles (7-bit max) | Stride in bytes (14-bit) | h3defs.h |

### Linear Mode

The Linux tdfxfb driver writes the stride directly in bytes:
```c
reg.stride = info->var.xres * cpp;  // e.g. 1024*4 = 4096
tdfx_outl(par, VIDDESKSTRIDE, reg.stride);
```

The 86Box code reads it back:
```c
// Linear: stride in bytes
svga->rowoffset = (banshee->vidDesktopOverlayStride & 0x3fff) >> 3;
// (divide by 8 because rowoffset is in QWORDs)
```

### Tiled Mode

The 86Box code for tiled stride:
```c
// Tiled: stride is tile count, multiply by 128 for bytes
svga->rowoffset = ((banshee->vidDesktopOverlayStride & 0x3fff) * 128) >> 3;
// desktop_stride_tiled = tile_count * 128_bytes_per_tile_row * 32_rows_per_tile
banshee->desktop_stride_tiled = (banshee->vidDesktopOverlayStride & 0x3fff) * 128 * 32;
```

---

## 7. vidDesktopStartAddr (0xE4) — Desktop Frame Start Address

| Bits | Field | Source |
|------|-------|--------|
| 0-25 | Start address | h3defs.h (SST_VIDEO_START_ADDR: 0x3FFFFFF mask) |

The Linux driver computes:
```c
reg.startaddr = yoffset * stride + xoffset * cpp;
tdfx_outl(par, VIDDESKSTART, reg.startaddr);
```

The 86Box code reads:
```c
banshee->vidDesktopStartAddr = val & 0xffffff;  // 24-bit mask
svga->memaddr_latch = banshee->vidDesktopStartAddr >> 2;  // convert to DWORDs
```

**Note**: h3defs.h defines a 26-bit mask (0x3FFFFFF) but 86Box currently uses 24-bit (0xFFFFFF).
The VSA-100 with 32MB VRAM needs at least 25 bits. This may need to be widened for Voodoo 4.

---

## 8. vidScreenSize (0x98) — Screen Dimensions

| Bits | Field | Source |
|------|-------|--------|
| 0-11 | Width (pixels) | h3defs.h (SST_VIDEO_SCREEN_WIDTH: 0xFFF mask) |
| 12-23 | Height (pixels) — normal mode | h3defs.h (SST_VIDEO_SCREEN_HEIGHT: 0xFFF mask, shift 12) |
| 13-24 | Height (pixels) — half mode | tdfxfb.c uses shift 13 for HALF_MODE |

From the Linux driver:
```c
// Normal mode:
reg.screensize = info->var.xres | (info->var.yres << 12);

// Half mode (double scan):
reg.screensize = info->var.xres | (info->var.yres << 13);
reg.vidcfg |= VIDCFG_HALF_MODE;
```

---

## 9. Tiled Memory Addressing

### Tile Dimensions

From h3defs.h:
```c
SST_TILE_WIDTH_BITS  = 7    // 2^7 = 128 bytes wide
SST_TILE_HEIGHT_BITS = 5    // 2^5 = 32 rows tall
SST_TILE_SIZE        = 128 * 32 = 4096 bytes per tile
```

A tile is 128 bytes wide and 32 scanlines tall = 4 KB per tile.
- At 16bpp: a tile covers 64 pixels wide x 32 pixels tall
- At 32bpp: a tile covers 32 pixels wide x 32 pixels tall

### Stride Computation (from Glide linhwc.c)

```c
static FxU32 calcBufferStride(hwcBoardInfo *bInfo, FxU32 xres, FxBool tiled) {
    FxU32 shift = (bInfo->h3pixelSize >> 1);  // 0 for 16bpp, 1 for 32bpp

    if (tiled) {
        FxU32 strideInTiles = (xres << shift) >> 7;      // bytes / 128
        if ((xres << shift) & (HWC_TILE_WIDTH - 1))      // round up
            strideInTiles++;
        return (strideInTiles * HWC_TILE_WIDTH);          // back to bytes
    } else {
        return (xres << shift);                           // just bytes
    }
}
```

Where `h3pixelSize` = 2 for 16bpp, 4 for 32bpp. The shift is `h3pixelSize >> 1` = 1 for 16bpp, 2 for 32bpp.

### Height in Tiles

```c
static FxU32 calcBufferHeightInTiles(hwcBoardInfo *bInfo, FxU32 yres) {
    yres = yres >> (bInfo->h3nwaySli >> 1);  // SLI adjustment
    FxU32 heightInTiles = yres >> 5;          // divide by 32
    if (yres & (HWC_TILE_HEIGHT - 1))         // round up
        heightInTiles++;
    return heightInTiles;
}
```

### Size in Tiles

```c
static FxU32 calcBufferSizeInTiles(hwcBoardInfo *bInfo, FxU32 xres, FxU32 yres) {
    return calcBufferHeightInTiles(bInfo, yres) *
           (calcBufferStride(bInfo, xres, FXTRUE) >> 7);
}
```

### Tiled Address Computation (86Box banshee_render_16bpp_tiled)

For scanout of tiled desktop at 16bpp:
```c
// Normal mode:
addr = desktop_addr
     + (desktop_y & 31) * 128              // row within tile (0-31) * 128 bytes
     + (desktop_y >> 5) * desktop_stride_tiled;  // tile row * full stride

// Half mode:
addr = desktop_addr
     + ((desktop_y >> 1) & 31) * 128
     + ((desktop_y >> 6) * desktop_stride_tiled);

// Where desktop_stride_tiled = tile_count * 128 * 32
// (tile_count from vidDesktopOverlayStride bits 0-13)
```

Within a tile row, pixels advance by 128 bytes for each tile column:
```c
for (x = 0; x < hdisp; x += 64) {  // 64 pixels = 128 bytes at 16bpp
    // render 64 pixels from vram[addr]
    addr += 128 * 32;  // skip to next tile column (128 bytes * 32 rows)
}
```

### Tile Address Formula (Generic)

Given a linear (x, y) pixel coordinate with stride S bytes:
```
tile_x = (x * bpp/8) / 128          // which tile column
tile_y = y / 32                       // which tile row
intra_x = (x * bpp/8) % 128         // byte offset within tile row
intra_y = y % 32                      // scanline within tile

tiled_addr = base
           + (tile_y * stride_in_tiles + tile_x) * 4096  // tile offset
           + intra_y * 128                                 // row within tile
           + intra_x                                       // byte within row
```

Or equivalently (from Glide hwcBufferLfbAddr):
```
tileNumber = (physAddress - primaryOffset) >> 12   // 4096 bytes per tile
tileRow = tileNumber / strideInTiles
tileCol = tileNumber % strideInTiles
tileScanline = (physAddress >> 7) & 31             // bits 7-11
tileXOffset  = physAddress & 127                   // bits 0-6

lfbYOffset = (tileRow * 32 + tileScanline) << sliShift
lfbAddr = primaryOffset + lfbYOffset * lfbBufferStride + tileCol * 128 + tileXOffset
```

---

## 10. DRAM Initialization (DRAMINIT0/DRAMINIT1)

### DRAMINIT0 (0x18)

| Bits | Field | Source |
|------|-------|--------|
| 0-1 | SGRAM_RRD | RAS-to-RAS delay | h3defs.h |
| 2-3 | SGRAM_RCD | RAS-to-CAS delay | h3defs.h |
| 4-5 | SGRAM_RP | RAS precharge | h3defs.h |
| 6-9 | SGRAM_RAS | RAS active time | h3defs.h |
| 10-13 | SGRAM_RC | Row cycle time | h3defs.h |
| 14-15 | SGRAM_CAS | CAS latency | h3defs.h |
| 16 | SGRAM_MRS | Mode register set | h3defs.h |
| **26** | **SGRAM_NUM** | **0=4 chips, 1=8 chips** | tdfx.h |
| **27-29** | **SGRAM_TYPE** | **Memory chip size encoding** | tdfx.h, h3defs.h |

#### SGRAM_TYPE values (bits 27-29) for Voodoo 4/5:
```c
0 = 8 Mbit  (1 MB per chip)
1 = 16 Mbit (2 MB per chip)
2 = 32 Mbit (4 MB per chip)
3 = 64 Mbit (8 MB per chip)
4 = 128 Mbit (16 MB per chip)
```

### DRAMINIT1 (0x1C)

| Bit | Field | Source |
|-----|-------|--------|
| 0 | DRAM_REFRESH_EN | Enable DRAM refresh | h3defs.h |
| 1-9 | DRAM_REFRESH_VALUE | Refresh timer value | h3defs.h |
| 10 | VIDEO_OVERRIDE_EN | Video override enable | h3defs.h |
| 11 | TRIPLE_BUFFER_EN | Triple buffering enable | h3defs.h |
| **30** | **MCTL_TYPE_SDRAM** | **1=SDRAM, 0=SGRAM** | h3defs.h, tdfx.h |

### VRAM Size Calculation (from Linux tdfxfb do_lfb_size)

```c
// For Voodoo 4/5 (dev_id >= PCI_DEVICE_ID_3DFX_VOODOO5):
int num_chips = (draminit0 & DRAMINIT0_SGRAM_NUM) ? 8 : 4;  // bit 26
int type_field = (draminit0 & DRAMINIT0_SGRAM_TYPE_MASK) >> DRAMINIT0_SGRAM_TYPE_SHIFT;
int chip_size_mb = 1 << type_field;  // 2^type MB per chip
unsigned long total = num_chips * chip_size_mb * 1024 * 1024;

// For Voodoo 4 4500: 4 chips * 8MB = 32MB typically
// For Voodoo 5 5500: 8 chips * 8MB = 64MB (32MB per VSA-100)
```

---

## 11. MISCINIT0 (0x10) — Misc Initialization 0

| Bit(s) | Name | Description | Source |
|--------|------|-------------|--------|
| 0 | GRX_RESET | Graphics engine reset | h3defs.h |
| 1 | FBI_FIFO_RESET | FIFO reset | h3defs.h |
| 2 | REGISTER_BYTE_SWIZZLE_EN | Byte swizzle on register access | h3defs.h |
| 3 | REGISTER_WORD_SWIZZLE_EN | Word swizzle on register access | h3defs.h |
| 4 | VIDEO_RESET | Video engine reset | h3defs.h |
| 5 | 2D_RESET | 2D engine reset | h3defs.h |
| **18-29** | **Y_ORIGIN_TOP** | **Y-origin swap value (12-bit)** | h3defs.h, 86Box |
| 30 | RAWLFB_BYTE_SWIZZLE_EN | Raw LFB byte swizzle | h3defs.h |
| 31 | RAWLFB_WORD_SWIZZLE_EN | Raw LFB word swizzle | h3defs.h |

---

## 12. MISCINIT1 (0x14) — Misc Initialization 1

| Bit | Name | Description | Source |
|-----|------|-------------|--------|
| 0 | CLUT_INV | Invert CLUT address | tdfx.h |
| 1-2 | TRI_MODE | Triangle mode | h3defs.h |
| 3 | WRITE_SUBSYSTEM | Write subsystem control | h3defs.h |
| 6 | DISABLE_TEXTURE | Disable texture mapping | h3defs.h |
| 7 | POWERDOWN_CLUT | Power down CLUT | h3defs.h |
| 8 | POWERDOWN_DAC | Power down DAC | h3defs.h |
| **15** | **2DBLOCK_DIS** | **Disable 2D block writes (needed for SDRAM)** | tdfx.h |
| 26 | PCI_66_MHZ | PCI 66 MHz capable | h3defs.h |
| 27 | PCI_AGP_ENABLED | AGP mode enabled | h3defs.h |

---

## 13. lfbMemoryConfig (0x0C) — LFB Memory Configuration

### LFB Mode Fields (3D rendering LFB, NOT desktop framebuffer)

| Bits | Field | Source |
|------|-------|--------|
| 0-3 | LFB_FORMAT | Pixel format for LFB writes | h3defs.h |
| 6-7 | LFB_READBUFSELECT | Read buffer select | h3defs.h |
| 8 | LFB_ENPIXPIPE | Enable pixel pipeline for LFB | h3defs.h |
| 9-10 | LFB_RGBALANES | RGBA lane routing | h3defs.h |
| 11 | LFB_WRITE_SWAP16 | Swap 16-bit words on write | h3defs.h |
| 12 | LFB_WRITE_BYTESWAP | Byte swap on write | h3defs.h |
| 13 | LFB_YORIGIN | Y-origin flip | h3defs.h |
| 14 | LFB_WSELECT | W buffer select | h3defs.h |
| 15 | LFB_READ_SWAP16 | Swap 16-bit words on read | h3defs.h |
| 16 | LFB_READ_BYTESWAP | Byte swap on read | h3defs.h |

### Raw LFB Control (for direct framebuffer access)

| Bits | Field | Source |
|------|-------|--------|
| 0-26 | RAW_LFB_ADDR | Raw LFB base address (27-bit) | h3defs.h |
| 29 | RAW_LFB_READ_CONTROL | Enable raw LFB reads | h3defs.h |
| 30 | RAW_LFB_UPDATE_CONTROL | Raw LFB update mode | h3defs.h |
| 31 | RAW_LFB_WRITE_CONTROL | Enable raw LFB writes | h3defs.h |

### LFB Format Values

| Value | Format | Source |
|-------|--------|--------|
| 0 | RGB565 | h3defs.h |
| 1 | RGB555 | h3defs.h |
| 2 | ARGB1555 | h3defs.h |
| 4 | RGB888 | h3defs.h |
| 5 | ARGB8888 | h3defs.h |
| 8 | Z32 | h3defs.h |
| 12 | Z+RGB565 | h3defs.h |
| 13 | Z+RGB555 | h3defs.h |
| 14 | Z+ARGB1555 | h3defs.h |
| 15 | Z+Z | h3defs.h |

### LFB Address Decoding

```c
SST_LFB_ADDR_STRIDE = 2048   // bytes between scanlines in LFB address space
x = (addr >> 0) & 0x7FF      // bits 0-10: X coordinate
y = (addr >> 11) & 0x7FF     // bits 11-21: Y coordinate
```

---

## 14. STATUS Register (0x00)

| Bits | Field | Source |
|------|-------|--------|
| 0-4 | FIFO free slots | 5 LSBs show available FIFO entries | tdfxfb.c |
| 6 | RETRACE | Vertical retrace active | tdfxdefs.h |
| 7 | FBI_BUSY | FBI engine busy | tdfxdefs.h |
| 9 | BUSY | Chip busy (overall) | tdfxdefs.h |

---

## 15. PCI Configuration Registers (Napalm-Specific)

From tdfxdefs.h, the VSA-100 has extended PCI config registers:

| Offset | Name | Description | Source |
|--------|------|-------------|--------|
| 0x04 | CFG_PCI_COMMAND | PCI command register | tdfxdefs.h |
| 0x10 | CFG_MEM0BASE | BAR0: register space (32MB) | tdfxdefs.h |
| 0x14 | CFG_MEM1BASE | BAR1: LFB space (32MB) | tdfxdefs.h |
| 0x40 | CFG_INIT_ENABLE | Init enable (snoop, swap, LFB cache) | tdfxdefs.h |
| 0x48 | CFG_PCI_DECODE | PCI decode control | tdfxdefs.h |
| **0x80** | **CFG_VIDEO_CTRL0** | **Enhanced video + SLI control** | tdfxdefs.h |
| **0x84** | **CFG_VIDEO_CTRL1** | **SLI render/compare masks (fetch + CRT)** | tdfxdefs.h |
| **0x88** | **CFG_VIDEO_CTRL2** | **SLI render/compare masks (AA FIFO)** | tdfxdefs.h |
| **0x8C** | **CFG_SLI_LFB_CTRL** | **SLI LFB render/scan masks** | tdfxdefs.h |
| **0x90** | **CFG_AA_ZBUFF_APERTURE** | **AA depth buffer aperture** | tdfxdefs.h |
| **0x94** | **CFG_AA_LFB_CTRL** | **AA LFB control + base addr** | tdfxdefs.h |
| **0xAC** | **CFG_SLI_AA_MISC** | **SLI/AA misc control** | tdfxdefs.h |

### CFG_VIDEO_CTRL0 Key Bits

| Bit(s) | Name | Description |
|--------|------|-------------|
| 0 | ENHANCED_VIDEO_EN | Enable enhanced video mode |
| 1 | ENHANCED_VIDEO_SLV | This chip is a slave |
| 2 | VIDEO_TV_OUTPUT_EN | TV output enable |
| 11 | VIDPLL_SEL | Video PLL select |
| 12-14 | DIVIDE_VIDEO | Video clock divider (by 1/2/4/8/16/32) |

### CFG_INIT_ENABLE Key Bits

| Bit(s) | Name | Description |
|--------|------|-------------|
| 10 | UPDATE_MEMBASE_LSBS | Allow MEMBASE LSB updates |
| 25 | CFG_SWAP_ALGORITHM | Swap buffer algorithm |
| 26 | CFG_SWAP_MASTER | This chip is swap master |
| 28 | CFG_MULTI_FUNCTION_DEV | Multi-function PCI device |

---

## 16. Display Mode Setup Sequence

From the Linux tdfxfb driver, the complete mode setting sequence is:

### Step 1: Disable video processor
```c
tdfx_outl(par, VIDPROCCFG, reg->vidcfg & ~0x00000001);  // clear bit 0
```

### Step 2: Program PLL
```c
tdfx_outl(par, PLLCTRL0, reg->vidpll);
```

### Step 3: Program VGA timing registers
```c
// Standard CRTC registers (0x00-0x18)
// Extended CRTC registers (0x1a, 0x1b for 10-bit overflow)
```

### Step 4: Program display configuration
```c
tdfx_outl(par, VGAINIT0, reg->vgainit0);     // 0x28: ext shift out, 8-bit DAC
tdfx_outl(par, DACMODE, reg->dacmode);        // 0x4C: 2x mode if needed
tdfx_outl(par, VIDDESKSTRIDE, reg->stride);   // 0xE8: stride in bytes
tdfx_outl(par, HWCURPATADDR, reg->curspataddr); // 0x60: cursor pattern
tdfx_outl(par, VIDSCREENSIZE, reg->screensize); // 0x98: width | (height<<12)
tdfx_outl(par, VIDDESKSTART, reg->startaddr); // 0xE4: frame start address
```

### Step 5: Enable video processor
```c
tdfx_outl(par, VIDPROCCFG, reg->vidcfg);     // 0x5C: full config with enable
```

### Step 6: Finalize
```c
tdfx_outl(par, VGAINIT1, reg->vgainit1);     // 0x2C: preserve lower 21 bits
tdfx_outl(par, MISCINIT0, reg->miscinit0);    // 0x10: Y-origin, byte swizzle
```

---

## 17. Maximum Pixel Clocks

| Card | Max Clock | Source |
|------|-----------|--------|
| Banshee | 270 MHz | tdfxfb.c |
| Voodoo 3 | 300 MHz | tdfxfb.c |
| Voodoo 4/5 | 350 MHz | tdfxfb.c |

---

## 18. Differences Between Banshee/V3 and VSA-100

Based on analysis of the Linux driver, X.org driver, and Glide source:

1. **PCI Device ID**: Banshee=0x0003, V3=0x0005, V4=0x0007, V4-4500/V5=0x0009
2. **VRAM size computation**: Different formula for V4/5 (shift-based chip_size)
3. **Memory type**: V4/5 always uses SDRAM (not SGRAM)
4. **Extended PCI config**: V4/5 adds CFG_VIDEO_CTRL0/1/2, CFG_SLI_LFB_CTRL, CFG_AA_* for SLI and FSAA
5. **Higher pixel clock**: 350 MHz vs 270/300 MHz
6. **32-bit color**: V4/5 natively supports ARGB8888 desktop (format value 3)
7. **SLI support**: V5 5500 uses dual VSA-100 chips with scan-line interleave
8. **T-buffer FSAA**: Handled through AA aperture and LFB control registers
9. **vidDesktopStartAddr**: May need 26-bit address for 32MB VRAM (vs 24-bit for Banshee/V3)

---

## 19. 2D Engine Registers (offset 0x100000)

| Register | Offset | Description |
|----------|--------|-------------|
| CLIP0MIN | 0x100008 | Clip rectangle 0 min (x, y) |
| CLIP0MAX | 0x10000C | Clip rectangle 0 max (x, y) |
| DSTBASE | 0x100010 | Destination base address |
| DSTFORMAT | 0x100014 | Destination format (stride + pixfmt) |
| SRCCOLORKEYMIN | 0x100018 | Source color key min |
| SRCCOLORKEYMAX | 0x10001C | Source color key max |
| DSTCOLORKEYMIN | 0x100020 | Destination color key min |
| DSTCOLORKEYMAX | 0x100024 | Destination color key max |
| ROP | 0x100030 | Raster operation |
| SRCBASE | 0x100034 | Source base address |
| COMMANDEXTRA | 0x100038 | Extra command bits |
| CLIP1MIN | 0x10004C | Clip rectangle 1 min |
| CLIP1MAX | 0x100050 | Clip rectangle 1 max |
| SRCFORMAT | 0x100054 | Source format (stride + pixfmt) |
| SRCSIZE | 0x100058 | Source dimensions |
| SRCXY | 0x10005C | Source position |
| COLORBACK | 0x100060 | Background color |
| COLORFORE | 0x100064 | Foreground color |
| DSTSIZE | 0x100068 | Destination dimensions |
| DSTXY | 0x10006C | Destination position |
| COMMAND | 0x100070 | 2D command |
| LAUNCH | 0x100080 | Launch 2D operation |

### 2D Command Opcodes

| Value | Command | Source |
|-------|---------|--------|
| 0 | NOP | tdfxdefs.h |
| 1 | Screen-to-screen blit | tdfxdefs.h |
| 2 | Screen-to-screen stretch | tdfxdefs.h |
| 3 | Host-to-screen blit | tdfxdefs.h |
| 4 | Host-to-screen stretch | tdfxdefs.h |
| 5 | Rectangle fill | tdfxdefs.h |
| 6 | Line draw | tdfxdefs.h |
| 7 | Polyline | tdfxdefs.h |
| 8 | Polygon fill | tdfxdefs.h |

---

## 20. 3D Engine Registers (offset 0x200000)

| Register | Offset | Description |
|----------|--------|-------------|
| STATUS (3D) | 0x200000 | 3D engine status |
| LFBMODE | 0x200114 | LFB mode (3D path) |
| COMMAND (3D) | 0x200120 | 3D command |
| SWAPBUFFERCMD | 0x200128 | Swap buffer command |
| SLICTRL | 0x20020C | SLI control (3D) |
| AACTRL | 0x200210 | Anti-aliasing control |
| SWAPPENDING | 0x20024C | Swap pending count |
| LEFTOVERLAYBUF | 0x200250 | Left overlay buffer addr |
| RIGHTOVERLAYBUF | 0x200254 | Right overlay buffer addr |
| FBISWAPHISTORY | 0x200258 | Swap history |

---

## 21. DDC / I2C Bit Definitions (VIDSERPARPORT, 0x78)

| Bit | Name | Direction | Source |
|-----|------|-----------|--------|
| 18 | DDC_ENAB | Control | tdfx.h |
| 19 | DDC_SCL_OUT | Write (clock) | tdfx.h |
| 20 | DDC_SDA_OUT | Write (data) | tdfx.h |
| 21 | DDC_SCL_IN | Read (clock) | tdfx.h |
| 22 | DDC_SDA_IN | Read (data) | tdfx.h |
| 23 | I2C_ENAB | Control | tdfx.h |
| 24 | I2C_SCL_OUT | Write (clock) | tdfx.h |
| 25 | I2C_SDA_OUT | Write (data) | tdfx.h |
| 26 | I2C_SCL_IN | Read (clock) | tdfx.h |
| 27 | I2C_SDA_IN | Read (data) | tdfx.h |
