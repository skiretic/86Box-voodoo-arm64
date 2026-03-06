/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Local APIC (Advanced Programmable Interrupt Controller)
 *          implementation for P6-class CPUs.
 *
 *          Implements the full register set per Intel SDM Vol 3A Ch 10:
 *          - APIC ID, Version, TPR, PPR, EOI
 *          - LDR, DFR, SVR (with enable bit)
 *          - ISR, TMR, IRR (256-bit interrupt vectors)
 *          - ICR (stubbed for Phase 1 -- full IPI in Phase 4)
 *          - LVT Timer, LINT0, LINT1, Error, Thermal, Perf
 *          - Timer with one-shot and periodic mode
 *
 *          Virtual wire mode: LINT0 as ExtINT (PIC pass-through),
 *          LINT1 as NMI.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/nmi.h>
#include <86box/apic.h>
#include <86box/ioapic.h>
#include <86box/plat_unused.h>
#include "cpu.h"

/* APIC version: P6 family local APIC, version 0x14, max LVT entry 5 (0-based). */
#define APIC_VERSION 0x00050014

/* Timer divide configuration lookup table.
   Index is the 4-bit DCR value (bits 3,1,0 of the register),
   value is the actual divisor. */
static const int apic_timer_divisors[8] = {
    2, 4, 8, 16, 32, 64, 128, 1
};

#ifdef ENABLE_APIC_LOG
int apic_do_log = ENABLE_APIC_LOG;

static void
apic_log(const char *fmt, ...)
{
    va_list ap;

    if (apic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define apic_log(fmt, ...)
#endif

/*
 * Local APIC state structure.
 */
struct apic_t {
    /* MMIO mapping. */
    mem_mapping_t mem_mapping;

    /* Base address and MSR. */
    uint32_t base_addr;
    uint64_t msr;

    /* APIC registers. */
    uint32_t id;  /* APIC ID (bits 27:24). */
    uint32_t tpr; /* Task Priority Register. */
    uint32_t ldr; /* Logical Destination Register. */
    uint32_t dfr; /* Destination Format Register. */
    uint32_t svr; /* Spurious Interrupt Vector Register. */

    /* 256-bit vector registers (8 x 32-bit words each). */
    uint32_t isr[8]; /* In-Service Register. */
    uint32_t tmr[8]; /* Trigger Mode Register. */
    uint32_t irr[8]; /* Interrupt Request Register. */

    uint32_t esr; /* Error Status Register (stub). */

    /* ICR (stubbed for Phase 1). */
    uint32_t icr_low;
    uint32_t icr_high;

    /* LVT entries. */
    uint32_t lvt_timer;
    uint32_t lvt_thermal;
    uint32_t lvt_perf;
    uint32_t lvt_lint0;
    uint32_t lvt_lint1;
    uint32_t lvt_error;

    /* Timer. */
    uint32_t   timer_icr; /* Initial Count Register. */
    uint32_t   timer_ccr; /* Current Count Register (shadow). */
    uint32_t   timer_dcr; /* Divide Configuration Register. */
    int        timer_div; /* Actual divisor value. */
    pc_timer_t timer;     /* 86Box timer. */
};

/* Global pointer to the APIC state. */
apic_t *apic = NULL;

/*
 * Find the highest-priority set bit in a 256-bit vector register.
 * Returns the bit number (0-255) or -1 if no bits are set.
 * Higher vector numbers have higher priority.
 */
static int
apic_find_highest_bit(const uint32_t *reg)
{
    for (int i = 7; i >= 0; i--) {
        if (reg[i]) {
            /* Find highest set bit in this 32-bit word. */
            int      bit = 31;
            uint32_t val = reg[i];
            while (bit >= 0 && !(val & (1u << bit)))
                bit--;
            return (i * 32) + bit;
        }
    }
    return -1;
}

/*
 * Set a bit in a 256-bit vector register.
 */
static void
apic_set_bit(uint32_t *reg, int bit)
{
    reg[bit >> 5] |= (1u << (bit & 31));
}

/*
 * Clear a bit in a 256-bit vector register.
 */
static void
apic_clear_bit(uint32_t *reg, int bit)
{
    reg[bit >> 5] &= ~(1u << (bit & 31));
}

/*
 * Test a bit in a 256-bit vector register.
 */
static int
apic_test_bit(const uint32_t *reg, int bit)
{
    return !!(reg[bit >> 5] & (1u << (bit & 31)));
}

/*
 * Compute the Processor Priority Register (PPR).
 * PPR = max(TPR, highest ISR vector class).
 * The "class" of a vector is its upper 4 bits (vector >> 4).
 */
static uint32_t
apic_get_ppr(apic_t *dev)
{
    int      highest_isr = apic_find_highest_bit(dev->isr);
    uint32_t isr_class   = (highest_isr >= 0) ? (highest_isr & 0xF0) : 0;
    uint32_t tpr_class   = dev->tpr & 0xF0;

    if (tpr_class >= isr_class)
        return dev->tpr;
    else
        return isr_class;
}

/*
 * Get the timer divisor from the DCR register.
 * DCR bits 3, 1, 0 encode the divisor.
 */
static int
apic_get_timer_divisor(uint32_t dcr)
{
    int idx = ((dcr >> 1) & 4) | (dcr & 3);
    return apic_timer_divisors[idx];
}

/*
 * Timer callback: fires when the APIC timer counts down to zero.
 */
static void
apic_timer_callback(void *priv)
{
    apic_t *dev = (apic_t *) priv;

    if (!(dev->svr & APIC_SVR_ENABLE))
        return;

    /* Fire the timer interrupt if not masked. */
    if (!(dev->lvt_timer & APIC_LVT_MASKED)) {
        int vector = dev->lvt_timer & 0xFF;
        apic_log("APIC: Timer interrupt, vector %02X\n", vector);
        apic_set_bit(dev->irr, vector);
    }

    /* In periodic mode, restart the timer. */
    if (dev->lvt_timer & APIC_LVT_TIMER_PERIODIC) {
        if (dev->timer_icr > 0) {
            /* Calculate period in CPU clock cycles:
               period = ICR * divisor.
               Convert to microseconds for timer_on_auto. */
            double period_us = ((double) dev->timer_icr * (double) dev->timer_div)
                / (cpu_busspeed / 1000000.0);
            timer_on_auto(&dev->timer, period_us);
        }
    }
    /* One-shot mode: timer stays stopped. */
}

/*
 * Start (or restart) the APIC timer.
 */
static void
apic_timer_start(apic_t *dev)
{
    if (dev->timer_icr == 0) {
        timer_disable(&dev->timer);
        return;
    }

    /* Calculate period in microseconds.
       Timer ticks at bus clock / divisor.
       Period = ICR * divisor / bus_clock_hz * 1e6. */
    double period_us = ((double) dev->timer_icr * (double) dev->timer_div)
        / (cpu_busspeed / 1000000.0);

    apic_log("APIC: Timer start, ICR=%08X div=%d period=%.2f us\n",
             dev->timer_icr, dev->timer_div, period_us);

    timer_on_auto(&dev->timer, period_us);
}

/*
 * MMIO read handler for the Local APIC.
 */
static uint8_t
apic_mem_readb(uint32_t addr, void *priv)
{
    /* APIC registers must be accessed as 32-bit aligned. */
    return 0xFF;
}

static uint16_t
apic_mem_readw(uint32_t addr, void *priv)
{
    return 0xFFFF;
}

static uint32_t
apic_mem_readl(uint32_t addr, void *priv)
{
    apic_t  *dev    = (apic_t *) priv;
    uint32_t offset = addr & 0xFFF;
    uint32_t ret    = 0;

    switch (offset) {
        case APIC_REG_ID:
            ret = dev->id;
            break;

        case APIC_REG_VERSION:
            ret = APIC_VERSION;
            break;

        case APIC_REG_TPR:
            ret = dev->tpr;
            break;

        case APIC_REG_APR:
            /* Arbitration Priority Register -- stub, return 0. */
            ret = 0;
            break;

        case APIC_REG_PPR:
            ret = apic_get_ppr(dev);
            break;

        case APIC_REG_LDR:
            ret = dev->ldr;
            break;

        case APIC_REG_DFR:
            ret = dev->dfr;
            break;

        case APIC_REG_SVR:
            ret = dev->svr;
            break;

        /* ISR: 8 registers at 0x100-0x170 */
        case 0x100:
        case 0x110:
        case 0x120:
        case 0x130:
        case 0x140:
        case 0x150:
        case 0x160:
        case 0x170:
            ret = dev->isr[(offset - APIC_REG_ISR_BASE) >> 4];
            break;

        /* TMR: 8 registers at 0x180-0x1F0 */
        case 0x180:
        case 0x190:
        case 0x1A0:
        case 0x1B0:
        case 0x1C0:
        case 0x1D0:
        case 0x1E0:
        case 0x1F0:
            ret = dev->tmr[(offset - APIC_REG_TMR_BASE) >> 4];
            break;

        /* IRR: 8 registers at 0x200-0x270 */
        case 0x200:
        case 0x210:
        case 0x220:
        case 0x230:
        case 0x240:
        case 0x250:
        case 0x260:
        case 0x270:
            ret = dev->irr[(offset - APIC_REG_IRR_BASE) >> 4];
            break;

        case APIC_REG_ESR:
            ret = dev->esr;
            break;

        case APIC_REG_ICR_LOW:
            ret = dev->icr_low;
            break;

        case APIC_REG_ICR_HIGH:
            ret = dev->icr_high;
            break;

        case APIC_REG_LVT_TIMER:
            ret = dev->lvt_timer;
            break;

        case APIC_REG_LVT_THERMAL:
            ret = dev->lvt_thermal;
            break;

        case APIC_REG_LVT_PERF:
            ret = dev->lvt_perf;
            break;

        case APIC_REG_LVT_LINT0:
            ret = dev->lvt_lint0;
            break;

        case APIC_REG_LVT_LINT1:
            ret = dev->lvt_lint1;
            break;

        case APIC_REG_LVT_ERROR:
            ret = dev->lvt_error;
            break;

        case APIC_REG_TIMER_ICR:
            ret = dev->timer_icr;
            break;

        case APIC_REG_TIMER_CCR:
            /* Current Count: compute from remaining timer time. */
            if (timer_is_enabled(&dev->timer) && dev->timer_icr > 0) {
                uint64_t remaining = timer_get_remaining_us(&dev->timer);
                double   tick_us   = (double) dev->timer_div / (cpu_busspeed / 1000000.0);
                if (tick_us > 0.0)
                    ret = (uint32_t) (remaining / tick_us);
                else
                    ret = 0;
            } else {
                ret = 0;
            }
            break;

        case APIC_REG_TIMER_DCR:
            ret = dev->timer_dcr;
            break;

        default:
            apic_log("APIC: Read from unknown register %03X\n", offset);
            ret = 0;
            break;
    }

    apic_log("APIC: Read  [%03X] = %08X\n", offset, ret);
    return ret;
}

/*
 * MMIO write handler for the Local APIC.
 */
static void
apic_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
    /* APIC registers must be accessed as 32-bit aligned -- ignore byte writes. */
}

static void
apic_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    /* Ignore word writes. */
}

static void
apic_mem_writel(uint32_t addr, uint32_t val, void *priv)
{
    apic_t  *dev    = (apic_t *) priv;
    uint32_t offset = addr & 0xFFF;

    apic_log("APIC: Write [%03X] = %08X\n", offset, val);

    switch (offset) {
        case APIC_REG_ID:
            /* Only bits 27:24 are writable (APIC ID). */
            dev->id = val & 0x0F000000;
            break;

        case APIC_REG_TPR:
            dev->tpr = val & 0xFF;
            break;

        case APIC_REG_EOI:
            /* EOI: clear the highest-priority bit in the ISR. */
            {
                int highest = apic_find_highest_bit(dev->isr);
                if (highest >= 0) {
                    apic_log("APIC: EOI, clearing ISR bit %d\n", highest);
                    apic_clear_bit(dev->isr, highest);

                    /* Notify the I/O APIC so it can clear Remote IRR
                       for level-triggered interrupts and re-deliver
                       if the line is still asserted. */
                    ioapic_eoi(highest);
                }
            }
            break;

        case APIC_REG_LDR:
            /* Only bits 31:24 are writable. */
            dev->ldr = val & 0xFF000000;
            break;

        case APIC_REG_DFR:
            /* Only bits 31:28 are writable, rest reads as 1. */
            dev->dfr = (val & 0xF0000000) | 0x0FFFFFFF;
            break;

        case APIC_REG_SVR:
            {
                int was_enabled = !!(dev->svr & APIC_SVR_ENABLE);
                dev->svr        = val & 0x1FF; /* Bits 8:0 are writable. */
                int now_enabled = !!(dev->svr & APIC_SVR_ENABLE);

                if (!was_enabled && now_enabled)
                    apic_log("APIC: Software enabled\n");
                else if (was_enabled && !now_enabled) {
                    apic_log("APIC: Software disabled\n");
                    /* Disable timer when APIC is disabled. */
                    timer_disable(&dev->timer);
                }
            }
            break;

        case APIC_REG_ESR:
            /* Writing to ESR clears it (write any value). */
            dev->esr = 0;
            break;

        case APIC_REG_ICR_LOW:
            /* ICR Low: stub for Phase 1.
               Store the value but don't deliver IPIs yet. */
            dev->icr_low = val;
            apic_log("APIC: ICR Low write: %08X (IPI delivery stubbed)\n", val);
            break;

        case APIC_REG_ICR_HIGH:
            dev->icr_high = val & 0xFF000000;
            break;

        case APIC_REG_LVT_TIMER:
            dev->lvt_timer = val & 0x000300FF;
            /* Bit 17 = periodic, bit 16 = mask, bits 7:0 = vector. */
            break;

        case APIC_REG_LVT_THERMAL:
            dev->lvt_thermal = val & 0x000107FF;
            break;

        case APIC_REG_LVT_PERF:
            dev->lvt_perf = val & 0x000107FF;
            break;

        case APIC_REG_LVT_LINT0:
            dev->lvt_lint0 = val & 0x0001F7FF;
            apic_log("APIC: LINT0 = %08X (DM=%d, mask=%d)\n",
                     dev->lvt_lint0,
                     (dev->lvt_lint0 >> 8) & 7,
                     !!(dev->lvt_lint0 & APIC_LVT_MASKED));
            break;

        case APIC_REG_LVT_LINT1:
            dev->lvt_lint1 = val & 0x0001F7FF;
            apic_log("APIC: LINT1 = %08X (DM=%d, mask=%d)\n",
                     dev->lvt_lint1,
                     (dev->lvt_lint1 >> 8) & 7,
                     !!(dev->lvt_lint1 & APIC_LVT_MASKED));
            break;

        case APIC_REG_LVT_ERROR:
            dev->lvt_error = val & 0x000100FF;
            break;

        case APIC_REG_TIMER_ICR:
            dev->timer_icr = val;
            /* Writing ICR starts the timer countdown. */
            if (dev->svr & APIC_SVR_ENABLE)
                apic_timer_start(dev);
            break;

        case APIC_REG_TIMER_DCR:
            dev->timer_dcr = val & 0x0B;
            dev->timer_div = apic_get_timer_divisor(dev->timer_dcr);
            apic_log("APIC: Timer divisor = %d\n", dev->timer_div);
            break;

        /* Read-only registers. */
        case APIC_REG_VERSION:
        case APIC_REG_APR:
        case APIC_REG_PPR:
        case APIC_REG_TIMER_CCR:
        case 0x100:
        case 0x110:
        case 0x120:
        case 0x130:
        case 0x140:
        case 0x150:
        case 0x160:
        case 0x170:
        case 0x180:
        case 0x190:
        case 0x1A0:
        case 0x1B0:
        case 0x1C0:
        case 0x1D0:
        case 0x1E0:
        case 0x1F0:
        case 0x200:
        case 0x210:
        case 0x220:
        case 0x230:
        case 0x240:
        case 0x250:
        case 0x260:
        case 0x270:
            /* Ignore writes to read-only registers. */
            break;

        default:
            apic_log("APIC: Write to unknown register %03X = %08X\n", offset, val);
            break;
    }
}

/*
 * Initialize the Local APIC.
 * Called from cpu_set() when the CPU supports APIC (is_p6).
 */
void
apic_init(void)
{
    /* Free any existing APIC state. */
    if (apic)
        apic_close();

    apic = (apic_t *) malloc(sizeof(apic_t));
    memset(apic, 0, sizeof(apic_t));

    /* Set default base address and MSR.
       BSP flag (bit 8) set, global enable (bit 11) set. */
    apic->base_addr = APIC_DEFAULT_BASE;
    apic->msr       = APIC_DEFAULT_BASE | APIC_MSR_ENABLE | APIC_MSR_BSP;

    /* Initialize timer. */
    timer_add(&apic->timer, apic_timer_callback, apic, 0);

    /* Set up MMIO mapping. */
    mem_mapping_add(&apic->mem_mapping,
                    apic->base_addr,
                    0x1000,
                    apic_mem_readb,
                    apic_mem_readw,
                    apic_mem_readl,
                    apic_mem_writeb,
                    apic_mem_writew,
                    apic_mem_writel,
                    NULL,
                    MEM_MAPPING_EXTERNAL,
                    apic);

    /* Reset to power-on defaults. */
    apic_reset();

    apic_log("APIC: Initialized at base %08X\n", apic->base_addr);
}

/*
 * Reset the Local APIC to power-on defaults.
 */
void
apic_reset(void)
{
    if (!apic)
        return;

    apic_t *dev = apic;

    /* APIC ID: 0 for BSP. */
    dev->id = 0;

    /* TPR: 0. */
    dev->tpr = 0;

    /* LDR: 0. */
    dev->ldr = 0;

    /* DFR: all 1s (flat model). */
    dev->dfr = 0xFFFFFFFF;

    /* SVR: APIC disabled, vector 0xFF.
       BIOS will enable it by setting bit 8. */
    dev->svr = 0xFF;

    /* Clear all vector registers. */
    memset(dev->isr, 0, sizeof(dev->isr));
    memset(dev->tmr, 0, sizeof(dev->tmr));
    memset(dev->irr, 0, sizeof(dev->irr));

    /* ESR: 0. */
    dev->esr = 0;

    /* ICR: 0. */
    dev->icr_low  = 0;
    dev->icr_high = 0;

    /* LVT entries: all masked on reset.
       LINT0: ExtINT mode (virtual wire), masked.
       LINT1: NMI mode, masked. */
    dev->lvt_timer   = APIC_LVT_MASKED;
    dev->lvt_thermal = APIC_LVT_MASKED;
    dev->lvt_perf    = APIC_LVT_MASKED;
    dev->lvt_lint0   = APIC_LVT_MASKED | APIC_LVT_DM_EXTINT;
    dev->lvt_lint1   = APIC_LVT_MASKED | APIC_LVT_DM_NMI;
    dev->lvt_error   = APIC_LVT_MASKED;

    /* Timer: stopped. */
    dev->timer_icr = 0;
    dev->timer_ccr = 0;
    dev->timer_dcr = 0;
    dev->timer_div = 2; /* Default divisor is 2. */
    timer_disable(&dev->timer);

    apic_log("APIC: Reset to defaults\n");
}

/*
 * Shut down the Local APIC.
 */
void
apic_close(void)
{
    if (!apic)
        return;

    mem_mapping_disable(&apic->mem_mapping);
    timer_disable(&apic->timer);

    free(apic);
    apic = NULL;
}

/*
 * Returns 1 if the Local APIC is enabled.
 * The APIC is enabled when:
 *   1. The MSR global enable bit (bit 11) is set, AND
 *   2. The SVR software enable bit (bit 8) is set.
 */
int
apic_enabled(void)
{
    if (!apic)
        return 0;

    return (apic->msr & APIC_MSR_ENABLE) && (apic->svr & APIC_SVR_ENABLE);
}

/*
 * Check if APIC has a pending interrupt that can be delivered.
 * Returns the vector number (0-255) or -1 if none pending.
 *
 * An interrupt can be delivered when:
 *   1. APIC is enabled
 *   2. There is a pending IRR bit
 *   3. The IRR priority is higher than TPR/PPR
 */
int
apic_get_interrupt(void)
{
    if (!apic || !apic_enabled())
        return -1;

    int highest_irr = apic_find_highest_bit(apic->irr);
    if (highest_irr < 0)
        return -1;

    /* Check priority against PPR. */
    uint32_t ppr = apic_get_ppr(apic);
    if ((highest_irr & 0xF0) <= (ppr & 0xF0))
        return -1;

    /* Accept the interrupt: move from IRR to ISR. */
    apic_clear_bit(apic->irr, highest_irr);
    apic_set_bit(apic->isr, highest_irr);

    apic_log("APIC: Delivering vector %02X\n", highest_irr);
    return highest_irr;
}

/*
 * Raise an interrupt in the APIC's IRR for the given vector.
 */
void
apic_set_irr(int vector)
{
    if (!apic || vector < 0 || vector > 255)
        return;

    apic_set_bit(apic->irr, vector);
}

/*
 * Signal LINT0 -- connected to PIC in virtual wire mode.
 *
 * In virtual wire mode, LINT0 is configured as ExtINT,
 * which passes through the 8259 PIC interrupt.
 * The PIC interrupt vector is obtained via picinterrupt()
 * at delivery time.
 */
void
apic_lint0_raise(void)
{
    if (!apic || !apic_enabled())
        return;

    /* Check if LINT0 is masked. */
    if (apic->lvt_lint0 & APIC_LVT_MASKED)
        return;

    int dm = apic->lvt_lint0 & APIC_LVT_DM_MASK;

    switch (dm) {
        case APIC_LVT_DM_EXTINT:
            /* ExtINT mode: the interrupt vector comes from the PIC
               at the time of acknowledgment. We don't set an IRR bit
               here -- instead, the interrupt dispatch code in the
               execution loop will call picinterrupt() directly.
               We just need to signal that an APIC interrupt is pending. */
            break;

        case APIC_LVT_DM_FIXED:
            /* Fixed mode: use the vector from the LVT entry. */
            apic_set_bit(apic->irr, apic->lvt_lint0 & 0xFF);
            break;

        default:
            apic_log("APIC: LINT0 delivery mode %d not handled\n", dm >> 8);
            break;
    }
}

/*
 * Signal LINT1 -- typically NMI.
 */
void
apic_lint1_raise(void)
{
    if (!apic || !apic_enabled())
        return;

    if (apic->lvt_lint1 & APIC_LVT_MASKED)
        return;

    int dm = apic->lvt_lint1 & APIC_LVT_DM_MASK;

    switch (dm) {
        case APIC_LVT_DM_NMI:
            /* NMI: deliver as interrupt vector 2. */
            nmi = 1;
            break;

        case APIC_LVT_DM_FIXED:
            apic_set_bit(apic->irr, apic->lvt_lint1 & 0xFF);
            break;

        default:
            apic_log("APIC: LINT1 delivery mode %d not handled\n", dm >> 8);
            break;
    }
}

/*
 * Read the IA32_APIC_BASE MSR (MSR 0x1B).
 */
uint64_t
apic_read_msr(void)
{
    if (!apic)
        return 0;

    return apic->msr;
}

/*
 * Write the IA32_APIC_BASE MSR (MSR 0x1B).
 * This can change the MMIO base address and enable/disable the APIC.
 */
void
apic_write_msr(uint64_t val)
{
    if (!apic)
        return;

    /* BSP flag (bit 8) is read-only -- preserve original value. */
    val = (val & ~APIC_MSR_BSP) | (apic->msr & APIC_MSR_BSP);

    uint32_t new_base    = (uint32_t) (val & APIC_MSR_BASE_MASK);
    int      was_enabled = !!(apic->msr & APIC_MSR_ENABLE);
    int      now_enabled = !!(val & APIC_MSR_ENABLE);

    apic_log("APIC: MSR write: %016llX (base=%08X, enable=%d)\n",
             (unsigned long long) val, new_base, now_enabled);

    /* Update the stored MSR value. */
    apic->msr = val;

    /* Remap MMIO if base address changed. */
    if (new_base != apic->base_addr) {
        apic_log("APIC: Remapping from %08X to %08X\n",
                 apic->base_addr, new_base);
        mem_mapping_set_addr(&apic->mem_mapping, new_base, 0x1000);
        apic->base_addr = new_base;
    }

    /* Handle enable/disable transitions. */
    if (was_enabled && !now_enabled) {
        apic_log("APIC: Globally disabled via MSR\n");
        mem_mapping_disable(&apic->mem_mapping);
        timer_disable(&apic->timer);
    } else if (!was_enabled && now_enabled) {
        apic_log("APIC: Globally enabled via MSR\n");
        mem_mapping_enable(&apic->mem_mapping);
    }
}

/*
 * Returns non-zero if APIC has a pending interrupt.
 * This is a fast check for block-end decisions in the execution loop.
 */
int
apic_int_pending(void)
{
    if (!apic || !apic_enabled())
        return 0;

    int highest_irr = apic_find_highest_bit(apic->irr);
    if (highest_irr < 0)
        return 0;

    uint32_t ppr = apic_get_ppr(apic);
    return (highest_irr & 0xF0) > (ppr & 0xF0);
}

/*
 * Returns non-zero if the CPU has an APIC (is_p6 or equivalent).
 */
int
apic_present(void)
{
    return is_p6;
}
