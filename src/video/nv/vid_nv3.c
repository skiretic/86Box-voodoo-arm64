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
 *          Phase 2: Core subsystems (PMC, PTIMER, PFB, PEXTDEV,
 *                   PRAMDAC, PCRTC). PLL clock programming, VBlank
 *                   interrupt, SVGA mode support.
 *
 *          Sources:
 *            - envytools rnndb register database
 *            - rivafb riva_tbl.h golden init tables
 *            - xf86-video-nv riva_hw.c (PLL formula reference)
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
#define ENABLE_NV3_LOG 1
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

/*
 * MMIO trace: controlled by ENABLE_NV3_TRACE. Disabled by default.
 * When enabled, logs the first NV3_TRACE_LIMIT read/write accesses
 * after PMC_ENABLE is written (driver init start signal).
 */
#define ENABLE_NV3_TRACE 0
#define NV3_TRACE_LIMIT 50000
static int nv3_trace_active  = 0;
static int nv3_trace_count   = 0;

/*
 * Render diagnostics: one-shot dump triggered on first extended mode entry.
 * Fires in nv3_vblank_start on the FIRST vblank after mode switch,
 * capturing the actual values the SVGA core uses for rendering (which
 * may differ from what nv3_recalctimings sets due to post-processing
 * in svga_recalctimings).
 */
static int nv3_diag_pending   = 0; /* set in recalctimings, consumed in vblank */
static int nv3_diag_fired     = 0; /* only fire once per session */
static int nv3_diag_scanlines = 0; /* count scanlines for per-scanline dump */

/*
 * Delayed render diagnostic: fires after ~30 frames to capture steady-state
 * values (after driver has had time to paint the desktop via PGRAPH 2D).
 */
static int nv3_diag_delayed_pending = 0;
static int nv3_diag_frame_count     = 0;

/*
 * Framebuffer write counters — used by LFB/banked write handlers and
 * the delayed diagnostic to track whether any framebuffer writes occurred.
 */
static uint32_t nv3_lfb_write_count    = 0;
static uint32_t nv3_banked_write_count = 0;

/*
 * Detailed MMIO read/write trace logging (Fix 3).
 * Controlled by ENABLE_NV3_MMIO_TRACE_LOG. Disabled by default.
 * When enabled, every MMIO read and write is logged with address,
 * value, and which subsystem handled the access.
 */
/* #define ENABLE_NV3_MMIO_TRACE_LOG 1 */
#ifdef ENABLE_NV3_MMIO_TRACE_LOG
static void
nv3_mmio_trace_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#else
#    define nv3_mmio_trace_log(fmt, ...)
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
static void     nv3_update_irq(nv3_t *nv3);
static void     nv3_vblank_start(svga_t *svga);

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

/* PFIFO helpers */
static uint32_t nv3_cache1_compute_status(nv3_t *nv3);
static uint32_t nv3_cache1_free_count(nv3_t *nv3);
static void     nv3_pfifo_puller_run(nv3_t *nv3);
static void     nv3_pfifo_push_pio(nv3_t *nv3, uint32_t channel,
                                   uint32_t subchan, uint32_t method,
                                   uint32_t data);

/* ========================================================================
 * PMC interrupt aggregation and PCI IRQ routing.
 *
 * Per envytools pmc.xml: PMC_INTR_0 is a READ-ONLY register that
 * aggregates the interrupt status from all subsystems. Each bit reflects
 * whether that subsystem has a pending, enabled interrupt.
 *
 * PMC_INTR_EN_0 is an enum (not a bitmask):
 *   0 = DISABLED — no interrupts asserted
 *   1 = HARDWARE — route aggregate interrupt to PCI INTA
 *   2 = SOFTWARE — set bit 31 of INTR_0
 *
 * The PCI interrupt line is asserted when:
 *   PMC_INTR_EN_0 == HARDWARE and PMC_INTR_0 != 0
 * ======================================================================== */
static void
nv3_pmc_update_intr(nv3_t *nv3)
{
    uint32_t pending = 0;

    /*
     * Aggregate subsystem interrupts.
     * Each subsystem has its own INTR_0 and INTR_EN_0. A subsystem
     * contributes to the PMC aggregate if (intr & intr_en) != 0.
     */

    /* PMEDIA — bit 4 */
    if (nv3->pme.intr_0 & nv3->pme.intr_en_0)
        pending |= (1 << NV3_PMC_INTR_PMEDIA);

    /* PFIFO — bit 8 */
    if (nv3->pfifo.intr_0 & nv3->pfifo.intr_en_0)
        pending |= (1 << NV3_PMC_INTR_PFIFO);

    /* PGRAPH — bit 12 */
    if (nv3->pgraph.intr_0 & nv3->pgraph.intr_en_0)
        pending |= (1 << NV3_PMC_INTR_PGRAPH0);

    /* PTIMER — bit 20 */
    if (nv3->ptimer.intr_0 & nv3->ptimer.intr_en_0)
        pending |= (1 << NV3_PMC_INTR_PTIMER);

    /* PCRTC — bit 24 (VBlank) */
    if (nv3->pcrtc.intr & nv3->pcrtc.intr_en)
        pending |= (1 << NV3_PMC_INTR_PCRTC);

    /* PBUS — bit 28 */
    if (nv3->pbus.intr_0 & nv3->pbus.intr_en_0)
        pending |= (1 << NV3_PMC_INTR_PBUS);

    /* SOFTWARE interrupt — bit 31 (set when INTR_EN_0 == SOFTWARE) */
    if (nv3->pmc.intr_en_0 == NV3_PMC_INTR_EN_SOFTWARE)
        pending |= (1u << NV3_PMC_INTR_SOFTWARE);

    nv3->pmc.intr_0 = pending;
}

static void
nv3_update_irq(nv3_t *nv3)
{
    nv3_pmc_update_intr(nv3);

    /*
     * Assert PCI INTA if hardware interrupts are enabled and any
     * subsystem has a pending interrupt.
     */
    if (nv3->pmc.intr_en_0 == NV3_PMC_INTR_EN_HARDWARE && nv3->pmc.intr_0 != 0) {
        pci_set_irq(nv3->pci_slot, PCI_INTA, &nv3->pci_irq_state);
    } else {
        pci_clear_irq(nv3->pci_slot, PCI_INTA, &nv3->pci_irq_state);
    }
}

/* ========================================================================
 * VBlank start callback.
 *
 * Called by the SVGA core at the start of vertical blank (when vc == dispend).
 * Sets the PCRTC VBlank interrupt pending bit and propagates to PMC/PCI.
 * ======================================================================== */
static void
nv3_vblank_start(svga_t *svga)
{
    nv3_t *nv3 = (nv3_t *) svga->priv;

    /* Set PCRTC VBlank interrupt pending */
    nv3->pcrtc.intr |= NV3_PCRTC_INTR_VBLANK;

    /* Propagate to PMC and PCI IRQ line */
    nv3_update_irq(nv3);

    /*
     * Render diagnostic: dump actual render-time state on first vblank
     * after extended mode entry. These values reflect the SVGA core's
     * post-processing (hblank_sub, lowres switch, overscan calc) which
     * runs AFTER nv3_recalctimings returns.
     */
    if (nv3_diag_pending && !nv3_diag_fired) {
        nv3_diag_fired   = 1;
        nv3_diag_pending = 0;

        nv3_log("NV3: ========== RENDER DIAGNOSTIC (at vblank) ==========\n");
        nv3_log("NV3: RENDER_DIAG: hdisp=%d hdisp_time=%d dispend=%d "
                "crtc1=0x%02x crtc13=0x%02x\n",
                svga->hdisp, svga->hdisp_time, svga->dispend,
                svga->crtc[1], svga->crtc[0x13]);
        nv3_log("NV3: RENDER_DIAG: rowoffset=%d (stride=%d) lowres=%d "
                "bpp=%d\n",
                svga->rowoffset, svga->rowoffset << 3, svga->lowres,
                svga->bpp);
        nv3_log("NV3: RENDER_DIAG: memaddr=0x%x memaddr_backup=0x%x "
                "memaddr_latch=0x%x hblank_sub=%d\n",
                svga->memaddr, svga->memaddr_backup,
                svga->memaddr_latch, svga->hblank_sub);
        nv3_log("NV3: RENDER_DIAG: packed_chain4=%d fb_only=%d "
                "chain4=%d force_old_addr=%d\n",
                svga->packed_chain4, svga->fb_only,
                svga->chain4, svga->force_old_addr);

        /* Compare render function pointer against known functions */
        nv3_log("NV3: RENDER_DIAG: render=%p "
                "8bpp_highres=%p 8bpp_lowres=%p blank=%p\n",
                (void *) svga->render,
                (void *) svga_render_8bpp_highres,
                (void *) svga_render_8bpp_lowres,
                (void *) svga_render_blank);
        int is_8hi  = (svga->render == svga_render_8bpp_highres);
        int is_8lo  = (svga->render == svga_render_8bpp_lowres);
        int is_blnk = (svga->render == svga_render_blank);
        nv3_log("NV3: RENDER_DIAG: render_is_8bpp_highres=%d "
                "render_is_8bpp_lowres=%d render_is_blank=%d\n",
                is_8hi, is_8lo, is_blnk);

        nv3_log("NV3: RENDER_DIAG: gdcreg5=0x%02x gdcreg6=0x%02x "
                "seqregs1=0x%02x seqregs4=0x%02x attrregs10=0x%02x\n",
                svga->gdcreg[5], svga->gdcreg[6],
                svga->seqregs[1], svga->seqregs[4], svga->attrregs[0x10]);
        nv3_log("NV3: RENDER_DIAG: override=%d hoverride=%d "
                "scrblank=%d crtc17=0x%02x attr_pal_en=%d\n",
                svga->override, svga->hoverride,
                svga->scrblank, svga->crtc[0x17], svga->attr_palette_enable);
        nv3_log("NV3: RENDER_DIAG: x_add=%d y_add=%d left_overscan=%d "
                "scrollcache=%d\n",
                svga->x_add, svga->y_add, svga->left_overscan,
                svga->scrollcache);
        nv3_log("NV3: RENDER_DIAG: dots_per_clock=%d char_width=%d "
                "linedbl=%d rowcount=%d split=%d\n",
                svga->dots_per_clock, svga->char_width,
                svga->linedbl, svga->rowcount, svga->split);
        nv3_log("NV3: RENDER_DIAG: htotal=%d vtotal=%d vsyncstart=%d "
                "vblankstart=%d vc=%d\n",
                svga->htotal, svga->vtotal, svga->vsyncstart,
                svga->vblankstart, svga->vc);
        nv3_log("NV3: RENDER_DIAG: write_bank=0x%x read_bank=0x%x "
                "vram_display_mask=0x%x\n",
                svga->write_bank, svga->read_bank,
                svga->vram_display_mask);
        nv3_log("NV3: RENDER_DIAG: adv_flags=0x%x remap_required=%d "
                "force_shifter_bypass=%d\n",
                svga->adv_flags, svga->remap_required,
                svga->force_shifter_bypass);
        nv3_log("NV3: RENDER_DIAG: crtc14=0x%02x (dwordshift=%d) "
                "crtc17=0x%02x (wordshift=%d wordincr=%d)\n",
                svga->crtc[0x14],
                (svga->crtc[0x14] & 0x40) ? 1 : 0,
                svga->crtc[0x17],
                (svga->crtc[0x17] & 0x40) ? 0 : 1,
                (svga->crtc[0x17] & 0x08) ? 1 : 0);

        /* Compute what the renderer will actually use */
        int forcepacked = svga->force_old_addr || svga->packed_chain4;
        int dwordshift  = (svga->crtc[0x14] & 0x40) ? 1 : 0;
        int wordshift2  = ((svga->crtc[0x17] & 0x40) == 0 && !dwordshift) ? 1 : 0;
        int incbypow2   = forcepacked ? 0 : (dwordshift ? 2 : wordshift2 ? 1 : 0);
        int dwordincr   = (svga->crtc[0x14] & 0x20) ? 1 : 0;
        int wordincr    = (svga->crtc[0x17] & 0x08) ? 1 : 0;
        int incevery    = forcepacked ? 1 : (dwordincr && !wordincr ? 4 : wordincr ? 2 : 1);
        nv3_log("NV3: RENDER_DIAG: COMPUTED: forcepacked=%d incbypow2=%d "
                "incevery=%d charwidth=%d\n",
                forcepacked, incbypow2, incevery,
                1 * 4); /* dotwidth=1, combine8bits charwidth=4 */

        /* Expected per-scanline memaddr advance:
         * renderer advances memaddr by 4 per character (incevery=1)
         * for hdisp/charwidth iterations.
         * Then SVGA core resets to memaddr_backup and adds rowoffset<<3.
         */
        int render_advance = (svga->hdisp / 4) * 4; /* 4 bytes per char */
        int scanline_advance = svga->rowoffset << 3;
        nv3_log("NV3: RENDER_DIAG: EXPECTED: render_bytes_per_line=%d "
                "scanline_advance=%d (should both=%d for 640x8bpp)\n",
                render_advance, scanline_advance, svga->hdisp);

        nv3_log("NV3: ========== END RENDER DIAGNOSTIC ==========\n");
    }

    /*
     * Delayed diagnostic: fires after ~30 frames of steady state.
     * By this time the driver should have painted the desktop (either
     * via CPU writes or PGRAPH 2D blit operations), so we can see
     * whether VRAM actually contains desktop data or still holds
     * stale VGA text mode content.
     */
    if (nv3_diag_delayed_pending) {
        nv3_diag_frame_count++;
        if (nv3_diag_frame_count >= 30) {
            nv3_diag_delayed_pending = 0;

            nv3_log("NV3: ========== DELAYED RENDER DIAGNOSTIC "
                    "(frame %d) ==========\n", nv3_diag_frame_count);
            nv3_log("NV3: DELAYED_DIAG: memaddr=0x%x memaddr_backup=0x%x "
                    "memaddr_latch=0x%x\n",
                    svga->memaddr, svga->memaddr_backup,
                    svga->memaddr_latch);
            nv3_log("NV3: DELAYED_DIAG: rowoffset=%d (stride=%d) bpp=%d "
                    "hdisp=%d dispend=%d\n",
                    svga->rowoffset, svga->rowoffset << 3, svga->bpp,
                    svga->hdisp, svga->dispend);
            nv3_log("NV3: DELAYED_DIAG: scrblank=%d attr_palette_enable=%d "
                    "crtc17=0x%02x\n",
                    svga->scrblank, svga->attr_palette_enable,
                    svga->crtc[0x17]);

            /* Render function pointer comparison */
            int is_8hi  = (svga->render == svga_render_8bpp_highres);
            int is_8lo  = (svga->render == svga_render_8bpp_lowres);
            int is_16hi = (svga->render == svga_render_16bpp_highres);
            int is_32hi = (svga->render == svga_render_32bpp_highres);
            int is_blnk = (svga->render == svga_render_blank);
            nv3_log("NV3: DELAYED_DIAG: render=%p "
                    "is_8hi=%d is_8lo=%d is_16hi=%d is_32hi=%d "
                    "is_blank=%d\n",
                    (void *) svga->render,
                    is_8hi, is_8lo, is_16hi, is_32hi, is_blnk);

            /* Framebuffer write activity since mode switch */
            nv3_log("NV3: DELAYED_DIAG: lfb_write_count=%u "
                    "banked_write_count=%u\n",
                    nv3_lfb_write_count, nv3_banked_write_count);

            /* VRAM content inspection: check for 80-byte repeat pattern
             * (VGA text mode artifact: 80 columns * 1 byte = 80-byte period) */
            nv3_log("NV3: DELAYED_DIAG: VRAM[0..15] = "
                    "%02x %02x %02x %02x %02x %02x %02x %02x "
                    "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    svga->vram[0], svga->vram[1], svga->vram[2],
                    svga->vram[3], svga->vram[4], svga->vram[5],
                    svga->vram[6], svga->vram[7], svga->vram[8],
                    svga->vram[9], svga->vram[10], svga->vram[11],
                    svga->vram[12], svga->vram[13], svga->vram[14],
                    svga->vram[15]);
            nv3_log("NV3: DELAYED_DIAG: VRAM[80..95] = "
                    "%02x %02x %02x %02x %02x %02x %02x %02x "
                    "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    svga->vram[80], svga->vram[81], svga->vram[82],
                    svga->vram[83], svga->vram[84], svga->vram[85],
                    svga->vram[86], svga->vram[87], svga->vram[88],
                    svga->vram[89], svga->vram[90], svga->vram[91],
                    svga->vram[92], svga->vram[93], svga->vram[94],
                    svga->vram[95]);
            nv3_log("NV3: DELAYED_DIAG: VRAM[640..655] = "
                    "%02x %02x %02x %02x %02x %02x %02x %02x "
                    "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    svga->vram[640], svga->vram[641], svga->vram[642],
                    svga->vram[643], svga->vram[644], svga->vram[645],
                    svga->vram[646], svga->vram[647], svga->vram[648],
                    svga->vram[649], svga->vram[650], svga->vram[651],
                    svga->vram[652], svga->vram[653], svga->vram[654],
                    svga->vram[655]);

            /* Check if VRAM[0..15] == VRAM[80..95] (80-byte repeat = stale text) */
            int repeat_80 = 1;
            for (int i = 0; i < 16; i++) {
                if (svga->vram[i] != svga->vram[80 + i]) {
                    repeat_80 = 0;
                    break;
                }
            }
            nv3_log("NV3: DELAYED_DIAG: 80-byte repeat detected = %s\n",
                    repeat_80 ? "YES (stale VGA text data!)" : "no");

            nv3_log("NV3: ========== END DELAYED RENDER DIAGNOSTIC "
                    "==========\n");
        }
    }
}

/* ========================================================================
 * PLL frequency calculation.
 *
 * Per xf86-video-nv riva_hw.c CalcVClock() and envytools nv1-clock.xml:
 *
 * The NV3 PLL consists of a VCO and a post-divider:
 *   VCO_freq  = crystal_freq * N / M
 *   out_freq  = VCO_freq / (1 << P)
 *
 * Therefore:
 *   out_freq = (crystal_freq * N) / (M * (1 << P))
 *
 * Where:
 *   M = coeff[7:0]   (divider, minimum 1)
 *   N = coeff[15:8]  (multiplier, minimum 1)
 *   P = coeff[18:16] (post-divider exponent, 0-4)
 *
 * Returns frequency in Hz as a double.
 * ======================================================================== */
static double
nv3_pll_calc_freq(uint32_t crystal_freq, uint32_t pll_coeff)
{
    uint32_t m = NV3_PLL_M(pll_coeff);
    uint32_t n = NV3_PLL_N(pll_coeff);
    uint32_t p = NV3_PLL_P(pll_coeff);

    /* Guard against division by zero; real hardware clamps M >= 1 */
    if (m == 0)
        m = 1;
    if (n == 0)
        n = 1;

    return ((double) crystal_freq * (double) n) / ((double) m * (double) (1 << p));
}

/* ========================================================================
 * MMIO register read (32-bit, internal)
 *
 * All MMIO registers are 32-bit aligned internally. Byte/word reads
 * extract the appropriate portion.
 * ======================================================================== */
static uint32_t
nv3_mmio_read_internal(nv3_t *nv3, uint32_t addr)
{
    uint32_t ret = 0;

    addr &= 0xFFFFFF; /* mask to 16MB MMIO space */

    switch (addr) {
        /* ============================================================
         * PMC — Master Control (0x000000-0x000FFF)
         *
         * Per envytools pmc.xml: BOOT_0 is read-only chip ID.
         * INTR_0 is read-only aggregated interrupt status.
         * INTR_EN_0 and ENABLE are R/W.
         * ============================================================ */
        case NV3_PMC_BOOT_0:
            ret = nv3->pmc.boot_0;
            break;
        case NV3_PMC_INTR_0:
            /*
             * PMC_INTR_0 is read-only: it is recomputed from subsystem
             * interrupt state on every read.
             */
            nv3_pmc_update_intr(nv3);
            ret = nv3->pmc.intr_0;
            break;
        case NV3_PMC_INTR_EN_0:
            ret = nv3->pmc.intr_en_0;
            break;
        case NV3_PMC_ENABLE:
            ret = nv3->pmc.enable;
            break;

        /* ============================================================
         * PBUS — Bus Control (0x001000-0x001FFF)
         * ============================================================ */
        case NV3_PBUS_INTR_0:
            ret = nv3->pbus.intr_0;
            break;
        case NV3_PBUS_INTR_EN_0:
            ret = nv3->pbus.intr_en_0;
            break;

        /* ============================================================
         * PFIFO — Command FIFO (0x002000-0x003FFF)
         *
         * Per envytools nv1_pfifo.xml:
         * Interrupt, config, CACHE0, CACHE1, and RUNOUT registers
         * are handled from dedicated state fields. All other PFIFO
         * registers fall through to the register bank in the default
         * handler.
         * ============================================================ */
        case NV3_PFIFO_INTR_0:
            ret = nv3->pfifo.intr_0;
            break;
        case NV3_PFIFO_INTR_EN_0:
            ret = nv3->pfifo.intr_en_0;
            break;

        /* PFIFO config registers — return from dedicated state */
        case NV3_PFIFO_CACHES:
            ret = nv3->pfifo.caches_enabled;
            break;
        case NV3_PFIFO_CONFIG:
            ret = nv3->pfifo.config;
            break;
        case NV3_PFIFO_RAMHT:
            ret = nv3->pfifo.ramht_config;
            break;
        case NV3_PFIFO_RAMFC:
            ret = nv3->pfifo.ramfc_config;
            break;
        case NV3_PFIFO_RAMRO:
            ret = nv3->pfifo.ramro_config;
            break;

        /* CACHE0 registers */
        case NV3_PFIFO_CACHE0_PUSH0:
            ret = nv3->pfifo.cache0.push_enabled;
            break;
        case NV3_PFIFO_CACHE0_PULL0:
            ret = nv3->pfifo.cache0.pull_enabled;
            break;
        case NV3_PFIFO_CACHE0_PUT:
            ret = nv3->pfifo.cache0.put;
            break;
        case NV3_PFIFO_CACHE0_GET:
            ret = nv3->pfifo.cache0.get;
            break;
        case NV3_PFIFO_CACHE0_ADDR:
            ret = nv3->pfifo.cache0.entry.addr;
            break;
        case NV3_PFIFO_CACHE0_DATA:
            ret = nv3->pfifo.cache0.entry.data;
            break;

        /*
         * PFIFO CACHE0_STATUS: read-only, computed from PUT/GET.
         *
         * Per envytools nv1_pfifo.xml: STATUS is computed dynamically.
         * Bit 4 (EMPTY) = PUT == GET.
         * Bit 8 (FULL) = single-entry cache and PUT != GET.
         */
        case NV3_PFIFO_CACHE0_STATUS: {
            ret = 0;
            if (nv3->pfifo.cache0.put == nv3->pfifo.cache0.get)
                ret |= NV3_PFIFO_CACHE_STATUS_EMPTY;
            else
                ret |= NV3_PFIFO_CACHE_STATUS_FULL;
            break;
        }

        /* CACHE1 registers */
        case NV3_PFIFO_CACHE1_PUSH0:
            ret = nv3->pfifo.cache1.push_enabled;
            break;
        case NV3_PFIFO_CACHE1_PUSH1:
            ret = nv3->pfifo.cache1.push_channel;
            break;
        case NV3_PFIFO_CACHE1_PUT:
            ret = nv3->pfifo.cache1.put;
            break;
        case NV3_PFIFO_CACHE1_GET:
            ret = nv3->pfifo.cache1.get;
            break;
        case NV3_PFIFO_CACHE1_PULL0:
            ret = nv3->pfifo.cache1.pull_enabled;
            break;
        case NV3_PFIFO_CACHE1_PULL1:
            ret = nv3->pfifo.cache1.pull_engine;
            break;
        case NV3_PFIFO_CACHE1_DMA_PUSH:
            ret = nv3->pfifo.cache1.dma_push;
            break;
        case NV3_PFIFO_CACHE1_DMA_FETCH:
            ret = nv3->pfifo.cache1.dma_fetch;
            break;
        case NV3_PFIFO_CACHE1_HASH:
            ret = nv3->pfifo.cache1.hash;
            break;
        case NV3_PFIFO_CACHE1_ENGINE:
            ret = nv3->pfifo.cache1.engine;
            break;

        /*
         * PFIFO CACHE1_STATUS: read-only, computed from PUT/GET.
         *
         * Per envytools nv1_pfifo.xml: computed dynamically.
         * The NVIDIA Windows driver polls this waiting for EMPTY bit
         * after enabling PFIFO. Without this, the driver hangs.
         */
        case NV3_PFIFO_CACHE1_STATUS:
            ret = nv3_cache1_compute_status(nv3);
            break;

        /*
         * PFIFO RUNOUT registers.
         */
        case NV3_PFIFO_RUNOUT_PUT:
            ret = nv3->pfifo.runout.put;
            break;
        case NV3_PFIFO_RUNOUT_GET:
            ret = nv3->pfifo.runout.get;
            break;

        /*
         * PFIFO RUNOUT STATUS: read-only, computed from PUT/GET.
         *
         * Per envytools nv1_pfifo.xml: same bit layout as CACHE STATUS.
         * The driver polls this after CACHE1_STATUS during init.
         */
        case NV3_PFIFO_RUNOUT_STATUS: {
            ret = 0;
            if (nv3->pfifo.runout.put == nv3->pfifo.runout.get)
                ret |= NV3_PFIFO_CACHE_STATUS_EMPTY;
            break;
        }

        /* ============================================================
         * PTIMER — Programmable Interval Timer (0x009000-0x009FFF)
         *
         * Per envytools nv1-clock.xml:
         * TIME_0 = low 32 bits (bits 4:0 are sub-nanosecond, bits 31:5 = ns)
         * TIME_1 = high 29 bits of time
         * The 64-bit concatenation TIME_1:TIME_0 gives nanoseconds << 5.
         *
         * For simplicity, we use TIME_0 as low word and TIME_1 as high
         * word of a 64-bit nanosecond counter, matching the behavior
         * that drivers actually depend on.
         * ============================================================ */
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
        case NV3_PTIMER_TIME_1:
            /*
             * Compute PTIMER from CPU TSC to get a running nanosecond counter.
             * This prevents infinite timeout loops in driver init code that
             * polls PTIMER for elapsed time measurement.
             *
             * Formula: ns = tsc_cycles * 1e9 / cpuclock_hz
             * Then scaled by NUMERATOR/DENOMINATOR per envytools nv1-clock.xml.
             *
             * Fix 4: compute into local variables only -- do NOT write back
             * to nv3->ptimer.time_0 / time_1 here. Writing back corrupts
             * state during byte/word RMW sequences (the byte read triggers
             * a full dword read_internal, which would overwrite the stored
             * time, then the RMW merges the wrong value). The stored fields
             * should only be modified by explicit MMIO writes.
             */
            {
                double ns = (double) tsc * 1e9 / cpuclock;
                if (nv3->ptimer.denominator > 0)
                    ns = ns * (double) nv3->ptimer.numerator / (double) nv3->ptimer.denominator;
                uint64_t time64 = (uint64_t) ns;
                uint32_t t0_local = (uint32_t) (time64 & 0xFFFFFFFF);
                uint32_t t1_local = (uint32_t) (time64 >> 32);
                ret = (addr == NV3_PTIMER_TIME_0) ? t0_local : t1_local;
            }
            break;
        case NV3_PTIMER_ALARM_0:
            ret = nv3->ptimer.alarm_0;
            break;

        /* ============================================================
         * PFB — Framebuffer Interface (0x100000-0x100FFF)
         * ============================================================ */
        case NV3_PFB_BOOT_0:
            ret = nv3->pfb.boot_0;
            break;
        case NV3_PFB_CONFIG_0:
            ret = nv3->pfb.config_0;
            break;
        case NV3_PFB_CONFIG_1:
            ret = nv3->pfb.config_1;
            break;

        /* ============================================================
         * PEXTDEV — External Devices / Straps (0x101000-0x101FFF)
         * ============================================================ */
        case NV3_PEXTDEV_STRAPS:
            ret = nv3->pextdev.straps;
            break;

        /* ============================================================
         * PCRTC — Display Controller (0x600000-0x600FFF)
         *
         * Per envytools nv3_pcrtc.xml.
         * ============================================================ */
        case NV3_PCRTC_INTR:
            ret = nv3->pcrtc.intr;
            break;
        case NV3_PCRTC_INTR_EN:
            ret = nv3->pcrtc.intr_en;
            break;
        case NV3_PCRTC_CONFIG:
            ret = nv3->pcrtc.config;
            break;
        case NV3_PCRTC_START_ADDR:
            ret = nv3->pcrtc.start_addr;
            break;

        /* ============================================================
         * PGRAPH — Graphics Engine (0x400000-0x400FFF)
         * Stubs for Phase 2; expanded in Phase 4/5.
         * ============================================================ */
        case NV3_PGRAPH_INTR_0:
            ret = nv3->pgraph.intr_0;
            break;
        case NV3_PGRAPH_INTR_EN_0:
            ret = nv3->pgraph.intr_en_0;
            break;

        /* ============================================================
         * PRAMDAC — DAC / PLL / Cursor (0x680000-0x680FFF)
         *
         * Per envytools nv3_pramdac.xml.
         * ============================================================ */
        case NV3_PRAMDAC_NVPLL_COEFF:
            ret = nv3->pramdac.nvpll_coeff;
            break;
        case NV3_PRAMDAC_MPLL_COEFF:
            ret = nv3->pramdac.mpll_coeff;
            break;
        case NV3_PRAMDAC_VPLL_COEFF:
            ret = nv3->pramdac.vpll_coeff;
            break;
        case NV3_PRAMDAC_PLL_CONTROL:
            /* Per envytools nv3_pramdac.xml: PLL programming mode select */
            ret = nv3->pramdac.pll_control;
            break;
        case NV3_PRAMDAC_PLL_SETUP:
            ret = nv3->pramdac.pll_setup;
            break;
        case NV3_PRAMDAC_GENERAL_CTRL:
            ret = nv3->pramdac.general_control;
            break;
        case NV3_PRAMDAC_CURSOR_POS:
            /* Per envytools: bits 15:0 = X, bits 31:16 = Y */
            ret = nv3->pramdac.cursor_pos;
            break;

        /* ============================================================
         * Default handler: PCI config mirror, register banks, PRAMIN.
         *
         * Register banks provide store/readback for subsystem registers
         * that are not explicitly handled above. This prevents the
         * driver from seeing unexpected zero values on readback after
         * writes, which can cause init failures.
         * ============================================================ */
        default:
            if (addr >= NV3_PBUS_PCI_START && addr <= NV3_PBUS_PCI_END) {
                /* Mirror of PCI config space via nv3_pci_read().
                 * Must use the read function (not pci_regs[] directly) because
                 * vendor ID, device ID, class code etc. are returned as
                 * hardcoded constants from nv3_pci_read(), not stored in
                 * pci_regs[]. */
                int pci_reg = (addr - NV3_PBUS_PCI_START) & ~3;
                if (pci_reg < 256) {
                    ret = nv3_pci_read(0, pci_reg, 1, nv3)
                        | (nv3_pci_read(0, pci_reg + 1, 1, nv3) << 8)
                        | (nv3_pci_read(0, pci_reg + 2, 1, nv3) << 16)
                        | (nv3_pci_read(0, pci_reg + 3, 1, nv3) << 24);
                }
            } else if (addr >= NV3_PBUS_START && addr <= NV3_PBUS_END) {
                /*
                 * PBUS register bank for non-interrupt, non-PCI registers.
                 * The driver accesses debug/config registers (e.g. 0x1200)
                 * that we don't explicitly handle.
                 */
                uint32_t pbus_idx = (addr - NV3_PBUS_START) >> 2;
                if (pbus_idx < 1024)
                    ret = nv3->pbus.regs[pbus_idx];
            } else if (addr >= NV3_PRAMDAC_START && addr <= NV3_PRAMDAC_END) {
                /*
                 * PRAMDAC register bank for unhandled registers.
                 * The driver probes various PRAMDAC registers during init
                 * (e.g., 0x680000, 0x68010C, 0x680110, 0x680610, 0x680710).
                 * These must store and readback written values.
                 */
                uint32_t pramdac_idx = (addr - NV3_PRAMDAC_START) >> 2;
                if (pramdac_idx < 1024)
                    ret = nv3->pramdac.regs[pramdac_idx];
            } else if (addr >= NV3_PCRTC_START && addr <= NV3_PCRTC_END) {
                /*
                 * PCRTC register bank for unhandled registers.
                 */
                uint32_t pcrtc_idx = (addr - NV3_PCRTC_START) >> 2;
                if (pcrtc_idx < 1024)
                    ret = nv3->pcrtc.regs[pcrtc_idx];
            } else if (addr >= NV3_PGRAPH_START && addr <= NV3_PGRAPH_END) {
                /*
                 * PGRAPH register bank: store/return values for driver
                 * init readback. Registers handled in the switch above
                 * (INTR_0, INTR_EN_0) never reach here.
                 * Full PGRAPH engine deferred to Phase 4/5.
                 */
                uint32_t pgraph_idx = (addr - NV3_PGRAPH_START) >> 2;
                if (pgraph_idx < 1024)
                    ret = nv3->pgraph.regs[pgraph_idx];
            } else if (addr >= NV3_PFIFO_START && addr <= NV3_PFIFO_END) {
                /*
                 * PFIFO register bank.
                 * Registers explicitly handled in the switch above never
                 * reach here. This handles:
                 *   - CACHE1 entry array (0x3800-0x3FFF) — reads from
                 *     the dedicated entries[] array
                 *   - Misc debug/config registers that the driver writes
                 *     and reads back
                 */
                if (addr >= NV3_PFIFO_CACHE1_ADDR_START
                    && addr < (NV3_PFIFO_CACHE1_ADDR_START + NV3T_CACHE1_SIZE * NV3_PFIFO_CACHE1_ENTRY_STRIDE)) {
                    /* CACHE1 entry array: decode index and field */
                    uint32_t entry_off = addr - NV3_PFIFO_CACHE1_ADDR_START;
                    uint32_t entry_idx = entry_off / NV3_PFIFO_CACHE1_ENTRY_STRIDE;
                    uint32_t field     = entry_off % NV3_PFIFO_CACHE1_ENTRY_STRIDE;
                    uint32_t max_entries = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                                           ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE;
                    if (entry_idx < max_entries) {
                        if (field == 0)
                            ret = nv3->pfifo.cache1.entries[entry_idx].addr;
                        else if (field == 4)
                            ret = nv3->pfifo.cache1.entries[entry_idx].data;
                    }
                } else {
                    uint32_t pfifo_idx = (addr - NV3_PFIFO_START) >> 2;
                    if (pfifo_idx < 2048)
                        ret = nv3->pfifo.regs[pfifo_idx];
                }
            } else if (addr >= NV3_PRAMIN_START && addr <= NV3_PRAMIN_END) {
                /*
                 * PRAMIN: 1MB window into the top of VRAM.
                 * Used by the driver to store DMA objects, hash tables
                 * (RAMHT), and channel contexts (RAMFC).
                 *
                 * VRAM address = (vram_size - 1MB) + pramin_offset
                 *
                 * For 4MB card: offset 0 => VRAM 0x300000
                 * For 8MB card: offset 0 => VRAM 0x700000
                 */
                uint32_t pramin_offset = addr - NV3_PRAMIN_START;
                uint32_t vram_offset = (nv3->vram_size - NV3_PRAMIN_VRAM_SIZE) + pramin_offset;
                if (vram_offset + 3 < nv3->vram_size)
                    ret = *(uint32_t *) &nv3->svga.vram[vram_offset];
            } else if (addr >= NV3_PMC_START && addr <= NV3_PMC_END) {
                /*
                 * PMC: BOOT_0, INTR_0, INTR_EN_0, ENABLE are handled
                 * above. Other PMC addresses are silently ignored (the
                 * driver writes to BOOT_0 which is read-only).
                 */
            } else if (addr >= NV3_PFB_START && addr <= NV3_PFB_END) {
                /*
                 * PFB register bank for registers not handled in the
                 * switch statement above (e.g. 0x100044 DELAY, 0x100080
                 * DEBUG_0, 0x1000C0 GREEN_0). The explicitly handled
                 * registers (BOOT_0, CONFIG_0, CONFIG_1) are synced into
                 * the bank so reads through either path return the same value.
                 */
                uint32_t pfb_idx = (addr - NV3_PFB_START) >> 2;
                if (pfb_idx < 1024)
                    ret = nv3->pfb.regs[pfb_idx];
            } else if (addr >= NV3_PEXTDEV_START && addr <= NV3_PEXTDEV_END) {
                /*
                 * PEXTDEV register bank for registers not handled in the
                 * switch statement above (e.g. 0x101114, 0x101200).
                 * The STRAPS register is synced into the bank at init.
                 */
                uint32_t pextdev_idx = (addr - NV3_PEXTDEV_START) >> 2;
                if (pextdev_idx < 1024)
                    ret = nv3->pextdev.regs[pextdev_idx];
            } else if (addr >= NV3_PME_START && addr <= NV3_PME_END) {
                /*
                 * PMEDIA (Mediaport) register space.
                 * Per envytools: INTR at 0x200100, INTR_EN at 0x200140.
                 * Other offsets go through the register bank.
                 */
                switch (addr) {
                    case 0x200100:
                        ret = nv3->pme.intr_0;
                        break;
                    case 0x200140:
                        ret = nv3->pme.intr_en_0;
                        break;
                    default: {
                        uint32_t pme_idx = (addr - NV3_PME_START) >> 2;
                        if (pme_idx < 1024)
                            ret = nv3->pme.regs[pme_idx];
                        break;
                    }
                }
            } else if (addr >= NV3_PROM_START && addr <= NV3_PROM_END) {
                /*
                 * PROM: read BIOS ROM data through MMIO.
                 * Maps the video BIOS as a byte-addressable read window.
                 */
                uint32_t rom_off = addr - NV3_PROM_START;
                if (rom_off + 3 < (uint32_t) nv3->bios_rom.sz) {
                    ret = (uint32_t) nv3->bios_rom.rom[rom_off]
                        | ((uint32_t) nv3->bios_rom.rom[rom_off + 1] << 8)
                        | ((uint32_t) nv3->bios_rom.rom[rom_off + 2] << 16)
                        | ((uint32_t) nv3->bios_rom.rom[rom_off + 3] << 24);
                }
            } else if (addr >= NV3_USER_START && addr <= NV3_USER_END) {
                /*
                 * USER space: PIO channel submission interface.
                 *
                 * Per rivafb riva_hw.h and envytools fifo/nv1-pfifo.html:
                 * Each channel has a 64KB window. Offset 0x10 within a
                 * channel is FifoFree — the number of 32-bit entries
                 * the FIFO can still accept. The driver polls this before
                 * submitting methods.
                 *
                 * We compute FifoFree from the actual CACHE1 free count,
                 * multiplied by 4 to convert from entries to bytes
                 * (the rivafb driver expects byte units per riva_hw.h).
                 */
                uint32_t channel_offset = (addr - NV3_USER_START) % NV3_USER_CHANNEL_STRIDE;
                uint32_t subchan_offset = channel_offset & (NV3_USER_SUBCHAN_STRIDE - 1);
                if (subchan_offset == NV3_USER_FREE_OFFSET) {
                    /*
                     * Report CACHE1 free space.
                     * Since the puller runs synchronously in our emulation,
                     * the cache is almost always empty. But compute the
                     * real value for correctness.
                     */
                    ret = nv3_cache1_free_count(nv3) * 4;
                } else {
                    ret = 0;
                }
            } else {
                /*
                 * Fix 1: Truly unmapped MMIO space.
                 *
                 * On real NV3 hardware, reading addresses that don't belong
                 * to any known subsystem returns 0x00000000 (bus floats low
                 * within the BAR0 decode range). The previous implementation
                 * used a mmio_fallback[] array that stored written values and
                 * returned them, which made the driver think it found a device
                 * at unmapped addresses like 0x030000-0x03FFFF, causing it to
                 * enter infinite init loops.
                 *
                 * Known subsystem ranges are already covered by the if-else
                 * chain above. Anything that falls through here is genuinely
                 * unmapped on NV3 and should return 0.
                 */
                ret = 0x00000000;
            }
            break;
    }

#if ENABLE_NV3_TRACE
    if (nv3_trace_active && nv3_trace_count < NV3_TRACE_LIMIT) {
        nv3_trace_count++;
        pclog("NV3 R %06x = %08x\n", addr, ret);
    }
#endif

    nv3_mmio_trace_log("NV3 MMIO R %06X = %08X\n", addr, ret);

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
        /* ============================================================
         * PMC — Master Control
         * ============================================================ */
        case NV3_PMC_INTR_0:
            /*
             * Per envytools pmc.xml: PMC_INTR_0 on NV3 is READ-ONLY.
             * It is an aggregation of subsystem interrupts, not a
             * write-to-clear register. Writes are ignored.
             *
             * (On NV4+, some bits become write-to-clear, but on NV3
             * the driver clears interrupts by writing to the individual
             * subsystem INTR_0 registers instead.)
             */
            break;
        case NV3_PMC_INTR_EN_0:
            /* Per envytools: only bits 1:0 are valid (enum, not bitmask) */
            nv3->pmc.intr_en_0 = val & 0x03;
            nv3_update_irq(nv3);
            break;
        case NV3_PMC_ENABLE:
            nv3->pmc.enable = val;
#if ENABLE_NV3_TRACE
            if (!nv3_trace_active && val == 0) {
                nv3_trace_active = 1;
                pclog("NV3: === MMIO TRACE START (PMC_ENABLE=0 reset) ===\n");
            }
#endif
            break;

        /* ============================================================
         * PBUS — Bus Control
         * ============================================================ */
        case NV3_PBUS_INTR_0:
            /* Write 1 to clear (Fix 5: W1C RMW corruption note — see PCRTC_INTR) */
            nv3->pbus.intr_0 &= ~val;
            nv3_update_irq(nv3);
            break;
        case NV3_PBUS_INTR_EN_0:
            nv3->pbus.intr_en_0 = val;
            nv3_update_irq(nv3);
            break;

        /* ============================================================
         * PFIFO — Command FIFO (0x002000-0x003FFF)
         *
         * Per envytools nv1_pfifo.xml:
         * All PFIFO registers are handled from dedicated state.
         * STATUS registers are read-only.
         * ============================================================ */
        case NV3_PFIFO_INTR_0:
            /* Write 1 to clear */
            nv3->pfifo.intr_0 &= ~val;
            nv3->pfifo.regs[(NV3_PFIFO_INTR_0 - NV3_PFIFO_START) >> 2] = nv3->pfifo.intr_0;
            nv3_update_irq(nv3);
            break;
        case NV3_PFIFO_INTR_EN_0:
            nv3->pfifo.intr_en_0 = val;
            nv3->pfifo.regs[(NV3_PFIFO_INTR_EN_0 - NV3_PFIFO_START) >> 2] = val;
            nv3_update_irq(nv3);
            break;

        /* PFIFO config registers */
        case NV3_PFIFO_CACHES:
            nv3->pfifo.caches_enabled = val & 1;
            nv3->pfifo.regs[(NV3_PFIFO_CACHES - NV3_PFIFO_START) >> 2] = nv3->pfifo.caches_enabled;
            nv3_log("NV3: PFIFO CACHES = %d (master %s)\n",
                    nv3->pfifo.caches_enabled,
                    nv3->pfifo.caches_enabled ? "enabled" : "disabled");
            break;
        case NV3_PFIFO_CONFIG:
            nv3->pfifo.config = val & 0xFF; /* bits 7:0 = per-channel DMA/PIO */
            nv3->pfifo.regs[(NV3_PFIFO_CONFIG - NV3_PFIFO_START) >> 2] = nv3->pfifo.config;
            break;
        case NV3_PFIFO_RAMHT:
            nv3->pfifo.ramht_config = val;
            nv3->pfifo.regs[(NV3_PFIFO_RAMHT - NV3_PFIFO_START) >> 2] = val;
            nv3_log("NV3: PFIFO RAMHT config = 0x%08X (base=0x%05X size=%dKB)\n",
                    val,
                    (val & NV3_PFIFO_RAMHT_BASE_MASK) << 8,
                    4 << ((val >> NV3_PFIFO_RAMHT_SIZE_SHIFT) & 0x3));
            break;
        case NV3_PFIFO_RAMFC:
            nv3->pfifo.ramfc_config = val;
            nv3->pfifo.regs[(NV3_PFIFO_RAMFC - NV3_PFIFO_START) >> 2] = val;
            nv3_log("NV3: PFIFO RAMFC config = 0x%08X (base=0x%05X)\n",
                    val, (val & NV3_PFIFO_RAMFC_BASE_MASK) << 8);
            break;
        case NV3_PFIFO_RAMRO:
            nv3->pfifo.ramro_config = val;
            nv3->pfifo.regs[(NV3_PFIFO_RAMRO - NV3_PFIFO_START) >> 2] = val;
            nv3_log("NV3: PFIFO RAMRO config = 0x%08X (base=0x%05X size=%s)\n",
                    val,
                    (val & NV3_PFIFO_RAMRO_BASE_MASK) << 8,
                    (val & NV3_PFIFO_RAMRO_SIZE_BIT) ? "8KB" : "512B");
            break;

        /* CACHE0 registers */
        case NV3_PFIFO_CACHE0_PUSH0:
            nv3->pfifo.cache0.push_enabled = val & 1;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE0_PUSH0 - NV3_PFIFO_START) >> 2] = val & 1;
            break;
        case NV3_PFIFO_CACHE0_PULL0:
            nv3->pfifo.cache0.pull_enabled = val & 1;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE0_PULL0 - NV3_PFIFO_START) >> 2] = val & 1;
            break;
        case NV3_PFIFO_CACHE0_PUT:
            nv3->pfifo.cache0.put = val & 1; /* single entry: 1 bit */
            nv3->pfifo.regs[(NV3_PFIFO_CACHE0_PUT - NV3_PFIFO_START) >> 2] = val & 1;
            break;
        case NV3_PFIFO_CACHE0_GET:
            nv3->pfifo.cache0.get = val & 1;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE0_GET - NV3_PFIFO_START) >> 2] = val & 1;
            break;
        case NV3_PFIFO_CACHE0_ADDR:
            nv3->pfifo.cache0.entry.addr = val;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE0_ADDR - NV3_PFIFO_START) >> 2] = val;
            break;
        case NV3_PFIFO_CACHE0_DATA:
            nv3->pfifo.cache0.entry.data = val;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE0_DATA - NV3_PFIFO_START) >> 2] = val;
            break;

        /* CACHE1 registers */
        case NV3_PFIFO_CACHE1_PUSH0:
            nv3->pfifo.cache1.push_enabled = val & 1;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_PUSH0 - NV3_PFIFO_START) >> 2] = val & 1;
            break;
        case NV3_PFIFO_CACHE1_PUSH1:
            nv3->pfifo.cache1.push_channel = val & 0x7; /* 3 bits for 8 channels */
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_PUSH1 - NV3_PFIFO_START) >> 2] = val & 0x7;
            break;
        case NV3_PFIFO_CACHE1_PUT: {
            uint32_t mask = ((nv3->card_type >= NV3_TYPE_NV3T_PCI)
                             ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE) - 1;
            nv3->pfifo.cache1.put = val & mask;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_PUT - NV3_PFIFO_START) >> 2] = val & mask;
            break;
        }
        case NV3_PFIFO_CACHE1_GET: {
            uint32_t mask = ((nv3->card_type >= NV3_TYPE_NV3T_PCI)
                             ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE) - 1;
            nv3->pfifo.cache1.get = val & mask;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_GET - NV3_PFIFO_START) >> 2] = val & mask;
            break;
        }
        case NV3_PFIFO_CACHE1_PULL0:
            nv3->pfifo.cache1.pull_enabled = val & 1;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_PULL0 - NV3_PFIFO_START) >> 2] = val & 1;
            /*
             * When the puller is enabled, run it immediately to drain
             * any pending entries in CACHE1.
             */
            if (nv3->pfifo.cache1.pull_enabled)
                nv3_pfifo_puller_run(nv3);
            break;
        case NV3_PFIFO_CACHE1_PULL1:
            nv3->pfifo.cache1.pull_engine = val;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_PULL1 - NV3_PFIFO_START) >> 2] = val;
            break;
        case NV3_PFIFO_CACHE1_DMA_PUSH:
            nv3->pfifo.cache1.dma_push = val & 1;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_DMA_PUSH - NV3_PFIFO_START) >> 2] = val & 1;
            break;
        case NV3_PFIFO_CACHE1_DMA_FETCH:
            nv3->pfifo.cache1.dma_fetch = val;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_DMA_FETCH - NV3_PFIFO_START) >> 2] = val;
            break;
        case NV3_PFIFO_CACHE1_HASH:
            nv3->pfifo.cache1.hash = val;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_HASH - NV3_PFIFO_START) >> 2] = val;
            break;
        case NV3_PFIFO_CACHE1_ENGINE:
            nv3->pfifo.cache1.engine = val;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE1_ENGINE - NV3_PFIFO_START) >> 2] = val;
            break;

        /* Runout registers */
        case NV3_PFIFO_RUNOUT_PUT:
            nv3->pfifo.runout.put = val;
            nv3->pfifo.regs[(NV3_PFIFO_RUNOUT_PUT - NV3_PFIFO_START) >> 2] = val;
            break;
        case NV3_PFIFO_RUNOUT_GET:
            nv3->pfifo.runout.get = val;
            nv3->pfifo.regs[(NV3_PFIFO_RUNOUT_GET - NV3_PFIFO_START) >> 2] = val;
            break;

        /* Read-only STATUS registers: ignore writes */
        case NV3_PFIFO_CACHE0_STATUS:
        case NV3_PFIFO_CACHE1_STATUS:
        case NV3_PFIFO_RUNOUT_STATUS:
            break;

        /* ============================================================
         * PTIMER
         * ============================================================ */
        case NV3_PTIMER_INTR_0:
            /* Write 1 to clear (Fix 5: W1C RMW corruption note — see PCRTC_INTR) */
            nv3->ptimer.intr_0 &= ~val;
            nv3_update_irq(nv3);
            break;
        case NV3_PTIMER_INTR_EN_0:
            nv3->ptimer.intr_en_0 = val;
            nv3_update_irq(nv3);
            break;
        case NV3_PTIMER_NUMERATOR:
            /* Per envytools: bits 15:0 are the numerator value */
            nv3->ptimer.numerator = val & 0xFFFF;
            break;
        case NV3_PTIMER_DENOMINATOR:
            /* Per envytools: bits 15:0 are the denominator value */
            nv3->ptimer.denominator = val & 0xFFFF;
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

        /* ============================================================
         * PFB — Framebuffer Interface
         *
         * PFB_BOOT_0 is read-only (hardware-determined).
         * CONFIG_0 and CONFIG_1 are writable.
         * ============================================================ */
        case NV3_PFB_BOOT_0:
            /* Read-only: ignore writes */
            break;
        case NV3_PFB_CONFIG_0:
            nv3->pfb.config_0 = val;
            /* Sync to register bank for consistent readback through default path */
            nv3->pfb.regs[(NV3_PFB_CONFIG_0 - NV3_PFB_START) >> 2] = val;
            break;
        case NV3_PFB_CONFIG_1:
            nv3->pfb.config_1 = val;
            /* Sync to register bank for consistent readback through default path */
            nv3->pfb.regs[(NV3_PFB_CONFIG_1 - NV3_PFB_START) >> 2] = val;
            break;

        /* PEXTDEV — straps are read-only, writes ignored */
        case NV3_PEXTDEV_STRAPS:
            break;

        /* ============================================================
         * PCRTC — Display Controller
         * ============================================================ */
        case NV3_PCRTC_INTR:
            /*
             * Write 1 to clear VBlank interrupt.
             *
             * Fix 5 note: byte/word RMW on this W1C register can
             * spuriously clear bits in unwritten bytes, because the
             * byte/word write path reads the full dword via
             * read_internal, merges, then writes the full dword here.
             * A proper fix requires access-width awareness in the
             * write path. This is a known limitation for now.
             */
            nv3->pcrtc.intr &= ~val;
            /* Fix 2: sync to register bank */
            nv3->pcrtc.regs[(NV3_PCRTC_INTR - NV3_PCRTC_START) >> 2] = nv3->pcrtc.intr;
            nv3_update_irq(nv3);
            break;
        case NV3_PCRTC_INTR_EN:
            nv3->pcrtc.intr_en = val;
            /* Fix 2: sync to register bank */
            nv3->pcrtc.regs[(NV3_PCRTC_INTR_EN - NV3_PCRTC_START) >> 2] = val;
            nv3_update_irq(nv3);
            break;
        case NV3_PCRTC_CONFIG:
            nv3->pcrtc.config = val;
            /* Fix 2: sync to register bank */
            nv3->pcrtc.regs[(NV3_PCRTC_CONFIG - NV3_PCRTC_START) >> 2] = val;
            break;
        case NV3_PCRTC_START_ADDR:
            nv3->pcrtc.start_addr = val;
            /* Fix 2: sync to register bank */
            nv3->pcrtc.regs[(NV3_PCRTC_START_ADDR - NV3_PCRTC_START) >> 2] = val;
            break;

        /* ============================================================
         * PGRAPH — Graphics Engine (stubs)
         * ============================================================ */
        case NV3_PGRAPH_INTR_0:
            /* Write 1 to clear (Fix 5: W1C RMW corruption note — see PCRTC_INTR) */
            nv3->pgraph.intr_0 &= ~val;
            /* Fix 2: sync to register bank */
            nv3->pgraph.regs[(NV3_PGRAPH_INTR_0 - NV3_PGRAPH_START) >> 2] = nv3->pgraph.intr_0;
            nv3_update_irq(nv3);
            break;
        case NV3_PGRAPH_INTR_EN_0:
            nv3->pgraph.intr_en_0 = val;
            /* Fix 2: sync to register bank */
            nv3->pgraph.regs[(NV3_PGRAPH_INTR_EN_0 - NV3_PGRAPH_START) >> 2] = val;
            nv3_update_irq(nv3);
            break;

        /* ============================================================
         * PRAMDAC — DAC / PLL / Cursor
         *
         * Writing to PLL coefficient registers reprograms the clock.
         * We trigger a recalctimings on VPLL write since that affects
         * the pixel clock used for display timing.
         * ============================================================ */
        case NV3_PRAMDAC_NVPLL_COEFF:
            nv3->pramdac.nvpll_coeff = val;
            /* Fix 2: sync dedicated field to register bank for consistent readback */
            nv3->pramdac.regs[(NV3_PRAMDAC_NVPLL_COEFF - NV3_PRAMDAC_START) >> 2] = val;
            nv3_log("NV3: NVPLL write M=%d N=%d P=%d\n",
                     NV3_PLL_M(val), NV3_PLL_N(val), NV3_PLL_P(val));
            break;
        case NV3_PRAMDAC_MPLL_COEFF:
            nv3->pramdac.mpll_coeff = val;
            /* Fix 2: sync dedicated field to register bank for consistent readback */
            nv3->pramdac.regs[(NV3_PRAMDAC_MPLL_COEFF - NV3_PRAMDAC_START) >> 2] = val;
            nv3_log("NV3: MPLL write M=%d N=%d P=%d\n",
                     NV3_PLL_M(val), NV3_PLL_N(val), NV3_PLL_P(val));
            break;
        case NV3_PRAMDAC_VPLL_COEFF:
            nv3->pramdac.vpll_coeff = val;
            /* Fix 2: sync dedicated field to register bank for consistent readback */
            nv3->pramdac.regs[(NV3_PRAMDAC_VPLL_COEFF - NV3_PRAMDAC_START) >> 2] = val;
            nv3_log("NV3: VPLL write addr=0x%06X val=0x%08X M=%d N=%d P=%d freq=%.2f MHz\n",
                     addr, val,
                     NV3_PLL_M(val), NV3_PLL_N(val), NV3_PLL_P(val),
                     nv3_pll_calc_freq(nv3->crystal_freq, val) / 1000000.0);
            /*
             * Trigger SVGA timing recalculation since the pixel clock
             * has changed. This makes the new resolution take effect.
             */
            nv3->svga.fullchange = changeframecount;
            svga_recalctimings(&nv3->svga);
            break;
        case NV3_PRAMDAC_PLL_CONTROL:
            /*
             * Per envytools nv3_pramdac.xml: PLL programming mode.
             * VPLL_PROG (bit 16) selects programmable VPLL for pixel clock.
             * Writing this affects which clock source is used, so trigger
             * a recalc.
             */
            nv3->pramdac.pll_control = val;
            /* Fix 2: sync dedicated field to register bank for consistent readback */
            nv3->pramdac.regs[(NV3_PRAMDAC_PLL_CONTROL - NV3_PRAMDAC_START) >> 2] = val;
            nv3_log("NV3: PLL_CONTROL write addr=0x%06X val=0x%08X "
                     "DLL_PROG=%d MPLL_PROG=%d VPLL_PROG=%d PCLK_SRC=%d VCLK_DB2=%d\n",
                     addr, val,
                     (val & NV3_PLL_CTRL_DLL_PROG) ? 1 : 0,
                     (val & NV3_PLL_CTRL_MPLL_PROG) ? 1 : 0,
                     (val & NV3_PLL_CTRL_VPLL_PROG) ? 1 : 0,
                     (int) ((val & NV3_PLL_CTRL_PCLK_SRC_MASK) >> NV3_PLL_CTRL_PCLK_SRC_SHIFT),
                     (val & NV3_PLL_CTRL_VCLK_DB2) ? 1 : 0);
            nv3->svga.fullchange = changeframecount;
            svga_recalctimings(&nv3->svga);
            break;
        case NV3_PRAMDAC_PLL_SETUP:
            nv3->pramdac.pll_setup = val;
            /* Fix 2: sync dedicated field to register bank for consistent readback */
            nv3->pramdac.regs[(NV3_PRAMDAC_PLL_SETUP - NV3_PRAMDAC_START) >> 2] = val;
            break;
        case NV3_PRAMDAC_GENERAL_CTRL:
            nv3->pramdac.general_control = val;
            /* Fix 2: sync dedicated field to register bank for consistent readback */
            nv3->pramdac.regs[(NV3_PRAMDAC_GENERAL_CTRL - NV3_PRAMDAC_START) >> 2] = val;
            nv3_log("NV3: PRAMDAC GENERAL_CTRL write 0x%08x\n", val);
            /*
             * General control affects DAC bit depth and display mode.
             * Trigger recalc in case DAC mode changed.
             */
            nv3->svga.fullchange = changeframecount;
            svga_recalctimings(&nv3->svga);
            break;
        case NV3_PRAMDAC_CURSOR_POS:
            /* Per envytools: bits 15:0 = X, bits 31:16 = Y */
            nv3->pramdac.cursor_pos = val;
            /* Fix 2: sync dedicated field to register bank for consistent readback */
            nv3->pramdac.regs[(NV3_PRAMDAC_CURSOR_POS - NV3_PRAMDAC_START) >> 2] = val;
            nv3->svga.hwcursor.x = val & 0xFFFF;
            nv3->svga.hwcursor.y = (val >> 16) & 0xFFFF;
            break;

        /* ============================================================
         * Default: PCI config mirror, register banks, PRAMIN.
         *
         * Register banks provide store/readback for subsystem registers
         * that are not explicitly handled above. This prevents the
         * driver from seeing unexpected zero values on readback after
         * writes, which can cause init failures.
         * ============================================================ */
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
            } else if (addr >= NV3_PBUS_START && addr <= NV3_PBUS_END) {
                /*
                 * PBUS register bank for non-interrupt, non-PCI registers.
                 */
                uint32_t pbus_idx = (addr - NV3_PBUS_START) >> 2;
                if (pbus_idx < 1024)
                    nv3->pbus.regs[pbus_idx] = val;
            } else if (addr >= NV3_PRAMDAC_START && addr <= NV3_PRAMDAC_END) {
                /*
                 * PRAMDAC register bank for unhandled registers.
                 */
                uint32_t pramdac_idx = (addr - NV3_PRAMDAC_START) >> 2;
                if (pramdac_idx < 1024)
                    nv3->pramdac.regs[pramdac_idx] = val;
            } else if (addr >= NV3_PCRTC_START && addr <= NV3_PCRTC_END) {
                /*
                 * PCRTC register bank for unhandled registers.
                 */
                uint32_t pcrtc_idx = (addr - NV3_PCRTC_START) >> 2;
                if (pcrtc_idx < 1024)
                    nv3->pcrtc.regs[pcrtc_idx] = val;
            } else if (addr >= NV3_PGRAPH_START && addr <= NV3_PGRAPH_END) {
                /*
                 * PGRAPH register bank: accept writes for driver init
                 * readback. Registers handled in the switch above
                 * (INTR_0, INTR_EN_0) never reach here.
                 * Full PGRAPH engine deferred to Phase 4/5.
                 */
                uint32_t pgraph_idx = (addr - NV3_PGRAPH_START) >> 2;
                if (pgraph_idx < 1024)
                    nv3->pgraph.regs[pgraph_idx] = val;
            } else if (addr >= NV3_PFIFO_START && addr <= NV3_PFIFO_END) {
                /*
                 * PFIFO register bank.
                 * Registers explicitly handled in the switch above never
                 * reach here. This handles CACHE1 entry array writes
                 * and misc debug/config registers.
                 */
                if (addr >= NV3_PFIFO_CACHE1_ADDR_START
                    && addr < (NV3_PFIFO_CACHE1_ADDR_START + NV3T_CACHE1_SIZE * NV3_PFIFO_CACHE1_ENTRY_STRIDE)) {
                    /* CACHE1 entry array write */
                    uint32_t entry_off = addr - NV3_PFIFO_CACHE1_ADDR_START;
                    uint32_t entry_idx = entry_off / NV3_PFIFO_CACHE1_ENTRY_STRIDE;
                    uint32_t field     = entry_off % NV3_PFIFO_CACHE1_ENTRY_STRIDE;
                    uint32_t max_entries = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                                           ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE;
                    if (entry_idx < max_entries) {
                        if (field == 0)
                            nv3->pfifo.cache1.entries[entry_idx].addr = val;
                        else if (field == 4)
                            nv3->pfifo.cache1.entries[entry_idx].data = val;
                    }
                }
                /* Always store in register bank too for consistency */
                uint32_t pfifo_idx = (addr - NV3_PFIFO_START) >> 2;
                if (pfifo_idx < 2048)
                    nv3->pfifo.regs[pfifo_idx] = val;
            } else if (addr >= NV3_PRAMIN_START && addr <= NV3_PRAMIN_END) {
                /*
                 * PRAMIN: 1MB window into the top of VRAM.
                 * Writes go directly to VRAM and mark the page dirty
                 * for display refresh tracking.
                 */
                uint32_t pramin_offset = addr - NV3_PRAMIN_START;
                uint32_t vram_offset = (nv3->vram_size - NV3_PRAMIN_VRAM_SIZE) + pramin_offset;
                if (vram_offset + 3 < nv3->vram_size) {
                    *(uint32_t *) &nv3->svga.vram[vram_offset] = val;
                    nv3->svga.changedvram[vram_offset >> 12] = changeframecount;
                }
            } else if (addr >= NV3_PMC_START && addr <= NV3_PMC_END) {
                /*
                 * PMC: INTR_0, INTR_EN_0, ENABLE handled above.
                 * Other PMC writes (e.g. to BOOT_0 which is read-only)
                 * are silently dropped.
                 */
            } else if (addr >= NV3_PFB_START && addr <= NV3_PFB_END) {
                /*
                 * PFB register bank for unhandled registers.
                 * Note: BOOT_0, CONFIG_0, CONFIG_1 are handled in the
                 * switch above and synced to the bank there.
                 */
                uint32_t pfb_idx = (addr - NV3_PFB_START) >> 2;
                if (pfb_idx < 1024)
                    nv3->pfb.regs[pfb_idx] = val;
            } else if (addr >= NV3_PEXTDEV_START && addr <= NV3_PEXTDEV_END) {
                /*
                 * PEXTDEV register bank for unhandled registers.
                 * Straps at offset 0 are read-only (handled in switch above).
                 */
                uint32_t pextdev_idx = (addr - NV3_PEXTDEV_START) >> 2;
                if (pextdev_idx < 1024)
                    nv3->pextdev.regs[pextdev_idx] = val;
            } else if (addr >= NV3_PME_START && addr <= NV3_PME_END) {
                /*
                 * PMEDIA (Mediaport) register space.
                 * Per envytools: INTR at 0x200100 (write-1-to-clear),
                 * INTR_EN at 0x200140.
                 */
                switch (addr) {
                    case 0x200100:
                        nv3->pme.intr_0 &= ~val;
                        nv3_update_irq(nv3);
                        break;
                    case 0x200140:
                        nv3->pme.intr_en_0 = val;
                        nv3_update_irq(nv3);
                        break;
                    default: {
                        uint32_t pme_idx = (addr - NV3_PME_START) >> 2;
                        if (pme_idx < 1024)
                            nv3->pme.regs[pme_idx] = val;
                        break;
                    }
                }
            } else if (addr >= NV3_PROM_START && addr <= NV3_PROM_END) {
                /* PROM is read-only, ignore writes */
            } else if (addr >= NV3_USER_START && addr <= NV3_USER_END) {
                /*
                 * USER space: PIO channel method submissions.
                 *
                 * Per envytools fifo/nv1-pfifo.html:
                 * Each channel has a 64KB window. Within each window,
                 * 8 subchannels at 0x2000 stride. Within each subchannel:
                 *   0x0000 = SetObject (bind handle)
                 *   0x0100+ = methods
                 *
                 * Only process if CACHES is enabled (PFIFO master enable).
                 */
                if (nv3->pfifo.caches_enabled) {
                    uint32_t user_off     = addr - NV3_USER_START;
                    uint32_t channel      = user_off / NV3_USER_CHANNEL_STRIDE;
                    uint32_t chan_off      = user_off % NV3_USER_CHANNEL_STRIDE;
                    uint32_t subchan      = (chan_off / NV3_USER_SUBCHAN_STRIDE) & 0x7;
                    uint32_t method       = chan_off & (NV3_USER_SUBCHAN_STRIDE - 1);

                    if (channel < NV3_PFIFO_NUM_CHANNELS) {
                        nv3_pfifo_push_pio(nv3, channel, subchan, method, val);
                    }
                }
            } else {
                /*
                 * Fix 1: Truly unmapped MMIO space -- silently drop writes.
                 *
                 * All known subsystem ranges are handled by the if-else chain
                 * above. Writes to addresses outside any known subsystem are
                 * silently dropped, matching real hardware behavior where
                 * unmapped BAR0 space does not store values.
                 */
            }
            break;
    }

    nv3_mmio_trace_log("NV3 MMIO W %06X = %08X\n", addr, val);

    if (nv3_trace_active && nv3_trace_count < NV3_TRACE_LIMIT) {
        nv3_trace_count++;
        pclog("NV3 W %06x = %08x\n", addr, val);
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
 * Simple dumb framebuffer access. Masks address to VRAM size, reads/writes
 * SVGA VRAM directly. RAMIN mirror at BAR1+0xC00000 will be added in
 * Phase 3.
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

/* Diagnostic: translate_address hook to log VGA banked write addresses */
static uint32_t
nv3_translate_address(uint32_t addr, void *priv)
{
    if (nv3_banked_write_count < 30
        || (nv3_banked_write_count >= 100000 && nv3_banked_write_count < 100010)
        || (nv3_banked_write_count % 500000 == 0)) {
        nv3_t  *nv3  = (nv3_t *) priv;
        svga_t *svga = &nv3->svga;
        nv3_log("NV3: BANKED write #%u final_addr=0x%08x "
                "write_bank=0x%x fb_only=%d chain4=%d\n",
                nv3_banked_write_count, addr,
                svga->write_bank, svga->fb_only, svga->chain4);
    }
    nv3_banked_write_count++;
    return addr; /* passthrough */
}

static void
nv3_lfb_write(uint32_t addr, uint8_t val, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;

    if (nv3_lfb_write_count < 5 || (nv3_lfb_write_count % 100000 == 0))
        nv3_log("NV3: LFB write #%u addr=0x%08x val=0x%02x\n",
                nv3_lfb_write_count, addr, val);
    nv3_lfb_write_count++;

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
 * Per envytools NV3 VGA docs.
 * ======================================================================== */
static uint8_t
nv3_svga_in(uint16_t addr, void *priv)
{
    nv3_t  *nv3  = (nv3_t *) priv;
    svga_t *svga = &nv3->svga;
    uint8_t ret  = 0;

    /* Handle RMA (Real Mode Access) window at 0x3D0-0x3D3 */
    if (addr >= NV3_RMA_ADDR_START && addr <= NV3_RMA_ADDR_END) {
        if (nv3->rma_mode == 0)
            return ret; /* RMA disabled */
        /*
         * RMA data access: the target MMIO address is assembled from
         * CRTC registers 0x39 (low), 0x3A (mid), 0x3B (high).
         * The BIOS sets these via standard CRTC index/data writes
         * before enabling RMA through CRTC 0x38.
         */
        {
            uint32_t rma_addr = svga->crtc[0x39]
                              | (svga->crtc[0x3A] << 8)
                              | (svga->crtc[0x3B] << 16);
            uint32_t rma_val = nv3_mmio_read_internal(nv3, rma_addr & ~3);
            ret = (uint8_t) (rma_val >> ((addr & 3) * 8));
        }
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
             * Per behavioral observation: returning 0x08 here prevents
             * freezes with certain NV3 driver versions. This appears
             * to be a status register that drivers poll.
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
        if (nv3->rma_mode == 0) {
            /* RMA disabled — do not intercept ports */
            return;
        }
        /*
         * RMA data access: the target MMIO address is assembled from
         * CRTC registers 0x39 (low), 0x3A (mid), 0x3B (high).
         * The BIOS sets these via standard CRTC index/data writes
         * before enabling RMA through CRTC 0x38.
         *
         * Ports 0x3D0-0x3D3 provide byte-granularity access to the
         * target MMIO register. The byte position is determined by
         * the VGA port offset: 0x3D0 = byte 0, 0x3D1 = byte 1, etc.
         */
        {
            uint32_t rma_addr = svga->crtc[0x39]
                              | (svga->crtc[0x3A] << 8)
                              | (svga->crtc[0x3B] << 16);
            uint32_t old = nv3_mmio_read_internal(nv3, rma_addr & ~3);
            int      shift = (addr & 3) * 8;
            uint32_t mask  = 0xFF << shift;
            uint32_t new_val = (old & ~mask) | ((uint32_t) val << shift);
            nv3_log("NV3: RMA write mode=%d addr=0x%06X byte=%d val=0x%02X => 0x%08X\n",
                    nv3->rma_mode, rma_addr, addr & 3, val, new_val);
            nv3_mmio_write_internal(nv3, rma_addr & ~3, new_val);
        }
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
                    /*
                     * Extended read bank selection.
                     * Per envytools: 32KB granularity ("32k units").
                     * fb_only=1 (packed linear): val << 15 = 32KB.
                     * fb_only=0 (VGA planar): val << 13 = 8KB pre-shift,
                     * which becomes 32KB after the planar addr <<= 2 transform.
                     */
                    nv3->cio_read_bank = val;
                    if (svga->fb_only)
                        svga->read_bank = (uint32_t) nv3->cio_read_bank << 15;
                    else
                        svga->read_bank = (uint32_t) nv3->cio_read_bank << 13;
                    break;

                case NV3_CRTC_WRITE_BANK:
                    /* Extended write bank selection (same granularity as read) */
                    nv3->cio_write_bank = val;
                    if (svga->fb_only)
                        svga->write_bank = (uint32_t) nv3->cio_write_bank << 15;
                    else
                        svga->write_bank = (uint32_t) nv3->cio_write_bank << 13;
                    nv3_log("NV3: BANK WRITE reg=0x%02x val=%d fb_only=%d "
                            "write_bank=0x%x chain4=%d gdcreg6=0x%02x\n",
                            crtcreg, val, svga->fb_only,
                            svga->write_bank, svga->chain4, svga->gdcreg[6]);
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
 * Called by the SVGA core whenever CRTC registers change or when we
 * explicitly trigger via svga_recalctimings() after PLL writes.
 *
 * This function:
 *  1. Applies NV3 extended CRTC register values (10-bit counters, etc.)
 *  2. Reads the VPLL coefficient register and calculates the pixel clock
 *  3. Sets svga->clock to drive the SVGA timing engine
 *  4. Sets the appropriate render function for the pixel depth
 *
 * The PLL frequency formula is:
 *   Freq = (crystal * N) / (M * (1 << P))
 * Per envytools nv3_pramdac.xml and xf86-video-nv CalcVClock().
 *
 * svga->clock represents the number of emulated CPU ticks per pixel clock.
 * It is computed as: (cpuclock * 2^32) / pixel_freq_hz
 * This matches the pattern used by S3 ViRGE and Voodoo Banshee.
 * ======================================================================== */
static void
nv3_recalctimings(svga_t *svga)
{
    nv3_t   *nv3        = (nv3_t *) svga->priv;
    uint32_t pixel_mode = svga->crtc[NV3_CRTC_PIXEL_MODE] & 0x03;
    uint32_t gen_ctrl   = nv3->pramdac.general_control;

    nv3_log("NV3: recalctimings: pixel_mode=%d gen_ctrl=0x%08x "
            "scrblank=%d crtc17=0x%02x attr_pal_en=%d\n",
            pixel_mode, gen_ctrl,
            svga->scrblank, svga->crtc[0x17], svga->attr_palette_enable);
    nv3_log("NV3: recalctimings: hdisp_time=%d hdisp=%d rowoffset=%d "
            "memaddr_latch=0x%x dots_per_clock=%d\n",
            svga->hdisp_time, svga->hdisp, svga->rowoffset,
            svga->memaddr_latch, svga->dots_per_clock);
    nv3_log("NV3: recalctimings: vtotal=%d htotal=%d dispend=%d "
            "seqregs1=0x%02x fmt=0x%02x char_width=%d\n",
            svga->vtotal, svga->htotal, svga->dispend,
            svga->seqregs[1], svga->crtc[NV3_CRTC_FORMAT], svga->char_width);
    nv3_log("NV3: recalctimings: vpll=0x%08x crystal=%d cpuclock=%.0f\n",
            nv3->pramdac.vpll_coeff, nv3->crystal_freq, cpuclock);
    nv3_log("NV3: recalctimings: CRTC[0x13]=0x%02x CRTC[0x14]=0x%02x "
            "CRTC[0x17]=0x%02x CRTC[0x19]=0x%02x CRTC[0x28]=0x%02x\n",
            svga->crtc[0x13], svga->crtc[0x14], svga->crtc[0x17],
            svga->crtc[NV3_CRTC_REPAINT0], svga->crtc[NV3_CRTC_PIXEL_MODE]);
    nv3_log("NV3: recalctimings: chain4=%d packed_chain4=%d force_old_addr=%d "
            "fb_only=%d gdcreg5=0x%02x seqregs4=0x%02x\n",
            svga->chain4, svga->packed_chain4, svga->force_old_addr,
            svga->fb_only, svga->gdcreg[5], svga->seqregs[4]);

    /*
     * Extended display buffer start address.
     * CRTC 0x19 (RPC0) bits 4:0 provide bits 20:16 of the start address.
     */
    svga->memaddr_latch += (svga->crtc[NV3_CRTC_REPAINT0] & 0x1F) << 16;

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
     * Detect extended (SVGA) mode.
     *
     * Primary: CRTC 0x28 (PIXEL_MODE) non-zero indicates extended mode.
     * Secondary: PRAMDAC GENERAL_CTRL VGA_STATE_SEL (bit 8) = 1 indicates
     * NV accelerated mode. The Windows 98 NVIDIA driver uses GENERAL_CTRL
     * to switch from VGA passthrough to accelerated mode; some drivers may
     * not explicitly set CRTC 0x28.
     */
    int is_extended = (pixel_mode != NV3_PIXEL_MODE_VGA)
                   || (gen_ctrl & NV3_PRAMDAC_GCTRL_VGA_STATE);

    if (!is_extended) {
        /*
         * Standard VGA mode.
         * Add HEB (horizontal extension bit) in pixel units.
         */
        if (svga->crtc[NV3_CRTC_HEB] & 0x01)
            svga->hdisp += svga->dots_per_clock * 0x100;
        svga->fb_only       = 0;
        svga->packed_chain4 = 0;
        svga->override      = 0;
        /* Recalculate bank offsets for VGA planar addressing (8KB pre-shift) */
        svga->write_bank = (uint32_t) nv3->cio_write_bank << 13;
        svga->read_bank  = (uint32_t) nv3->cio_read_bank << 13;
        return;
    }

    /*
     * ================================================================
     * Extended SVGA mode
     * ================================================================
     *
     * We do NOT set svga->override = 1 because we want the standard
     * SVGA rendering engine to draw the framebuffer for us (similar
     * to how Banshee works). We set up the render function and pixel
     * format parameters ourselves.
     */
    svga->override = 0;

    /*
     * Force packed linear byte addressing in the SVGA renderer.
     *
     * Without this, svga_render_8bpp_highres -> svga_render_indexed_gfx
     * applies VGA address mangling based on CRTC[0x14]/CRTC[0x17]:
     * if CRTC[0x14] bit 6 is set (dword mode), addresses get shifted
     * left by 2, causing 4x horizontal compression. Setting packed_chain4
     * forces incbypow2=0 and incevery=1, giving direct byte addressing.
     * Banshee uses the same mechanism (vid_voodoo_banshee.c line ~877).
     */
    svga->packed_chain4 = 1;

    /*
     * Compute horizontal display width in pixels ourselves.
     *
     * The SVGA core computes hdisp = (crtc[1] + 1) and then
     * multiplies it by dots_per_clock ONLY if:
     *   !scrblank && crtc[0x17] bit 7 && attr_palette_enable
     * During a mode switch, the screen may be momentarily blanked
     * (SR1 bit 5 set, or CRTC[0x17] bit 7 cleared), which prevents
     * the multiplication. This leaves hdisp as the raw character
     * count (e.g. 80 instead of 640).
     *
     * To avoid this, we compute hdisp ourselves from hdisp_time
     * (the character count saved before any multiplication).
     * NV3 always uses 8-pixel character clocks in extended modes.
     */
    svga->hdisp = svga->hdisp_time * 8;

    /* Extended horizontal bit: add 256 characters * 8 pixels */
    if (svga->crtc[NV3_CRTC_HEB] & 0x01)
        svga->hdisp += 0x100 * 8;

    /*
     * Determine pixel depth.
     * If CRTC 0x28 (PIXEL_MODE) was not explicitly set by the driver,
     * but GENERAL_CTRL VGA_STATE_SEL indicates accelerated mode,
     * default to 8bpp. This is the most common initial SVGA mode for
     * the Windows 98 NVIDIA driver.
     */
    if (pixel_mode == NV3_PIXEL_MODE_VGA)
        pixel_mode = NV3_PIXEL_MODE_8BPP;

    /*
     * Adjust row offset for extended pixel modes.
     *
     * The SVGA core computes rowoffset from CRTC[0x13] and advances
     * memaddr by rowoffset << 3 per scanline (1 unit = 8 bytes).
     *
     * The NV3 driver programs CRTC[0x13] = byte_pitch / 16 for ALL
     * color depths (e.g. 640-wide: CRTC[0x13] = 40). We must scale
     * rowoffset by the pixel depth to produce the correct byte stride:
     *   8bpp:  40 << 1 = 80,  byte stride = 80  * 8 = 640
     *   16bpp: 40 << 2 = 160, byte stride = 160 * 8 = 1280
     *   32bpp: 40 << 3 = 320, byte stride = 320 * 8 = 2560
     *
     * REPAINT0 bits 7:5 provide rowoffset[10:8], extending the 8-bit
     * CRTC[0x13] to an 11-bit base value. These high bits must be added
     * BEFORE the depth scaling shift.
     */
    svga->rowoffset += (svga->crtc[NV3_CRTC_REPAINT0] & 0xE0) << 3;

    switch (pixel_mode) {
        case NV3_PIXEL_MODE_8BPP:
            svga->bpp    = 8;
            svga->lowres = 0;
            svga->render = svga_render_8bpp_highres;
            svga->map8   = svga->pallook;
            svga->rowoffset <<= 1;
            break;

        case NV3_PIXEL_MODE_16BPP:
            svga->bpp    = 16;
            svga->lowres = 0;
            svga->render = svga_render_16bpp_highres;
            svga->rowoffset <<= 2;
            break;

        case NV3_PIXEL_MODE_32BPP:
            svga->bpp    = 32;
            svga->lowres = 0;
            svga->render = svga_render_32bpp_highres;
            svga->rowoffset <<= 3;
            break;

        default:
            break;
    }

    /*
     * Handle DAC bit depth from PRAMDAC GENERAL_CONTROL.
     *
     * Per envytools nv3_pramdac.xml: bit 20 = BPC_8BITS.
     *   0 = 6-bit DAC, 1 = 8-bit DAC.
     */
    if (nv3->pramdac.general_control & NV3_PRAMDAC_GCTRL_BPC_8BIT)
        svga_set_ramdac_type(svga, RAMDAC_8BIT);
    else
        svga_set_ramdac_type(svga, RAMDAC_6BIT);

    /*
     * Calculate pixel clock from VPLL coefficient register.
     * In extended mode, use the programmable VPLL unless PLL_CONTROL
     * indicates bypass or a different clock source.
     *
     * Per envytools nv3_pramdac.xml:
     *   VPLL_BYPASS (bit 20) = bypass VPLL, use crystal
     *   VPLL_PROG (bit 16) = VPLL is programmable
     *   PCLK_SRC (bits 25:24) = pixel clock source (0=VPLL, 1=VIP, 2=XTAL)
     */
    {
        uint32_t vpll     = nv3->pramdac.vpll_coeff;
        uint32_t pll_ctrl = nv3->pramdac.pll_control;
        double   freq;
        int      pclk_src = (pll_ctrl & NV3_PLL_CTRL_PCLK_SRC_MASK) >> NV3_PLL_CTRL_PCLK_SRC_SHIFT;

        if ((pll_ctrl & NV3_PLL_CTRL_VPLL_BYPASS) || pclk_src == 2) {
            /* VPLL bypassed or crystal selected: use raw crystal frequency */
            freq = (double) nv3->crystal_freq;
        } else if (!(pll_ctrl & NV3_PLL_CTRL_VPLL_PROG)) {
            /*
             * VPLL not programmable: the BIOS hasn't set up PLL_CONTROL yet,
             * or it failed to reach us. Use the VPLL coefficient anyway since
             * we initialize it to a valid 25 MHz default.
             */
            freq = nv3_pll_calc_freq(nv3->crystal_freq, vpll);
        } else {
            /* Normal case: VPLL is programmable and selected */
            freq = nv3_pll_calc_freq(nv3->crystal_freq, vpll);
        }

        nv3_log("NV3: recalctimings PCLK: vpll=0x%08X M=%d N=%d P=%d "
                "pll_ctrl=0x%08X VPLL_PROG=%d PCLK_SRC=%d freq=%.2f MHz\n",
                vpll, NV3_PLL_M(vpll), NV3_PLL_N(vpll), NV3_PLL_P(vpll),
                pll_ctrl,
                (pll_ctrl & NV3_PLL_CTRL_VPLL_PROG) ? 1 : 0,
                pclk_src,
                freq / 1000000.0);

        if (freq > 1000.0)
            svga->clock = (cpuclock * (float) (1ULL << 32)) / freq;
    }

    /*
     * Extended mode blanking.
     * Start blank at display end, similar to Banshee/ViRGE
     * "special blanking mode" behavior.
     */
    svga->vblankstart = svga->dispend;
    svga->split       = 99999; /* Disable split screen in extended modes */

    /*
     * In extended mode, the dots per clock are always 8 (never 9).
     * Set unconditionally — do NOT gate on scrblank/attr_palette_enable,
     * because the screen may be momentarily blanked during mode switch
     * and we need the timing to be correct regardless.
     */
    svga->dots_per_clock = (svga->seqregs[1] & 8) ? 16 : 8;

    /* NV3 uses 8-pixel character clocks in extended modes (never 9) */
    svga->char_width = 8;

    /* Disable line doubling in extended modes */
    svga->linedbl = 0;

    /* Enable linear framebuffer addressing for the write path */
    svga->fb_only = 1;

    /* Recalculate bank offsets for packed linear addressing (32KB granularity) */
    svga->write_bank = (uint32_t) nv3->cio_write_bank << 15;
    svga->read_bank  = (uint32_t) nv3->cio_read_bank << 15;

    /* Disable overscan in extended modes */
    svga->monitor->mon_overscan_y = 0;
    svga->monitor->mon_overscan_x = 0;

    /*
     * Force screen-on in extended mode.
     *
     * On real NV3 hardware, the PCRTC scanout engine operates independently
     * of the VGA sequencer screen-off bit. The NV3 driver may set SR1 bit 5
     * during mode switch, but the hardware still scans out normally.
     *
     * In our emulation, scrblank prevents the SVGA core from properly
     * setting up some timing parameters. While we override the critical
     * ones (render, hdisp, dots_per_clock), clearing scrblank ensures
     * the post-processing code in svga_recalctimings (overscan, hblank_sub)
     * also gets correct values.
     */
    svga->scrblank = 0;
    svga->attr_palette_enable = 1;

    nv3_log("NV3: recalctimings EXTENDED: bpp=%d hdisp=%d dispend=%d "
            "rowoffset=%d memaddr_latch=0x%x clock=0x%016llx\n",
            svga->bpp, svga->hdisp, svga->dispend,
            svga->rowoffset, svga->memaddr_latch,
            (unsigned long long) svga->clock);
    nv3_log("NV3: recalctimings EXTENDED STATE: gdcreg6=0x%02x "
            "gdcreg3=0x%02x gdcreg8=0x%02x gdcreg1=0x%02x "
            "writemode=%d seqregs2=0x%02x seqregs4=0x%02x\n",
            svga->gdcreg[6], svga->gdcreg[3], svga->gdcreg[8],
            svga->gdcreg[1], svga->writemode, svga->seqregs[2],
            svga->seqregs[4]);
    nv3_log("NV3: recalctimings EXTENDED BANKS: write_bank=0x%x "
            "read_bank=0x%x cio_wb=%d cio_rb=%d rowcount=%d "
            "vram_display_mask=0x%x banked_mask=0x%x\n",
            svga->write_bank, svga->read_bank,
            nv3->cio_write_bank, nv3->cio_read_bank,
            svga->rowcount, svga->vram_display_mask, svga->banked_mask);
    nv3_log("NV3: recalctimings EXTENDED CRTC: [0x09]=0x%02x [0x13]=0x%02x "
            "[0x14]=0x%02x [0x17]=0x%02x [0x19]=0x%02x [0x28]=0x%02x\n",
            svga->crtc[0x09], svga->crtc[0x13], svga->crtc[0x14],
            svga->crtc[0x17], svga->crtc[NV3_CRTC_REPAINT0],
            svga->crtc[NV3_CRTC_PIXEL_MODE]);

    /*
     * Diagnostic: trigger a one-shot render state dump on the next vblank.
     * This captures values AFTER the SVGA core's post-processing
     * (hblank_sub subtraction, lowres switching, etc.) which may differ
     * from what we set here.
     */
    if (!nv3_diag_fired) {
        nv3_diag_pending   = 1;
        nv3_diag_scanlines = 0;

        /* Also arm the delayed diagnostic (fires after ~30 frames) */
        nv3_diag_delayed_pending = 1;
        nv3_diag_frame_count     = 0;

        /* Dump what WE set (pre-SVGA-core post-processing) */
        nv3_log("NV3: RECALC_DIAG (pre-postprocess): hdisp=%d dispend=%d "
                "rowoffset=%d memaddr_latch=0x%x\n",
                svga->hdisp, svga->dispend, svga->rowoffset,
                svga->memaddr_latch);
        nv3_log("NV3: RECALC_DIAG: bpp=%d lowres=%d packed_chain4=%d "
                "fb_only=%d chain4=%d force_old_addr=%d\n",
                svga->bpp, svga->lowres, svga->packed_chain4,
                svga->fb_only, svga->chain4, svga->force_old_addr);
        nv3_log("NV3: RECALC_DIAG: override=%d hoverride=%d "
                "dots_per_clock=%d char_width=%d linedbl=%d\n",
                svga->override, svga->hoverride, svga->dots_per_clock,
                svga->char_width, svga->linedbl);
        nv3_log("NV3: RECALC_DIAG: adv_flags=0x%x remap_required=%d "
                "vram_display_mask=0x%x banked_mask=0x%x\n",
                svga->adv_flags, svga->remap_required,
                svga->vram_display_mask, svga->banked_mask);

        /* Dump VRAM[0..31] to verify writes landed correctly */
        nv3_log("NV3: RECALC_DIAG: VRAM[0..15] = "
                "%02x %02x %02x %02x %02x %02x %02x %02x "
                "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                svga->vram[0], svga->vram[1], svga->vram[2], svga->vram[3],
                svga->vram[4], svga->vram[5], svga->vram[6], svga->vram[7],
                svga->vram[8], svga->vram[9], svga->vram[10], svga->vram[11],
                svga->vram[12], svga->vram[13], svga->vram[14], svga->vram[15]);
        nv3_log("NV3: RECALC_DIAG: VRAM[16..31] = "
                "%02x %02x %02x %02x %02x %02x %02x %02x "
                "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                svga->vram[16], svga->vram[17], svga->vram[18], svga->vram[19],
                svga->vram[20], svga->vram[21], svga->vram[22], svga->vram[23],
                svga->vram[24], svga->vram[25], svga->vram[26], svga->vram[27],
                svga->vram[28], svga->vram[29], svga->vram[30], svga->vram[31]);
        /* Also dump VRAM at offset 640 (start of second scanline if pitch=640) */
        nv3_log("NV3: RECALC_DIAG: VRAM[640..655] = "
                "%02x %02x %02x %02x %02x %02x %02x %02x "
                "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                svga->vram[640], svga->vram[641], svga->vram[642], svga->vram[643],
                svga->vram[644], svga->vram[645], svga->vram[646], svga->vram[647],
                svga->vram[648], svga->vram[649], svga->vram[650], svga->vram[651],
                svga->vram[652], svga->vram[653], svga->vram[654], svga->vram[655]);
    }

    video_force_resize_set_monitor(1, svga->monitor_index);
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
        nv3_log("NV3: mapping LFB at 0x%08x size=0x%x\n", nv3->bar1_base, nv3->vram_size);
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
            nv3_log("NV3: PCI BAR1 write byte 0x17 val=0x%02x -> bar1_base=0x%08x\n",
                    val, nv3->bar1_base);
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
 * Per envytools nv3_pfb.xml: this register encodes RAM size,
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
    /* Sync to register bank for consistent readback through default path */
    nv3->pfb.regs[(NV3_PFB_BOOT_0 - NV3_PFB_START) >> 2] = boot_0;
}

/* ========================================================================
 * Initialize PEXTDEV straps based on card type.
 *
 * Per envytools: PEXTDEV_STRAPS is a read-only register reflecting the
 * board's hardware strap pins. We synthesize the value based on the
 * emulated card configuration.
 * ======================================================================== */
static void
nv3_pextdev_init(nv3_t *nv3)
{
    uint32_t straps = 0;

    /* BIOS present */
    straps |= (1 << NV3_STRAPS_BIOS_PRESENT);

    /* 14.318MHz crystal (standard for most Riva 128 boards) */
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
    /* Sync to register bank for consistent readback through default path */
    nv3->pextdev.regs[(NV3_PEXTDEV_STRAPS - NV3_PEXTDEV_START) >> 2] = straps;

    /*
     * Set crystal frequency based on straps.
     * Per envytools: bit 6 = 0 means 13.5MHz, 1 means 14.318MHz.
     */
    if (straps & (1 << NV3_STRAPS_CRYSTAL))
        nv3->crystal_freq = NV3_CRYSTAL_FREQ_14318;
    else
        nv3->crystal_freq = NV3_CRYSTAL_FREQ_13500;
}

/* ========================================================================
 * Initialize PRAMDAC default PLL coefficients.
 *
 * Per rivafb riva_tbl.h and xf86-video-nv riva_hw.c: the BIOS programs
 * initial PLL values during POST. We provide reasonable defaults here
 * that represent a typical VGA-rate pixel clock.
 *
 * Default VPLL: ~25.175 MHz (standard VGA pixel clock for 640x480)
 * For 14.318 MHz crystal:
 *   25.175 = 14.318 * N / (M * (1 << P))
 *   With M=14, N=50, P=1: 14.318 * 50 / (14 * 2) = 25.568 (close enough)
 *   With M=12, N=21, P=0: 14.318 * 21 / (12 * 1) = 25.056 (closer)
 *
 * Default NVPLL: ~75 MHz core clock
 *   With M=13, N=68, P=0: 14.318 * 68 / (13 * 1) = 74.89 MHz
 *
 * Default MPLL: ~100 MHz memory clock
 *   With M=13, N=91, P=0: 14.318 * 91 / (13 * 1) = 100.26 MHz
 *
 * These are approximate; the BIOS will reprogram them during POST.
 * ======================================================================== */
static void
nv3_pramdac_init(nv3_t *nv3)
{
    /*
     * Encode PLL coefficients: bits [7:0]=M, [15:8]=N, [18:16]=P.
     * VPLL: M=12, N=21, P=0 => ~25 MHz
     */
    nv3->pramdac.vpll_coeff  = (0 << 16) | (21 << 8) | 12;
    nv3_log("NV3: pramdac_init: VPLL default = 0x%08X (M=%d N=%d P=%d)\n",
            nv3->pramdac.vpll_coeff,
            NV3_PLL_M(nv3->pramdac.vpll_coeff),
            NV3_PLL_N(nv3->pramdac.vpll_coeff),
            NV3_PLL_P(nv3->pramdac.vpll_coeff));
    /* NVPLL: M=13, N=68, P=0 => ~75 MHz */
    nv3->pramdac.nvpll_coeff = (0 << 16) | (68 << 8) | 13;
    /* MPLL: M=13, N=91, P=0 => ~100 MHz */
    nv3->pramdac.mpll_coeff  = (0 << 16) | (91 << 8) | 13;

    /*
     * PLL_CONTROL defaults: all PLLs in fixed (non-programmable) mode
     * initially. The BIOS will set VPLL_PROG and MPLL_PROG during POST.
     * Per envytools nv3_pramdac.xml (NV3:NV4 variant).
     */
    nv3->pramdac.pll_control = 0;
    nv3->pramdac.pll_setup   = 0;

    /*
     * General control: default to 8-bit DAC, VGA state off.
     *
     * Per envytools nv3_pramdac.xml: BPC_8BITS is bit 20.
     * xf86-video-nv uses 0x00100100 (VGA_STATE_SEL=1, BPC_8BITS=1)
     * for SVGA modes, but at init we just set BPC_8BITS.
     */
    nv3->pramdac.general_control = NV3_PRAMDAC_GCTRL_BPC_8BIT;
    nv3->pramdac.cursor_pos      = 0;

    /*
     * Fix 2: sync all dedicated PRAMDAC fields to register bank at init
     * so reads through the default bank path are consistent.
     */
    nv3->pramdac.regs[(NV3_PRAMDAC_NVPLL_COEFF - NV3_PRAMDAC_START) >> 2] = nv3->pramdac.nvpll_coeff;
    nv3->pramdac.regs[(NV3_PRAMDAC_MPLL_COEFF - NV3_PRAMDAC_START) >> 2] = nv3->pramdac.mpll_coeff;
    nv3->pramdac.regs[(NV3_PRAMDAC_VPLL_COEFF - NV3_PRAMDAC_START) >> 2] = nv3->pramdac.vpll_coeff;
    nv3->pramdac.regs[(NV3_PRAMDAC_PLL_CONTROL - NV3_PRAMDAC_START) >> 2] = nv3->pramdac.pll_control;
    nv3->pramdac.regs[(NV3_PRAMDAC_PLL_SETUP - NV3_PRAMDAC_START) >> 2] = nv3->pramdac.pll_setup;
    nv3->pramdac.regs[(NV3_PRAMDAC_GENERAL_CTRL - NV3_PRAMDAC_START) >> 2] = nv3->pramdac.general_control;
    nv3->pramdac.regs[(NV3_PRAMDAC_CURSOR_POS - NV3_PRAMDAC_START) >> 2] = nv3->pramdac.cursor_pos;
}

/* ========================================================================
 * PFIFO helper: compute RAMHT hash.
 *
 * Per envytools fifo/nv1-pfifo.html:
 * The hash function for NV3 RAMHT is:
 *   hash = handle ^ (handle >> RAMHT_size_shift) ^ channel_id
 * The result is then masked to RAMHT size to get an entry index.
 *
 * The RAMHT size determines the number of entries:
 *   4KB  => 512 entries (4096 / 8 bytes per entry), mask = 0x1FF
 *   8KB  => 1024 entries, mask = 0x3FF
 *   16KB => 2048 entries, mask = 0x7FF
 *   32KB => 4096 entries, mask = 0xFFF
 *
 * The hash function XORs the handle with shifted versions of itself
 * to spread the bits across the table.
 * ======================================================================== */
static uint32_t
nv3_ramht_hash(nv3_t *nv3, uint32_t handle, uint32_t channel_id)
{
    /*
     * Determine RAMHT size from config register.
     * Per envytools: bits [17:16] select size.
     */
    uint32_t size_sel = (nv3->pfifo.ramht_config >> NV3_PFIFO_RAMHT_SIZE_SHIFT) & 0x3;
    uint32_t num_entries;

    switch (size_sel) {
        case 0:  num_entries = 512;  break;  /* 4KB */
        case 1:  num_entries = 1024; break;  /* 8KB */
        case 2:  num_entries = 2048; break;  /* 16KB */
        case 3:  num_entries = 4096; break;  /* 32KB */
        default: num_entries = 512;  break;
    }

    uint32_t mask = num_entries - 1;

    /*
     * NV3 RAMHT hash algorithm.
     * Per envytools: XOR handle bits in groups matching the index width,
     * then XOR with the channel ID.
     *
     * For a table of N entries (log2(N) = B bits):
     *   hash = handle[B-1:0] ^ handle[2B-1:B] ^ ... ^ channel_id
     *
     * This distributes handles evenly across the table.
     */
    uint32_t bits = 0;
    uint32_t tmp  = num_entries;
    while (tmp > 1) {
        bits++;
        tmp >>= 1;
    }

    uint32_t hash = 0;
    uint32_t h = handle;
    while (h != 0) {
        hash ^= (h & mask);
        h >>= bits;
    }
    hash ^= (channel_id & mask);

    return hash & mask;
}

/* ========================================================================
 * PFIFO helper: look up an object handle in RAMHT.
 *
 * Walks the RAMHT starting at the hash index. If the entry matches
 * the handle, returns true and fills in *engine and *instance.
 * Returns false if the handle is not found.
 *
 * Per envytools: RAMHT entries are 8 bytes:
 *   word 0 = object handle
 *   word 1 = bits [15:0] = instance >> 4, bits [23:16] = engine, bit 31 = valid
 *
 * The table uses linear probing: if the hashed slot does not match,
 * check the next slot, wrapping around. Stop when an empty slot is
 * found (valid bit not set).
 * ======================================================================== */
static bool
nv3_ramht_lookup(nv3_t *nv3, uint32_t handle, uint32_t channel_id,
                 uint32_t *engine, uint32_t *instance)
{
    /*
     * Compute RAMHT base offset in PRAMIN.
     * Per envytools: bits [8:4] of RAMHT config give the base address in
     * 4KB units within PRAMIN, so base = (config & 0x1F0) << 8.
     */
    uint32_t ramht_base = (nv3->pfifo.ramht_config & NV3_PFIFO_RAMHT_BASE_MASK) << 8;

    uint32_t size_sel = (nv3->pfifo.ramht_config >> NV3_PFIFO_RAMHT_SIZE_SHIFT) & 0x3;
    uint32_t num_entries;
    switch (size_sel) {
        case 0:  num_entries = 512;  break;
        case 1:  num_entries = 1024; break;
        case 2:  num_entries = 2048; break;
        case 3:  num_entries = 4096; break;
        default: num_entries = 512;  break;
    }

    uint32_t hash = nv3_ramht_hash(nv3, handle, channel_id);

    /*
     * Translate PRAMIN offset to VRAM address.
     * PRAMIN maps to the top 1MB of VRAM.
     */
    uint32_t pramin_vram_base = nv3->vram_size - NV3_PRAMIN_VRAM_SIZE;

    /*
     * Linear probe through the RAMHT.
     * Stop after checking all entries (worst case: full table).
     */
    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t idx = (hash + i) & (num_entries - 1);
        uint32_t entry_offset = ramht_base + (idx * NV3_RAMHT_ENTRY_SIZE);
        uint32_t vram_addr = pramin_vram_base + entry_offset;

        if (vram_addr + 7 >= nv3->vram_size)
            break;

        uint32_t entry_handle = *(uint32_t *) &nv3->svga.vram[vram_addr];
        uint32_t entry_ctx    = *(uint32_t *) &nv3->svga.vram[vram_addr + 4];

        /* Check valid bit */
        if (!(entry_ctx & NV3_RAMHT_ENTRY_VALID)) {
            /* Empty slot — handle not found */
            return false;
        }

        /* Check if handle matches */
        if (entry_handle == handle) {
            *engine   = (entry_ctx >> NV3_RAMHT_ENTRY_ENGINE_SHIFT) & 0xFF;
            *instance = (entry_ctx & NV3_RAMHT_ENTRY_INSTANCE_MASK) << 4;
            return true;
        }
    }

    return false;
}

/* ========================================================================
 * PFIFO helper: compute CACHE1 STATUS from PUT/GET pointers.
 *
 * Per envytools nv1_pfifo.xml:
 *   EMPTY (bit 4) = PUT == GET
 *   FULL  (bit 8) = next PUT would collide with GET (ring buffer full)
 * ======================================================================== */
static uint32_t
nv3_cache1_compute_status(nv3_t *nv3)
{
    uint32_t put = nv3->pfifo.cache1.put;
    uint32_t get = nv3->pfifo.cache1.get;
    uint32_t size = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                    ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE;
    uint32_t status = 0;

    if (put == get)
        status |= NV3_PFIFO_CACHE_STATUS_EMPTY;

    /* FULL: next write position would equal GET */
    if (((put + 1) % size) == get)
        status |= NV3_PFIFO_CACHE_STATUS_FULL;

    return status;
}

/* ========================================================================
 * PFIFO helper: compute number of free CACHE1 entries.
 *
 * Used by the USER space FifoFree register to report how many more
 * commands can be submitted before the cache is full.
 * ======================================================================== */
static uint32_t
nv3_cache1_free_count(nv3_t *nv3)
{
    uint32_t put = nv3->pfifo.cache1.put;
    uint32_t get = nv3->pfifo.cache1.get;
    uint32_t size = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                    ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE;

    if (put >= get)
        return (size - 1) - (put - get);
    else
        return (get - put) - 1;
}

/* ========================================================================
 * PFIFO puller: process one entry from CACHE1.
 *
 * The puller reads the front entry from CACHE1 (at GET pointer),
 * decodes the subchannel and method, and dispatches to the appropriate
 * engine (PGRAPH for graphics objects, SW for software objects).
 *
 * Per envytools nv1-pfifo.html:
 * - Method 0x0000 (within a subchannel) = SetObject — binds an object
 *   handle to the subchannel via RAMHT lookup.
 * - Methods 0x0100+ = engine-specific method dispatch.
 * ======================================================================== */
static void
nv3_pfifo_puller_run(nv3_t *nv3)
{
    uint32_t size = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                    ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE;

    /* Only run if puller is enabled and cache has data */
    if (!nv3->pfifo.cache1.pull_enabled)
        return;

    while (nv3->pfifo.cache1.get != nv3->pfifo.cache1.put) {
        uint32_t get = nv3->pfifo.cache1.get;
        nv3_cache_entry_t *entry = &nv3->pfifo.cache1.entries[get];

        uint32_t method  = (entry->addr & NV3_CACHE_ADDR_METHOD_MASK) >> NV3_CACHE_ADDR_METHOD_SHIFT;
        uint32_t subchan = (entry->addr & NV3_CACHE_ADDR_SUBCHAN_MASK) >> NV3_CACHE_ADDR_SUBCHAN_SHIFT;
        uint32_t data    = entry->data;
        uint32_t channel = nv3->pfifo.cache1.push_channel;

        /*
         * Method 0x0000 = SetObject (bind object to subchannel).
         * The data field contains the object handle.
         * Look it up in RAMHT to find engine and instance.
         */
        if (method == 0) {
            uint32_t eng = 0, inst = 0;
            if (nv3_ramht_lookup(nv3, data, channel, &eng, &inst)) {
                nv3->pfifo.subchan_object[subchan] = data;
                nv3->pfifo.cache1.pull_engine = eng;
                nv3_log("NV3: PFIFO puller: SetObject subchan=%d handle=0x%08X "
                        "engine=%d instance=0x%04X\n",
                        subchan, data, eng, inst);
            } else {
                /*
                 * Object not found in RAMHT — this is a CACHE_ERROR.
                 * On real hardware this would raise a PFIFO interrupt
                 * and potentially log to RAMRO.
                 */
                nv3_log("NV3: PFIFO puller: RAMHT miss! handle=0x%08X "
                        "channel=%d subchan=%d\n",
                        data, channel, subchan);
                nv3->pfifo.intr_0 |= NV3_PFIFO_INTR_CACHE_ERROR;
                nv3->pfifo.regs[(NV3_PFIFO_INTR_0 - NV3_PFIFO_START) >> 2] = nv3->pfifo.intr_0;
                nv3_update_irq(nv3);
            }
        } else {
            /*
             * Engine method dispatch.
             *
             * Phase 3 stub: for now, log the method and silently consume it.
             * In Phase 4, this will dispatch to PGRAPH for 2D methods.
             * In Phase 5, this will dispatch to PGRAPH for 3D methods.
             *
             * The method address is: (method_number << 2) + 0x100
             * within the subchannel's address space.
             */
            uint32_t method_addr = method << 2;
            (void) method_addr;
            (void) data;

            nv3_log("NV3: PFIFO puller: dispatch ch=%d sub=%d method=0x%04X "
                    "data=0x%08X (engine stub)\n",
                    channel, subchan, method << 2, data);
        }

        /* Advance GET pointer (ring buffer wrap) */
        nv3->pfifo.cache1.get = (get + 1) % size;
    }
}

/* ========================================================================
 * PFIFO pusher: push a USER space PIO write into the appropriate cache.
 *
 * Per envytools nv1-pfifo.html:
 * When CACHES is enabled and a write arrives at the USER space for a
 * channel, the pusher:
 *   1. Checks if the channel matches CACHE1's active channel
 *   2. Encodes the subchannel + method into a CACHE entry ADDR field
 *   3. Stores the data in the CACHE entry DATA field
 *   4. Advances the PUT pointer
 *   5. If the puller is enabled, triggers puller processing
 *
 * Channel 0 goes to CACHE0, all other channels go to CACHE1.
 * If the cache is full, the write goes to RAMRO (runout).
 * ======================================================================== */
static void
nv3_pfifo_push_pio(nv3_t *nv3, uint32_t channel, uint32_t subchan,
                   uint32_t method, uint32_t data)
{
    uint32_t cache_size = (nv3->card_type >= NV3_TYPE_NV3T_PCI)
                          ? NV3T_CACHE1_SIZE : NV3_CACHE1_SIZE;

    /* Encode the CACHE entry address field */
    uint32_t addr_field = ((method >> 2) << NV3_CACHE_ADDR_METHOD_SHIFT)
                        | (subchan << NV3_CACHE_ADDR_SUBCHAN_SHIFT);

    if (channel == 0) {
        /*
         * Channel 0 -> CACHE0 (single entry).
         * Only push if CACHE0 push is enabled.
         */
        if (!nv3->pfifo.cache0.push_enabled) {
            nv3_log("NV3: PFIFO push ch0: CACHE0 push disabled, dropping\n");
            return;
        }

        /* CACHE0 is a single entry; if not empty, it's full */
        if (nv3->pfifo.cache0.put != nv3->pfifo.cache0.get) {
            nv3_log("NV3: PFIFO push ch0: CACHE0 full, dropping method=0x%04X\n",
                    method);
            return;
        }

        nv3->pfifo.cache0.entry.addr = addr_field;
        nv3->pfifo.cache0.entry.data = data;
        nv3->pfifo.cache0.put = (nv3->pfifo.cache0.put + 1) & 1;

        /* Sync to register bank */
        nv3->pfifo.regs[(NV3_PFIFO_CACHE0_PUT - NV3_PFIFO_START) >> 2] = nv3->pfifo.cache0.put;
        nv3->pfifo.regs[(NV3_PFIFO_CACHE0_ADDR - NV3_PFIFO_START) >> 2] = addr_field;
        nv3->pfifo.regs[(NV3_PFIFO_CACHE0_DATA - NV3_PFIFO_START) >> 2] = data;

        /*
         * CACHE0 puller: process the entry immediately if enabled.
         * CACHE0 is simple — the puller just consumes the single entry.
         */
        if (nv3->pfifo.cache0.pull_enabled) {
            /* For now, just advance GET to match PUT (consume the entry) */
            nv3->pfifo.cache0.get = nv3->pfifo.cache0.put;
            nv3->pfifo.regs[(NV3_PFIFO_CACHE0_GET - NV3_PFIFO_START) >> 2] = nv3->pfifo.cache0.get;
        }
    } else {
        /*
         * Channels 1-7 -> CACHE1.
         * Only push if CACHE1 push is enabled.
         */
        if (!nv3->pfifo.cache1.push_enabled) {
            nv3_log("NV3: PFIFO push ch%d: CACHE1 push disabled, dropping\n", channel);
            return;
        }

        /*
         * Check if a channel switch is needed.
         * Per envytools: if the incoming channel differs from PUSH1,
         * a context switch should occur. For Phase 3, we simply update
         * the channel and continue (full context switching via RAMFC
         * will be added when DMA mode is implemented).
         */
        if (channel != nv3->pfifo.cache1.push_channel) {
            nv3_log("NV3: PFIFO: channel switch %d -> %d (simplified)\n",
                    nv3->pfifo.cache1.push_channel, channel);
            nv3->pfifo.cache1.push_channel = channel;
        }

        /* Check if CACHE1 is full */
        uint32_t next_put = (nv3->pfifo.cache1.put + 1) % cache_size;
        if (next_put == nv3->pfifo.cache1.get) {
            nv3_log("NV3: PFIFO push ch%d: CACHE1 full! method=0x%04X "
                    "data=0x%08X (dropped to runout)\n",
                    channel, method, data);
            /* Would go to RAMRO on real hardware */
            return;
        }

        /* Write entry to CACHE1 at PUT position */
        uint32_t put = nv3->pfifo.cache1.put;
        nv3->pfifo.cache1.entries[put].addr = addr_field;
        nv3->pfifo.cache1.entries[put].data = data;
        nv3->pfifo.cache1.put = next_put;

        /* Sync to register bank */
        nv3->pfifo.regs[(NV3_PFIFO_CACHE1_PUT - NV3_PFIFO_START) >> 2] = nv3->pfifo.cache1.put;
        /* Also sync the entry register corresponding to this index */
        uint32_t entry_addr_reg = NV3_PFIFO_CACHE1_ADDR_START + put * NV3_PFIFO_CACHE1_ENTRY_STRIDE;
        uint32_t entry_data_reg = NV3_PFIFO_CACHE1_DATA_START + put * NV3_PFIFO_CACHE1_ENTRY_STRIDE;
        nv3->pfifo.regs[(entry_addr_reg - NV3_PFIFO_START) >> 2] = addr_field;
        nv3->pfifo.regs[(entry_data_reg - NV3_PFIFO_START) >> 2] = data;

        /* Trigger puller if enabled */
        if (nv3->pfifo.cache1.pull_enabled)
            nv3_pfifo_puller_run(nv3);
    }
}

/* ========================================================================
 * Initialize PFIFO subsystem default state.
 *
 * Per envytools nv1_pfifo.xml and rivafb riva_tbl.h nv3TablePFIFO:
 * The driver initializes PFIFO by:
 *   1. Disabling CACHES (master enable = 0)
 *   2. Writing RAMHT/RAMFC/RAMRO config
 *   3. Setting up CACHE0/CACHE1 push/pull enables
 *   4. Enabling CACHES
 *
 * At power-on, caches start empty (PUT == GET == 0), all enables are off.
 * ======================================================================== */
static void
nv3_pfifo_init(nv3_t *nv3)
{
    /* All interrupts cleared and disabled */
    nv3->pfifo.intr_0    = 0;
    nv3->pfifo.intr_en_0 = 0;

    /* Master FIFO disabled at startup */
    nv3->pfifo.caches_enabled = 0;

    /* Configuration registers — will be set by driver during init */
    nv3->pfifo.config       = 0;  /* All channels PIO mode by default */
    nv3->pfifo.ramht_config = 0;
    nv3->pfifo.ramfc_config = 0;
    nv3->pfifo.ramro_config = 0;

    /* CACHE0 starts empty with push/pull disabled */
    nv3->pfifo.cache0.push_enabled = 0;
    nv3->pfifo.cache0.pull_enabled = 0;
    nv3->pfifo.cache0.put          = 0;
    nv3->pfifo.cache0.get          = 0;
    nv3->pfifo.cache0.entry.addr   = 0;
    nv3->pfifo.cache0.entry.data   = 0;

    /* CACHE1 starts empty with push/pull disabled */
    nv3->pfifo.cache1.push_enabled  = 0;
    nv3->pfifo.cache1.push_channel  = 0;
    nv3->pfifo.cache1.pull_enabled  = 0;
    nv3->pfifo.cache1.pull_engine   = 0;
    nv3->pfifo.cache1.put           = 0;
    nv3->pfifo.cache1.get           = 0;
    nv3->pfifo.cache1.dma_push      = 0;
    nv3->pfifo.cache1.dma_fetch     = 0;
    nv3->pfifo.cache1.hash          = 0;
    nv3->pfifo.cache1.engine        = 0;
    memset(nv3->pfifo.cache1.entries, 0, sizeof(nv3->pfifo.cache1.entries));

    /* Runout empty */
    nv3->pfifo.runout.put = 0;
    nv3->pfifo.runout.get = 0;

    /* No objects bound to subchannels */
    memset(nv3->pfifo.subchan_object, 0, sizeof(nv3->pfifo.subchan_object));

    /*
     * Sync key PFIFO registers to the register bank.
     * The driver reads these back to verify initialization.
     */
    nv3->pfifo.regs[(NV3_PFIFO_INTR_0 - NV3_PFIFO_START) >> 2]    = 0;
    nv3->pfifo.regs[(NV3_PFIFO_INTR_EN_0 - NV3_PFIFO_START) >> 2] = 0;
    nv3->pfifo.regs[(NV3_PFIFO_CACHES - NV3_PFIFO_START) >> 2]    = 0;
    nv3->pfifo.regs[(NV3_PFIFO_CONFIG - NV3_PFIFO_START) >> 2]     = 0;
    nv3->pfifo.regs[(NV3_PFIFO_RAMHT - NV3_PFIFO_START) >> 2]     = 0;
    nv3->pfifo.regs[(NV3_PFIFO_RAMFC - NV3_PFIFO_START) >> 2]     = 0;
    nv3->pfifo.regs[(NV3_PFIFO_RAMRO - NV3_PFIFO_START) >> 2]     = 0;

    nv3_log("NV3: PFIFO initialized\n");
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
              NULL,  /* hwcursor_draw - TODO: hardware cursor rendering */
              NULL); /* overlay_draw - not used */

    /* Set decode mask to VRAM size */
    nv3->svga.decode_mask = nv3->vram_size - 1;
    nv3->svga.bpp         = 8;
    nv3->svga.miscout     = 1;

    /* Diagnostic: hook translate_address to log VGA banked writes */
    nv3->svga.translate_address = nv3_translate_address;

    /* Register VBlank start callback for interrupt generation */
    nv3->svga.vblank_start = nv3_vblank_start;

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

    /* PMC - Set boot register based on chip revision.
     * Per envytools pmc.xml: BOOT_0 encodes chip arch/impl/revision. */
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

    /* Interrupts disabled at startup */
    nv3->pmc.intr_en_0 = NV3_PMC_INTR_EN_DISABLED;

    /* PFB - Memory config */
    nv3_pfb_init(nv3);

    /* PEXTDEV - Straps (also sets crystal_freq) */
    nv3_pextdev_init(nv3);

    /* PTIMER defaults.
     * Per envytools: NUMERATOR and DENOMINATOR default to sensible values.
     * The ratio NUMERATOR/DENOMINATOR * core_clock_period gives the timer
     * increment rate. Default 1/1 is a common starting point.
     */
    nv3->ptimer.numerator   = 1;
    nv3->ptimer.denominator = 1;

    /* PRAMDAC - PLL clocks and DAC */
    nv3_pramdac_init(nv3);

    /* PFIFO - Command FIFO */
    nv3_pfifo_init(nv3);

    /* PCRTC - Display controller defaults */
    nv3->pcrtc.intr     = 0;
    nv3->pcrtc.intr_en  = 0;
    nv3->pcrtc.config   = 0;
    nv3->pcrtc.start_addr = 0;

    /* I2C / DDC */
    nv3->i2c = i2c_gpio_init("nv3_i2c");
    nv3->ddc = ddc_init(i2c_gpio_get_bus(nv3->i2c));

    /* Inform video subsystem of our timing class */
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_nv3_pci);

    nv3_log("NV3: init complete, crystal=%d Hz\n", nv3->crystal_freq);
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
