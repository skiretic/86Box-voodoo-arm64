/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA Riva 128 (NV3) register address defines.
 *
 *          Sources:
 *            - envytools rnndb XML register database
 *            - PCBox/86Box-nv reference implementation
 *            - rivafb riva_tbl.h golden init tables
 *            - xf86-video-nv riva_hw.c
 *
 *
 * Authors: skiretic
 *
 */
#ifndef VID_NV3_REGS_H
#define VID_NV3_REGS_H

/* ========================================================================
 * PCI Configuration Space
 * Vendor 0x12D2 (SGS-Thomson/NVidia), Device 0x0018 (NV3) / 0x0019 (NV3T)
 * ======================================================================== */
#define NV3_PCI_VENDOR_ID         0x12D2
#define NV3_PCI_DEVICE_ID_NV3    0x0018
#define NV3_PCI_DEVICE_ID_NV3T   0x0019

/* PCI revision IDs per chip stepping */
#define NV3_PCI_REV_A00           0x00   /* Engineering sample, Jan 1997 */
#define NV3_PCI_REV_B00           0x10   /* Production Riva 128, Sep 1997 */
#define NV3_PCI_REV_C00           0x20   /* NV3T / Riva 128 ZX, 1998 */

/* PCI class: 0x03 = Display, subclass 0x00 = VGA */
#define NV3_PCI_CLASS_CODE        0x03
#define NV3_PCI_SUBCLASS_CODE     0x00

/* PCI BAR sizes -- both BARs are 16MB and prefetchable */
#define NV3_BAR0_SIZE             0x01000000  /* 16MB MMIO */
#define NV3_BAR1_SIZE             0x01000000  /* 16MB framebuffer */
#define NV3_MMIO_SIZE             NV3_BAR0_SIZE

/* VRAM sizes */
#define NV3_VRAM_SIZE_2MB         0x200000
#define NV3_VRAM_SIZE_4MB         0x400000
#define NV3_VRAM_SIZE_8MB         0x800000

/* ========================================================================
 * Boot / Chip ID register (PMC_BOOT_0, offset 0x000000)
 * ======================================================================== */
#define NV3_PMC_BOOT_0            0x000000
#define NV3_BOOT_REG_REV_A00      0x00030100
#define NV3_BOOT_REG_REV_B00      0x00030110
#define NV3_BOOT_REG_REV_C00      0x00030120

/* ========================================================================
 * GPU Subsystem MMIO Address Ranges
 * Per envytools docs and PCBox/86Box-nv reference
 * ======================================================================== */

/* PMC - Chip Master Control */
#define NV3_PMC_START             0x000000
#define NV3_PMC_INTR_0            0x000100
#define NV3_PMC_INTR_EN_0         0x000140
#define NV3_PMC_ENABLE            0x000200
#define NV3_PMC_END               0x000FFF

/* PBUS - Bus Control */
#define NV3_PBUS_START            0x001000
#define NV3_PBUS_INTR_0           0x001100
#define NV3_PBUS_INTR_EN_0        0x001140
#define NV3_PBUS_PCI_START        0x001800
#define NV3_PBUS_PCI_END          0x0018FF
#define NV3_PBUS_END              0x001FFF

/* PFIFO - Command FIFO */
#define NV3_PFIFO_START           0x002000
#define NV3_PFIFO_INTR_0          0x002100
#define NV3_PFIFO_INTR_EN_0       0x002140

/*
 * PFIFO_INTR_0 / INTR_EN_0 bit definitions.
 *
 * Per envytools nv1_pfifo.xml:
 *   bit 0 = CACHE_ERROR  — invalid method/object error
 *   bit 4 = RUNOUT       — RAMRO runout buffer overflow
 *   bit 8 = RUNOUT_OVERFLOW — RAMRO overflow (fatal)
 *   bit 12 = DMA_PUSHER  — DMA pusher error
 *   bit 16 = DMA_PT      — DMA page table error (NV3+)
 */
#define NV3_PFIFO_INTR_CACHE_ERROR    (1 << 0)
#define NV3_PFIFO_INTR_RUNOUT         (1 << 4)
#define NV3_PFIFO_INTR_RUNOUT_OVERFLOW (1 << 8)
#define NV3_PFIFO_INTR_DMA_PUSHER    (1 << 12)
#define NV3_PFIFO_INTR_DMA_PT        (1 << 16)

/*
 * PFIFO configuration registers.
 * Per envytools nv1_pfifo.xml.
 */
#define NV3_PFIFO_DELAY_0         0x002040   /* Access delay (debug) */
#define NV3_PFIFO_DMA_TIMESLICE   0x002044   /* DMA timeslice (debug) */
#define NV3_PFIFO_CONFIG          0x002200   /* PFIFO mode config */
#define NV3_PFIFO_RAMHT           0x002210   /* Hash table config */
#define NV3_PFIFO_RAMFC           0x002214   /* FIFO context config */
#define NV3_PFIFO_RAMRO           0x002218   /* Runout area config */

/*
 * PFIFO_CONFIG register bit definitions.
 *
 * Per envytools nv1_pfifo.xml (NV3 variant):
 *   bits [7:0] = per-channel DMA/PIO mode select (1 bit per channel).
 *                0 = PIO mode, 1 = DMA mode.
 *   NV3 supports 8 channels (bits 7:0 used).
 */

/*
 * PFIFO_RAMHT register bit definitions.
 *
 * Per envytools nv1_pfifo.xml:
 *   bits [3:0] = RAMHT base address in RAMIN (in 4KB units).
 *                Address = value * 0x1000.
 *   bits [17:16] = RAMHT size selector:
 *     0 = 4KB   (1024 entries, 4 bytes per entry)
 *     1 = 8KB   (2048 entries)
 *     2 = 16KB  (4096 entries)
 *     3 = 32KB  (8192 entries)
 */
#define NV3_PFIFO_RAMHT_BASE_SHIFT    0
#define NV3_PFIFO_RAMHT_BASE_MASK     0x1F0
#define NV3_PFIFO_RAMHT_SIZE_SHIFT    16
#define NV3_PFIFO_RAMHT_SIZE_MASK     (0x3 << 16)

/*
 * PFIFO_RAMFC register bit definitions.
 *
 * Per envytools nv1_pfifo.xml:
 *   bits [8:1] = RAMFC base address in RAMIN (in 512-byte units).
 *                Address = value * 0x200.
 *   NV3 RAMFC stores 8 channels * 8 bytes = 64 bytes minimum.
 *   (But typically 1KB is reserved.)
 */
#define NV3_PFIFO_RAMFC_BASE_SHIFT    1
#define NV3_PFIFO_RAMFC_BASE_MASK     0x1FE

/*
 * PFIFO_RAMRO register bit definitions.
 *
 * Per envytools nv1_pfifo.xml:
 *   bits [8:1] = RAMRO base address in RAMIN (in 512-byte units).
 *                Address = value * 0x200.
 *   bit 16 = RAMRO size: 0 = 512 bytes, 1 = 8192 bytes.
 */
#define NV3_PFIFO_RAMRO_BASE_SHIFT    1
#define NV3_PFIFO_RAMRO_BASE_MASK     0x1FE
#define NV3_PFIFO_RAMRO_SIZE_BIT      (1 << 16)

/* PFIFO CACHE_ERROR (read-only status) */
#define NV3_PFIFO_CACHE_ERROR     0x002080

/*
 * PFIFO CACHES register.
 * Per envytools: bit 0 = boolean enable for the reassignment engine.
 * When enabled, PFIFO routes incoming USER writes to the appropriate
 * cache (CACHE0 for channel 0, CACHE1 for others).
 */
#define NV3_PFIFO_CACHES          0x002500

/*
 * PFIFO RUNOUT registers.
 * Per envytools nv1_pfifo.xml.
 */
#define NV3_PFIFO_RUNOUT_STATUS   0x002400
#define NV3_PFIFO_RUNOUT_PUT      0x002410
#define NV3_PFIFO_RUNOUT_GET      0x002420

/*
 * PFIFO CACHE0 registers.
 * Per envytools nv1_pfifo.xml (NV3 variant):
 *   PUSH0 (0x003000): bit 0 = push access enable
 *   PULL0 (0x003040): bit 0 = puller access enable
 *   PUT   (0x003010): pusher write pointer (only bit 0 valid — single entry)
 *   GET   (0x003070): puller read pointer (only bit 0 valid — single entry)
 *   STATUS(0x003014): bit 0=RANOUT, bit 4=EMPTY, bit 8=FULL
 *
 * CACHE0 entry registers (single entry):
 *   ADDR  (0x003080): method + subchannel address
 *   DATA  (0x003084): method data
 */
#define NV3_PFIFO_CACHE0_PUSH0    0x003000
#define NV3_PFIFO_CACHE0_PUT      0x003010
#define NV3_PFIFO_CACHE0_STATUS   0x003014
#define NV3_PFIFO_CACHE0_PULL0    0x003040
#define NV3_PFIFO_CACHE0_GET      0x003070
#define NV3_PFIFO_CACHE0_ADDR     0x003080
#define NV3_PFIFO_CACHE0_DATA     0x003084

/*
 * PFIFO CACHE1 registers (NV3 layout).
 * Per envytools nv1_pfifo.xml (NV3 variant):
 *   PUSH0  (0x003200): bit 0 = push access enable
 *   PUSH1  (0x003204): channel ID for pusher [bits 3:0 on NV3]
 *   PUT    (0x003210): pusher write pointer [bits 4:0 for 32 entries]
 *   STATUS (0x003214): bit 0=RANOUT, bit 4=EMPTY, bit 8=FULL
 *   DMA_PUSH (0x003220): bit 0 = DMA pusher access enable
 *   DMA_FETCH (0x003224): DMA fetch configuration
 *   DMA_PUT  (0x003240 [NV4] or via RAMFC on NV3)
 *   DMA_GET  (0x003244 [NV4] or via RAMFC on NV3)
 *   PULL0  (0x003240): bit 0 = puller access enable
 *   PULL1  (0x003250): puller engine state
 *   GET    (0x003270): puller read pointer [bits 4:0 for 32 entries]
 *
 * CACHE1 entries: 32 entries (NV3) or 64 (NV3T).
 *   ADDR[i] (0x003800 + i*8): method address + subchannel
 *   DATA[i] (0x003804 + i*8): method data
 */
#define NV3_PFIFO_CACHE1_PUSH0    0x003200
#define NV3_PFIFO_CACHE1_PUSH1    0x003204
#define NV3_PFIFO_CACHE1_PUT      0x003210
#define NV3_PFIFO_CACHE1_STATUS   0x003214
#define NV3_PFIFO_CACHE1_DMA_PUSH 0x003220
#define NV3_PFIFO_CACHE1_DMA_FETCH 0x003224
#define NV3_PFIFO_CACHE1_PULL0    0x003240
#define NV3_PFIFO_CACHE1_PULL1    0x003250
#define NV3_PFIFO_CACHE1_HASH     0x003258
#define NV3_PFIFO_CACHE1_GET      0x003270
#define NV3_PFIFO_CACHE1_ENGINE   0x003280
#define NV3_PFIFO_CACHE1_ADDR_START 0x003800
#define NV3_PFIFO_CACHE1_DATA_START 0x003804
#define NV3_PFIFO_CACHE1_ENTRY_STRIDE 8

/*
 * CACHE entry ADDR register bit definitions.
 *
 * Per envytools nv1_pfifo.xml:
 *   bits [12:2]  = method address >> 2 (11 bits, dword-aligned)
 *   bits [15:13] = subchannel index (0-7)
 */
#define NV3_CACHE_ADDR_METHOD_SHIFT  2
#define NV3_CACHE_ADDR_METHOD_MASK   (0x7FF << 2)
#define NV3_CACHE_ADDR_SUBCHAN_SHIFT 13
#define NV3_CACHE_ADDR_SUBCHAN_MASK  (0x7 << 13)

/*
 * CACHE STATUS register bit definitions.
 * Per envytools nv1_pfifo.xml:
 *   bit 0 = RANOUT — cache has run out of entries
 *   bit 4 = EMPTY  — cache contains no pending entries (PUT == GET)
 *   bit 8 = FULL   — cache has no free entry slots
 *
 * These apply to both CACHE0_STATUS, CACHE1_STATUS, and RUNOUT_STATUS.
 */
#define NV3_PFIFO_CACHE_STATUS_RANOUT  (1 << 0)
#define NV3_PFIFO_CACHE_STATUS_EMPTY   (1 << 4)
#define NV3_PFIFO_CACHE_STATUS_FULL    (1 << 8)

/* CACHE1 capacity: 32 entries on NV3, 64 on NV3T */
#define NV3_CACHE1_SIZE            32
#define NV3T_CACHE1_SIZE           64

/* Maximum number of PFIFO channels */
#define NV3_PFIFO_NUM_CHANNELS     8
#define NV3_PFIFO_NUM_SUBCHANNELS  8

/*
 * USER space layout.
 *
 * Per envytools fifo/nv1-pfifo.html:
 * Each channel gets a 64KB window at USER_START + chid * 0x10000.
 * Within a channel window, 8 subchannels at 0x2000 stride:
 *   subchannel[i] = channel_base + i * 0x2000
 *
 * Within each subchannel:
 *   0x0000 = object handle (write binds object to subchannel)
 *   0x0100-0x1FFC = methods (offset >> 2 = method number)
 *
 * The FREE register at channel_base + 0x10 returns the number
 * of free 32-bit entries in the cache (for PIO flow control).
 */
#define NV3_USER_SUBCHAN_STRIDE   0x2000
#define NV3_USER_SUBCHAN_OBJECT   0x0000
#define NV3_USER_SUBCHAN_METHOD_START 0x0100

/*
 * RAMHT (Hash Table) entry format (4 bytes per entry on NV3).
 *
 * Per envytools fifo/nv1-pfifo.html:
 *   bits [31:0] of the handle are used for hash computation.
 *   The entry stored in RAMHT is 8 bytes on NV3:
 *     word 0 = object handle
 *     word 1:
 *       bits [15:0]  = RAMIN instance address >> 4
 *       bits [23:16] = engine (0=SW, 1=PGRAPH, 2=PDMA)
 *       bit 31       = valid flag
 */
#define NV3_RAMHT_ENTRY_SIZE       8
#define NV3_RAMHT_ENTRY_VALID      (1u << 31)
#define NV3_RAMHT_ENTRY_ENGINE_SHIFT 16
#define NV3_RAMHT_ENTRY_ENGINE_MASK  (0xFF << 16)
#define NV3_RAMHT_ENTRY_INSTANCE_MASK 0xFFFF

/* Engine IDs for RAMHT entries */
#define NV3_ENGINE_SW              0
#define NV3_ENGINE_PGRAPH          1
#define NV3_ENGINE_PDMA            2

/*
 * RAMFC (FIFO Context) entry format.
 *
 * Per envytools fifo/nv1-pfifo.html (NV3 variant):
 * Each channel has an 8-byte context stored in RAMFC:
 *   word 0 = DMA_PUT (for DMA mode)
 *   word 1 = DMA_GET (for DMA mode)
 * The channel's RAMFC entry is at: RAMFC_base + chid * 8
 */
#define NV3_RAMFC_ENTRY_SIZE       8

#define NV3_PFIFO_END             0x003FFF

/* PRM - Real Mode Device Support */
#define NV3_PRM_START             0x004000
#define NV3_PRM_END               0x004FFF

/* PRAM - Local RAM/Cache */
#define NV3_PRAM_START            0x006000
#define NV3_PRAM_END              0x006FFF

/* PRMIO - Real Mode I/O */
#define NV3_PRMIO_START           0x007000
#define NV3_PRMIO_END             0x007FFF

/* PTIMER - Programmable Interval Timer */
#define NV3_PTIMER_START          0x009000
#define NV3_PTIMER_INTR_0         0x009100
#define NV3_PTIMER_INTR_EN_0      0x009140
#define NV3_PTIMER_NUMERATOR      0x009200
#define NV3_PTIMER_DENOMINATOR    0x009210
#define NV3_PTIMER_TIME_0         0x009400
#define NV3_PTIMER_TIME_1         0x009410
#define NV3_PTIMER_ALARM_0        0x009420
#define NV3_PTIMER_END            0x009FFF

/*
 * PTIMER_INTR_0 bit positions.
 * Per envytools: bit 0 = ALARM (time reached alarm threshold).
 */
#define NV3_PTIMER_INTR_ALARM     (1 << 0)

/* VGA emulation VRAM window */
#define NV3_VGA_VRAM_START        0x0A0000
#define NV3_VGA_VRAM_END          0x0BFFFF

/* VGA emulation ROM window */
#define NV3_VGA_ROM_START         0x0C0000
#define NV3_VGA_ROM_END           0x0C7FFF

/* PRMVIO - VGA sequencer/misc through MMIO */
#define NV3_PRMVIO_START          0x0C0000
#define NV3_PRMVIO_END            0x0C0400

/* PFB - Framebuffer Interface */
#define NV3_PFB_START             0x100000
#define NV3_PFB_BOOT_0            0x100000
#define NV3_PFB_CONFIG_0          0x100200
#define NV3_PFB_CONFIG_1          0x100204
#define NV3_PFB_END               0x100FFF

/* PFB_BOOT_0 bit definitions */
#define NV3_PFB_BOOT_RAM_AMOUNT_SHIFT   0
#define NV3_PFB_BOOT_RAM_AMOUNT_8MB     0x0  /* 1MB on NV3A */
#define NV3_PFB_BOOT_RAM_AMOUNT_2MB     0x1
#define NV3_PFB_BOOT_RAM_AMOUNT_4MB     0x2
#define NV3_PFB_BOOT_RAM_WIDTH_SHIFT    2
#define NV3_PFB_BOOT_RAM_WIDTH_64       0x0
#define NV3_PFB_BOOT_RAM_WIDTH_128      0x1
#define NV3_PFB_BOOT_RAM_BANKS_SHIFT    3
#define NV3_PFB_BOOT_RAM_BANKS_2        0x0
#define NV3_PFB_BOOT_RAM_BANKS_4        0x1

/* PEXTDEV - External Devices (straps) */
#define NV3_PEXTDEV_START         0x101000
#define NV3_PEXTDEV_STRAPS        0x101000
#define NV3_PEXTDEV_END           0x101FFF

/* PEXTDEV straps bit positions */
#define NV3_STRAPS_BUS_SPEED      0     /* 0=33MHz, 1=66MHz */
#define NV3_STRAPS_BIOS_PRESENT   1     /* 0=absent, 1=present */
#define NV3_STRAPS_RAM_TYPE       2     /* 0=16Mbit, 1=8Mbit */
#define NV3_STRAPS_BUS_WIDTH      4     /* 0=64bit, 1=128bit */
#define NV3_STRAPS_BUS_TYPE       5     /* 0=PCI, 1=AGP */
#define NV3_STRAPS_CRYSTAL        6     /* 0=13.5MHz, 1=14.318MHz */
#define NV3_STRAPS_TVMODE_SHIFT   7     /* 2 bits: SECAM/NTSC/PAL/NONE */
#define NV3_STRAPS_AGP2X          9     /* 0=enabled, 1=disabled */

/* PROM - VBIOS mirror through MMIO */
#define NV3_PROM_START            0x110000
#define NV3_PROM_END              0x11FFFF

/* PALT */
#define NV3_PALT_START            0x120000
#define NV3_PALT_END              0x12FFFF

/* PME - Mediaport */
#define NV3_PME_START             0x200000
#define NV3_PME_END               0x200FFF

/* PGRAPH - 2D/3D Graphics Engine */
#define NV3_PGRAPH_START          0x400000
#define NV3_PGRAPH_INTR_0         0x400100
#define NV3_PGRAPH_INTR_EN_0      0x400140
#define NV3_PGRAPH_END            0x400FFF

/* ========================================================================
 * PCRTC - Display Controller (0x600000-0x600FFF)
 *
 * Per envytools nv3_pcrtc.xml:
 *   INTR at 0x600100 — VBlank interrupt status.
 *   INTR_EN at 0x600140 — VBlank interrupt enable.
 *   CONFIG at 0x600200 — Display configuration (interlace, double-scan).
 *   START at 0x600800 — Framebuffer start address for display scanout.
 * ======================================================================== */
#define NV3_PCRTC_START           0x600000
#define NV3_PCRTC_INTR            0x600100
#define NV3_PCRTC_INTR_EN         0x600140
#define NV3_PCRTC_CONFIG          0x600200
#define NV3_PCRTC_START_ADDR      0x600800
#define NV3_PCRTC_END             0x600FFF

/*
 * PCRTC_INTR / PCRTC_INTR_EN bit positions.
 * Per envytools: bit 0 = VBLANK.
 */
#define NV3_PCRTC_INTR_VBLANK     (1 << 0)

/* PRMCIO - CRTC registers through MMIO */
#define NV3_PRMCIO_START          0x601000
#define NV3_PRMCIO_END            0x601FFF

/* ========================================================================
 * PRAMDAC - DAC/PLL/Cursor (0x680000-0x680FFF)
 *
 * Register layout per envytools nv3_pramdac.xml and xf86-video-nv riva_hw.c.
 *
 * PLL coefficient registers encode three fields:
 *   bits [7:0]   = M divider (1-based, values 1..13 valid per spec)
 *   bits [15:8]  = N multiplier (1-based, values 1..255 valid)
 *   bits [18:16] = P log2 post-divider (0..4 typically)
 *
 * PLL output frequency formula (per xf86-video-nv CalcVClock):
 *   Freq = (crystal_freq * N) / (1 << P)
 *   where the VCO frequency is: crystal_freq * N / M
 *   and the output is divided by (1 << P).
 *   So: Freq = (crystal_freq * N) / (M * (1 << P))
 * ======================================================================== */
#define NV3_PRAMDAC_START         0x680000
#define NV3_PRAMDAC_END           0x680FFF

/*
 * PLL coefficient registers.
 *
 * Per envytools nv3_pramdac.xml:
 *   0x500 is "DLL" on NV3, "NVPLL" on NV4+. We use NVPLL_COEFF for
 *   compatibility with the init code, but note the NV3 distinction.
 */
#define NV3_PRAMDAC_NVPLL_COEFF   0x680500   /* DLL/Core clock PLL */
#define NV3_PRAMDAC_MPLL_COEFF    0x680504   /* Memory clock PLL */
#define NV3_PRAMDAC_VPLL_COEFF    0x680508   /* Pixel clock PLL */

/*
 * PLL_CONTROL register (0x68050C).
 *
 * Per envytools nv3_pramdac.xml (NV3:NV4 variant):
 *   bit 0  = DLL_PROG  — DLL is programmable (vs. fixed)
 *   bit 4  = DLL_BYPASS — bypass DLL
 *   bit 8  = MPLL_PROG — memory PLL is programmable
 *   bit 12 = MPLL_BYPASS — bypass memory PLL
 *   bit 16 = VPLL_PROG — video/pixel PLL is programmable
 *   bit 20 = VPLL_BYPASS — bypass video PLL
 *   bits [25:24] = PCLK_SOURCE — pixel clock source select:
 *                  0=VPLL, 1=VIP, 2=XTAL
 *   bit 28 = VCLK_DB2 — divide VCLK by 2
 *
 * The xf86-video-nv driver sets this to 0x10010100 for SVGA modes:
 *   MPLL_PROG=1, VPLL_PROG=1, VCLK_DB2=1
 */
#define NV3_PRAMDAC_PLL_CONTROL   0x68050C
#define NV3_PRAMDAC_PLL_SETUP     0x680510   /* PLL setup control */

#define NV3_PLL_CTRL_DLL_PROG     (1 << 0)
#define NV3_PLL_CTRL_DLL_BYPASS   (1 << 4)
#define NV3_PLL_CTRL_MPLL_PROG    (1 << 8)
#define NV3_PLL_CTRL_MPLL_BYPASS  (1 << 12)
#define NV3_PLL_CTRL_VPLL_PROG    (1 << 16)
#define NV3_PLL_CTRL_VPLL_BYPASS  (1 << 20)
#define NV3_PLL_CTRL_PCLK_SRC_SHIFT 24
#define NV3_PLL_CTRL_PCLK_SRC_MASK  (0x3 << 24)
#define NV3_PLL_CTRL_PCLK_SRC_VPLL  (0 << 24)
#define NV3_PLL_CTRL_PCLK_SRC_VIP   (1 << 24)
#define NV3_PLL_CTRL_PCLK_SRC_XTAL  (2 << 24)
#define NV3_PLL_CTRL_VCLK_DB2    (1 << 28)

/*
 * PRAMDAC GENERAL_CONTROL register (0x680600).
 *
 * Per envytools nv3_pramdac.xml:
 *   bits [5:4]  = PIXMIX — 0=OFF, 3=ON
 *   bit 8       = VGA_STATE_SEL — 0=VGA passthrough, 1=NV accelerated
 *   bit 12      = ALT_MODE_SEL — alternative pixel format mode
 *   bits [19:16]= TERMINATION — termination impedance (2=750 ohm)
 *   bit 20      = BPC_8BITS — 0=6-bit DAC, 1=8-bit DAC
 *   bit 29      = PIPE_LONG — long pipeline mode
 *
 * Cross-reference: xf86-video-nv sets this to 0x00100100 for SVGA modes
 * (VGA_STATE_SEL=1 at bit 8, BPC_8BITS=1 at bit 20).
 */
#define NV3_PRAMDAC_GENERAL_CTRL  0x680600

/*
 * Hardware cursor position register (0x680300).
 *
 * Per envytools nv3_pramdac.xml:
 *   bits [15:0]  = X position
 *   bits [31:16] = Y position
 */
#define NV3_PRAMDAC_CURSOR_POS    0x680300

/* NV3 PRAMDAC general control bitfield masks.
 * Per envytools nv3_pramdac.xml (verified against xf86-video-nv). */
#define NV3_PRAMDAC_GCTRL_PIXMIX_SHIFT   4
#define NV3_PRAMDAC_GCTRL_PIXMIX_MASK    (0x3 << 4)
#define NV3_PRAMDAC_GCTRL_PIXMIX_OFF     (0 << 4)
#define NV3_PRAMDAC_GCTRL_PIXMIX_ON      (3 << 4)
#define NV3_PRAMDAC_GCTRL_VGA_STATE      (1 << 8)    /* 0=VGA, 1=NV accel */
#define NV3_PRAMDAC_GCTRL_ALT_MODE       (1 << 12)
#define NV3_PRAMDAC_GCTRL_TERMINATION_SHIFT  16
#define NV3_PRAMDAC_GCTRL_TERMINATION_MASK   (0xF << 16)
#define NV3_PRAMDAC_GCTRL_BPC_8BIT       (1 << 20)   /* 0=6-bit, 1=8-bit DAC */
#define NV3_PRAMDAC_GCTRL_PIPE_LONG      (1 << 29)

/* PLL coefficient field extraction macros.
 * Per envytools nv3_pramdac.xml:
 *   [7:0]   = M divider
 *   [15:8]  = N multiplier
 *   [18:16] = P post-divider exponent
 */
#define NV3_PLL_M(coeff)   ((coeff) & 0xFF)
#define NV3_PLL_N(coeff)   (((coeff) >> 8) & 0xFF)
#define NV3_PLL_P(coeff)   (((coeff) >> 16) & 0x07)

/* Crystal oscillator frequencies in Hz.
 * Per envytools and xf86-video-nv:
 *   Straps bit 6 selects between these two.
 */
#define NV3_CRYSTAL_FREQ_13500   13500000   /* 13.500 MHz */
#define NV3_CRYSTAL_FREQ_14318   14318180   /* 14.31818 MHz */

/* USER DAC registers (palette access through MMIO) */
#define NV3_USER_DAC_START        0x681200
#define NV3_USER_DAC_PALETTE_START 0x6813C6
#define NV3_USER_DAC_PALETTE_END  0x6813C9
#define NV3_USER_DAC_END          0x681FFF

/* PRAMIN - RAM INput area (DMA objects, hash tables) */
#define NV3_PRAMIN_START          0x700000
#define NV3_PRAMIN_END            0x7FFFFF
#define NV3_PRAMIN_VRAM_SIZE      0x100000  /* 1MB PRAMIN window into top of VRAM */

/* USER - PFIFO user channel space */
#define NV3_USER_START            0x800000
#define NV3_USER_END              0xFFFFFF
#define NV3_USER_CHANNEL_STRIDE   0x10000   /* 64KB per channel */
#define NV3_USER_FREE_OFFSET      0x0010    /* FifoFree: 16-bit count of free entries */

/* ========================================================================
 * Linear Framebuffer (BAR1) Layout
 * ======================================================================== */
#define NV3_LFB_RAMIN_MIRROR_START  0x400000
#define NV3_LFB_MIRROR_START        0x800000
#define NV3_LFB_RAMIN_START         0xC00000
#define NV3_LFB_MAPPING_SIZE        0x400000

/* ========================================================================
 * Extended CRTC Registers (NVIDIA proprietary)
 * Per envytools NV3 VGA docs and xf86-video-nv
 * ======================================================================== */
#define NV3_CRTC_REPAINT0         0x19   /* RPC0: bits 7:5 = rowoffset[10:8], bits 4:0 = startaddr[20:16] */
#define NV3_CRTC_REPAINT1         0x1A   /* RPC1: misc display config */
#define NV3_CRTC_READ_BANK        0x1D   /* Banked VRAM read window */
#define NV3_CRTC_WRITE_BANK       0x1E   /* Banked VRAM write window */
#define NV3_CRTC_FORMAT           0x25   /* Format/extended bits (10-bit counters) */
#define NV3_CRTC_PIXEL_MODE       0x28   /* Pixel depth: 0=VGA, 1=8bpp, 2=16bpp, 3=32bpp */
#define NV3_CRTC_HEB              0x2D   /* Horizontal extension bit */
#define NV3_CRTC_RL0              0x34   /* Read-only: low byte of scanline counter */
#define NV3_CRTC_RL1              0x35   /* Read-only: high bits of scanline counter */
#define NV3_CRTC_RMA              0x38   /* Real Mode Access to GPU MMIO */
#define NV3_CRTC_I2C_READ         0x3E   /* I2C/DDC read */
#define NV3_CRTC_I2C_WRITE        0x3F   /* I2C/DDC write */

/* NV3_CRTC_FORMAT bit positions */
#define NV3_CRTC_FORMAT_VDT10     0      /* 10-bit vtotal */
#define NV3_CRTC_FORMAT_VDE10     1      /* 10-bit dispend */
#define NV3_CRTC_FORMAT_VRS10     2      /* 10-bit vblank start */
#define NV3_CRTC_FORMAT_VBS10     3      /* 10-bit vsync start */
#define NV3_CRTC_FORMAT_HBE6      4      /* 6-bit horizontal blank end */

/* NV3_CRTC_PIXEL_MODE values */
#define NV3_PIXEL_MODE_VGA        0x00
#define NV3_PIXEL_MODE_8BPP       0x01
#define NV3_PIXEL_MODE_16BPP      0x02
#define NV3_PIXEL_MODE_32BPP      0x03

/* RMA register window (3D0-3D3) */
#define NV3_RMA_ADDR_START        0x3D0
#define NV3_RMA_ADDR_END          0x3D3
#define NV3_RMA_MODE_MAX          0x0F

/* VGA banked addressing modes (GDC reg 6, bits 3:2) */
#define NV3_BANKED_128K_A0000     0x00
#define NV3_BANKED_64K_A0000      0x04
#define NV3_BANKED_32K_B0000      0x08
#define NV3_BANKED_32K_B8000      0x0C

/* ========================================================================
 * PMC_INTR_0 interrupt bit positions
 *
 * Per envytools pmc.xml: PMC_INTR_0 is a read-only aggregation of all
 * subsystem interrupt lines. Each bit reflects whether that subsystem
 * has a pending interrupt (subsystem INTR_0 & subsystem INTR_EN_0 != 0).
 * ======================================================================== */
#define NV3_PMC_INTR_PMEDIA       4
#define NV3_PMC_INTR_PFIFO        8
#define NV3_PMC_INTR_PGRAPH0      12
#define NV3_PMC_INTR_PGRAPH1      13
#define NV3_PMC_INTR_PVIDEO       16
#define NV3_PMC_INTR_PTIMER       20
#define NV3_PMC_INTR_PCRTC        24
#define NV3_PMC_INTR_PBUS         28
#define NV3_PMC_INTR_SOFTWARE     31

/* ========================================================================
 * PMC_ENABLE subsystem enable bits
 * ======================================================================== */
#define NV3_PMC_ENABLE_PMEDIA     (1 << 4)
#define NV3_PMC_ENABLE_PFIFO      (1 << 8)
#define NV3_PMC_ENABLE_PGRAPH     (1 << 12)
#define NV3_PMC_ENABLE_PPMI       (1 << 16)
#define NV3_PMC_ENABLE_PFB        (1 << 20)
#define NV3_PMC_ENABLE_PCRTC      (1 << 24)
#define NV3_PMC_ENABLE_PVIDEO     (1 << 28)

/* ========================================================================
 * PMC_INTR_EN_0 values
 *
 * Per envytools: this is NOT a bitmask. It is a 2-bit enum:
 *   0 = DISABLED — no interrupt output
 *   1 = HARDWARE — route to PCI INTA
 *   2 = SOFTWARE — route to software interrupt (bit 31 of INTR_0)
 * ======================================================================== */
#define NV3_PMC_INTR_EN_DISABLED  0x00000000
#define NV3_PMC_INTR_EN_HARDWARE  0x00000001
#define NV3_PMC_INTR_EN_SOFTWARE  0x00000002

#endif /* VID_NV3_REGS_H */
