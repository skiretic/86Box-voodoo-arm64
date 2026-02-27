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
 *
 * PFB spans 0x100000-0x100FFF (4KB = 1024 dwords).
 * The regs[] array provides a general register bank for driver readback
 * of registers not explicitly handled (e.g., 0x100044 DELAY, 0x100080
 * DEBUG_0, 0x1000C0 GREEN_0, 0x100100 UNK100, 0x100110).
 * Explicitly handled registers (BOOT_0, CONFIG_0, CONFIG_1) have
 * dedicated fields AND are mirrored into the bank for consistent readback.
 */
typedef struct nv3_pfb_s {
    uint32_t boot_0;         /* Memory config (size, width, banks) */
    uint32_t config_0;       /* Framebuffer config register 0 */
    uint32_t config_1;       /* Framebuffer config register 1 */
    uint32_t regs[1024];     /* General register bank for unhandled PFB regs */
} nv3_pfb_t;

/*
 * PEXTDEV (External Devices / Straps) state.
 *
 * PEXTDEV spans 0x101000-0x101FFF (4KB = 1024 dwords).
 * The regs[] array provides a general register bank for driver readback
 * of registers not explicitly handled (e.g., 0x101114, 0x101200).
 * The STRAPS register at offset 0 has a dedicated field AND is mirrored
 * into the bank for consistent readback.
 */
typedef struct nv3_pextdev_s {
    uint32_t straps;         /* Board configuration straps */
    uint32_t regs[1024];     /* General register bank for unhandled PEXTDEV regs */
} nv3_pextdev_t;

/*
 * PME (PMEDIA / Mediaport) state.
 *
 * PME spans 0x200000-0x200FFF (4KB = 1024 dwords).
 * Per envytools: PMEDIA has its own interrupt registers that aggregate
 * to PMC_INTR_0 bit 4. The regs[] array handles unhandled offsets.
 */
typedef struct nv3_pme_s {
    uint32_t intr_0;         /* PMEDIA interrupt status (write-1-to-clear) */
    uint32_t intr_en_0;      /* PMEDIA interrupt enable mask */
    uint32_t regs[1024];     /* General register bank for unhandled PME regs */
} nv3_pme_t;

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
 * NV3 2D object class IDs.
 *
 * Per envytools graph/nv3-pgraph.html:
 * These class numbers are stored in the RAMIN instance data and
 * identify what kind of graphics object is bound to a subchannel.
 *
 * NV3 uses NV1-style class numbering for context objects (0x01-0x06)
 * and NV3-specific numbered classes for rendering objects (0x07+).
 * The actual class stored in RAMIN uses the "graph class" numbering
 * which differs from the PGRAPH internal class register.
 *
 * For NV1/NV3: the low 8 bits of instance word 0 contain the class.
 */
#define NV3_CLASS_BETA          0x0012  /* Beta blending factor */
#define NV3_CLASS_ROP           0x0043  /* Raster operation (ROP3) */
#define NV3_CLASS_CHROMA        0x0017  /* Color key / transparency */
#define NV3_CLASS_PLANE         0x0044  /* Bitplane mask */
#define NV3_CLASS_CLIP          0x0019  /* Clipping rectangle */
#define NV3_CLASS_PATTERN       0x0018  /* 8x8 fill pattern */
#define NV3_CLASS_RECT          0x001E  /* Filled rectangle (color+ROP) */
#define NV3_CLASS_POINT         0x0046  /* Point rendering */
#define NV3_CLASS_LINE          0x001C  /* Line rendering */
#define NV3_CLASS_LIN           0x001D  /* Line without endpoint */
#define NV3_CLASS_TRI           0x001D  /* 2D triangle rendering - shares with LIN on NV3 */
#define NV3_CLASS_BLIT          0x001F  /* Screen-to-screen blit */
#define NV3_CLASS_IMAGE_FROM_CPU 0x0021 /* Image transfer from CPU */
#define NV3_CLASS_BITMAP        0x0036  /* Monochrome bitmap */
#define NV3_CLASS_GDI_TEXT      0x004A  /* GDI text rendering (NV3+) */
#define NV3_CLASS_M2MF          0x0039  /* Memory-to-memory format convert */
#define NV3_CLASS_SURF_2D       0x0042  /* NV03 combined 2D surfaces (src+dst) */
#define NV3_CLASS_SURF_DST      0x0058  /* Destination surface (NV3+) */
#define NV3_CLASS_SURF_SRC      0x0059  /* Source surface for blit (NV3+) */
#define NV3_CLASS_SURF_COLOR    0x005A  /* Color surface (NV3+, for 3D) */
#define NV3_CLASS_SURF_ZETA     0x005B  /* Zeta surface (NV3+, for 3D) */

/*
 * NV4-compatible class aliases.
 * Some NV3 drivers use NV4 class numbers for backwards compatibility.
 */
#define NV4_CLASS_GDI_TEXT      0x004B  /* NV04 variant of GDI text */

/*
 * NV3 pixel format IDs.
 *
 * Per envytools: surface color format is encoded in the surface object.
 * These values are used in the PGRAPH context and surface methods.
 */
#define NV3_SURFACE_FORMAT_Y8        0x01  /* 8bpp indexed */
#define NV3_SURFACE_FORMAT_X1R5G5B5  0x02  /* 15bpp (1-5-5-5) */
#define NV3_SURFACE_FORMAT_R5G6B5    0x04  /* 16bpp (5-6-5) */
#define NV3_SURFACE_FORMAT_X8R8G8B8  0x07  /* 32bpp (8-8-8-8) */

/*
 * Maximum subchannels and image transfer buffer size.
 */
#define NV3_PGRAPH_NUM_SUBCHANNELS  8

/*
 * ImageFromCPU and GDI text pixel accumulation state.
 *
 * When the driver sends pixel data in multiple dword writes,
 * we need to track the current position within the destination
 * rectangle so successive COLOR[] writes fill left-to-right,
 * top-to-bottom.
 */
typedef struct nv3_image_state_s {
    int32_t  dst_x;       /* Destination rectangle origin X */
    int32_t  dst_y;       /* Destination rectangle origin Y */
    int32_t  size_in_w;   /* Input image width (pixels) */
    int32_t  size_in_h;   /* Input image height (pixels) */
    int32_t  size_out_w;  /* Output (destination) width */
    int32_t  size_out_h;  /* Output (destination) height */
    uint32_t color_format; /* Pixel format of incoming data */
    int32_t  cur_x;       /* Current X within dest rect */
    int32_t  cur_y;       /* Current Y within dest rect */
    bool     active;      /* True if a transfer is in progress */
} nv3_image_state_t;

/*
 * GDI text rendering state.
 *
 * The NV3 GDI text class renders monochrome (1bpp) bitmaps,
 * expanding each bit to the foreground or background color.
 * Used for Windows 95/98 text acceleration.
 *
 * Per envytools: class 0x004A (NV3 GDI rectangle+text)
 */
typedef struct nv3_gdi_text_state_s {
    uint32_t color_format;  /* Color format for output */
    uint32_t mono_format;   /* Monochrome data format (LE/CGA6) */
    uint32_t color0;        /* Background color */
    uint32_t color1;        /* Foreground color (1 bits) */
    int32_t  clip_x;        /* Unclipped rect X */
    int32_t  clip_y;        /* Unclipped rect Y */
    int32_t  clip_w;        /* Unclipped rect width */
    int32_t  clip_h;        /* Unclipped rect height */
    int32_t  rect_x;        /* 1bpp bitmap dest X */
    int32_t  rect_y;        /* 1bpp bitmap dest Y */
    int32_t  rect_w;        /* 1bpp bitmap width */
    int32_t  rect_h;        /* 1bpp bitmap height */
    int32_t  cur_x;         /* Current X within bitmap */
    int32_t  cur_y;         /* Current Y within bitmap */
    bool     active;        /* True if 1bpp transfer active */
} nv3_gdi_text_state_t;

/*
 * PGRAPH (2D/3D Graphics Engine) state.
 *
 * The regs[] array provides a general register bank for driver readback.
 * PGRAPH spans 0x400000-0x400FFF (4KB = 1024 dwords). Registers handled
 * explicitly in the switch statement (INTR_0, INTR_EN_0) are returned
 * from their dedicated fields; all other registers go through the bank.
 *
 * Phase 4 additions: 2D acceleration state for object classes.
 *
 * Per envytools graph/nv3-pgraph.html:
 * PGRAPH maintains per-subchannel object class bindings and a set of
 * context objects (clip, rop, pattern, beta, surfaces) that affect
 * all 2D rendering operations.
 */
typedef struct nv3_pgraph_s {
    /* Interrupt state */
    uint32_t intr_0;
    uint32_t intr_en_0;

    /* Debug registers (driver programs these during init) */
    uint32_t debug_0;
    uint32_t debug_1;
    uint32_t debug_2;
    uint32_t debug_3;

    /* Context register (PGRAPH_CTX_SWITCH at 0x400180) */
    uint32_t ctx_switch;

    /*
     * Per-subchannel class binding.
     * When SetObject binds an object to subchannel N, we read the
     * object's class from its RAMIN instance and store it here.
     * Method dispatch uses this to know which class handler to call.
     */
    uint32_t subchan_class[NV3_PGRAPH_NUM_SUBCHANNELS];

    /* ---- Context object state (set by context object methods) ---- */

    /*
     * Clip rectangle (set by class 0x0019).
     * Per envytools: clip applies to all rendering operations.
     * If no clip object is set, the clip covers the entire surface.
     */
    int32_t  clip_x;
    int32_t  clip_y;
    int32_t  clip_w;
    int32_t  clip_h;
    bool     clip_valid;

    /*
     * Raster operation (set by class 0x0043).
     * Per envytools: the ROP3 value (0-255) defines how source,
     * destination, and pattern bits are combined.
     * Default: 0xCC (SRCCOPY — just use source/fill color).
     */
    uint32_t rop3;

    /*
     * Pattern (set by class 0x0018).
     * Per envytools: 8x8 mono or color pattern used for fill.
     */
    uint32_t pattern_shape;      /* 0=8x8, 1=64x1, 2=1x64 */
    uint32_t pattern_color0;     /* Color for 0 bits (mono mode) */
    uint32_t pattern_color1;     /* Color for 1 bits (mono mode) */
    uint32_t pattern_mono[2];    /* 64-bit monochrome bitmap (2 x 32-bit) */
    uint32_t pattern_color_format;
    uint32_t pattern_mono_format;

    /*
     * Beta factor (set by class 0x0012).
     * Per envytools: beta is a 31-bit fixed-point value [0, 1].
     * Bit 31 is the integer part (0 or 1), bits 30:0 are the fraction.
     * Used as alpha blending factor in certain ROP modes.
     */
    uint32_t beta;

    /*
     * Chroma key (set by class 0x0017).
     * When chroma key is enabled, pixels matching this color are
     * not written (transparency).
     */
    uint32_t chroma_color;
    bool     chroma_valid;

    /* ---- Surface state ---- */

    /*
     * Destination surface (set by class 0x0058).
     * This is where all 2D rendering writes go.
     */
    uint32_t surf_dst_offset;  /* Byte offset in VRAM */
    uint32_t surf_dst_pitch;   /* Bytes per scanline */
    uint32_t surf_dst_format;  /* Pixel format (NV3_SURFACE_FORMAT_*) */

    /*
     * Source surface (set by class 0x0059).
     * Used by blit operations as the read source.
     */
    uint32_t surf_src_offset;
    uint32_t surf_src_pitch;
    uint32_t surf_src_format;

    /* ---- Per-class rendering state ---- */

    /*
     * Rectangle class state (class 0x001E).
     * Per envytools: the color is latched, then one or more
     * POINT+SIZE pairs define rectangles to fill.
     */
    uint32_t rect_color;

    /*
     * Blit class state (class 0x001F).
     * Per envytools: POINT_IN sets source origin, POINT_OUT sets
     * destination origin, SIZE triggers the copy.
     */
    int32_t  blit_src_x;
    int32_t  blit_src_y;
    int32_t  blit_dst_x;
    int32_t  blit_dst_y;

    /* ImageFromCPU state (class 0x0021) */
    nv3_image_state_t image;

    /* GDI text/rect state (class 0x004A) */
    nv3_gdi_text_state_t gdi;

    /* General register bank for unhandled PGRAPH offsets */
    uint32_t regs[1024];
} nv3_pgraph_t;

/*
 * PFIFO cache entry.
 * Per envytools nv1_pfifo.xml: each cache entry stores a pending
 * method submission as an (address, data) pair.
 *   addr: bits [12:2] = method, bits [15:13] = subchannel
 *   data: 32-bit method parameter
 */
typedef struct nv3_cache_entry_s {
    uint32_t addr;
    uint32_t data;
} nv3_cache_entry_t;

/*
 * PFIFO (Command FIFO) state.
 *
 * NV3 PFIFO architecture (per envytools nv1-pfifo docs):
 *
 * The PFIFO is a command submission engine that sits between the CPU
 * (or DMA engine) and the graphics/DMA engines. Commands flow through:
 *   USER space write -> pusher -> CACHE1 -> puller -> PGRAPH/PDMA
 *
 * Two caches:
 *   CACHE0: 1 entry, reserved for channel 0 (low-priority notifier path)
 *   CACHE1: 32 entries (NV3) / 64 entries (NV3T), handles channels 1-7
 *
 * RAMHT: Hash table mapping object handles -> (engine, class, instance)
 * RAMFC: Per-channel FIFO context (DMA pointers for context switching)
 * RAMRO: Runout buffer for error logging
 *
 * The regs[] array provides a general register bank for driver readback.
 * PFIFO spans 0x002000-0x003FFF (8KB = 2048 dwords).
 */
typedef struct nv3_pfifo_s {
    /* Interrupt state */
    uint32_t intr_0;
    uint32_t intr_en_0;

    /* Master FIFO enable (CACHES register, bit 0) */
    uint32_t caches_enabled;

    /* PFIFO configuration */
    uint32_t config;          /* Per-channel DMA/PIO mode (bits 7:0) */
    uint32_t ramht_config;    /* RAMHT base + size config */
    uint32_t ramfc_config;    /* RAMFC base config */
    uint32_t ramro_config;    /* RAMRO base + size config */

    /*
     * CACHE0: single-entry cache for channel 0.
     * Per envytools: CACHE0 has exactly 1 entry.
     */
    struct {
        uint32_t push_enabled;  /* PUSH0 bit 0: push access enable */
        uint32_t pull_enabled;  /* PULL0 bit 0: puller access enable */
        uint32_t put;           /* Write pointer (0 or 1) */
        uint32_t get;           /* Read pointer (0 or 1) */
        nv3_cache_entry_t entry; /* The single cache entry */
    } cache0;

    /*
     * CACHE1: main command cache.
     * 32 entries on NV3 (NV3_CACHE1_SIZE).
     */
    struct {
        uint32_t push_enabled;  /* PUSH0 bit 0: push access enable */
        uint32_t push_channel;  /* PUSH1: channel ID for pusher */
        uint32_t pull_enabled;  /* PULL0 bit 0: puller access enable */
        uint32_t pull_engine;   /* PULL1: engine select for puller */
        uint32_t put;           /* Write pointer (index into entries) */
        uint32_t get;           /* Read pointer (index into entries) */
        uint32_t dma_push;      /* DMA_PUSH: bit 0 = DMA pusher enable */
        uint32_t dma_fetch;     /* DMA fetch config */
        uint32_t hash;          /* Last RAMHT hash result */
        uint32_t engine;        /* Engine assignment for current subchannel */
        nv3_cache_entry_t entries[NV3T_CACHE1_SIZE]; /* Max size for NV3T */
    } cache1;

    /* RAMRO (Runout) state */
    struct {
        uint32_t put;           /* Write pointer */
        uint32_t get;           /* Read pointer */
    } runout;

    /*
     * Per-subchannel state for the puller.
     * Tracks which object handle is currently bound to each subchannel.
     */
    uint32_t subchan_object[NV3_PFIFO_NUM_SUBCHANNELS];

    /* General register bank for unhandled PFIFO offsets */
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
    nv3_pme_t     pme;
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
