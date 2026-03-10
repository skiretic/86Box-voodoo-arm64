/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3dfx Voodoo 4 4500 (VSA-100) emulation — standalone driver.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_display.h>
#include <86box/vid_voodoo_fb.h>
#include <86box/vid_voodoo_fifo.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>
#include <86box/vid_voodoo_texture.h>

#define ROM_VOODOO4_4500 "roms/video/voodoo/V4_4500_AGP_SD_1.18.rom"

static video_timings_t timing_v4_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

/*
 * V4 private state — mirrors the Banshee structure closely but
 * is completely independent so we can evolve it for VSA-100.
 */
typedef struct voodoo4_t {
    svga_t svga;

    rom_t bios_rom;

    uint8_t pci_regs[256];

    uint32_t memBaseAddr0; /* BAR0: registers (32 MB decode) */
    uint32_t memBaseAddr1; /* BAR1: linear framebuffer (32 MB) */
    uint32_t ioBaseAddr;   /* BAR2: I/O */

    /* Init registers (IO remap space 0x000-0x0FF) */
    uint32_t agpInit0;
    uint32_t dramInit0, dramInit1;
    uint32_t lfbMemoryConfig;
    uint32_t miscInit0, miscInit1;
    uint32_t pciInit0;
    uint32_t vgaInit0, vgaInit1;

    /* 2D engine */
    uint32_t command_2d;
    uint32_t srcBaseAddr_2d;

    /* PLL */
    uint32_t pllCtrl0, pllCtrl1, pllCtrl2;

    /* DAC */
    uint32_t dacMode;
    int      dacAddr;

    /* Video */
    uint32_t vidDesktopOverlayStride;
    uint32_t vidDesktopStartAddr;
    uint32_t vidProcCfg;
    uint32_t vidScreenSize;
    uint32_t vidSerialParallelPort;
    uint32_t vidChromaKeyMin;
    uint32_t vidChromaKeyMax;

    /* AGP */
    uint32_t agpReqSize;
    uint32_t agpHostAddressHigh;
    uint32_t agpHostAddressLow;
    uint32_t agpGraphicsAddress;
    uint32_t agpGraphicsStride;
    uint32_t agpMoveCMD;

    int overlay_pix_fmt;

    uint32_t hwCurPatAddr, hwCurLoc, hwCurC0, hwCurC1;

    uint32_t intrCtrl;

    uint32_t overlay_buffer[2][4096];

    mem_mapping_t linear_mapping;
    mem_mapping_t reg_mapping_low;  /* 0x0000000-0x07fffff */
    mem_mapping_t reg_mapping_high; /* 0x0c00000-0x1ffffff */

    voodoo_t *voodoo;

    uint32_t desktop_addr;
    int      desktop_y;
    uint32_t desktop_stride_tiled;

    int has_bios, vblank_irq;

    uint8_t pci_slot;
    uint8_t irq_state;

    bool chroma_key_enabled;

    void *i2c, *i2c_ddc, *ddc;

    int render_diag_count; /* diagnostic: count render calls for first-frame logging */
} voodoo4_t;

/* Init register offsets — same layout as Banshee/Avenger */
enum {
    V4_Init_status          = 0x00,
    V4_Init_pciInit0        = 0x04,
    V4_Init_lfbMemoryConfig = 0x0c,
    V4_Init_miscInit0       = 0x10,
    V4_Init_miscInit1       = 0x14,
    V4_Init_dramInit0       = 0x18,
    V4_Init_dramInit1       = 0x1c,
    V4_Init_agpInit0        = 0x20,
    V4_Init_vgaInit0        = 0x28,
    V4_Init_vgaInit1        = 0x2c,
    V4_Init_2dCommand       = 0x30,
    V4_Init_2dSrcBaseAddr   = 0x34,
    V4_Init_strapInfo       = 0x38,

    V4_PLL_pllCtrl0 = 0x40,
    V4_PLL_pllCtrl1 = 0x44,
    V4_PLL_pllCtrl2 = 0x48,

    V4_DAC_dacMode = 0x4c,
    V4_DAC_dacAddr = 0x50,
    V4_DAC_dacData = 0x54,

    V4_Video_vidProcCfg                   = 0x5c,
    V4_Video_maxRgbDelta                  = 0x58,
    V4_Video_hwCurPatAddr                 = 0x60,
    V4_Video_hwCurLoc                     = 0x64,
    V4_Video_hwCurC0                      = 0x68,
    V4_Video_hwCurC1                      = 0x6c,
    V4_Video_vidSerialParallelPort        = 0x78,
    V4_Video_vidChromaKeyMin              = 0x8c,
    V4_Video_vidChromaKeyMax              = 0x90,
    V4_Video_vidScreenSize                = 0x98,
    V4_Video_vidOverlayStartCoords        = 0x9c,
    V4_Video_vidOverlayEndScreenCoords    = 0xa0,
    V4_Video_vidOverlayDudx               = 0xa4,
    V4_Video_vidOverlayDudxOffsetSrcWidth = 0xa8,
    V4_Video_vidOverlayDvdy               = 0xac,
    V4_Video_vidOverlayDvdyOffset         = 0xe0,
    V4_Video_vidDesktopStartAddr          = 0xe4,
    V4_Video_vidDesktopOverlayStride      = 0xe8,
};

/* vidProcCfg bits — reuse the same layout as Banshee */
#define V4_VGAINIT0_RAMDAC_8BIT            (1 << 2)
#define V4_VGAINIT0_EXTENDED_SHIFT_OUT     (1 << 12)

#define V4_VIDPROCCFG_VIDPROC_ENABLE       (1 << 0)
#define V4_VIDPROCCFG_CURSOR_MODE          (1 << 1)
#define V4_VIDPROCCFG_INTERLACE            (1 << 3)
#define V4_VIDPROCCFG_HALF_MODE            (1 << 4)
#define V4_VIDPROCCFG_OVERLAY_ENABLE       (1 << 8)
#define V4_VIDPROCCFG_DESKTOP_CLUT_BYPASS  (1 << 10)
#define V4_VIDPROCCFG_OVERLAY_CLUT_BYPASS  (1 << 11)
#define V4_VIDPROCCFG_DESKTOP_CLUT_SEL     (1 << 12)
#define V4_VIDPROCCFG_OVERLAY_CLUT_SEL     (1 << 13)
#define V4_VIDPROCCFG_H_SCALE_ENABLE       (1 << 14)
#define V4_VIDPROCCFG_V_SCALE_ENABLE       (1 << 15)
#define V4_VIDPROCCFG_DESKTOP_PIX_FORMAT   ((v4->vidProcCfg >> 18) & 7)
#define V4_VIDPROCCFG_OVERLAY_PIX_FORMAT   ((v4->vidProcCfg >> 21) & 7)
#define V4_VIDPROCCFG_DESKTOP_TILE         (1 << 24)
#define V4_VIDPROCCFG_OVERLAY_TILE         (1 << 25)
#define V4_VIDPROCCFG_2X_MODE              (1 << 26)
#define V4_VIDPROCCFG_HWCURSOR_ENA         (1 << 27)

#define V4_PIX_FORMAT_8                    0
#define V4_PIX_FORMAT_RGB565               1
#define V4_PIX_FORMAT_RGB24                2
#define V4_PIX_FORMAT_RGB32                3

#define V4_VIDSERIAL_DDC_EN                (1 << 18)
#define V4_VIDSERIAL_DDC_DCK_W             (1 << 19)
#define V4_VIDSERIAL_DDC_DDA_W             (1 << 20)
#define V4_VIDSERIAL_DDC_DCK_R             (1 << 21)
#define V4_VIDSERIAL_DDC_DDA_R             (1 << 22)
#define V4_VIDSERIAL_I2C_EN                (1 << 23)
#define V4_VIDSERIAL_I2C_SCK_W             (1 << 24)
#define V4_VIDSERIAL_I2C_SDA_W             (1 << 25)
#define V4_VIDSERIAL_I2C_SCK_R             (1 << 26)
#define V4_VIDSERIAL_I2C_SDA_R             (1 << 27)

#define V4_MISCINIT0_Y_ORIGIN_SWAP_SHIFT   (18)
#define V4_MISCINIT0_Y_ORIGIN_SWAP_MASK    (0xfff << V4_MISCINIT0_Y_ORIGIN_SWAP_SHIFT)

#define V4_OVERLAY_START_X_MASK            (0xfff)
#define V4_OVERLAY_START_Y_SHIFT           (12)
#define V4_OVERLAY_START_Y_MASK            (0xfff << V4_OVERLAY_START_Y_SHIFT)
#define V4_OVERLAY_END_X_MASK              (0xfff)
#define V4_OVERLAY_END_Y_SHIFT             (12)
#define V4_OVERLAY_END_Y_MASK              (0xfff << V4_OVERLAY_END_Y_SHIFT)
#define V4_OVERLAY_SRC_WIDTH_SHIFT         (19)
#define V4_OVERLAY_SRC_WIDTH_MASK          (0x1fff << V4_OVERLAY_SRC_WIDTH_SHIFT)
#define V4_VID_STRIDE_OVERLAY_SHIFT        (16)
#define V4_VID_STRIDE_OVERLAY_MASK         (0x7fff << V4_VID_STRIDE_OVERLAY_SHIFT)
#define V4_VID_DUDX_MASK                   (0xffffff)
#define V4_VID_DVDY_MASK                   (0xffffff)

/* Forward declarations */
static uint32_t v4_status(voodoo4_t *v4);
static void     v4_updatemapping(voodoo4_t *v4);
static void     v4_render_16bpp_tiled(svga_t *svga);

/*
 * v4_sync_tile_params — auto-configure LFB tiling parameters from display state.
 *
 * The VSA-100 BIOS may enable tiled desktop (vidProcCfg bit 24) and set
 * vidDesktopStartAddr / vidDesktopOverlayStride for scanout, but the BIOS
 * might configure lfbMemoryConfig before or after the display registers.
 * The LFB write handler tiles data using tile_base/tile_stride/tile_x_real
 * from lfbMemoryConfig. If those haven't been set yet (tile_x_real == 0),
 * LFB writes get mangled by a degenerate tiling formula.
 *
 * This function computes correct tile parameters from the display state and
 * applies them when lfbMemoryConfig hasn't been explicitly programmed. Once
 * the BIOS writes lfbMemoryConfig, those values take precedence.
 */
static void
v4_sync_tile_params(voodoo4_t *v4)
{
    voodoo_t *voodoo = v4->voodoo;

    /* Only act when tiling is enabled but lfbMemoryConfig hasn't been configured */
    if (!(v4->vidProcCfg & V4_VIDPROCCFG_DESKTOP_TILE))
        return;
    if (v4->lfbMemoryConfig != 0)
        return; /* BIOS has explicitly configured lfbMemoryConfig */

    uint32_t stride_tiles = v4->vidDesktopOverlayStride & 0x3fff;
    if (stride_tiles == 0)
        return;

    /* Compute the linear pitch for this tiled stride.
     * For 16bpp: pitch = stride_tiles * 128 bytes.
     * Find the smallest power-of-2 tile_stride >= pitch. */
    uint32_t pitch = stride_tiles * 128;
    uint32_t ts    = 1024;
    int      shift = 10;
    while (ts < pitch && shift < 17) {
        ts <<= 1;
        shift++;
    }

    voodoo->tile_base         = v4->vidDesktopStartAddr;
    voodoo->tile_stride       = ts;
    voodoo->tile_stride_shift = shift;
    voodoo->tile_x            = stride_tiles * 128;
    voodoo->tile_x_real       = stride_tiles * 128 * 32;

    fprintf(stderr, "V4 tile sync: tile_base=0x%x tile_stride=%u tile_stride_shift=%d "
            "tile_x=%u tile_x_real=%u (stride_tiles=%u pitch=%u)\n",
            voodoo->tile_base, voodoo->tile_stride, voodoo->tile_stride_shift,
            voodoo->tile_x, voodoo->tile_x_real, stride_tiles, pitch);
}

/* ============================================================
 * VGA I/O — standard VGA ports 0x3C0-0x3DF
 * ============================================================ */

static int
v4_vga_vsync_enabled(voodoo4_t *v4)
{
    if (!(v4->svga.crtc[0x11] & 0x20) && (v4->svga.crtc[0x11] & 0x10) && ((v4->pciInit0 >> 18) & 1) != 0)
        return 1;
    return 0;
}

static void
v4_update_irqs(voodoo4_t *v4)
{
    if (v4->vblank_irq > 0 && v4_vga_vsync_enabled(v4))
        pci_set_irq(v4->pci_slot, PCI_INTA, &v4->irq_state);
    else
        pci_clear_irq(v4->pci_slot, PCI_INTA, &v4->irq_state);
}

static void
v4_vblank_start(svga_t *svga)
{
    voodoo4_t *v4 = (voodoo4_t *) svga->priv;
    if (v4->vblank_irq >= 0) {
        v4->vblank_irq = 1;
        v4_update_irqs(v4);
    }
}

static void
v4_out(uint16_t addr, uint8_t val, void *priv)
{
    voodoo4_t *v4   = (voodoo4_t *) priv;
    svga_t    *svga = &v4->svga;
    uint8_t    old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if (addr != 0x3c9) /* skip DAC data spam */
        fprintf(stderr, "V4 VGA out: port=0x%04x val=0x%02x\n", addr, val);

    switch (addr) {
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            fprintf(stderr, "V4 VGA out: CRTC[0x%02x] = 0x%02x (was 0x%02x)\n",
                    svga->crtcreg, val, old);
            if (old != val) {
                if (svga->crtcreg == 0x11) {
                    if (!(val & 0x10)) {
                        if (v4->vblank_irq > 0)
                            v4->vblank_irq = -1;
                    } else if (v4->vblank_irq < 0) {
                        v4->vblank_irq = 0;
                    }
                    v4_update_irqs(v4);
                    if ((val & ~0x30) == (old & ~0x30))
                        old = val;
                }
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x11 || (svga->crtcreg == 0x11 && old != val)) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange    = 3;
                        svga->memaddr_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;

        default:
            break;
    }
    svga_out(addr, val, svga);

    /* After svga_out processes the register write, force fast path
     * when video processor is active (fb_only). The BIOS didn't set
     * up VGA chain4 mode, so GDC writes from the guest may clear
     * the fast flag. Override it. */
    if (svga->fb_only)
        svga->fast = 1;
}

static uint8_t
v4_in(uint16_t addr, void *priv)
{
    voodoo4_t *v4   = (voodoo4_t *) priv;
    svga_t    *svga = &v4->svga;
    uint8_t    temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c2:
            if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x40)
                temp = 0;
            else
                temp = 0x10;
            if (v4->vblank_irq > 0)
                temp |= 0x80;
            break;
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            temp = svga->crtc[svga->crtcreg];
            fprintf(stderr, "V4 VGA in: CRTC[0x%02x] = 0x%02x\n", svga->crtcreg, temp);
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
    if (addr != 0x3da && addr != 0x3c9) /* skip status/DAC spam */
        fprintf(stderr, "V4 VGA in: port=0x%04x val=0x%02x\n", addr, temp);
    return temp;
}

/* ============================================================
 * Timing recalculation — driven by SVGA core
 * ============================================================ */

static void
v4_recalctimings(svga_t *svga)
{
    voodoo4_t      *v4     = (voodoo4_t *) svga->priv;
    const voodoo_t *voodoo = v4->voodoo;

    fprintf(stderr, "V4 recalctimings: vgaInit0=0x%08x vidProcCfg=0x%08x "
            "extShiftOut=%d vidProcEn=%d fb_only=%d "
            "hdisp=%d dispend=%d rowoffset=%d bpp=%d "
            "crtc1a=0x%02x crtc1b=0x%02x gdcreg5=0x%02x gdcreg6=0x%02x "
            "chain4=%d packed_chain4=%d\n",
            v4->vgaInit0, v4->vidProcCfg,
            !!(v4->vgaInit0 & V4_VGAINIT0_EXTENDED_SHIFT_OUT),
            !!(v4->vidProcCfg & V4_VIDPROCCFG_VIDPROC_ENABLE),
            svga->fb_only,
            svga->hdisp, svga->dispend, svga->rowoffset, svga->bpp,
            svga->crtc[0x1a], svga->crtc[0x1b],
            svga->gdcreg[5], svga->gdcreg[6],
            svga->chain4, svga->packed_chain4);

    if (svga->crtc[0x1a] & 0x01)
        svga->htotal += 0x100;
    if (svga->crtc[0x1a] & 0x04)
        svga->hdisp += 0x100;

    if (v4->vidProcCfg & V4_VIDPROCCFG_VIDPROC_ENABLE) {
        if (v4->vgaInit0 & 0x40) {
            svga->hblankstart     = svga->crtc[1] + (((svga->crtc[0x1a] & 0x04) >> 2) << 8);
            svga->hblank_end_mask = 0x0000007f;
        } else {
            svga->hblankstart     = svga->crtc[1];
            svga->hblank_end_mask = 0x0000003f;
        }
        svga->hblank_end_val = svga->htotal - 1;

        if (!svga->scrblank && svga->attr_palette_enable)
            svga->dots_per_clock = (svga->seqregs[1] & 8) ? 16 : 8;

        svga->monitor->mon_overscan_y = 0;
        svga->monitor->mon_overscan_x = 0;
        svga->vblankstart             = svga->dispend;
        svga->linedbl                 = 0;
    } else {
        if (v4->vgaInit0 & 0x40) {
            svga->hblankstart     = (((svga->crtc[0x1a] & 0x10) >> 4) << 8) + svga->crtc[2];
            svga->hblank_end_val  = (svga->crtc[3] & 0x1f) | (((svga->crtc[5] & 0x80) >> 7) << 5) |
                                    (((svga->crtc[0x1a] & 0x20) >> 5) << 6);
            svga->hblank_end_mask = 0x0000007f;
        } else {
            svga->hblankstart     = svga->crtc[2];
            svga->hblank_end_val  = (svga->crtc[3] & 0x1f) | (((svga->crtc[5] & 0x80) >> 7) << 5);
            svga->hblank_end_mask = 0x0000003f;
        }
    }

    if (svga->crtc[0x1b] & 0x01)
        svga->vtotal += 0x400;
    if (svga->crtc[0x1b] & 0x04)
        svga->dispend += 0x400;
    if (svga->crtc[0x1b] & 0x10)
        svga->vblankstart += 0x400;
    if (svga->crtc[0x1b] & 0x40)
        svga->vsyncstart += 0x400;

    svga->interlace = 0;

    if (v4->vgaInit0 & V4_VGAINIT0_EXTENDED_SHIFT_OUT) {
        switch (V4_VIDPROCCFG_DESKTOP_PIX_FORMAT) {
            case V4_PIX_FORMAT_8:
                svga->render = svga_render_8bpp_highres;
                svga->bpp    = 8;
                break;
            case V4_PIX_FORMAT_RGB565:
                svga->render = (v4->vidProcCfg & V4_VIDPROCCFG_DESKTOP_TILE) ? v4_render_16bpp_tiled : svga_render_16bpp_highres;
                svga->bpp    = 16;
                break;
            case V4_PIX_FORMAT_RGB24:
                svga->render = svga_render_24bpp_highres;
                svga->bpp    = 24;
                break;
            case V4_PIX_FORMAT_RGB32:
                svga->render = svga_render_32bpp_highres;
                svga->bpp    = 32;
                break;
            default:
                break;
        }
        if (!(v4->vidProcCfg & V4_VIDPROCCFG_DESKTOP_TILE) && (v4->vidProcCfg & V4_VIDPROCCFG_HALF_MODE))
            svga->rowcount = 1;
        else
            svga->rowcount = 0;
        if (v4->vidProcCfg & V4_VIDPROCCFG_DESKTOP_TILE)
            svga->rowoffset = ((v4->vidDesktopOverlayStride & 0x3fff) * 128) >> 3;
        else
            svga->rowoffset = (v4->vidDesktopOverlayStride & 0x3fff) >> 3;
        /* TODO: implement proper VBE banking. For now, the Standard VGA driver
         * writes through the VGA aperture at A0000, which lands at VRAM offset 0
         * (write_bank=0). vidDesktopStartAddr=0x760000 points to empty VRAM.
         * Override to read from where VGA data actually is. */
        svga->memaddr_latch       = 0; /* was: v4->vidDesktopStartAddr >> 2 */
        v4->desktop_stride_tiled  = (v4->vidDesktopOverlayStride & 0x3fff) * 128 * 32;

        svga->char_width = 8;
        svga->split      = 99999;

        if (v4->vidProcCfg & V4_VIDPROCCFG_2X_MODE) {
            svga->hdisp *= 2;
            svga->dots_per_clock *= 2;
        }

        svga->interlace = !!(v4->vidProcCfg & V4_VIDPROCCFG_INTERLACE);

        svga->overlay.ena = v4->vidProcCfg & V4_VIDPROCCFG_OVERLAY_ENABLE;

        svga->overlay.x         = voodoo->overlay.start_x;
        svga->overlay.y         = voodoo->overlay.start_y;
        svga->overlay.cur_xsize = voodoo->overlay.size_x;
        svga->overlay.cur_ysize = voodoo->overlay.size_y;
        svga->overlay.pitch     = (v4->vidDesktopOverlayStride & V4_VID_STRIDE_OVERLAY_MASK) >> V4_VID_STRIDE_OVERLAY_SHIFT;
        if (v4->vidProcCfg & V4_VIDPROCCFG_OVERLAY_TILE)
            svga->overlay.pitch *= 128 * 32;
        if (svga->overlay.cur_xsize <= 0 || svga->overlay.cur_ysize <= 0)
            svga->overlay.ena = 0;
        if (svga->overlay.ena) {
            if (!voodoo->overlay.start_x && !voodoo->overlay.start_y &&
                svga->hdisp == voodoo->overlay.size_x && svga->dispend == voodoo->overlay.size_y) {
                svga->render = svga_render_null;
                svga->bpp    = 0;
            }
        }
    } else {
        svga->bpp = 8;
    }

    svga->fb_only = (v4->vidProcCfg & V4_VIDPROCCFG_VIDPROC_ENABLE);

    /* Fix VESA display stride: when extShiftOut is active (VESA mode),
     * force packed_chain4 so VGA aperture writes use byte-per-pixel
     * addressing instead of planar (addr<<2).  The BIOS tried to set
     * this via vgaInit1 bit 20, but the write landed at the wrong I/O
     * port before BAR2 was assigned, so packed_chain4 stays 0.
     * Also force the GDC memory map to mode 1 (A0000-AFFFF, 64 KB)
     * so svga_decode_addr applies write_bank for VBE banking. */
    if (svga->fb_only && (v4->vgaInit0 & V4_VGAINIT0_EXTENDED_SHIFT_OUT)) {
        svga->packed_chain4 = 1;
        /* Force GDC memory map to mode 1 (A0000-AFFFF, 64 KB) so that
         * svga_decode_addr applies write_bank for VBE windowed access.
         * Also update the VGA aperture mapping to match. */
        if (((svga->gdcreg[6] >> 2) & 3) != 1) {
            svga->gdcreg[6]   = (svga->gdcreg[6] & ~0x0c) | 0x04;
            svga->banked_mask = 0xffff;
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
        }
        /* Force VGA core to chain4 + pass-through.
         * The BIOS intended to set these during VBE mode switch but the
         * vgaInit1 write went to the wrong I/O port before BAR2 was assigned. */
        svga->chain4     = 1;
        svga->gdcreg[8]  = 0xff;         /* Bit Mask: all bits writable */
        svga->gdcreg[3] &= ~0x18;        /* Data Rotate: no function select */
        svga->gdcreg[1]  = 0;            /* Enable Set/Reset: none */
        svga->fast        = 1;
    }

    /* When the video processor is enabled (fb_only) but EXTENDED_SHIFT_OUT
     * is not set in vgaInit0, the BIOS still expects scanout from
     * vidDesktopStartAddr.  Override the VGA CRTC-derived memaddr_latch
     * and set up the desktop pixel format / stride so the SVGA render
     * functions read from the correct VRAM location. */
    if (svga->fb_only && !(v4->vgaInit0 & V4_VGAINIT0_EXTENDED_SHIFT_OUT)) {
        switch (V4_VIDPROCCFG_DESKTOP_PIX_FORMAT) {
            case V4_PIX_FORMAT_8:
                svga->render = svga_render_8bpp_highres;
                svga->bpp    = 8;
                break;
            case V4_PIX_FORMAT_RGB565:
                svga->render = svga_render_16bpp_highres;
                svga->bpp    = 16;
                break;
            case V4_PIX_FORMAT_RGB24:
                svga->render = svga_render_24bpp_highres;
                svga->bpp    = 24;
                break;
            case V4_PIX_FORMAT_RGB32:
                svga->render = svga_render_32bpp_highres;
                svga->bpp    = 32;
                break;
            default:
                break;
        }
        svga->rowoffset     = (v4->vidDesktopOverlayStride & 0x3fff) >> 3;
        svga->memaddr_latch = v4->vidDesktopStartAddr >> 2;
        svga->char_width    = 8;
        svga->split         = 99999;

        fprintf(stderr, "V4 recalctimings: fb_only override — memaddr_latch=0x%x "
                "rowoffset=%d bpp=%d vidDesktopStartAddr=0x%06x stride=0x%04x\n",
                svga->memaddr_latch, svga->rowoffset, svga->bpp,
                v4->vidDesktopStartAddr, v4->vidDesktopOverlayStride & 0x3fff);
    }

    if (((svga->miscout >> 2) & 3) == 3) {
        int    k    = v4->pllCtrl0 & 3;
        int    m    = (v4->pllCtrl0 >> 2) & 0x3f;
        int    n    = (v4->pllCtrl0 >> 8) & 0xff;
        double freq = (((double) n + 2) / (((double) m + 2) * (double) (1 << k))) * 14318184.0;

        svga->clock = (cpuclock * (float) (1ULL << 32)) / freq;
    }

    fprintf(stderr, "V4 recalctimings DONE: hdisp=%d dispend=%d rowoffset=%d bpp=%d "
            "fb_only=%d render=%p memaddr_latch=0x%x split=%d lowres=%d "
            "chain4=%d packed_chain4=%d writemode=%d readmode=%d gdcreg5=0x%02x gdcreg6=0x%02x "
            "write_bank=0x%x read_bank=0x%x "
            "lfbMemCfg=0x%08x tile_base=0x%x tile_stride=%u tile_x_real=%u\n",
            svga->hdisp, svga->dispend, svga->rowoffset, svga->bpp,
            svga->fb_only, (void *) svga->render, svga->memaddr_latch, svga->split,
            svga->lowres, svga->chain4, svga->packed_chain4, svga->writemode, svga->readmode,
            svga->gdcreg[5], svga->gdcreg[6],
            svga->write_bank, svga->read_bank,
            v4->lfbMemoryConfig, voodoo->tile_base,
            voodoo->tile_stride, voodoo->tile_x_real);

    /* One-shot VRAM content dump to find where pixel data lives */
    {
        static int vram_dump_count = 0;
        if (svga->fb_only && vram_dump_count < 3) {
            uint32_t latch_addr = svga->memaddr_latch << 2;
            uint32_t desk_addr  = v4->vidDesktopStartAddr;
            int nonzero_at_0 = 0, nonzero_at_latch = 0, nonzero_at_desk = 0;
            for (int i = 0; i < 4096; i++) {
                if (svga->vram[i]) nonzero_at_0++;
                if ((latch_addr + i) < svga->vram_max && svga->vram[latch_addr + i]) nonzero_at_latch++;
                if ((desk_addr + i) < svga->vram_max && svga->vram[desk_addr + i]) nonzero_at_desk++;
            }
            fprintf(stderr, "V4 VRAM probe: vram[0..4095] nonzero=%d, vram[0x%06x..+4095] nonzero=%d, "
                    "vram[0x%06x..+4095] nonzero=%d, vram_max=0x%x\n",
                    nonzero_at_0, latch_addr, nonzero_at_latch, desk_addr, nonzero_at_desk, svga->vram_max);
            vram_dump_count++;
        }
    }
}

/* ============================================================
 * Memory-mapped register I/O — ext I/O remap handlers
 * These handle the init/PLL/DAC/video registers at BAR0+0x00xxxx
 * ============================================================ */

static void
v4_ext_out(uint16_t addr, uint8_t val, void *priv)
{
    fprintf(stderr, "V4 ext out: port=0x%04x (off=0x%02x) val=0x%02x\n", addr, addr & 0xff, val);

    /* Byte-wide writes to VGA-mapped ports */
    switch (addr & 0xff) {
        case 0xb0: case 0xb1: case 0xb2: case 0xb3:
        case 0xb4: case 0xb5: case 0xb6: case 0xb7:
        case 0xb8: case 0xb9: case 0xba: case 0xbb:
        case 0xbc: case 0xbd: case 0xbe: case 0xbf:
        case 0xc0: case 0xc1: case 0xc2: case 0xc3:
        case 0xc4: case 0xc5: case 0xc6: case 0xc7:
        case 0xc8: case 0xc9: case 0xca: case 0xcb:
        case 0xcc: case 0xcd: case 0xce: case 0xcf:
        case 0xd0: case 0xd1: case 0xd2: case 0xd3:
        case 0xd4: case 0xd5: case 0xd6: case 0xd7:
        case 0xd8: case 0xd9: case 0xda: case 0xdb:
        case 0xdc: case 0xdd: case 0xde: case 0xdf:
            v4_out((addr & 0xff) + 0x300, val, priv);
            break;
        default:
            fprintf(stderr, "V4 ext out: UNHANDLED off=0x%02x val=0x%02x\n", addr & 0xff, val);
            break;
    }
}

static uint8_t
v4_ext_in(uint16_t addr, void *priv)
{
    voodoo4_t *v4  = (voodoo4_t *) priv;
    uint8_t    ret = 0xff;

    switch (addr & 0xff) {
        case V4_Init_status:
        case V4_Init_status + 1:
        case V4_Init_status + 2:
        case V4_Init_status + 3:
            ret = (v4_status(v4) >> ((addr & 3) * 8)) & 0xff;
            break;
        case 0xb0: case 0xb1: case 0xb2: case 0xb3:
        case 0xb4: case 0xb5: case 0xb6: case 0xb7:
        case 0xb8: case 0xb9: case 0xba: case 0xbb:
        case 0xbc: case 0xbd: case 0xbe: case 0xbf:
        case 0xc0: case 0xc1: case 0xc2: case 0xc3:
        case 0xc4: case 0xc5: case 0xc6: case 0xc7:
        case 0xc8: case 0xc9: case 0xca: case 0xcb:
        case 0xcc: case 0xcd: case 0xce: case 0xcf:
        case 0xd0: case 0xd1: case 0xd2: case 0xd3:
        case 0xd4: case 0xd5: case 0xd6: case 0xd7:
        case 0xd8: case 0xd9: case 0xda: case 0xdb:
        case 0xdc: case 0xdd: case 0xde: case 0xdf:
            ret = v4_in((addr & 0xff) + 0x300, priv);
            break;
        default:
            fprintf(stderr, "V4 ext in: UNHANDLED off=0x%02x ret=0xff\n", addr & 0xff);
            break;
    }
    if ((addr & 0xff) >= 0x04) /* skip status register spam */
        fprintf(stderr, "V4 ext in: port=0x%04x (off=0x%02x) val=0x%02x\n", addr, addr & 0xff, ret);
    return ret;
}

static void
v4_ext_outl(uint16_t addr, uint32_t val, void *priv)
{
    voodoo4_t *v4     = (voodoo4_t *) priv;
    voodoo_t  *voodoo = v4->voodoo;
    svga_t    *svga   = &v4->svga;

    fprintf(stderr, "V4 ext outl: port=0x%04x (off=0x%02x) val=0x%08x\n", addr, addr & 0xff, val);

    switch (addr & 0xff) {
        case V4_Init_pciInit0:
            v4->pciInit0       = val;
            voodoo->read_time  = agp_nonburst_time + agp_burst_time * ((val & 0x100) ? 2 : 1);
            voodoo->burst_time = agp_burst_time * ((val & 0x200) ? 1 : 0);
            voodoo->write_time = agp_nonburst_time + voodoo->burst_time;
            break;

        case V4_Init_lfbMemoryConfig:
            v4->lfbMemoryConfig = val;
            voodoo->tile_base         = (val & 0x1fff) << 12;
            voodoo->tile_stride       = 1024 << ((val >> 13) & 7);
            voodoo->tile_stride_shift = 10 + ((val >> 13) & 7);
            voodoo->tile_x            = ((val >> 16) & 0x7f) * 128;
            voodoo->tile_x_real       = ((val >> 16) & 0x7f) * 128 * 32;
            fprintf(stderr, "V4 ext outl: lfbMemoryConfig=0x%08x tile_base=0x%x "
                    "tile_stride=%u tile_stride_shift=%d tile_x=%u tile_x_real=%u\n",
                    val, voodoo->tile_base, voodoo->tile_stride,
                    voodoo->tile_stride_shift, voodoo->tile_x, voodoo->tile_x_real);
            break;

        case V4_Init_miscInit0:
            v4->miscInit0             = val;
            voodoo->y_origin_swap     = (val & V4_MISCINIT0_Y_ORIGIN_SWAP_MASK) >> V4_MISCINIT0_Y_ORIGIN_SWAP_SHIFT;
            break;
        case V4_Init_miscInit1:
            v4->miscInit1 = val;
            break;
        case V4_Init_dramInit0:
            v4->dramInit0 = val;
            break;
        case V4_Init_dramInit1:
            v4->dramInit1 = val;
            break;
        case V4_Init_agpInit0:
            v4->agpInit0 = val;
            break;

        case V4_Init_2dCommand:
            v4->command_2d = val;
            break;
        case V4_Init_2dSrcBaseAddr:
            v4->srcBaseAddr_2d = val;
            break;
        case V4_Init_vgaInit0:
            v4->vgaInit0 = val;
            fprintf(stderr, "V4 ext outl: vgaInit0=0x%08x extShiftOut=%d ramdac8bit=%d extHblank=%d\n",
                    val, !!(val & V4_VGAINIT0_EXTENDED_SHIFT_OUT),
                    !!(val & V4_VGAINIT0_RAMDAC_8BIT), !!(val & 0x40));
            svga_set_ramdac_type(svga, (val & V4_VGAINIT0_RAMDAC_8BIT ? RAMDAC_8BIT : RAMDAC_6BIT));
            svga_recalctimings(svga);
            break;
        case V4_Init_vgaInit1:
            v4->vgaInit1        = val;
            svga->write_bank    = (val & 0x3ff) << 15;
            svga->read_bank     = ((val >> 10) & 0x3ff) << 15;
            svga->packed_chain4 = !!(val & 0x00100000);
            svga->fast          = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) &&
                                  ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) &&
                                  !(svga->adv_flags & FLAG_ADDR_BY8);
            break;

        case V4_PLL_pllCtrl0:
            v4->pllCtrl0 = val;
            break;
        case V4_PLL_pllCtrl1:
            v4->pllCtrl1 = val;
            break;
        case V4_PLL_pllCtrl2:
            v4->pllCtrl2 = val;
            break;

        case V4_DAC_dacMode:
            v4->dacMode  = val;
            svga->dpms   = !!(val & 0x0a);
            svga_recalctimings(svga);
            break;
        case V4_DAC_dacAddr:
            v4->dacAddr = val & 0x1ff;
            break;
        case V4_DAC_dacData: {
            uint8_t r = (val >> 16) & 0xFF;
            uint8_t g = (val >> 8) & 0xFF;
            uint8_t b = val & 0xFF;
            svga->pallook[v4->dacAddr] = makecol32(r, g, b);
            svga->fullchange           = changeframecount;
            break;
        }

        case V4_Video_vidProcCfg:
            v4->vidProcCfg       = val;
            v4->overlay_pix_fmt  = (val >> 21) & 7;
            svga->hwcursor.ena   = val & V4_VIDPROCCFG_HWCURSOR_ENA;
            svga->fullchange     = changeframecount;
            svga->lut_map        = !(val & V4_VIDPROCCFG_DESKTOP_CLUT_BYPASS) && (svga->bpp < 24);
            fprintf(stderr, "V4 ext outl: vidProcCfg=0x%08x enable=%d pixfmt=%d cursor=%d overlay=%d "
                    "clutBypass=%d halfMode=%d tile=%d 2xMode=%d\n",
                    val, !!(val & V4_VIDPROCCFG_VIDPROC_ENABLE),
                    (val >> 18) & 7, !!(val & V4_VIDPROCCFG_HWCURSOR_ENA),
                    !!(val & V4_VIDPROCCFG_OVERLAY_ENABLE),
                    !!(val & V4_VIDPROCCFG_DESKTOP_CLUT_BYPASS),
                    !!(val & V4_VIDPROCCFG_HALF_MODE),
                    !!(val & V4_VIDPROCCFG_DESKTOP_TILE),
                    !!(val & V4_VIDPROCCFG_2X_MODE));
            v4_sync_tile_params(v4);
            svga_recalctimings(svga);
            break;

        case V4_Video_maxRgbDelta:
            voodoo->scrfilterThreshold = val;
            voodoo->scrfilterEnabled   = (val > 0x00) ? 1 : 0;
            voodoo_threshold_check(voodoo);
            break;

        case V4_Video_hwCurPatAddr:
            v4->hwCurPatAddr  = val;
            svga->hwcursor.addr = (val & 0xfffff0) + (svga->hwcursor.yoff * 16);
            break;
        case V4_Video_hwCurLoc:
            v4->hwCurLoc     = val;
            svga->hwcursor.x = (val & 0x7ff) - 64;
            svga->hwcursor.y = ((val >> 16) & 0x7ff) - 64;
            if (svga->hwcursor.y < 0) {
                svga->hwcursor.yoff = -svga->hwcursor.y;
                svga->hwcursor.y    = 0;
            } else
                svga->hwcursor.yoff = 0;
            svga->hwcursor.addr      = (v4->hwCurPatAddr & 0xfffff0) + (svga->hwcursor.yoff * 16);
            svga->hwcursor.cur_xsize = 64;
            svga->hwcursor.cur_ysize = 64;
            break;
        case V4_Video_hwCurC0:
            v4->hwCurC0 = val;
            break;
        case V4_Video_hwCurC1:
            v4->hwCurC1 = val;
            break;

        case V4_Video_vidSerialParallelPort:
            v4->vidSerialParallelPort = val;
            i2c_gpio_set(v4->i2c_ddc, !!(val & V4_VIDSERIAL_DDC_DCK_W), !!(val & V4_VIDSERIAL_DDC_DDA_W));
            i2c_gpio_set(v4->i2c, !!(val & V4_VIDSERIAL_I2C_SCK_W), !!(val & V4_VIDSERIAL_I2C_SDA_W));
            break;

        case V4_Video_vidChromaKeyMin:
            v4->vidChromaKeyMin = val;
            break;
        case V4_Video_vidChromaKeyMax:
            v4->vidChromaKeyMax = val;
            break;

        case V4_Video_vidScreenSize:
            v4->vidScreenSize = val;
            voodoo->h_disp    = (val & 0xfff) + 1;
            voodoo->v_disp    = (val >> 12) & 0xfff;
            break;
        case V4_Video_vidOverlayStartCoords:
            voodoo->overlay.vidOverlayStartCoords = val;
            voodoo->overlay.start_x               = val & V4_OVERLAY_START_X_MASK;
            voodoo->overlay.start_y               = (val & V4_OVERLAY_START_Y_MASK) >> V4_OVERLAY_START_Y_SHIFT;
            voodoo->overlay.size_x                = voodoo->overlay.end_x - voodoo->overlay.start_x;
            voodoo->overlay.size_y                = voodoo->overlay.end_y - voodoo->overlay.start_y;
            svga_recalctimings(svga);
            break;
        case V4_Video_vidOverlayEndScreenCoords:
            voodoo->overlay.vidOverlayEndScreenCoords = val;
            voodoo->overlay.end_x                     = val & V4_OVERLAY_END_X_MASK;
            voodoo->overlay.end_y                     = (val & V4_OVERLAY_END_Y_MASK) >> V4_OVERLAY_END_Y_SHIFT;
            voodoo->overlay.size_x                    = (voodoo->overlay.end_x - voodoo->overlay.start_x) + 1;
            voodoo->overlay.size_y                    = (voodoo->overlay.end_y - voodoo->overlay.start_y) + 1;
            svga_recalctimings(svga);
            break;
        case V4_Video_vidOverlayDudx:
            voodoo->overlay.vidOverlayDudx = val & V4_VID_DUDX_MASK;
            break;
        case V4_Video_vidOverlayDudxOffsetSrcWidth:
            voodoo->overlay.vidOverlayDudxOffsetSrcWidth = val;
            voodoo->overlay.overlay_bytes                = (val & V4_OVERLAY_SRC_WIDTH_MASK) >> V4_OVERLAY_SRC_WIDTH_SHIFT;
            break;
        case V4_Video_vidOverlayDvdy:
            voodoo->overlay.vidOverlayDvdy = val & V4_VID_DVDY_MASK;
            break;
        case V4_Video_vidOverlayDvdyOffset:
            voodoo->overlay.vidOverlayDvdyOffset = val;
            break;
        case V4_Video_vidDesktopStartAddr:
            v4->vidDesktopStartAddr = val & 0xffffff;
            fprintf(stderr, "V4 ext outl: vidDesktopStartAddr=0x%06x\n", v4->vidDesktopStartAddr);
            svga->fullchange       = changeframecount;
            v4_sync_tile_params(v4);
            svga_recalctimings(svga);
            break;
        case V4_Video_vidDesktopOverlayStride:
            v4->vidDesktopOverlayStride = val;
            fprintf(stderr, "V4 ext outl: vidDesktopOverlayStride=0x%08x (desktop=%d overlay=%d)\n",
                    val, val & 0x3fff, (val >> 16) & 0x7fff);
            svga->fullchange            = changeframecount;
            v4_sync_tile_params(v4);
            svga_recalctimings(svga);
            break;

        default:
            fprintf(stderr, "V4 ext outl: UNHANDLED off=0x%02x val=0x%08x\n", addr & 0xff, val);
            break;
    }
}

static uint32_t
v4_ext_inl(uint16_t addr, void *priv)
{
    voodoo4_t      *v4     = (voodoo4_t *) priv;
    const voodoo_t *voodoo = v4->voodoo;
    const svga_t   *svga   = &v4->svga;
    uint32_t        ret    = 0xffffffff;

    cycles -= voodoo->read_time;

    switch (addr & 0xff) {
        case V4_Init_status:
            ret = v4_status(v4);
            break;
        case V4_Init_pciInit0:
            ret = v4->pciInit0;
            break;
        case V4_Init_lfbMemoryConfig:
            ret = v4->lfbMemoryConfig;
            break;
        case V4_Init_miscInit0:
            ret = v4->miscInit0;
            break;
        case V4_Init_miscInit1:
            ret = v4->miscInit1;
            break;
        case V4_Init_dramInit0:
            ret = v4->dramInit0;
            break;
        case V4_Init_dramInit1:
            ret = v4->dramInit1;
            break;
        case V4_Init_agpInit0:
            ret = v4->agpInit0;
            break;
        case V4_Init_vgaInit0:
            ret = v4->vgaInit0;
            break;
        case V4_Init_vgaInit1:
            ret = v4->vgaInit1;
            break;
        case V4_Init_2dCommand:
            ret = v4->command_2d;
            break;
        case V4_Init_2dSrcBaseAddr:
            ret = v4->srcBaseAddr_2d;
            break;
        case V4_Init_strapInfo:
            /* VSA-100: 32 MB SDRAM, AGP, IRQ enabled, 64kB BIOS */
            ret = 0x00000060;
            break;

        case V4_PLL_pllCtrl0:
            ret = v4->pllCtrl0;
            break;
        case V4_PLL_pllCtrl1:
            ret = v4->pllCtrl1;
            break;
        case V4_PLL_pllCtrl2:
            ret = v4->pllCtrl2;
            break;

        case V4_DAC_dacMode:
            ret = v4->dacMode;
            break;
        case V4_DAC_dacAddr:
            ret = v4->dacAddr;
            break;
        case V4_DAC_dacData:
            ret = svga->pallook[v4->dacAddr];
            break;

        case V4_Video_vidProcCfg:
            ret = v4->vidProcCfg;
            break;
        case V4_Video_hwCurPatAddr:
            ret = v4->hwCurPatAddr;
            break;
        case V4_Video_hwCurLoc:
            ret = v4->hwCurLoc;
            break;
        case V4_Video_hwCurC0:
            ret = v4->hwCurC0;
            break;
        case V4_Video_hwCurC1:
            ret = v4->hwCurC1;
            break;

        case V4_Video_vidSerialParallelPort:
            ret = v4->vidSerialParallelPort & ~(V4_VIDSERIAL_DDC_DCK_R | V4_VIDSERIAL_DDC_DDA_R | V4_VIDSERIAL_I2C_SCK_R | V4_VIDSERIAL_I2C_SDA_R);
            if (v4->vidSerialParallelPort & V4_VIDSERIAL_DDC_EN) {
                if (i2c_gpio_get_scl(v4->i2c_ddc))
                    ret |= V4_VIDSERIAL_DDC_DCK_R;
                if (i2c_gpio_get_sda(v4->i2c_ddc))
                    ret |= V4_VIDSERIAL_DDC_DDA_R;
            }
            if (v4->vidSerialParallelPort & V4_VIDSERIAL_I2C_EN) {
                if (i2c_gpio_get_scl(v4->i2c))
                    ret |= V4_VIDSERIAL_I2C_SCK_R;
                if (i2c_gpio_get_sda(v4->i2c))
                    ret |= V4_VIDSERIAL_I2C_SDA_R;
            }
            break;

        case V4_Video_vidChromaKeyMin:
            ret = v4->vidChromaKeyMin;
            break;
        case V4_Video_vidChromaKeyMax:
            ret = v4->vidChromaKeyMax;
            break;
        case V4_Video_vidScreenSize:
            ret = v4->vidScreenSize;
            break;
        case V4_Video_vidOverlayStartCoords:
            ret = voodoo->overlay.vidOverlayStartCoords;
            break;
        case V4_Video_vidOverlayEndScreenCoords:
            ret = voodoo->overlay.vidOverlayEndScreenCoords;
            break;
        case V4_Video_vidOverlayDudx:
            ret = voodoo->overlay.vidOverlayDudx;
            break;
        case V4_Video_vidOverlayDudxOffsetSrcWidth:
            ret = voodoo->overlay.vidOverlayDudxOffsetSrcWidth;
            break;
        case V4_Video_vidOverlayDvdy:
            ret = voodoo->overlay.vidOverlayDvdy;
            break;
        case V4_Video_vidOverlayDvdyOffset:
            ret = voodoo->overlay.vidOverlayDvdyOffset;
            break;
        case V4_Video_vidDesktopStartAddr:
            ret = v4->vidDesktopStartAddr;
            break;
        case V4_Video_vidDesktopOverlayStride:
            ret = v4->vidDesktopOverlayStride;
            break;

        default:
            fprintf(stderr, "V4 ext inl: UNHANDLED off=0x%02x ret=0xffffffff\n", addr & 0xff);
            break;
    }

    if ((addr & 0xff) != V4_Init_status) /* skip status register spam */
        fprintf(stderr, "V4 ext inl: port=0x%04x (off=0x%02x) val=0x%08x\n", addr, addr & 0xff, ret);
    return ret;
}

/* ============================================================
 * Status register
 * ============================================================ */

static uint32_t
v4_status(voodoo4_t *v4)
{
    voodoo_t     *voodoo       = v4->voodoo;
    const svga_t *svga         = &v4->svga;
    int           fifo_entries = FIFO_ENTRIES;
    int           swap_count   = voodoo->swap_count;
    int           written      = voodoo->cmd_written + voodoo->cmd_written_fifo;
    int           busy         = (written - voodoo->cmd_read) ||
                                 (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr) ||
                                 (voodoo->cmdfifo_depth_rd_2 != voodoo->cmdfifo_depth_wr_2) ||
                                 RENDER_VOODOO_BUSY(voodoo, 0) || RENDER_VOODOO_BUSY(voodoo, 1) ||
                                 RENDER_VOODOO_BUSY(voodoo, 2) || RENDER_VOODOO_BUSY(voodoo, 3) ||
                                 voodoo->voodoo_busy;
    uint32_t      ret          = 0;

    if (fifo_entries < 0x20)
        ret |= 0x1f - fifo_entries;
    else
        ret |= 0x1f;
    if (fifo_entries)
        ret |= 0x20;
    if (swap_count < 7)
        ret |= (swap_count << 28);
    else
        ret |= (7 << 28);
    if (!(svga->cgastat & 8))
        ret |= 0x40;

    if (busy)
        ret |= 0x780;

    if (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr)
        ret |= (1 << 11);
    if (voodoo->cmdfifo_depth_rd_2 != voodoo->cmdfifo_depth_wr_2)
        ret |= (1 << 12);

    if (!voodoo->voodoo_busy)
        voodoo_wake_fifo_thread(voodoo);

    return ret;
}

/* ============================================================
 * Tiled 16bpp render — tiles are 128 bytes wide x 32 rows high
 * ============================================================ */

static void
v4_render_16bpp_tiled(svga_t *svga)
{
    voodoo4_t *v4 = (voodoo4_t *) svga->priv;
    uint32_t  *p = &(svga->monitor->target_buffer->line[svga->displine + svga->y_add])[svga->x_add];
    uint32_t   addr;
    int        drawn = 0;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (v4->vidProcCfg & V4_VIDPROCCFG_HALF_MODE)
        addr = v4->desktop_addr + ((v4->desktop_y >> 1) & 31) * 128 + ((v4->desktop_y >> 6) * v4->desktop_stride_tiled);
    else
        addr = v4->desktop_addr + (v4->desktop_y & 31) * 128 + ((v4->desktop_y >> 5) * v4->desktop_stride_tiled);

    if (v4->render_diag_count < 20) {
        voodoo_t *voodoo_diag = v4->voodoo;
        fprintf(stderr, "V4 TILED RENDER: y=%d desktop_addr=0x%x desktop_y=%d addr=0x%x "
                "stride_tiled=%u hdisp=%d displine=%d half=%d "
                "tile_base=0x%x tile_stride=%u tile_x_real=%u lfbMemCfg=0x%08x "
                "vram[addr]=%02x%02x%02x%02x\n",
                v4->desktop_y, v4->desktop_addr, v4->desktop_y, addr,
                v4->desktop_stride_tiled, svga->hdisp, svga->displine,
                !!(v4->vidProcCfg & V4_VIDPROCCFG_HALF_MODE),
                voodoo_diag->tile_base, voodoo_diag->tile_stride,
                voodoo_diag->tile_x_real, v4->lfbMemoryConfig,
                (addr < svga->vram_max) ? svga->vram[addr] : 0xDE,
                (addr + 1 < svga->vram_max) ? svga->vram[addr + 1] : 0xAD,
                (addr + 2 < svga->vram_max) ? svga->vram[addr + 2] : 0xBE,
                (addr + 3 < svga->vram_max) ? svga->vram[addr + 3] : 0xEF);
        v4->render_diag_count++;
    }

    if (addr >= svga->vram_max)
        return;

    for (int x = 0; x < svga->hdisp; x += 64) {
        if (svga->hwcursor_on || svga->overlay_on)
            svga->changedvram[addr >> 12] = 2;
        if (svga->changedvram[addr >> 12] || svga->fullchange) {
            const uint16_t *vram_p = (uint16_t *) &svga->vram[addr & svga->vram_display_mask];

            for (uint8_t xx = 0; xx < 64; xx++)
                *p++ = svga->conv_16to32(svga, *vram_p++, 16);

            drawn = 1;
        } else
            p += 64;
        addr += 128 * 32;
    }

    if (drawn) {
        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
    }

    v4->desktop_y++;
}

/* ============================================================
 * Hardware cursor draw — same as Banshee
 * ============================================================ */

static void
v4_hwcursor_draw(svga_t *svga, int displine)
{
    voodoo4_t *v4 = (voodoo4_t *) svga->priv;
    int        x;
    uint32_t  *p;
    int        x_off;
    int        xx;

    p     = &(svga->monitor->target_buffer->line[displine + svga->y_add])[svga->x_add];
    x_off = svga->hwcursor_on;

    for (x = 0; x < 64 - x_off; x += 2) {
        uint8_t dat = svga->vram[svga->hwcursor.addr & svga->vram_mask];

        xx = svga->hwcursor.x + x_off + x + 32;
        if (!(v4->vidProcCfg & V4_VIDPROCCFG_CURSOR_MODE)) {
            /* X11 mode */
            if (xx >= 0 && xx < svga->hdisp) {
                switch (dat & 0xc0) {
                    case 0x00:
                        p[xx] = v4->hwCurC0;
                        break;
                    case 0x40:
                        p[xx] = v4->hwCurC1;
                        break;
                    case 0xc0:
                        p[xx] ^= 0xffffff;
                        break;
                    default:
                        break;
                }
            }
            xx++;
            if (xx >= 0 && xx < svga->hdisp) {
                switch (dat & 0x30) {
                    case 0x00:
                        p[xx] = v4->hwCurC0;
                        break;
                    case 0x10:
                        p[xx] = v4->hwCurC1;
                        break;
                    case 0x30:
                        p[xx] ^= 0xffffff;
                        break;
                    default:
                        break;
                }
            }
        } else {
            /* Microsoft mode */
            if (xx >= 0 && xx < svga->hdisp) {
                if (dat & 0x80)
                    p[xx] = (dat & 0x40) ? (p[xx] ^ 0xffffff) : p[xx];
                else
                    p[xx] = (dat & 0x40) ? v4->hwCurC1 : v4->hwCurC0;
            }
            xx++;
            if (xx >= 0 && xx < svga->hdisp) {
                if (dat & 0x20)
                    p[xx] = (dat & 0x10) ? (p[xx] ^ 0xffffff) : p[xx];
                else
                    p[xx] = (dat & 0x10) ? v4->hwCurC1 : v4->hwCurC0;
            }
        }

        svga->hwcursor.addr++;
    }
}

/* ============================================================
 * Overlay draw — stub for now
 * ============================================================ */

static void
v4_overlay_draw(svga_t *svga, int displine)
{
    /* TODO: implement overlay when needed */
    (void) svga;
    (void) displine;
}

/* ============================================================
 * SVGA vsync callback
 * ============================================================ */

static void
v4_vsync_callback(svga_t *svga)
{
    voodoo4_t *v4     = (voodoo4_t *) svga->priv;
    voodoo_t  *voodoo = v4->voodoo;

    voodoo->v_retrace++;

    v4->desktop_addr = v4->vidDesktopStartAddr;
    v4->desktop_y    = 0;

    static int vsync_log_count = 0;
    if (vsync_log_count < 10) {
        fprintf(stderr, "V4 VSYNC: desktop_addr=0x%x startAddr=0x%x stride=%u strideField=0x%x "
                "vidProcCfg=0x%08x tile=%d hdisp=%d dispend=%d bpp=%d fb_only=%d "
                "render=%s\n",
                v4->desktop_addr, v4->vidDesktopStartAddr,
                v4->desktop_stride_tiled, v4->vidDesktopOverlayStride,
                v4->vidProcCfg,
                !!(v4->vidProcCfg & V4_VIDPROCCFG_DESKTOP_TILE),
                svga->hdisp, svga->dispend, svga->bpp, svga->fb_only,
                (svga->render == v4_render_16bpp_tiled) ? "tiled16" :
                (svga->render == svga_render_16bpp_highres) ? "highres16" :
                (svga->render == svga_render_8bpp_highres) ? "highres8" :
                (svga->render == svga_render_null) ? "null" : "other");
        vsync_log_count++;
    }
}

/* ============================================================
 * Linear framebuffer read/write — BAR1
 * ============================================================ */

static uint8_t
v4_read_linear(uint32_t addr, void *priv)
{
    voodoo4_t      *v4     = (voodoo4_t *) priv;
    const voodoo_t *voodoo = v4->voodoo;
    const svga_t   *svga   = &v4->svga;

    cycles -= voodoo->read_time;

    if ((v4->pci_regs[0x30] & 0x01) && addr >= v4->bios_rom.mapping.base && addr < (v4->bios_rom.mapping.base + v4->bios_rom.sz))
        return rom_read(addr & (v4->bios_rom.sz - 1), &v4->bios_rom);

    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x, y;
        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;
        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
    }
    if (addr >= svga->vram_max)
        return 0xff;

    cycles -= svga->monitor->mon_video_timing_read_b;
    return svga->vram[addr & svga->vram_mask];
}

static uint16_t
v4_read_linear_w(uint32_t addr, void *priv)
{
    voodoo4_t      *v4     = (voodoo4_t *) priv;
    const voodoo_t *voodoo = v4->voodoo;
    svga_t         *svga   = &v4->svga;

    if (addr & 1)
        return v4_read_linear(addr, priv) | (v4_read_linear(addr + 1, priv) << 8);

    cycles -= voodoo->read_time;
    if ((v4->pci_regs[0x30] & 0x01) && addr >= v4->bios_rom.mapping.base && addr < (v4->bios_rom.mapping.base + v4->bios_rom.sz))
        return rom_readw(addr & (v4->bios_rom.sz - 1), &v4->bios_rom);

    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x, y;
        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;
        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
    }
    if (addr >= svga->vram_max)
        return 0xff;

    cycles -= svga->monitor->mon_video_timing_read_w;
    return *(uint16_t *) &svga->vram[addr & svga->vram_mask];
}

static uint32_t
v4_read_linear_l(uint32_t addr, void *priv)
{
    voodoo4_t      *v4     = (voodoo4_t *) priv;
    const voodoo_t *voodoo = v4->voodoo;
    svga_t         *svga   = &v4->svga;

    if (addr & 3)
        return v4_read_linear_w(addr, priv) | (v4_read_linear_w(addr + 2, priv) << 16);

    cycles -= voodoo->read_time;
    if ((v4->pci_regs[0x30] & 0x01) && addr >= v4->bios_rom.mapping.base && addr < (v4->bios_rom.mapping.base + v4->bios_rom.sz))
        return rom_readl(addr & (v4->bios_rom.sz - 1), &v4->bios_rom);

    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x, y;
        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;
        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
    }
    if (addr >= svga->vram_max)
        return 0xff;

    cycles -= svga->monitor->mon_video_timing_read_l;
    return *(uint32_t *) &svga->vram[addr & svga->vram_mask];
}

static void
v4_write_linear(uint32_t addr, uint8_t val, void *priv)
{
    voodoo4_t      *v4     = (voodoo4_t *) priv;
    const voodoo_t *voodoo = v4->voodoo;
    svga_t         *svga   = &v4->svga;
    static int lfb_write_count = 0;

    if (lfb_write_count < 32) {
        fprintf(stderr, "V4 LFB write: addr=0x%08x val=0x%02x (vram_addr=0x%08x)\n",
                addr, val, addr & svga->decode_mask);
        lfb_write_count++;
    }

    cycles -= voodoo->write_time;
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x, y;
        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;
        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
    }
    if (addr >= svga->vram_max)
        return;

    cycles -= svga->monitor->mon_video_timing_write_b;
    svga->changedvram[addr >> 12]      = changeframecount;
    svga->vram[addr & svga->vram_mask] = val;
}

static void
v4_write_linear_w(uint32_t addr, uint16_t val, void *priv)
{
    voodoo4_t      *v4     = (voodoo4_t *) priv;
    const voodoo_t *voodoo = v4->voodoo;
    svga_t         *svga   = &v4->svga;

    if (addr & 1) {
        v4_write_linear(addr, val, priv);
        v4_write_linear(addr + 1, val >> 8, priv);
        return;
    }

    cycles -= voodoo->write_time;
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x, y;
        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;
        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
    }
    if (addr >= svga->vram_max)
        return;

    cycles -= svga->monitor->mon_video_timing_write_w;
    svga->changedvram[addr >> 12]                        = changeframecount;
    *(uint16_t *) &svga->vram[addr & svga->vram_mask] = val;
}

static void
v4_write_linear_l(uint32_t addr, uint32_t val, void *priv)
{
    voodoo4_t      *v4     = (voodoo4_t *) priv;
    const voodoo_t *voodoo = v4->voodoo;
    svga_t         *svga   = &v4->svga;

    if (addr & 3) {
        v4_write_linear_w(addr, val, priv);
        v4_write_linear_w(addr + 2, val >> 16, priv);
        return;
    }

    cycles -= voodoo->write_time;
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x, y;
        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;
        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
    }
    if (addr >= svga->vram_max)
        return;

    cycles -= svga->monitor->mon_video_timing_write_l;
    svga->changedvram[addr >> 12]                        = changeframecount;
    *(uint32_t *) &svga->vram[addr & svga->vram_mask] = val;
}

/* ============================================================
 * MMIO register read/write — BAR0 (register space)
 * ============================================================ */

static uint32_t v4_reg_readl(uint32_t addr, void *priv);

static uint8_t
v4_reg_read(uint32_t addr, void *priv)
{
    return v4_reg_readl(addr & ~3, priv) >> (8 * (addr & 3));
}

static uint16_t
v4_reg_readw(uint32_t addr, void *priv)
{
    return v4_reg_readl(addr & ~3, priv) >> (8 * (addr & 2));
}

static uint32_t
v4_reg_readl(uint32_t addr, void *priv)
{
    voodoo4_t *v4     = (voodoo4_t *) priv;
    voodoo_t  *voodoo = v4->voodoo;
    uint32_t   ret    = 0xffffffff;

    cycles -= voodoo->read_time;

    switch (addr & 0x1f00000) {
        case 0x0000000: /* IO remap */
            if (!(addr & 0x80000))
                ret = v4_ext_inl(addr & 0xff, v4);
            break;

        case 0x0100000: /* 2D registers */
            voodoo_flush(voodoo);
            switch (addr & 0x1fc) {
                case SST_status:
                    ret = v4_status(v4);
                    break;
                case SST_intrCtrl:
                    ret = v4->intrCtrl & 0x0030003f;
                    break;
                case 0x08:
                    ret = voodoo->banshee_blt.clip0Min;
                    break;
                case 0x0c:
                    ret = voodoo->banshee_blt.clip0Max;
                    break;
                case 0x10:
                    ret = voodoo->banshee_blt.dstBaseAddr;
                    break;
                case 0x14:
                    ret = voodoo->banshee_blt.dstFormat;
                    break;
                case 0x34:
                    ret = voodoo->banshee_blt.srcBaseAddr;
                    break;
                case 0x38:
                    ret = voodoo->banshee_blt.commandExtra;
                    break;
                case 0x5c:
                    ret = voodoo->banshee_blt.srcXY;
                    break;
                case 0x60:
                    ret = voodoo->banshee_blt.colorBack;
                    break;
                case 0x64:
                    ret = voodoo->banshee_blt.colorFore;
                    break;
                case 0x68:
                    ret = voodoo->banshee_blt.dstSize;
                    break;
                case 0x6c:
                    ret = voodoo->banshee_blt.dstXY;
                    break;
                case 0x70:
                    ret = voodoo->banshee_blt.command;
                    break;
                default:
                    break;
            }
            break;

        case 0x0200000:
        case 0x0300000:
        case 0x0400000:
        case 0x0500000: /* 3D registers */
            switch (addr & 0x3fc) {
                case SST_status:
                    ret = v4_status(v4);
                    break;
                case SST_intrCtrl:
                    ret = v4->intrCtrl & 0x0030003f;
                    break;
                case SST_fbzColorPath:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.fbzColorPath;
                    break;
                case SST_fogMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.fogMode;
                    break;
                case SST_alphaMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.alphaMode;
                    break;
                case SST_fbzMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.fbzMode;
                    break;
                case SST_lfbMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->lfbMode;
                    break;
                case SST_clipLeftRight:
                    ret = voodoo->params.clipRight | (voodoo->params.clipLeft << 16);
                    break;
                case SST_clipLowYHighY:
                    ret = voodoo->params.clipHighY | (voodoo->params.clipLowY << 16);
                    break;
                case SST_clipLeftRight1:
                    ret = voodoo->params.clipRight1 | (voodoo->params.clipLeft1 << 16);
                    break;
                case SST_clipTopBottom1:
                    ret = voodoo->params.clipHighY1 | (voodoo->params.clipLowY1 << 16);
                    break;
                case SST_stipple:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.stipple;
                    break;
                case SST_color0:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.color0;
                    break;
                case SST_color1:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.color1;
                    break;
                case SST_fbiPixelsIn:
                    ret = voodoo->fbiPixelsIn & 0xffffff;
                    break;
                case SST_fbiChromaFail:
                    ret = voodoo->fbiChromaFail & 0xffffff;
                    break;
                case SST_fbiZFuncFail:
                    ret = voodoo->fbiZFuncFail & 0xffffff;
                    break;
                case SST_fbiAFuncFail:
                    ret = voodoo->fbiAFuncFail & 0xffffff;
                    break;
                case SST_fbiPixelsOut:
                    ret = voodoo->fbiPixelsOut & 0xffffff;
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }

    return ret;
}

static void
v4_reg_write(UNUSED(uint32_t addr), UNUSED(uint8_t val), UNUSED(void *priv))
{
    /* Byte writes to register space — ignored */
}

static void
v4_reg_writew(uint32_t addr, uint16_t val, void *priv)
{
    voodoo4_t *v4     = (voodoo4_t *) priv;
    voodoo_t  *voodoo = v4->voodoo;

    cycles -= voodoo->write_time;

    switch (addr & 0x1f00000) {
        case 0x1000000: case 0x1100000: case 0x1200000: case 0x1300000:
        case 0x1400000: case 0x1500000: case 0x1600000: case 0x1700000:
        case 0x1800000: case 0x1900000: case 0x1a00000: case 0x1b00000:
        case 0x1c00000: case 0x1d00000: case 0x1e00000: case 0x1f00000:
            voodoo_queue_command(voodoo, (addr & 0xffffff) | FIFO_WRITEW_FB, val);
            break;
        default:
            break;
    }
}

static void
v4_reg_writel(uint32_t addr, uint32_t val, void *priv)
{
    voodoo4_t *v4     = (voodoo4_t *) priv;
    voodoo_t  *voodoo = v4->voodoo;

    if (addr == voodoo->last_write_addr + 4)
        cycles -= voodoo->burst_time;
    else
        cycles -= voodoo->write_time;
    voodoo->last_write_addr = addr;

    switch (addr & 0x1f00000) {
        case 0x0000000: /* IO remap */
            if (!(addr & 0x80000))
                v4_ext_outl(addr & 0xff, val, v4);
            break;

        case 0x0100000: /* 2D registers */
            if ((addr & 0x3fc) == SST_intrCtrl) {
                v4->intrCtrl = val & 0x0030003f;
            } else {
                voodoo_queue_command(voodoo, (addr & 0x1fc) | FIFO_WRITEL_2DREG, val);
            }
            break;

        case 0x0200000:
        case 0x0300000:
        case 0x0400000:
        case 0x0500000: /* 3D registers */
            switch (addr & 0x3fc) {
                case SST_intrCtrl:
                    v4->intrCtrl = val & 0x0030003f;
                    break;
                case SST_userIntrCMD:
                    break;
                case SST_swapbufferCMD:
                    voodoo->cmd_written++;
                    voodoo_queue_command(voodoo, (addr & 0x3fc) | FIFO_WRITEL_REG, val);
                    if (!voodoo->voodoo_busy)
                        voodoo_wake_fifo_threads(voodoo->set, voodoo);
                    break;
                case SST_triangleCMD:
                case SST_ftriangleCMD:
                case SST_fastfillCMD:
                case SST_nopCMD:
                    voodoo->cmd_written++;
                    voodoo_queue_command(voodoo, (addr & 0x3fc) | FIFO_WRITEL_REG, val);
                    if (!voodoo->voodoo_busy)
                        voodoo_wake_fifo_threads(voodoo->set, voodoo);
                    break;
                case SST_swapPending:
                    thread_wait_mutex(voodoo->swap_mutex);
                    voodoo->swap_count++;
                    thread_release_mutex(voodoo->swap_mutex);
                    break;
                default:
                    voodoo_queue_command(voodoo, (addr & 0x3ffffc) | FIFO_WRITEL_REG, val);
                    break;
            }
            break;

        case 0x0600000:
        case 0x0700000: /* TMU0 Texture download */
            voodoo->tex_count++;
            voodoo_queue_command(voodoo, (addr & 0x1ffffc) | FIFO_WRITEL_TEX, val);
            break;

        case 0x1000000: case 0x1100000: case 0x1200000: case 0x1300000:
        case 0x1400000: case 0x1500000: case 0x1600000: case 0x1700000:
        case 0x1800000: case 0x1900000: case 0x1a00000: case 0x1b00000:
        case 0x1c00000: case 0x1d00000: case 0x1e00000: case 0x1f00000:
            voodoo_queue_command(voodoo, (addr & 0xfffffc) | FIFO_WRITEL_FB, val);
            break;

        default:
            break;
    }
}

/* ============================================================
 * Memory mapping update — BAR remap
 * ============================================================ */

static void
v4_updatemapping(voodoo4_t *v4)
{
    svga_t *svga = &v4->svga;

    if (!(v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&v4->linear_mapping);
        mem_mapping_disable(&v4->reg_mapping_low);
        mem_mapping_disable(&v4->reg_mapping_high);
        return;
    }

    switch (svga->gdcreg[6] & 0xc) {
        case 0x0:
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
            svga->banked_mask = 0xffff;
            break;
        case 0x4:
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
            svga->banked_mask = 0xffff;
            break;
        case 0x8:
            mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;
        case 0xC:
            mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;
        default:
            break;
    }

    if (v4->memBaseAddr1)
        mem_mapping_set_addr(&v4->linear_mapping, v4->memBaseAddr1, 32 << 20);
    else
        mem_mapping_disable(&v4->linear_mapping);
    if (v4->memBaseAddr0) {
        mem_mapping_set_addr(&v4->reg_mapping_low, v4->memBaseAddr0, 8 << 20);
        mem_mapping_set_addr(&v4->reg_mapping_high, v4->memBaseAddr0 + 0xc00000, 20 << 20);
    } else {
        mem_mapping_disable(&v4->reg_mapping_low);
        mem_mapping_disable(&v4->reg_mapping_high);
    }
    fprintf(stderr, "V4 updatemapping: BAR0=0x%08x BAR1=0x%08x (linear @ 0x%08x, 32MB)\n",
            v4->memBaseAddr0, v4->memBaseAddr1, v4->memBaseAddr1);
}

/* ============================================================
 * PCI configuration space
 * ============================================================ */

static uint8_t
v4_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    const voodoo4_t *v4  = (voodoo4_t *) priv;
    uint8_t          ret = 0;

    if (func) {
        fprintf(stderr, "V4 PCI read: func=%d reg=0x%02x val=0xff (non-zero func)\n", func, addr);
        return 0xff;
    }

    switch (addr) {
        /* Vendor ID: 0x121A (3dfx) */
        case 0x00: ret = 0x1a; break;
        case 0x01: ret = 0x12; break;

        /* Device ID: 0x0009 (VSA-100) */
        case 0x02: ret = 0x09; break;
        case 0x03: ret = 0x00; break;

        /* Command register (16-bit: 0x04-0x05) */
        case 0x04:
            ret = v4->pci_regs[0x04] & 0x27;
            break;
        case 0x05:
            ret = 0x00; /* Command high byte: no special bits */
            break;

        /* Status register (16-bit: 0x06-0x07)
         * Byte 0x06 bits: [4] = Capabilities List present
         * Byte 0x07 bits: [1] = medium DEVSEL, [5] = back-to-back capable */
        case 0x06: ret = 0x10; break; /* Capabilities List bit set */
        case 0x07: ret = v4->pci_regs[0x07] | 0x02; break; /* Medium DEVSEL timing */

        /* Revision: VSA-100 rev 1 */
        case 0x08: ret = 0x01; break;
        /* Prog IF */
        case 0x09: ret = 0x00; break;
        /* Sub-class: VGA compatible */
        case 0x0a: ret = 0x00; break;
        /* Base class: Display controller */
        case 0x0b: ret = 0x03; break;

        /* Cache line size */
        case 0x0c: ret = v4->pci_regs[0x0c]; break;
        /* Latency timer */
        case 0x0d: ret = v4->pci_regs[0x0d] & 0xf8; break;
        /* Header type: standard (type 0) */
        case 0x0e: ret = 0x00; break;

        /* BAR0: memory registers (32 MB) */
        case 0x10: ret = 0x00; break;
        case 0x11: ret = 0x00; break;
        case 0x12: ret = 0x00; break;
        case 0x13: ret = v4->memBaseAddr0 >> 24; break;

        /* BAR1: linear framebuffer (32 MB) */
        case 0x14: ret = 0x00; break;
        case 0x15: ret = 0x00; break;
        case 0x16: ret = 0x00; break;
        case 0x17: ret = v4->memBaseAddr1 >> 24; break;

        /* BAR2: I/O (256 bytes) */
        case 0x18: ret = 0x01; break;
        case 0x19: ret = v4->ioBaseAddr >> 8; break;
        case 0x1a: ret = v4->ioBaseAddr >> 16; break;
        case 0x1b: ret = v4->ioBaseAddr >> 24; break;

        /* Subsystem vendor ID: 0x121A (3dfx) */
        case 0x2c: ret = v4->pci_regs[0x2c]; break;
        case 0x2d: ret = v4->pci_regs[0x2d]; break;
        /* Subsystem device ID */
        case 0x2e: ret = v4->pci_regs[0x2e]; break;
        case 0x2f: ret = v4->pci_regs[0x2f]; break;

        /* Expansion ROM base address */
        case 0x30: ret = v4->pci_regs[0x30] & 0x01; break;
        case 0x31: ret = 0x00; break;
        case 0x32: ret = v4->pci_regs[0x32]; break;
        case 0x33: ret = v4->pci_regs[0x33]; break;

        /* Capabilities pointer -- AGP capability at 0x54 */
        case 0x34: ret = 0x54; break;

        /* Interrupt line */
        case 0x3c: ret = v4->pci_regs[0x3c]; break;
        /* Interrupt pin: INTA */
        case 0x3d: ret = 0x01; break;
        /* Min grant */
        case 0x3e: ret = 0x04; break;
        /* Max latency */
        case 0x3f: ret = 0xff; break;

        /* Voodoo-specific config at 0x40 */
        case 0x40: ret = 0x01; break;

        case 0x50: ret = v4->pci_regs[0x50]; break;

        /* AGP capability -- cap ID=0x02, next=0x60, AGP 2.0 */
        case 0x54: ret = 0x02; break; /* Cap ID: AGP */
        case 0x55: ret = 0x60; break; /* Next cap pointer: PM at 0x60 */
        case 0x56: ret = 0x20; break; /* AGP major=2, minor=0 */
        case 0x57: ret = 0x00; break; /* AGP version high byte */

        /* AGP status (0x58-0x5b): 4x/2x/1x, RQ depth=7 */
        case 0x58: ret = 0x27; break; /* SBA, 4x/2x/1x capable */
        case 0x59: ret = 0x02; break;
        case 0x5a: ret = 0x00; break;
        case 0x5b: ret = 0x07; break; /* RQ depth */

        /* AGP command (0x5c-0x5f) */
        case 0x5c: ret = v4->pci_regs[0x5c]; break;
        case 0x5d: ret = v4->pci_regs[0x5d]; break;
        case 0x5e: ret = v4->pci_regs[0x5e]; break;
        case 0x5f: ret = v4->pci_regs[0x5f]; break;

        /* Power management capability (0x60-0x63) */
        case 0x60: ret = 0x01; break; /* Cap ID: Power Management */
        case 0x61: ret = 0x00; break; /* Next cap pointer: end of chain */
        case 0x62: ret = 0x21; break; /* PM caps: D1/D2 not supported, PME from D3 */
        case 0x63: ret = 0x00; break; /* PM caps high byte */

        /* PM control/status (0x64-0x67) */
        case 0x64: ret = v4->pci_regs[0x64]; break;
        case 0x65: ret = v4->pci_regs[0x65]; break;
        case 0x66: ret = v4->pci_regs[0x66]; break;
        case 0x67: ret = v4->pci_regs[0x67]; break;

        default:
            break;
    }

    fprintf(stderr, "V4 PCI read: reg=0x%02x val=0x%02x\n", addr, ret);
    return ret;
}

static void
v4_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    voodoo4_t *v4 = (voodoo4_t *) priv;

    if (func)
        return;

    fprintf(stderr, "V4 PCI write: reg=0x%02x val=0x%02x\n", addr, val);

    switch (addr) {
        /* Read-only registers */
        case 0x00: case 0x01: case 0x02: case 0x03:
        case 0x06: case 0x08: case 0x09: case 0x0a: case 0x0b:
        case 0x0e: case 0x3d: case 0x3e: case 0x3f:
            return;

        case PCI_REG_COMMAND:
            if (val & PCI_COMMAND_IO) {
                io_removehandler(0x03c0, 0x0020, v4_in, NULL, NULL, v4_out, NULL, NULL, v4);
                if (v4->ioBaseAddr)
                    io_removehandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
                io_sethandler(0x03c0, 0x0020, v4_in, NULL, NULL, v4_out, NULL, NULL, v4);
                if (v4->ioBaseAddr)
                    io_sethandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            } else {
                io_removehandler(0x03c0, 0x0020, v4_in, NULL, NULL, v4_out, NULL, NULL, v4);
                io_removehandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            }
            v4->pci_regs[PCI_REG_COMMAND] = val & 0x27;
            /* VSA-100 ROM requires both IO and MEM enabled during init.
               Some BIOSes only enable IO before executing the option ROM.
               Force MEM on whenever IO is set so the ROM's device probe succeeds. */
            if (v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                v4->pci_regs[PCI_REG_COMMAND] |= PCI_COMMAND_MEM;
            fprintf(stderr, "V4 PCI: command=0x%02x (IO=%d MEM=%d)\n",
                    v4->pci_regs[PCI_REG_COMMAND],
                    !!(v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO),
                    !!(v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM));
            v4_updatemapping(v4);
            return;

        case 0x07:
            v4->pci_regs[0x07] = val & 0x3e;
            return;
        case 0x0c: /* Cache line size */
            v4->pci_regs[0x0c] = val;
            return;
        case 0x0d:
            v4->pci_regs[0x0d] = val & 0xf8;
            return;

        case 0x13: /* BAR0 high byte */
            v4->memBaseAddr0 = (val & 0xfe) << 24;
            fprintf(stderr, "V4 PCI: BAR0=0x%08x\n", v4->memBaseAddr0);
            v4_updatemapping(v4);
            return;

        case 0x17: /* BAR1 high byte */
            v4->memBaseAddr1 = (val & 0xfe) << 24;
            fprintf(stderr, "V4 PCI: BAR1=0x%08x\n", v4->memBaseAddr1);
            v4_updatemapping(v4);
            return;

        case 0x19: /* I/O BAR byte 1 */
            if (v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                io_removehandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            v4->ioBaseAddr &= 0xffff00ff;
            v4->ioBaseAddr |= val << 8;
            if ((v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && v4->ioBaseAddr)
                io_sethandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            fprintf(stderr, "V4 PCI: ioBaseAddr=0x%08x\n", v4->ioBaseAddr);
            return;

        case 0x1a:
            if (v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                io_removehandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            v4->ioBaseAddr &= 0xff00ffff;
            v4->ioBaseAddr |= val << 16;
            if ((v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && v4->ioBaseAddr)
                io_sethandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            fprintf(stderr, "V4 PCI: ioBaseAddr=0x%08x\n", v4->ioBaseAddr);
            return;
        case 0x1b:
            if (v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                io_removehandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            v4->ioBaseAddr &= 0x00ffffff;
            v4->ioBaseAddr |= val << 24;
            if ((v4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && v4->ioBaseAddr)
                io_sethandler(v4->ioBaseAddr, 0x0100, v4_ext_in, NULL, v4_ext_inl, v4_ext_out, NULL, v4_ext_outl, v4);
            fprintf(stderr, "V4 PCI: ioBaseAddr=0x%08x\n", v4->ioBaseAddr);
            return;

        /* Expansion ROM */
        case 0x30: case 0x32: case 0x33:
            if (!v4->has_bios)
                return;
            v4->pci_regs[addr] = val;
            if (v4->pci_regs[0x30] & 0x01) {
                uint32_t biosaddr = (v4->pci_regs[0x32] << 16) | (v4->pci_regs[0x33] << 24);
                fprintf(stderr, "V4 PCI: ROM enabled at 0x%08x\n", biosaddr);
                mem_mapping_set_addr(&v4->bios_rom.mapping, biosaddr, 0x10000);
                mem_mapping_enable(&v4->bios_rom.mapping);
            } else {
                fprintf(stderr, "V4 PCI: ROM disabled\n");
                mem_mapping_disable(&v4->bios_rom.mapping);
            }
            return;

        case 0x3c:
        case 0x50:
        case 0x65:
        case 0x67:
            v4->pci_regs[addr] = val;
            return;

        /* AGP command */
        case 0x5c:
            v4->pci_regs[0x5c] = val & 0x27;
            return;
        case 0x5d:
            v4->pci_regs[0x5d] = val & 0x03;
            return;
        case 0x5f:
            v4->pci_regs[0x5f] = val;
            return;

        /* Power management */
        case 0x64:
            v4->pci_regs[0x64] = val & 0x03;
            return;
        case 0x66:
            v4->pci_regs[0x66] = val & 0xc0;
            return;

        default:
            fprintf(stderr, "V4 PCI write: UNHANDLED reg=0x%02x val=0x%02x\n", addr, val);
            break;
    }
}

/* ============================================================
 * 16-to-32 color conversion for SVGA display
 * ============================================================ */

static uint32_t
v4_conv_16to32(svga_t *svga, uint16_t color, UNUSED(uint8_t bpp))
{
    voodoo4_t *v4 = (voodoo4_t *) svga->priv;
    uint32_t   ret;
    uint16_t   src_b = (color & 0x1f) << 3;
    uint16_t   src_g = (color & 0x7e0) >> 3;
    uint16_t   src_r = (color & 0xf800) >> 8;

    if (v4->vidProcCfg & V4_VIDPROCCFG_DESKTOP_CLUT_SEL) {
        src_b += 256;
        src_g += 256;
        src_r += 256;
    }

    if (svga->lut_map) {
        uint8_t b = getcolr(svga->pallook[src_b]);
        uint8_t g = getcolg(svga->pallook[src_g]);
        uint8_t r = getcolb(svga->pallook[src_r]);
        ret = (video_16to32[color] & 0xFF000000) | makecol(r, g, b);
    } else
        ret = video_16to32[color];

    return ret;
}

/* ============================================================
 * Init / Close / Available
 * ============================================================ */

static void *
v4_4500_agp_init(const device_t *info)
{
    int        mem_size = 32; /* 32 MB SDRAM */
    voodoo4_t *v4       = malloc(sizeof(voodoo4_t));
    memset(v4, 0, sizeof(voodoo4_t));

    v4->has_bios = 1;
    v4->chroma_key_enabled = device_get_config_int("chromakey");

    /* STEP 1: Initialize BIOS ROM */
    int rom_ret = rom_init(&v4->bios_rom, ROM_VOODOO4_4500, 0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
    if (rom_ret < 0)
        v4->has_bios = 0;
    mem_mapping_disable(&v4->bios_rom.mapping);

    /* STEP 2: Initialize SVGA base class */
    svga_init(info, &v4->svga, v4, mem_size << 20,
              v4_recalctimings,
              v4_in, v4_out,
              v4_hwcursor_draw,
              v4_overlay_draw);
    v4->svga.vsync_callback = v4_vsync_callback;

    /* STEP 3: Add memory mappings */
    mem_mapping_add(&v4->linear_mapping, 0, 0,
                    v4_read_linear, v4_read_linear_w, v4_read_linear_l,
                    v4_write_linear, v4_write_linear_w, v4_write_linear_l,
                    NULL, MEM_MAPPING_EXTERNAL, &v4->svga);
    mem_mapping_add(&v4->reg_mapping_low, 0, 0,
                    v4_reg_read, v4_reg_readw, v4_reg_readl,
                    v4_reg_write, v4_reg_writew, v4_reg_writel,
                    NULL, MEM_MAPPING_EXTERNAL, v4);
    mem_mapping_add(&v4->reg_mapping_high, 0, 0,
                    v4_reg_read, v4_reg_readw, v4_reg_readl,
                    v4_reg_write, v4_reg_writew, v4_reg_writel,
                    NULL, MEM_MAPPING_EXTERNAL, v4);

    /* STEP 4: Callbacks and initial state */
    v4->svga.vblank_start = v4_vblank_start;
    v4->svga.bpp          = 8;
    v4->svga.miscout      = 1;

    /* DRAM init: 32 MB SDRAM */
    v4->dramInit0 = (1 << 27);
    v4->dramInit1 = (1 << 30); /* SDRAM */
    v4->svga.decode_mask = 0x1ffffff;

    /* STEP 5: Register PCI card as AGP */
    pci_add_card(PCI_ADD_AGP, v4_pci_read, v4_pci_write, v4, &v4->pci_slot);

    /* STEP 6: Initialize Voodoo core — use VOODOO_3 pipeline for now */
    v4->voodoo               = voodoo_2d3d_card_init(VOODOO_3);
    v4->voodoo->priv         = v4;
    v4->voodoo->vram         = v4->svga.vram;
    v4->voodoo->changedvram  = v4->svga.changedvram;
    v4->voodoo->fb_mem       = v4->svga.vram;
    v4->voodoo->fb_mask      = v4->svga.vram_mask;
    v4->voodoo->tex_mem[0]   = v4->svga.vram;
    v4->voodoo->tex_mem_w[0] = (uint16_t *) v4->svga.vram;
    v4->voodoo->tex_mem[1]   = v4->svga.vram;
    v4->voodoo->tex_mem_w[1] = (uint16_t *) v4->svga.vram;
    v4->voodoo->texture_mask = v4->svga.vram_mask;
    v4->voodoo->cmd_status   = (1 << 28);
    v4->voodoo->cmd_status_2 = (1 << 28);

    /* Set tile_base to the top of VRAM so that no LFB address triggers
     * tile remapping until lfbMemoryConfig is explicitly programmed.
     * With tile_base=0 (from memset), every LFB write would go through
     * the degenerate tile formula, scattering pixel data across VRAM
     * and causing display corruption in linear (non-tiled) modes. */
    v4->voodoo->tile_base = (uint32_t)(mem_size << 20);

    voodoo_generate_filter_v1(v4->voodoo);

    /* STEP 7: I2C/DDC */
    v4->vidSerialParallelPort = V4_VIDSERIAL_DDC_DCK_W | V4_VIDSERIAL_DDC_DDA_W;
    v4->i2c     = i2c_gpio_init("i2c_voodoo4");
    v4->i2c_ddc = i2c_gpio_init("ddc_voodoo4");
    v4->ddc     = ddc_init(i2c_gpio_get_bus(v4->i2c_ddc));

    v4->svga.conv_16to32 = v4_conv_16to32;

    /* Subsystem IDs: 3dfx Voodoo 4 4500 */
    v4->pci_regs[0x2c] = 0x1a; /* subsystem vendor: 0x121A */
    v4->pci_regs[0x2d] = 0x12;
    v4->pci_regs[0x2e] = 0x04; /* subsystem device: 0x0004 */
    v4->pci_regs[0x2f] = 0x00;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_v4_agp);

    return v4;
}

static void
v4_close(void *priv)
{
    voodoo4_t *v4 = (voodoo4_t *) priv;

    voodoo_card_close(v4->voodoo);
    svga_close(&v4->svga);
    ddc_close(v4->ddc);
    i2c_gpio_close(v4->i2c_ddc);
    i2c_gpio_close(v4->i2c);

    free(v4);
}

static int
v4_4500_agp_available(void)
{
    return rom_present(ROM_VOODOO4_4500);
}

static void
v4_speed_changed(void *priv)
{
    voodoo4_t *v4 = (voodoo4_t *) priv;
    svga_recalctimings(&v4->svga);
}

static void
v4_force_redraw(void *priv)
{
    voodoo4_t *v4 = (voodoo4_t *) priv;
    v4->svga.fullchange = changeframecount;
}

/* ============================================================
 * Device configuration
 * ============================================================ */

// clang-format off
static const device_config_t v4_4500_config[] = {
    {
        .name           = "bilinear",
        .description    = "Bilinear filtering",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "chromakey",
        .description    = "Video chroma-keying",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dithersub",
        .description    = "Dither subtraction",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dacfilter",
        .description    = "Screen Filter",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = "4", .value = 4 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
#ifndef NO_CODEGEN
    {
        .name           = "recompiler",
        .description    = "Dynamic Recompiler",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
    {
        .name           = "jit_debug",
        .description    = "JIT Debug Logging",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

/* ============================================================
 * Device definition
 * ============================================================ */

const device_t voodoo_4_4500_agp_device = {
    .name          = "3dfx Voodoo4 4500 AGP",
    .internal_name = "voodoo4_4500_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v4_4500_agp_init,
    .close         = v4_close,
    .reset         = NULL,
    .available     = v4_4500_agp_available,
    .speed_changed = v4_speed_changed,
    .force_redraw  = v4_force_redraw,
    .config        = v4_4500_config
};
