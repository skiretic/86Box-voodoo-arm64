/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA Riva 128 (NV3) / Riva 128 ZX (NV3T) emulation.
 *
 *          Phase 1: Device skeleton, PCI config, VGA boot.
 *          Provides enough to boot DOS with VGA text mode output.
 *
 *          Sources:
 *            - envytools rnndb register database
 *            - PCBox/86Box-nv reference implementation
 *            - rivafb riva_tbl.h golden init tables
 *            - xf86-video-nv riva_hw.c
 *
 *
 * Authors: skiretic
 *
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#include "vid_nv3.h"

/* ========================================================================
 * ROM paths for each card variant
 * ======================================================================== */
#define ROM_NV3_ELSA_ERAZOR     "roms/video/nvidia/nv3/VCERAZOR.BIN"
#define ROM_NV3_DIAMOND_V330    "roms/video/nvidia/nv3/diamond_v330_rev-e.vbi"
#define ROM_NV3T_DIAMOND_V330   "roms/video/nvidia/nv3/nv3t182b.rom"
#define ROM_NV3T_REFERENCE      "roms/video/nvidia/nv3/vgasgram.rom"

/* ========================================================================
 * Logging
 * ======================================================================== */
#ifdef ENABLE_NV3_LOG
int nv3_do_log = ENABLE_NV3_LOG;

static void
nv3_log(const char *fmt, ...)
{
    va_list ap;

    if (nv3_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nv3_log(fmt, ...)
#endif

/* ========================================================================
 * Video timing constants (placeholder values from Banshee)
 * ======================================================================== */
static video_timings_t timing_nv3_pci = {
    .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1,
    .read_b = 20, .read_w = 20, .read_l = 21
};

/* ========================================================================
 * Forward declarations
 * ======================================================================== */
static void     nv3_recalctimings(svga_t *svga);
static uint8_t  nv3_svga_in(uint16_t addr, void *priv);
static void     nv3_svga_out(uint16_t addr, uint8_t val, void *priv);
static void     nv3_update_mappings(nv3_t *nv3);

static uint8_t  nv3_pci_read(int func, int addr, int len, void *priv);
static void     nv3_pci_write(int func, int addr, int len, uint8_t val, void *priv);

static uint8_t  nv3_mmio_read(uint32_t addr, void *priv);
static uint16_t nv3_mmio_readw(uint32_t addr, void *priv);
static uint32_t nv3_mmio_readl(uint32_t addr, void *priv);
static void     nv3_mmio_write(uint32_t addr, uint8_t val, void *priv);
static void     nv3_mmio_writew(uint32_t addr, uint16_t val, void *priv);
static void     nv3_mmio_writel(uint32_t addr, uint32_t val, void *priv);

static uint8_t  nv3_lfb_read(uint32_t addr, void *priv);
static uint16_t nv3_lfb_readw(uint32_t addr, void *priv);
static uint32_t nv3_lfb_readl(uint32_t addr, void *priv);
static void     nv3_lfb_write(uint32_t addr, uint8_t val, void *priv);
static void     nv3_lfb_writew(uint32_t addr, uint16_t val, void *priv);
static void     nv3_lfb_writel(uint32_t addr, uint32_t val, void *priv);

/* ========================================================================
 * MMIO register read (32-bit, internal)
 *
 * All MMIO registers are 32-bit aligned internally. Byte/word reads
 * extract the appropriate portion.
 *
 * Phase 1: Most registers return stub/default values to allow VGA BIOS
 * to initialize. Real subsystem handling comes in Phase 2+.
 * ======================================================================== */
static uint32_t
nv3_mmio_read_internal(nv3_t *nv3, uint32_t addr)
{
    uint32_t ret = 0;

    addr &= 0xFFFFFF; /* mask to 16MB MMIO space */

    switch (addr) {
        /* PMC - Master Control */
        case NV3_PMC_BOOT_0:
            ret = nv3->pmc.boot_0;
            break;
        case NV3_PMC_INTR_0:
            ret = nv3->pmc.intr_0;
            break;
        case NV3_PMC_INTR_EN_0:
            ret = nv3->pmc.intr_en_0;
            break;
        case NV3_PMC_ENABLE:
            ret = nv3->pmc.enable;
            break;

        /* PBUS - Bus Control */
        case NV3_PBUS_INTR_0:
            ret = nv3->pbus.intr_0;
            break;
        case NV3_PBUS_INTR_EN_0:
            ret = nv3->pbus.intr_en_0;
            break;

        /* PFB - Framebuffer Interface */
        case NV3_PFB_BOOT_0:
            ret = nv3->pfb.boot_0;
            break;
        case NV3_PFB_CONFIG_0:
            ret = nv3->pfb.config_0;
            break;
        case NV3_PFB_CONFIG_1:
            ret = nv3->pfb.config_1;
            break;

        /* PEXTDEV - Straps */
        case NV3_PEXTDEV_STRAPS:
            ret = nv3->pextdev.straps;
            break;

        /* PTIMER */
        case NV3_PTIMER_INTR_0:
            ret = nv3->ptimer.intr_0;
            break;
        case NV3_PTIMER_INTR_EN_0:
            ret = nv3->ptimer.intr_en_0;
            break;
        case NV3_PTIMER_NUMERATOR:
            ret = nv3->ptimer.numerator;
            break;
        case NV3_PTIMER_DENOMINATOR:
            ret = nv3->ptimer.denominator;
            break;
        case NV3_PTIMER_TIME_0:
            ret = nv3->ptimer.time_0;
            break;
        case NV3_PTIMER_TIME_1:
            ret = nv3->ptimer.time_1;
            break;
        case NV3_PTIMER_ALARM_0:
            ret = nv3->ptimer.alarm_0;
            break;

        /* PBUS PCI config mirror (0x1800-0x18FF) */
        default:
            if (addr >= NV3_PBUS_PCI_START && addr <= NV3_PBUS_PCI_END) {
                /* Mirror of PCI config space */
                int pci_reg = addr - NV3_PBUS_PCI_START;
                if (pci_reg < 256) {
                    ret = nv3->pci_regs[pci_reg & ~3]
                        | (nv3->pci_regs[(pci_reg & ~3) + 1] << 8)
                        | (nv3->pci_regs[(pci_reg & ~3) + 2] << 16)
                        | (nv3->pci_regs[(pci_reg & ~3) + 3] << 24);
                }
            } else {
                nv3_log("NV3: MMIO read32 unknown addr=0x%06x\n", addr);
            }
            break;
    }

    return ret;
}

/* ========================================================================
 * MMIO register write (32-bit, internal)
 * ======================================================================== */
static void
nv3_mmio_write_internal(nv3_t *nv3, uint32_t addr, uint32_t val)
{
    addr &= 0xFFFFFF;

    switch (addr) {
        /* PMC */
        case NV3_PMC_INTR_0:
            /* Write 1 to clear pending interrupts */
            nv3->pmc.intr_0 &= ~val;
            break;
        case NV3_PMC_INTR_EN_0:
            nv3->pmc.intr_en_0 = val;
            break;
        case NV3_PMC_ENABLE:
            nv3->pmc.enable = val;
            break;

        /* PBUS */
        case NV3_PBUS_INTR_0:
            nv3->pbus.intr_0 &= ~val;
            break;
        case NV3_PBUS_INTR_EN_0:
            nv3->pbus.intr_en_0 = val;
            break;

        /* PFB */
        case NV3_PFB_CONFIG_0:
            nv3->pfb.config_0 = val;
            break;
        case NV3_PFB_CONFIG_1:
            nv3->pfb.config_1 = val;
            break;

        /* PEXTDEV - straps are read-only, writes ignored */

        /* PTIMER */
        case NV3_PTIMER_INTR_0:
            nv3->ptimer.intr_0 &= ~val;
            break;
        case NV3_PTIMER_INTR_EN_0:
            nv3->ptimer.intr_en_0 = val;
            break;
        case NV3_PTIMER_NUMERATOR:
            nv3->ptimer.numerator = val;
            break;
        case NV3_PTIMER_DENOMINATOR:
            nv3->ptimer.denominator = val;
            break;
        case NV3_PTIMER_TIME_0:
            nv3->ptimer.time_0 = val;
            break;
        case NV3_PTIMER_TIME_1:
            nv3->ptimer.time_1 = val;
            break;
        case NV3_PTIMER_ALARM_0:
            nv3->ptimer.alarm_0 = val;
            break;

        default:
            if (addr >= NV3_PBUS_PCI_START && addr <= NV3_PBUS_PCI_END) {
                /* PCI config mirror - forward to PCI write logic */
                int pci_reg = addr - NV3_PBUS_PCI_START;
                if (pci_reg < 256) {
                    nv3_pci_write(0, pci_reg, 1, val & 0xFF, nv3);
                    nv3_pci_write(0, pci_reg + 1, 1, (val >> 8) & 0xFF, nv3);
                    nv3_pci_write(0, pci_reg + 2, 1, (val >> 16) & 0xFF, nv3);
                    nv3_pci_write(0, pci_reg + 3, 1, (val >> 24) & 0xFF, nv3);
                }
            } else {
                nv3_log("NV3: MMIO write32 unknown addr=0x%06x val=0x%08x\n", addr, val);
            }
            break;
    }
}

/* ========================================================================
 * Determine if MMIO address should redirect to SVGA I/O
 *
 * Per NV3 architecture, certain MMIO address ranges map directly to the
 * Weitek VGA I/O core:
 *   - PRMVIO (0x0C0000-0x0C0400): VGA sequencer/misc registers
 *   - PRMCIO (0x601000-0x601FFF): CRTC registers
 *   - USER_DAC (0x681200-0x681FFF): DAC/palette registers
 * ======================================================================== */
static bool
nv3_is_svga_mmio_addr(uint32_t addr)
{
    return (addr >= NV3_PRMVIO_START && addr <= NV3_PRMVIO_END)
        || (addr >= NV3_PRMCIO_START && addr <= NV3_PRMCIO_END)
        || (addr >= NV3_USER_DAC_START && addr <= NV3_USER_DAC_END);
}

/* ========================================================================
 * BAR0 MMIO read handlers (byte, word, dword)
 * ======================================================================== */
static uint8_t
nv3_mmio_read(uint32_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    uint32_t off = addr & 0xFFFFFF;

    /* Check for SVGA I/O redirect */
    if (nv3_is_svga_mmio_addr(off)) {
        uint16_t vga_addr = off & 0x3FF;
        return nv3_svga_in(vga_addr, nv3);
    }

    uint32_t val32 = nv3_mmio_read_internal(nv3, off & ~3);
    return (uint8_t) (val32 >> ((off & 3) * 8));
}

static uint16_t
nv3_mmio_readw(uint32_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    uint32_t off = addr & 0xFFFFFF;

    if (nv3_is_svga_mmio_addr(off)) {
        uint16_t vga_addr = off & 0x3FF;
        return nv3_svga_in(vga_addr, nv3)
             | (nv3_svga_in(vga_addr + 1, nv3) << 8);
    }

    uint32_t val32 = nv3_mmio_read_internal(nv3, off & ~3);
    return (uint16_t) (val32 >> ((off & 3) * 8));
}

static uint32_t
nv3_mmio_readl(uint32_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    uint32_t off = addr & 0xFFFFFF;

    if (nv3_is_svga_mmio_addr(off)) {
        uint16_t vga_addr = off & 0x3FF;
        return nv3_svga_in(vga_addr, nv3)
             | (nv3_svga_in(vga_addr + 1, nv3) << 8)
             | (nv3_svga_in(vga_addr + 2, nv3) << 16)
             | (nv3_svga_in(vga_addr + 3, nv3) << 24);
    }

    return nv3_mmio_read_internal(nv3, off);
}

/* ========================================================================
 * BAR0 MMIO write handlers
 * ======================================================================== */
static void
nv3_mmio_write(uint32_t addr, uint8_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    uint32_t off = addr & 0xFFFFFF;

    if (nv3_is_svga_mmio_addr(off)) {
        uint16_t vga_addr = off & 0x3FF;
        nv3_svga_out(vga_addr, val, nv3);
        return;
    }

    /* Read-modify-write for sub-dword access */
    uint32_t old = nv3_mmio_read_internal(nv3, off & ~3);
    int      shift = (off & 3) * 8;
    uint32_t mask  = 0xFF << shift;
    uint32_t new_val = (old & ~mask) | ((uint32_t) val << shift);
    nv3_mmio_write_internal(nv3, off & ~3, new_val);
}

static void
nv3_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    uint32_t off = addr & 0xFFFFFF;

    if (nv3_is_svga_mmio_addr(off)) {
        uint16_t vga_addr = off & 0x3FF;
        nv3_svga_out(vga_addr, val & 0xFF, nv3);
        nv3_svga_out(vga_addr + 1, (val >> 8) & 0xFF, nv3);
        return;
    }

    uint32_t old = nv3_mmio_read_internal(nv3, off & ~3);
    int      shift = (off & 3) * 8;
    uint32_t mask  = 0xFFFF << shift;
    uint32_t new_val = (old & ~mask) | ((uint32_t) val << shift);
    nv3_mmio_write_internal(nv3, off & ~3, new_val);
}

static void
nv3_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    uint32_t off = addr & 0xFFFFFF;

    if (nv3_is_svga_mmio_addr(off)) {
        uint16_t vga_addr = off & 0x3FF;
        nv3_svga_out(vga_addr, val & 0xFF, nv3);
        nv3_svga_out(vga_addr + 1, (val >> 8) & 0xFF, nv3);
        nv3_svga_out(vga_addr + 2, (val >> 16) & 0xFF, nv3);
        nv3_svga_out(vga_addr + 3, (val >> 24) & 0xFF, nv3);
        return;
    }

    nv3_mmio_write_internal(nv3, off, val);
}

/* ========================================================================
 * BAR1 Linear Framebuffer read/write handlers
 *
 * Simple dumb framebuffer access for Phase 1.
 * Masks address to VRAM size, reads/writes SVGA VRAM directly.
 * ======================================================================== */
static uint8_t
nv3_lfb_read(uint32_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    addr &= svga->vram_mask;
    return svga->vram[addr];
}

static uint16_t
nv3_lfb_readw(uint32_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    addr &= svga->vram_mask;
    return *(uint16_t *) &svga->vram[addr];
}

static uint32_t
nv3_lfb_readl(uint32_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    addr &= svga->vram_mask;
    return *(uint32_t *) &svga->vram[addr];
}

static void
nv3_lfb_write(uint32_t addr, uint8_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    addr &= svga->vram_mask;
    svga->vram[addr]             = val;
    svga->changedvram[addr >> 12] = changeframecount;
}

static void
nv3_lfb_writew(uint32_t addr, uint16_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    addr &= svga->vram_mask;
    *(uint16_t *) &svga->vram[addr] = val;
    svga->changedvram[addr >> 12]   = changeframecount;
}

static void
nv3_lfb_writel(uint32_t addr, uint32_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    addr &= svga->vram_mask;
    *(uint32_t *) &svga->vram[addr] = val;
    svga->changedvram[addr >> 12]   = changeframecount;
}

/* ========================================================================
 * VGA I/O read handler (port 0x3C0-0x3DF)
 *
 * Handles NVIDIA extended CRTC registers. Standard VGA registers are
 * passed through to svga_in().
 *
 * Per envytools NV3 VGA docs and the 86Box-nv reference implementation.
 * ======================================================================== */
static uint8_t
nv3_svga_in(uint16_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;
    uint8_t ret  = 0;

    /* Handle RMA (Real Mode Access) window at 0x3D0-0x3D3 */
    if (addr >= NV3_RMA_ADDR_START && addr <= NV3_RMA_ADDR_END) {
        if (!(nv3->rma_mode & 0x01))
            return ret;

        /* Phase 1: stub - RMA reads return 0 */
        return ret;
    }

    /* Remap 3Bx/3Dx based on miscout bit 0 (monochrome/color) */
    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0)
        && addr < 0x3DE && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3D4:
            /* CRTC index register */
            ret = svga->crtcreg;
            break;

        case 0x3D5:
            /* CRTC data register - handle extended NV3 registers */
            switch (svga->crtcreg) {
                case NV3_CRTC_RL0:
                    /* Read-only: low byte of current scanline */
                    ret = svga->displine & 0xFF;
                    break;

                case NV3_CRTC_RL1:
                    /* Read-only: high bits of current scanline */
                    ret = (svga->displine >> 8) & 0x07;
                    break;

                case NV3_CRTC_I2C_READ:
                    /* I2C/DDC read-back */
                    if (nv3->i2c) {
                        ret = (i2c_gpio_get_sda(nv3->i2c) << 3)
                            | (i2c_gpio_get_scl(nv3->i2c) << 2);
                    }
                    break;

                default:
                    /* Standard CRTC register or other NV3 extended reg */
                    ret = svga->crtc[svga->crtcreg];
                    break;
            }
            break;

        case 0x3D8:
            /*
             * Per 86Box-nv: returning 0x08 here prevents freezes
             * with certain NV3 driver versions.
             */
            ret = 0x08;
            break;

        default:
            /* Pass to standard SVGA handler */
            ret = svga_in(addr, svga);
            break;
    }

    return ret;
}

/* ========================================================================
 * VGA I/O write handler (port 0x3C0-0x3DF)
 *
 * Handles NVIDIA extended CRTC registers, sequencer lock, and bank
 * switching. Standard VGA writes are forwarded to svga_out().
 * ======================================================================== */
static void
nv3_svga_out(uint16_t addr, uint8_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    /* Handle RMA (Real Mode Access) window at 0x3D0-0x3D3 */
    if (addr >= NV3_RMA_ADDR_START && addr <= NV3_RMA_ADDR_END) {
        nv3->rma_regs[addr & 3] = val;
        if (!(nv3->rma_mode & 0x01))
            return;

        /* Phase 1: stub - RMA writes are ignored */
        return;
    }

    /* Remap 3Bx/3Dx based on miscout bit 0 */
    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0)
        && addr < 0x3DE && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C4:
            /* Sequencer index register - pass through */
            svga_out(addr, val, svga);
            break;

        case 0x3C5:
            /*
             * Sequencer data register.
             * Index 0x06: NV3 lock register.
             *   Write 0x57 to unlock extended registers.
             *   Write anything else (typically 0x99) to lock them.
             */
            if (svga->seqaddr == 0x06) {
                /* NV3 sequencer lock/unlock per envytools NV3 VGA docs */
                if (val == 0x57)
                    svga->seqregs[0x06] = 0x57; /* unlocked */
                else
                    svga->seqregs[0x06] = 0x99; /* locked */
                return;
            }
            svga_out(addr, val, svga);
            break;

        case 0x3D4:
            /* CRTC index register */
            svga->crtcreg = val;
            break;

        case 0x3D5: {
            /* CRTC data register - handle extended NV3 registers */
            uint8_t crtcreg  = svga->crtcreg;
            uint8_t old_val  = svga->crtc[crtcreg];

            /*
             * VGA protect: if CRTC[0x11] bit 7 is set, registers 0x00-0x07
             * are read-only (except bit 4 of register 0x07).
             */
            if (crtcreg < 0x07 && (svga->crtc[0x11] & 0x80))
                return;
            if (crtcreg == 0x07 && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[0x07] & ~0x10) | (val & 0x10);

            /* Store the value */
            svga->crtc[crtcreg] = val;

            /* Act on extended NV3 CRTC registers */
            switch (crtcreg) {
                case NV3_CRTC_READ_BANK:
                    /* Extended read bank selection */
                    nv3->cio_read_bank = val;
                    if (svga->chain4)
                        svga->read_bank = (uint32_t) nv3->cio_read_bank << 15;
                    else
                        svga->read_bank = (uint32_t) nv3->cio_read_bank << 13;
                    break;

                case NV3_CRTC_WRITE_BANK:
                    /* Extended write bank selection */
                    nv3->cio_write_bank = val;
                    if (svga->chain4)
                        svga->write_bank = (uint32_t) nv3->cio_write_bank << 15;
                    else
                        svga->write_bank = (uint32_t) nv3->cio_write_bank << 13;
                    break;

                case NV3_CRTC_RMA:
                    /* Real Mode Access mode register */
                    nv3->rma_mode = val & NV3_RMA_MODE_MAX;
                    break;

                case NV3_CRTC_I2C_WRITE:
                    /* I2C/DDC GPIO write */
                    if (nv3->i2c) {
                        uint8_t scl = !!(val & 0x20);
                        uint8_t sda = !!(val & 0x10);
                        i2c_gpio_set(nv3->i2c, scl, sda);
                    }
                    break;

                default:
                    break;
            }

            /* Recalculate timings if a timing-related register changed */
            if (old_val != val) {
                if (crtcreg < 0x0E || crtcreg > 0x10) {
                    svga->fullchange = changeframecount;
                    svga_recalctimings(svga);
                }
            }
            break;
        }

        default:
            /* Pass to standard SVGA handler */
            svga_out(addr, val, svga);
            break;
    }
}

/* ========================================================================
 * Recalculate video timings.
 *
 * Phase 1: Minimal implementation for VGA text mode boot.
 * Handles extended CRTC registers for row offset, pixel depth,
 * and 10-bit counter extensions.
 *
 * Full PLL calculation and PRAMDAC integration comes in Phase 2.
 * ======================================================================== */
static void
nv3_recalctimings(svga_t *svga)
{
    nv3_t   *nv3       = (nv3_t *) svga->priv;
    uint32_t pixel_mode = svga->crtc[NV3_CRTC_PIXEL_MODE] & 0x03;

    /*
     * Extended display buffer start address.
     * CRTC 0x19 (RPC0) bits 4:0 provide bits 20:16 of the start address.
     */
    svga->memaddr_latch += (svga->crtc[NV3_CRTC_REPAINT0] & 0x1F) << 16;

    /*
     * Override the standard SVGA rendering path when not in VGA text mode.
     * In graphical modes, the NV3 uses its own rendering pipeline
     * (handled in later phases).
     */
    svga->override = (pixel_mode != NV3_PIXEL_MODE_VGA);

    /* Set pixel format and adjust row offset */
    switch (pixel_mode) {
        case NV3_PIXEL_MODE_8BPP:
            svga->rowoffset += (svga->crtc[NV3_CRTC_REPAINT0] & 0xE0) << 1;
            svga->bpp    = 8;
            svga->lowres = 0;
            svga->map8   = svga->pallook;
            break;

        case NV3_PIXEL_MODE_16BPP:
            svga->rowoffset += (svga->crtc[NV3_CRTC_REPAINT0] & 0xE0) << 3;
            svga->bpp    = 16;
            svga->lowres = 0;
            break;

        case NV3_PIXEL_MODE_32BPP:
            svga->rowoffset += (svga->crtc[NV3_CRTC_REPAINT0] & 0xE0) << 3;
            svga->bpp    = 32;
            svga->lowres = 0;
            break;

        default:
            /* VGA text/standard mode - use default SVGA settings */
            break;
    }

    /*
     * Extended 10-bit vertical counters.
     * CRTC 0x25 (FORMAT) provides bit 10 for vtotal, dispend,
     * vblank start, vsync start.
     */
    uint8_t fmt = svga->crtc[NV3_CRTC_FORMAT];
    if (fmt & (1 << NV3_CRTC_FORMAT_VDT10))
        svga->vtotal += 0x400;
    if (fmt & (1 << NV3_CRTC_FORMAT_VDE10))
        svga->dispend += 0x400;
    if (fmt & (1 << NV3_CRTC_FORMAT_VRS10))
        svga->vblankstart += 0x400;
    if (fmt & (1 << NV3_CRTC_FORMAT_VBS10))
        svga->vsyncstart += 0x400;

    /*
     * Extended horizontal: CRTC 0x2D bit 0 adds 0x100 to hdisp.
     */
    if (svga->crtc[NV3_CRTC_HEB] & 0x01)
        svga->hdisp += 0x100;
}

/* ========================================================================
 * Update PCI BAR memory mappings after config change.
 *
 * Called whenever PCI command register or BAR addresses change.
 * Sets up the MMIO (BAR0), framebuffer (BAR1), and banked VGA
 * memory windows.
 * ======================================================================== */
static void
nv3_update_mappings(nv3_t *nv3)
{
    svga_t *svga = &nv3->svga;

    /* Disable all mappings by default */
    mem_mapping_disable(&nv3->mmio_mapping);
    mem_mapping_disable(&nv3->lfb_mapping);

    /* Handle VGA I/O enable/disable */
    io_removehandler(0x03c0, 0x0020,
                     nv3_svga_in, NULL, NULL,
                     nv3_svga_out, NULL, NULL, nv3);

    if (nv3->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) {
        io_sethandler(0x03c0, 0x0020,
                      nv3_svga_in, NULL, NULL,
                      nv3_svga_out, NULL, NULL, nv3);
    }

    if (!(nv3->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))
        return;

    /* BAR0: MMIO */
    if (nv3->bar0_base) {
        mem_mapping_set_addr(&nv3->mmio_mapping, nv3->bar0_base, NV3_MMIO_SIZE);
    }

    /* BAR1: Linear framebuffer + RAMIN */
    if (nv3->bar1_base) {
        mem_mapping_set_addr(&nv3->lfb_mapping, nv3->bar1_base, nv3->vram_size);
    }

    /* Banked VGA window */
    switch (svga->gdcreg[0x06] & 0x0C) {
        case NV3_BANKED_128K_A0000:
            mem_mapping_set_addr(&svga->mapping, 0xA0000, 0x20000);
            svga->banked_mask = 0x1FFFF;
            break;
        case NV3_BANKED_64K_A0000:
            mem_mapping_set_addr(&svga->mapping, 0xA0000, 0x10000);
            svga->banked_mask = 0xFFFF;
            break;
        case NV3_BANKED_32K_B0000:
            mem_mapping_set_addr(&svga->mapping, 0xB0000, 0x8000);
            svga->banked_mask = 0x7FFF;
            break;
        case NV3_BANKED_32K_B8000:
            mem_mapping_set_addr(&svga->mapping, 0xB8000, 0x8000);
            svga->banked_mask = 0x7FFF;
            break;
    }
}

/* ========================================================================
 * PCI config space read handler.
 *
 * Returns the correct values for the NV3/NV3T PCI configuration space.
 * Vendor 0x12D2, Device 0x0018 (NV3) or 0x0019 (NV3T).
 * Both BARs are 16MB, prefetchable.
 * ======================================================================== */
static uint8_t
nv3_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    nv3_t  *nv3 = (nv3_t *) priv;
    uint8_t ret = 0;

    switch (addr) {
        /* Vendor ID: 0x12D2 (SGS-Thomson/NVidia) */
        case 0x00:
            ret = NV3_PCI_VENDOR_ID & 0xFF;
            break;
        case 0x01:
            ret = NV3_PCI_VENDOR_ID >> 8;
            break;

        /* Device ID: 0x0018 (NV3) or 0x0019 (NV3T) */
        case 0x02:
            ret = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                ? (NV3_PCI_DEVICE_ID_NV3T & 0xFF)
                : (NV3_PCI_DEVICE_ID_NV3 & 0xFF);
            break;
        case 0x03:
            ret = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                ? (NV3_PCI_DEVICE_ID_NV3T >> 8)
                : (NV3_PCI_DEVICE_ID_NV3 >> 8);
            break;

        /* Command register */
        case PCI_REG_COMMAND:
            ret = nv3->pci_regs[PCI_REG_COMMAND];
            break;
        case PCI_REG_COMMAND + 1:
            ret = nv3->pci_regs[PCI_REG_COMMAND + 1];
            break;

        /* Status register */
        case 0x06:
            ret = nv3->pci_regs[0x06];
            break;
        case 0x07:
            ret = nv3->pci_regs[0x07];
            break;

        /* Revision ID */
        case 0x08:
            ret = nv3->gpu_revision;
            break;

        /* Programming interface */
        case 0x09:
            ret = 0x00;
            break;

        /* Subclass: VGA-compatible (0x00) */
        case 0x0A:
            ret = NV3_PCI_SUBCLASS_CODE;
            break;

        /* Class: Display (0x03) */
        case 0x0B:
            ret = NV3_PCI_CLASS_CODE;
            break;

        /* Cache line size */
        case 0x0C:
            ret = 0x40;
            break;

        /* Latency timer */
        case 0x0D:
            ret = nv3->pci_regs[0x0D];
            break;

        /* Header type */
        case 0x0E:
            ret = 0x00;
            break;

        /* BIST */
        case 0x0F:
            ret = 0x00;
            break;

        /*
         * BAR0: 16MB MMIO, prefetchable.
         * Bits 31:24 = base address, bits 23:4 hardwired to 0.
         * Bit 3 = prefetchable = 1.
         * Bits 2:1 = type 00 (32-bit), bit 0 = memory space = 0.
         */
        case 0x10:
            ret = 0x08; /* prefetchable, 32-bit, memory */
            break;
        case 0x11:
        case 0x12:
            ret = 0x00;
            break;
        case 0x13:
            ret = nv3->bar0_base >> 24;
            break;

        /* BAR1: 16MB framebuffer, prefetchable */
        case 0x14:
            ret = 0x08; /* prefetchable, 32-bit, memory */
            break;
        case 0x15:
        case 0x16:
            ret = 0x00;
            break;
        case 0x17:
            ret = nv3->bar1_base >> 24;
            break;

        /* BAR2-BAR5: not used, hardwired to 0 */
        case 0x18 ... 0x27:
            ret = 0x00;
            break;

        /* Subsystem vendor/device ID */
        case 0x2C:
        case 0x2D:
        case 0x2E:
        case 0x2F:
            ret = nv3->pci_regs[addr];
            break;

        /* Expansion ROM BAR */
        case 0x30:
            ret = nv3->pci_regs[0x30] & 0x01;
            break;
        case 0x31:
            ret = 0x00;
            break;
        case 0x32:
            ret = nv3->pci_regs[0x32];
            break;
        case 0x33:
            ret = nv3->pci_regs[0x33];
            break;

        /* Interrupt line */
        case 0x3C:
            ret = nv3->pci_regs[0x3C];
            break;

        /* Interrupt pin: INTA */
        case 0x3D:
            ret = PCI_INTA;
            break;

        /* Min grant */
        case 0x3E:
            ret = 0x03;
            break;

        /* Max latency */
        case 0x3F:
            ret = 0x01;
            break;

        default:
            ret = nv3->pci_regs[addr];
            break;
    }

    return ret;
}

/* ========================================================================
 * PCI config space write handler.
 * ======================================================================== */
static void
nv3_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    nv3_t *nv3 = (nv3_t *) priv;

    switch (addr) {
        /* Read-only registers */
        case 0x00 ... 0x03:  /* Vendor/device ID */
        case 0x08 ... 0x0B:  /* Rev/class */
        case 0x0E:           /* Header type */
        case 0x0F:           /* BIST */
        case 0x18 ... 0x27:  /* Unused BARs */
        case 0x3D ... 0x3F:  /* Interrupt pin, min grant, max latency */
            return;

        /* Command register */
        case PCI_REG_COMMAND:
            nv3->pci_regs[PCI_REG_COMMAND] = val;
            nv3_update_mappings(nv3);
            return;
        case PCI_REG_COMMAND + 1:
            nv3->pci_regs[PCI_REG_COMMAND + 1] = val;
            return;

        /* Status register - write 1 to clear */
        case 0x07:
            nv3->pci_regs[0x07] &= ~(val & 0x30);
            return;

        /* Latency timer */
        case 0x0D:
            nv3->pci_regs[0x0D] = val;
            return;

        /* BAR0 base address (only top byte is writable for 16MB alignment) */
        case 0x13:
            nv3->bar0_base = (uint32_t) val << 24;
            nv3_update_mappings(nv3);
            return;

        /* BAR1 base address */
        case 0x17:
            nv3->bar1_base = (uint32_t) val << 24;
            nv3_update_mappings(nv3);
            return;

        /* Expansion ROM BAR */
        case 0x30:
        case 0x32:
        case 0x33:
            nv3->pci_regs[addr] = val;
            if (nv3->pci_regs[0x30] & 0x01) {
                uint32_t rom_addr = (nv3->pci_regs[0x32] << 16)
                                  | (nv3->pci_regs[0x33] << 24);
                mem_mapping_set_addr(&nv3->bios_rom.mapping, rom_addr, 0x8000);
            } else {
                mem_mapping_disable(&nv3->bios_rom.mapping);
            }
            return;

        /* Interrupt line */
        case 0x3C:
            nv3->pci_regs[0x3C] = val;
            return;

        default:
            nv3->pci_regs[addr] = val;
            return;
    }
}

/* ========================================================================
 * Initialize PFB_BOOT_0 register based on VRAM size.
 *
 * Per envytools and 86Box-nv reference: this register encodes RAM size,
 * bus width, and bank count. The BIOS reads this to detect memory config.
 * ======================================================================== */
static void
nv3_pfb_init(nv3_t *nv3)
{
    uint32_t boot_0 = 0;

    switch (nv3->vram_size) {
        case NV3_VRAM_SIZE_2MB:
            boot_0 = (NV3_PFB_BOOT_RAM_AMOUNT_2MB << NV3_PFB_BOOT_RAM_AMOUNT_SHIFT)
                    | (NV3_PFB_BOOT_RAM_WIDTH_64 << NV3_PFB_BOOT_RAM_WIDTH_SHIFT)
                    | (NV3_PFB_BOOT_RAM_BANKS_2 << NV3_PFB_BOOT_RAM_BANKS_SHIFT);
            break;
        case NV3_VRAM_SIZE_4MB:
            boot_0 = (NV3_PFB_BOOT_RAM_AMOUNT_4MB << NV3_PFB_BOOT_RAM_AMOUNT_SHIFT)
                    | (NV3_PFB_BOOT_RAM_WIDTH_128 << NV3_PFB_BOOT_RAM_WIDTH_SHIFT)
                    | (NV3_PFB_BOOT_RAM_BANKS_4 << NV3_PFB_BOOT_RAM_BANKS_SHIFT);
            break;
        case NV3_VRAM_SIZE_8MB:
            boot_0 = (NV3_PFB_BOOT_RAM_AMOUNT_8MB << NV3_PFB_BOOT_RAM_AMOUNT_SHIFT)
                    | (NV3_PFB_BOOT_RAM_WIDTH_128 << NV3_PFB_BOOT_RAM_WIDTH_SHIFT)
                    | (NV3_PFB_BOOT_RAM_BANKS_4 << NV3_PFB_BOOT_RAM_BANKS_SHIFT);
            break;
        default:
            boot_0 = (NV3_PFB_BOOT_RAM_AMOUNT_4MB << NV3_PFB_BOOT_RAM_AMOUNT_SHIFT);
            break;
    }

    nv3->pfb.boot_0 = boot_0;
}

/* ========================================================================
 * Initialize PEXTDEV straps based on card type.
 * ======================================================================== */
static void
nv3_pextdev_init(nv3_t *nv3)
{
    uint32_t straps = 0;

    /* BIOS present */
    straps |= (1 << NV3_STRAPS_BIOS_PRESENT);

    /* 14.318MHz crystal (standard) */
    straps |= (1 << NV3_STRAPS_CRYSTAL);

    /* No TV output */
    straps |= (0x3 << NV3_STRAPS_TVMODE_SHIFT);

    /* Bus type */
    if (nv3->is_agp) {
        straps |= (1 << NV3_STRAPS_BUS_TYPE);
    }

    /* 128-bit bus width for 4MB+ */
    if (nv3->vram_size >= NV3_VRAM_SIZE_4MB) {
        straps |= (1 << NV3_STRAPS_BUS_WIDTH);
    }

    nv3->pextdev.straps = straps;
}

/* ========================================================================
 * Device init function.
 *
 * Creates the nv3_t, initializes SVGA, PCI, memory mappings,
 * ROM, and all subsystem default state.
 * ======================================================================== */
static void *
nv3_init(const device_t *info)
{
    nv3_t *nv3 = calloc(1, sizeof(nv3_t));
    if (!nv3)
        return NULL;

    nv3->card_type = info->local;

    /* Determine VRAM size and revision based on card type */
    switch (nv3->card_type) {
        case NV3_TYPE_NV3_PCI:
            nv3->vram_size   = NV3_VRAM_SIZE_4MB;
            nv3->gpu_revision = NV3_PCI_REV_B00;
            nv3->is_agp      = 0;
            break;
        case NV3_TYPE_NV3_AGP:
            nv3->vram_size   = NV3_VRAM_SIZE_4MB;
            nv3->gpu_revision = NV3_PCI_REV_B00;
            nv3->is_agp      = 1;
            break;
        case NV3_TYPE_NV3T_PCI:
            nv3->vram_size   = NV3_VRAM_SIZE_8MB;
            nv3->gpu_revision = NV3_PCI_REV_C00;
            nv3->is_agp      = 0;
            break;
        case NV3_TYPE_NV3T_AGP:
            nv3->vram_size   = NV3_VRAM_SIZE_8MB;
            nv3->gpu_revision = NV3_PCI_REV_C00;
            nv3->is_agp      = 1;
            break;
        default:
            free(nv3);
            return NULL;
    }

    nv3_log("NV3: init card_type=%d vram=%dMB rev=0x%02x agp=%d\n",
            nv3->card_type, nv3->vram_size >> 20, nv3->gpu_revision, nv3->is_agp);

    /* Determine ROM path based on card type */
    const char *rom_fn;
    if (nv3->card_type >= NV3_TYPE_NV3T_PCI)
        rom_fn = ROM_NV3T_REFERENCE;
    else
        rom_fn = ROM_NV3_ELSA_ERAZOR;

    /* Load BIOS ROM: 32KB mapped at 0xC0000 */
    rom_init(&nv3->bios_rom, rom_fn, 0xC0000, 0x8000, 0x7FFF, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&nv3->bios_rom.mapping);

    /* Initialize SVGA core */
    svga_init(info, &nv3->svga, nv3, nv3->vram_size,
              nv3_recalctimings,
              nv3_svga_in, nv3_svga_out,
              NULL,  /* hwcursor_draw - Phase 2 */
              NULL); /* overlay_draw - not used */

    /* Set decode mask to VRAM size */
    nv3->svga.decode_mask = nv3->vram_size - 1;
    nv3->svga.bpp         = 8;
    nv3->svga.miscout     = 1;

    /* Initialize memory mappings (initially disabled) */
    mem_mapping_add(&nv3->mmio_mapping, 0, 0,
                    nv3_mmio_read, nv3_mmio_readw, nv3_mmio_readl,
                    nv3_mmio_write, nv3_mmio_writew, nv3_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, nv3);

    mem_mapping_add(&nv3->lfb_mapping, 0, 0,
                    nv3_lfb_read, nv3_lfb_readw, nv3_lfb_readl,
                    nv3_lfb_write, nv3_lfb_writew, nv3_lfb_writel,
                    nv3->svga.vram, MEM_MAPPING_EXTERNAL, &nv3->svga);

    /* Set up VGA I/O handlers */
    io_sethandler(0x03c0, 0x0020,
                  nv3_svga_in, NULL, NULL,
                  nv3_svga_out, NULL, NULL, nv3);

    /* Add PCI card */
    pci_add_card(nv3->is_agp ? PCI_ADD_AGP : PCI_ADD_NORMAL,
                 nv3_pci_read, nv3_pci_write, nv3, &nv3->pci_slot);

    /* Initialize PCI command register defaults */
    nv3->pci_regs[PCI_REG_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEM;
    nv3->pci_regs[0x3C]           = 0xFF; /* Interrupt line: unassigned */

    /* Initialize GPU subsystems */

    /* PMC - Set boot register based on chip revision */
    switch (nv3->gpu_revision) {
        case NV3_PCI_REV_A00:
            nv3->pmc.boot_0 = NV3_BOOT_REG_REV_A00;
            break;
        case NV3_PCI_REV_B00:
            nv3->pmc.boot_0 = NV3_BOOT_REG_REV_B00;
            break;
        case NV3_PCI_REV_C00:
            nv3->pmc.boot_0 = NV3_BOOT_REG_REV_C00;
            break;
        default:
            nv3->pmc.boot_0 = NV3_BOOT_REG_REV_B00;
            break;
    }

    /* Enable all subsystems by default */
    nv3->pmc.enable = NV3_PMC_ENABLE_PMEDIA | NV3_PMC_ENABLE_PFIFO
                    | NV3_PMC_ENABLE_PGRAPH | NV3_PMC_ENABLE_PPMI
                    | NV3_PMC_ENABLE_PFB | NV3_PMC_ENABLE_PCRTC
                    | NV3_PMC_ENABLE_PVIDEO;

    /* PFB - Memory config */
    nv3_pfb_init(nv3);

    /* PEXTDEV - Straps */
    nv3_pextdev_init(nv3);

    /* PTIMER defaults */
    nv3->ptimer.numerator   = 1;
    nv3->ptimer.denominator = 1;

    /* I2C / DDC */
    nv3->i2c = i2c_gpio_init("nv3_i2c");
    nv3->ddc = ddc_init(i2c_gpio_get_bus(nv3->i2c));

    /* Inform video subsystem of our timing class */
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_nv3_pci);

    nv3_log("NV3: init complete\n");
    return nv3;
}

/* ========================================================================
 * Device close function.
 * ======================================================================== */
static void
nv3_close(void *priv)
{
    nv3_t *nv3 = (nv3_t *) priv;

    if (!nv3)
        return;

    svga_close(&nv3->svga);

    if (nv3->ddc)
        ddc_close(nv3->ddc);
    if (nv3->i2c)
        i2c_gpio_close(nv3->i2c);

    free(nv3);
}

/* ========================================================================
 * Speed changed callback.
 * ======================================================================== */
static void
nv3_speed_changed(void *priv)
{
    nv3_t *nv3 = (nv3_t *) priv;

    if (!nv3)
        return;

    svga_recalctimings(&nv3->svga);
}

/* ========================================================================
 * Force redraw callback.
 * ======================================================================== */
static void
nv3_force_redraw(void *priv)
{
    nv3_t *nv3 = (nv3_t *) priv;

    if (!nv3)
        return;

    nv3->svga.fullchange = changeframecount;
}

/* ========================================================================
 * ROM availability check functions.
 * ======================================================================== */
static int
nv3_available(void)
{
    return rom_present(ROM_NV3_ELSA_ERAZOR);
}

static int
nv3t_available(void)
{
    return rom_present(ROM_NV3T_REFERENCE);
}

/* ========================================================================
 * Device configuration options.
 * ======================================================================== */
static const device_config_t nv3_config[] = {
    { .name = "", .description = "", .type = CONFIG_END }
};

/* ========================================================================
 * device_t definitions for Riva 128 PCI and Riva 128 ZX PCI.
 *
 * These are the Phase 1 entries. AGP variants can be added in Phase 8.
 * ======================================================================== */
const device_t nv3_device_pci = {
    .name          = "NVIDIA Riva 128 PCI",
    .internal_name = "riva128_pci",
    .flags         = DEVICE_PCI,
    .local         = NV3_TYPE_NV3_PCI,
    .init          = nv3_init,
    .close         = nv3_close,
    .reset         = NULL,
    .available     = nv3_available,
    .speed_changed = nv3_speed_changed,
    .force_redraw  = nv3_force_redraw,
    .config        = nv3_config
};

const device_t nv3t_device_pci = {
    .name          = "NVIDIA Riva 128 ZX PCI",
    .internal_name = "riva128zx_pci",
    .flags         = DEVICE_PCI,
    .local         = NV3_TYPE_NV3T_PCI,
    .init          = nv3_init,
    .close         = nv3_close,
    .reset         = NULL,
    .available     = nv3t_available,
    .speed_changed = nv3_speed_changed,
    .force_redraw  = nv3_force_redraw,
    .config        = nv3_config
};
