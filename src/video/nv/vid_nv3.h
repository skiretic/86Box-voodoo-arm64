/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA Riva 128 (NV3) private header.
 *          Defines the nv3_t struct and subsystem state.
 *
 *
 * Authors: skiretic
 *
 */
#ifndef VID_NV3_H
#define VID_NV3_H

#include <stdint.h>
#include <stdbool.h>
#include <86box/vid_svga.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/nv/vid_nv3_regs.h>

/*
 * NV3 chip variant enum.
 * Determines PCI device ID, VRAM, revision, and feature set.
 */
enum {
    NV3_TYPE_NV3_PCI = 0,   /* Riva 128, PCI, 4MB */
    NV3_TYPE_NV3_AGP,       /* Riva 128, AGP, 4MB */
    NV3_TYPE_NV3T_PCI,      /* Riva 128 ZX, PCI, 8MB */
    NV3_TYPE_NV3T_AGP,      /* Riva 128 ZX, AGP, 8MB */
};

/*
 * PMC (Chip Master Control) state.
 * Per envytools pmc.xml.
 */
typedef struct nv3_pmc_s {
    uint32_t boot_0;         /* Chip ID register (read-only) */
    uint32_t intr_0;         /* Interrupt status (aggregated, read-only) */
    uint32_t intr_en_0;      /* Interrupt enable (0=disabled, 1=hardware, 2=software) */
    uint32_t enable;         /* Subsystem enable bitmask */
} nv3_pmc_t;

/*
 * PBUS (Bus Control) state.
 *
 * PBUS spans 0x001000-0x001FFF (4KB = 1024 dwords).
 * The regs[] array provides a general register bank for driver readback
 * of registers not explicitly handled (e.g., 0x001200 debug registers).
 * Interrupt registers have dedicated fields.
 */
typedef struct nv3_pbus_s {
    uint32_t intr_0;         /* Interrupt status */
    uint32_t intr_en_0;      /* Interrupt enable */
    uint32_t regs[1024];     /* General register bank for unhandled PBUS regs */
} nv3_pbus_t;

/*
 * PFB (Framebuffer Interface) state.
 */
typedef struct nv3_pfb_s {
    uint32_t boot_0;         /* Memory config (size, width, banks) */
    uint32_t config_0;       /* Framebuffer config register 0 */
    uint32_t config_1;       /* Framebuffer config register 1 */
} nv3_pfb_t;

/*
 * PEXTDEV (External Devices / Straps) state.
 */
typedef struct nv3_pextdev_s {
    uint32_t straps;         /* Board configuration straps */
} nv3_pextdev_t;

/*
 * PTIMER (Programmable Interval Timer) state.
 *
 * The NV3 PTIMER provides a monotonically increasing 64-bit nanosecond
 * counter (TIME_1:TIME_0) that increments at a rate determined by
 * NUMERATOR/DENOMINATOR relative to the core crystal clock.
 *
 * When TIME reaches ALARM_0, bit 0 of INTR_0 is set, which propagates
 * to PMC_INTR_0 bit 20 (PTIMER).
 *
 * Per envytools nv1-clock.xml and pmc.xml.
 */
typedef struct nv3_ptimer_s {
    uint32_t intr_0;
    uint32_t intr_en_0;
    uint32_t numerator;
    uint32_t denominator;
    uint32_t time_0;         /* Low 32 bits of time in nanoseconds */
    uint32_t time_1;         /* High 32 bits of time */
    uint32_t alarm_0;
} nv3_ptimer_t;

/*
 * PCRTC (Display Controller) state.
 *
 * Per envytools nv3_pcrtc.xml:
 *   INTR (0x600100) — bit 0 is VBLANK interrupt pending.
 *   INTR_EN (0x600140) — bit 0 enables VBLANK interrupt.
 *   CONFIG (0x600200) — display config (interlace, etc.).
 *   START (0x600800) — framebuffer start address for scanout.
 */
typedef struct nv3_pcrtc_s {
    uint32_t intr;           /* VBlank interrupt status, bit 0 = VBLANK */
    uint32_t intr_en;        /* VBlank interrupt enable, bit 0 = VBLANK_EN */
    uint32_t config;         /* Display configuration */
    uint32_t start_addr;     /* Framebuffer scanout start address */
    uint32_t regs[1024];     /* General register bank for unhandled regs */
} nv3_pcrtc_t;

/*
 * PRAMDAC (DAC / PLL / Cursor) state.
 *
 * Per envytools nv3_pramdac.xml:
 *   DLL (0x680500): DLL PLL coefficients (NV3-specific, NVPLL on NV4+)
 *   MPLL (0x680504): Memory clock PLL coefficients
 *   VPLL (0x680508): Pixel clock PLL coefficients [M, N, P]
 *   PLL_CONTROL (0x68050C): PLL programming and clock source select
 *   PLL_SETUP_CONTROL (0x680510): PLL setup parameters
 *   CURSOR_POS (0x680300): Cursor X/Y position
 *   GENERAL_CONTROL (0x680600): DAC mode, pixel format
 *
 * PLL formula: Freq = (crystal * N) / (M * (1 << P))
 * where crystal is 13.5MHz or 14.318MHz selected by straps.
 *
 * The regs[] array provides a general register bank for unhandled
 * PRAMDAC registers (driver readback). PRAMDAC spans 0x680000-0x680FFF
 * (4KB = 1024 dwords). Explicitly handled registers (PLLs, general_control)
 * have dedicated fields and are NOT stored in the bank.
 */
typedef struct nv3_pramdac_s {
    uint32_t nvpll_coeff;    /* DLL/core clock PLL coefficients (0x500) */
    uint32_t mpll_coeff;     /* Memory clock PLL coefficients (0x504) */
    uint32_t vpll_coeff;     /* Pixel clock PLL coefficients (0x508) */
    uint32_t pll_control;    /* PLL programming mode and clock source (0x50C) */
    uint32_t pll_setup;      /* PLL setup control (0x510) */
    uint32_t general_control; /* DAC/display control (0x600) */
    uint32_t cursor_pos;     /* Cursor X/Y position (0x300) */
    uint32_t regs[1024];     /* General register bank for unhandled regs */
} nv3_pramdac_t;

/*
 * PGRAPH (2D/3D Graphics Engine) state.
 * Stub for Phase 1; will be expanded in Phase 4/5.
 *
 * The regs[] array provides a general register bank for driver readback.
 * PGRAPH spans 0x400000-0x400FFF (4KB = 1024 dwords). Registers handled
 * explicitly in the switch statement (INTR_0, INTR_EN_0) are returned
 * from their dedicated fields; all other registers go through the bank.
 */
typedef struct nv3_pgraph_s {
    uint32_t intr_0;
    uint32_t intr_en_0;
    uint32_t debug_0;
    uint32_t debug_1;
    uint32_t debug_2;
    uint32_t debug_3;
    uint32_t regs[1024];
} nv3_pgraph_t;

/*
 * PFIFO (Command FIFO) state.
 * Stub for Phase 1; will be expanded in Phase 3.
 *
 * The regs[] array provides a general register bank for driver readback.
 * PFIFO spans 0x002000-0x003FFF (8KB = 2048 dwords). Registers handled
 * explicitly in the switch statement (INTR_0, INTR_EN_0) are returned
 * from their dedicated fields; all other registers go through the bank.
 */
typedef struct nv3_pfifo_s {
    uint32_t intr_0;
    uint32_t intr_en_0;
    uint32_t regs[2048];
} nv3_pfifo_t;

/*
 * Main NV3 device structure.
 *
 * CRITICAL: svga_t MUST be the first member so that the 86Box SVGA layer
 * can cast nv3_t* <-> svga_t* transparently. This is the standard pattern
 * used by all SVGA-based video drivers in 86Box (see S3 ViRGE, ATI Mach64,
 * Cirrus GD543x, etc.).
 */
typedef struct nv3_s {
    svga_t   svga;              /* MUST be first member */

    rom_t    bios_rom;          /* Video BIOS ROM */

    uint8_t  pci_regs[256];     /* PCI config space shadow */
    uint8_t  pci_slot;          /* PCI slot number assigned by 86Box */
    uint8_t  pci_irq_state;     /* Current PCI IRQ line state */

    int      card_type;         /* NV3_TYPE_* enum value */
    int      is_agp;            /* True if AGP bus */
    uint32_t gpu_revision;      /* PCI revision ID (0x00, 0x10, 0x20) */

    uint32_t vram_size;         /* Total VRAM in bytes */

    /*
     * Crystal oscillator frequency in Hz.
     * Determined by PEXTDEV straps bit 6: 13.5MHz or 14.318MHz.
     */
    uint32_t crystal_freq;

    /* PCI BAR base addresses (host physical) */
    uint32_t bar0_base;         /* BAR0: MMIO base */
    uint32_t bar1_base;         /* BAR1: Linear framebuffer base */

    /* Memory mappings */
    mem_mapping_t mmio_mapping;           /* BAR0 MMIO region */
    mem_mapping_t lfb_mapping;            /* BAR1 linear framebuffer */
    mem_mapping_t lfb_ramin_mapping;      /* RAMIN window within BAR1 */

    /* Extended VGA state (not in svga_t) */
    uint32_t cio_read_bank;     /* Extended read bank register (CRTC 0x1D) */
    uint32_t cio_write_bank;    /* Extended write bank register (CRTC 0x1E) */

    /* RMA (Real Mode Access) - CRTC 0x38-based MMIO access */
    uint8_t  rma_mode;          /* RMA mode register */
    uint8_t  rma_regs[4];      /* RMA data staging registers */

    /* GPU subsystem state */
    nv3_pmc_t     pmc;
    nv3_pbus_t    pbus;
    nv3_pfb_t     pfb;
    nv3_pextdev_t pextdev;
    nv3_ptimer_t  ptimer;
    nv3_pcrtc_t   pcrtc;
    nv3_pramdac_t pramdac;
    nv3_pgraph_t  pgraph;
    nv3_pfifo_t   pfifo;

    /* I2C / DDC for monitor EDID */
    void *i2c;
    void *ddc;

} nv3_t;

/* device_t declarations for vid_table.c */
extern const device_t nv3_device_pci;
extern const device_t nv3t_device_pci;

#endif /* VID_NV3_H */
