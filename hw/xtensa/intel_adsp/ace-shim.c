/* CAVS SHIM register operations
 * IP Region: SHIM host interface core (clock, IPC bridge, timers, wake, IRQ glue).
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"
#include "common.h"
#include "../trace.h"

/* Moved from ace.h */
#define SHIM_PWRCTL     0x90  /* Power Write Control */
#define SHIM_PWRSTS     0x92  /* Power Status */
#define SHIM_DSSCS      0xF0  /* DSP Subsystem Control/Status */
#define SHIM_DSPC0CTL   0x100 /* DSP Core 0 Control */
#define SHIM_DSPC1CTL   0x104 /* DSP Core 1 Control */
#define SHIM_DSPC2CTL   0x108 /* DSP Core 2 Control */
#define SHIM_DSPC3CTL   0x10C /* DSP Core 3 Control */
#define SHIM_DSPC4CTL   0x110 /* DSP Core 4 Control */
#define SHIM_DSPCxCTL(x) (0x100 + (x) * 4) /* DSP Core x Control */
#define PWRCTL_WPDSP0PG    (1 << 0)  /* DSP Core 0 Power Gate control */
#define PWRCTL_WPDSP1PG    (1 << 1)  /* DSP Core 1 Power Gate control */
#define PWRCTL_WPDSP2PG    (1 << 2)  /* DSP Core 2 Power Gate control */
#define PWRCTL_WPDSP3PG    (1 << 3)  /* DSP Core 3 Power Gate control */
#define PWRCTL_WPDSP4PG    (1 << 4)  /* DSP Core 4 Power Gate control */
#define PWRCTL_WPML0PG     (1 << 8)  /* ML Block 0 Power Gate control */
#define PWRCTL_WPML1PG     (1 << 9)  /* ML Block 1 Power Gate control */
#define PWRCTL_WPHSTPG     (1 << 16) /* Host domain Power Gate control */
#define PWRCTL_WPHUBHPPG   (1 << 20) /* HUB High Performance domain Power Gate */
#define PWRCTL_PHUBULPPG   (1 << 24) /* HUB Ultra Low Power domain Power Gate */
#define PWRCTL_WPIO0PG     (1 << 28) /* I/O domain 0 Power Gate control */
#define PWRCTL_WPIO1PG     (1 << 29) /* I/O domain 1 Power Gate control */
#define PWRCTL_WPIO2PG     (1 << 30) /* I/O domain 2 Power Gate control */
#define PWRCTL_WPIO3PG     (1 << 31) /* I/O domain 3 Power Gate control */
#define PWRSTS_DSP0PGS     (1 << 0)  /* DSP Core 0 Power Gate Status */
#define PWRSTS_DSP1PGS     (1 << 1)  /* DSP Core 1 Power Gate Status */
#define PWRSTS_DSP2PGS     (1 << 2)  /* DSP Core 2 Power Gate Status */
#define PWRSTS_DSP3PGS     (1 << 3)  /* DSP Core 3 Power Gate Status */
#define PWRSTS_DSP4PGS     (1 << 4)  /* DSP Core 4 Power Gate Status */
#define PWRSTS_ML0PGS      (1 << 8)  /* ML Block 0 Power Gate Status */
#define PWRSTS_ML1PGS      (1 << 9)  /* ML Block 1 Power Gate Status */
#define PWRSTS_HSTPGS      (1 << 16) /* Host domain Power Gate Status */
#define PWRSTS_HUBHPPGS    (1 << 20) /* HUB HP domain Power Gate Status */
#define PWRSTS_HUBULPPGS   (1 << 24) /* HUB ULP domain Power Gate Status */
#define PWRSTS_IO0PGS      (1 << 28) /* I/O domain 0 Power Gate Status */
#define PWRSTS_IO1PGS      (1 << 29) /* I/O domain 1 Power Gate Status */
#define PWRSTS_IO2PGS      (1 << 30) /* I/O domain 2 Power Gate Status */
#define DSSCS_SPA          (1 << 0)  /* Set Power Active (1=boot DSP subsystem) */
#define DSSCS_CPA          (1 << 8)  /* Current Power Active (1=subsystem powered) */
#define DSPCxCTL_SPA       (1 << 0)  /* Set Power Active (1=boot core, 0=reset) */
#define DSPCxCTL_CPA       (1 << 8)  /* Current Power Active (1=core running) */
#define DSPCxCTL_BYPROM    (1 << 16) /* Bypass ROM (1=use DSPCxBADDR instead) */
#define CLKSTS_OCS             (0x3 << 0)   /* Current Oscillator Clock Select */
#define CLKSTS_EOCS            (1 << 2)     /* Current Extended OCS */
#define SHIM_D0HIPCIE          0xC0    /* DSP Core 0 Host IPC Interrupt Enable */
#define SHIM_D0SBIPCIE         0xC4    /* DSP Core 0 Sideband IPC Interrupt Enable */
#define SHIM_D1HIPCIE          0xC8    /* DSP Core 1 Host IPC Interrupt Enable */
#define SHIM_D1SBIPCIE         0xCC    /* DSP Core 1 Sideband IPC Interrupt Enable */
#define SHIM_D2HIPCIE          0xD0    /* DSP Core 2 Host IPC Interrupt Enable */
#define SHIM_D2SBIPCIE         0xD4    /* DSP Core 2 Sideband IPC Interrupt Enable */
#define SHIM_D3HIPCIE          0xD8    /* DSP Core 3 Host IPC Interrupt Enable */
#define SHIM_D3SBIPCIE         0xDC    /* DSP Core 3 Sideband IPC Interrupt Enable */
#define HIPCIE_ALL             0xF         /* All IPC instances */
#define SHIM_HIPCIS            0xE0    /* Host IPC Interrupt Status */
#define SHIM_SPSREQ            0xF8    /* SoC Power State Request */
#define SHIM_SPSRSP            0xFC    /* SoC Power State Response */
#define SPSREQ_RDDRP           (0x3 << 0)   /* Request DDR Power State */
#define SPSREQ_RVNNP           (0x3 << 2)   /* Request Vnn Power State */
#define SPSREQ_SLPLVL          (0x3 << 12)  /* Sleep Level Request */
#define SPSRSP_DDRSPDRE        (1 << 0)     /* DDR State Power Down Request Event */
#define SPSRSP_DDRSPDAE        (1 << 1)     /* DDR State Power Down Acknowledge Event */
#define SPSRSP_VNNSPDRE        (1 << 2)     /* Vnn State Power Down Request Event */
#define SPSRSP_VNNSPDAE        (1 << 3)     /* Vnn State Power Down Acknowledge Event */
#define SPSRSP_SLPLVRQRE       (1 << 12)    /* Sleep Level Request Event */
#define SHIM_LTRC              0xE4    /* Latency Tolerance Reporting Control */
#define LTRC_SNOOP_REQ         (0x3FF << 0)  /* Snoop latency requirement (1.024us units) */
#define LTRC_NO_SNOOP_REQ      (0x3FF << 10) /* No-snoop latency requirement */
#define LTRC_GB                (0x7 << 20)   /* Guard Band (scale factor) */
#define SHIM_L2LMCAP           0xE8    /* L2 Local Memory Capabilities */
#define SHIM_L2MPAT            0xEC    /* L2 Memory Protection Attributes */
#define L2LMCAP_PRESENT        (1 << 16)     /* L2 Local Memory Present */
#define L2MPAT_BASE            (0xFFFF << 0) /* Base address (4KB aligned) */
#define L2MPAT_SIZE            (0xFF << 16)  /* Size (4KB units) */

#define ace_region(raddr)    info->region[(raddr) >> 2]

#define ACE30_SECONDARY_ENTRY_OFFSET 0x10

static bool ace_secondary_entry_read(struct adsp_dev *adsp, uint32_t *entry)
{
    struct adsp_mem_desc *mem;

    /* In ACE platforms, the secondary core entry point is dynamically stored
     * by the Host or the ROM in a known location in LP-SRAM (or IMR).
     * Zephyr places the reset vector at ROM_JUMP_ADDR (LP-SRAM + 0x10).
     */
    mem = adsp_get_mem_space(adsp, 0xa0000000); /* LP_SRAM_BASE */
    if (mem && mem->ptr) {
        *entry = ldl_le_p((uint8_t *)mem->ptr + 0x10);
        return true;
    }

    *entry = 0xa0000400; /* Default LP-SRAM reset vector for Zephyr */
    return true;
}

void ace_secondary_core_set_running(struct adsp_dev *adsp,
                                           uint32_t core_num,
                                           bool running,
                                           const char *reason)
{
    struct adsp_xtensa *xt;
    bool was_running;
    uint32_t entry;

    if (core_num == 0 || core_num >= ARRAY_SIZE(adsp->xtensa)) {
        return;
    }

    xt = adsp->xtensa[core_num];
    if (!xt || !xt->env) {
        return;
    }

    was_running = !xt->env->runstall;

    if (running) {
        cpu_reset(CPU(xt->cpu));

        if (ace_secondary_entry_read(adsp, &entry)) {
            xt->env->pc = entry;
            qemu_log(
                    "ACE30 core%u: entry from LPSRAM+0x%x = 0x%08x\n",
                    core_num, ACE30_SECONDARY_ENTRY_OFFSET, entry);
        } else {
            qemu_log(
                    "ACE30 core%u: failed to read entry at LPSRAM+0x%x, using reset vector\n",
                    core_num, ACE30_SECONDARY_ENTRY_OFFSET);
        }

        xtensa_runstall(xt->env, false);
    } else {
        xtensa_runstall(xt->env, true);
        cpu_reset(CPU(xt->cpu));
    }

    qemu_log(
            "ACE30 core%u: %s -> %s (%s)\n",
            core_num,
            was_running ? "running" : "stalled",
            running ? "running" : "stalled",
            reason ? reason : "unspecified");
}

static inline uint64_t ns2ticks(uint64_t ns, uint64_t clk_kHz)
{
    return (ns * clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000);
}

static inline uint64_t ticks2ns(uint64_t ticks, uint64_t clk_kHz)
{
    return (ticks * ACE_TIMER_MULTIPLIER * 1000000) / clk_kHz;
}

uint64_t ace_set_time(struct adsp_dev *adsp, struct adsp_io_info *info)
{
    uint64_t time = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - adsp->timer[0].start);
    uint64_t ticks = ns2ticks(time, adsp->timer[0].clk_kHz);

    ticks += ace_timer_ticks_adjustment;

    /* SHIM exposes DSPWC as a split 64-bit free-running counter. */
    ace_region((SHIM_DSPWC + 4)) = (uint32_t)(ticks >> 32);
    ace_region((SHIM_DSPWC + 0)) = (uint32_t)(ticks & 0xffffffff);

    return time;
}

void ace_rearm_ext_timer0(struct adsp_dev *adsp, struct adsp_io_info *info)
{
    uint64_t waketicks = ((uint64_t)(ace_region((SHIM_DSPWCTT0C + 4))) << 32) |
        ace_region((SHIM_DSPWCTT0C + 0));

    ace_set_time(adsp, info);

    uint64_t adjusted_target = (waketicks > ace_timer_ticks_adjustment) ? 
                               (waketicks - ace_timer_ticks_adjustment) : 0;

    uint64_t waketime = ticks2ns(adjusted_target, adsp->timer[0].clk_kHz) + adsp->timer[0].start;

    timer_mod(adsp->timer[0].timer, waketime);
}

void ace_rearm_ext_timer1(struct adsp_dev *adsp, struct adsp_io_info *info)
{
    uint64_t waketicks = ((uint64_t)(ace_region((SHIM_DSPWCTT1C + 4))) << 32) |
        ace_region((SHIM_DSPWCTT1C + 0));

    ace_set_time(adsp, info);

    uint64_t adjusted_target = (waketicks > ace_timer_ticks_adjustment) ? 
                               (waketicks - ace_timer_ticks_adjustment) : 0;

    uint64_t waketime = ticks2ns(adjusted_target, adsp->timer[0].clk_kHz) + adsp->timer[0].start;

    timer_mod(adsp->timer[1].timer, waketime);
}

void ace_ext_timer_cb0(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;

    /* The arm bit drops when hardware reaches the programmed compare value. */
    ace_region(SHIM_DSPWCTTCS) &= ~SHIM_DSPWCTTCS_T0A;
    /* Latch timeout status until FW acknowledges it with a W1C write. */
    ace_region(SHIM_DSPWCTTCS) |= SHIM_DSPWCTTCS_T0T;

    /* Route timer expiry to the level-2 root line used by FW on ACE1.5/2.0. */
    adsp_set_lvl1_irq(adsp, IRQ_NUM_EXT_LEVEL2, 1);
}

void ace_ext_timer_cb1(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;

    /* Mirror timer 0 semantics for timer 1: clear the arm request once it fires. */
    ace_region(SHIM_DSPWCTTCS) &= ~SHIM_DSPWCTTCS_T1A;
    /* Latch timer 1 timeout so FW can observe and clear it explicitly. */
    ace_region(SHIM_DSPWCTTCS) |= SHIM_DSPWCTTCS_T1T;

    /* Route timer expiry to the level-2 root line used by FW on ACE1.5/2.0. */
    adsp_set_lvl1_irq(adsp, IRQ_NUM_EXT_LEVEL2, 1);
}

void ace_shim_reset(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;

    memset(info->region, 0, space->desc.size);
    
    /* Initialize read-only capability registers to their static values */
    ace_region(SHIM_L2LMCAP) = (1024 << 0) | L2LMCAP_PRESENT;  /* 1MB L2 memory */
}

/* SHIM IO from ADSP */
uint64_t ace_shim_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    hwaddr aligned_addr = addr & ~3;  /* Align to 4-byte boundary */
    uint32_t word = ace_region(aligned_addr);
    uint32_t byte_offset = addr & 3;  /* Get byte offset within word */
    uint32_t reg_val;
    uint64_t read_val;
    char status_buf[256];
    
    /* Extract the requested bytes based on offset and size */
    if (!ace_extract_subword_read(word, size, byte_offset, &read_val)) {
        qemu_log( "SHIM read: unsupported size %u at addr 0x%lx\n", size, (unsigned long)addr);
        reg_val = 0;
        goto out;
    }
    reg_val = read_val;
    
    /* Use aligned_addr for register offset comparisons */
    addr = aligned_addr;

    switch (addr) {
    case SHIM_DSPWC:
        ace_set_time(adsp, info);
        break;
    case SHIM_PWRCTL:
        /* TODO: trace_adsp_dsp_pwrctl_read(reg_val); */
        qemu_log( "PWRCTL: read 0x%08x\n", reg_val);
        break;
    case SHIM_PWRSTS:
        /* Build human-readable status string for logging */
        snprintf(status_buf, sizeof(status_buf), "%s%s%s%s%s%s%s%s%s%s%s%s%s",
                 (reg_val & PWRSTS_DSP0PGS) ? "DSP0 " : "",
                 (reg_val & PWRSTS_DSP1PGS) ? "DSP1 " : "",
                 (reg_val & PWRSTS_DSP2PGS) ? "DSP2 " : "",
                 (reg_val & PWRSTS_DSP3PGS) ? "DSP3 " : "",
                 (reg_val & PWRSTS_DSP4PGS) ? "DSP4 " : "",
                 (reg_val & PWRSTS_ML0PGS) ? "ML0 " : "",
                 (reg_val & PWRSTS_ML1PGS) ? "ML1 " : "",
                 (reg_val & PWRSTS_HSTPGS) ? "HST " : "",
                 (reg_val & PWRSTS_HUBHPPGS) ? "HUB-HP " : "",
                 (reg_val & PWRSTS_HUBULPPGS) ? "HUB-ULP " : "",
                 (reg_val & PWRSTS_IO0PGS) ? "IO0 " : "",
                 (reg_val & PWRSTS_IO1PGS) ? "IO1 " : "",
                 (reg_val & PWRSTS_IO2PGS) ? "IO2 " : "");
        /* TODO: trace_adsp_dsp_pwrsts_read(reg_val, status_buf); */
        qemu_log( "PWRSTS: read 0x%08x [%s]\n", reg_val, status_buf);
        break;
    case SHIM_DSSCS:
        snprintf(status_buf, sizeof(status_buf), "SPA=%d CPA=%d",
                 !!(reg_val & DSSCS_SPA), !!(reg_val & DSSCS_CPA));
        /* TODO: trace_adsp_dsp_dsscs_read(reg_val, status_buf); */
        qemu_log( "DSSCS: read 0x%08x (%s)\n", reg_val, status_buf);
        break;
    case SHIM_DSPC0CTL:
    case SHIM_DSPC1CTL:
    case SHIM_DSPC2CTL:
    case SHIM_DSPC3CTL:
    case SHIM_DSPC4CTL:
        snprintf(status_buf, sizeof(status_buf), "SPA=%d CPA=%d BYPROM=%d",
                 !!(reg_val & DSPCxCTL_SPA),
                 !!(reg_val & DSPCxCTL_CPA),
                 !!(reg_val & DSPCxCTL_BYPROM));
        /* TODO: trace_adsp_dsp_dspcctl_read((addr - SHIM_DSPC0CTL) / 4, reg_val, status_buf); */
        qemu_log( "DSPC%dCTL: read 0x%08x (%s)\n", (int)((addr - SHIM_DSPC0CTL) / 4), reg_val, status_buf);
        break;
    case SHIM_CLKCTL:
        qemu_log( "CLKCTL: read 0x%08x\n", reg_val);
        break;
    case SHIM_CLKSTS: {
        uint32_t ocs = (reg_val & CLKSTS_OCS) >> 0;
        uint32_t eocs = (reg_val & CLKSTS_EOCS) >> 2;
        qemu_log( "CLKSTS: read 0x%08x (OCS=%d, EOCS=%d)\n", reg_val, ocs, eocs);
        break;
    }
    case SHIM_D0HIPCIE:
    case SHIM_D1HIPCIE:
    case SHIM_D2HIPCIE:
    case SHIM_D3HIPCIE: {
        uint32_t core = (addr - SHIM_D0HIPCIE) / 8;
        qemu_log( "D%dHIPCIE: read 0x%08x\n", core, reg_val);
        break;
    }
    case SHIM_D0SBIPCIE:
    case SHIM_D1SBIPCIE:
    case SHIM_D2SBIPCIE:
    case SHIM_D3SBIPCIE: {
        uint32_t core = (addr - SHIM_D0SBIPCIE) / 8;
        qemu_log( "D%dSBIPCIE: read 0x%08x\n", core, reg_val);
        break;
    }
    case SHIM_HIPCIS:
        qemu_log( "HIPCIS: read 0x%08x (active IPCs)\n", reg_val);
        break;
    case SHIM_SPSREQ:
        qemu_log( "SPSREQ: read 0x%08x\n", reg_val);
        break;
    case SHIM_SPSRSP: {
        uint32_t ddr_evt = !!(reg_val & (SPSRSP_DDRSPDRE | SPSRSP_DDRSPDAE));
        uint32_t vnn_evt = !!(reg_val & (SPSRSP_VNNSPDRE | SPSRSP_VNNSPDAE));
        qemu_log( "SPSRSP: read 0x%08x (DDR_evt=%d, Vnn_evt=%d)\n", reg_val, ddr_evt, vnn_evt);
        break;
    }
    case SHIM_LTRC:
        qemu_log( "LTRC: read 0x%08x\n", reg_val);
        break;
    case SHIM_L2LMCAP: {
        /* L2LMCAP is a read-only capability register (post-reset static value).
         * It's initialized once and should not change during simulation.
         * Value: 1MB (1024 KB) L2 local memory present.
         * Previous implementation recalculated this on every read, which is incorrect
         * for a capability register. Now properly initialized at boot. */
        uint32_t size_kb = reg_val & 0xFFFF;
        uint32_t present = !!(reg_val & L2LMCAP_PRESENT);
        qemu_log( "L2LMCAP: read 0x%08x (size=%d KB, present=%d)\n", reg_val, size_kb, present);
        break;
    }
    case SHIM_L2MPAT:
        qemu_log( "L2MPAT: read 0x%08x\n", reg_val);
        break;
    default:
        /* Bounds checking for unmapped register offsets
         * Valid SHIM register range is 0x20-0xFC (256-byte aperture)
         * Unhandled addresses within valid range should log a warning */
        if (aligned_addr >= 0x20 && aligned_addr < 0x100) {
            qemu_log_mask(LOG_UNIMP, "SHIM: unimplemented read at offset 0x%lx\n", (unsigned long)aligned_addr);
        }
        break;
    }

out:
    trace_adsp_dsp_shim_read(addr, reg_val);
    qemu_log( "SHIM read: addr=0x%lx size=%u val=0x%x (byte_offset=%u)\n",
            (unsigned long)addr, size, reg_val, byte_offset);

    return reg_val;
}

/* SHIM IO from ADSP */
void ace_shim_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    hwaddr aligned_addr = addr & ~3;  /* Align to 4-byte boundary */
    uint32_t byte_offset = addr & 3;  /* Get byte offset within word */
    uint32_t word;
    uint64_t final_val;
    uint64_t waketicks;

    qemu_log( "SHIM write: addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)addr, size, (unsigned long)val, byte_offset);
    
    /* Sub-word writes are performed with a standard read-modify-write merge. */
    word = ace_region(aligned_addr);
    if (!ace_merge_subword_write(word, val, size, byte_offset, &word)) {
        qemu_log( "SHIM write: unsupported size %u\n", size);
        return;
    }
    final_val = word;
    
    /* Use aligned_addr for register offset comparisons */
    addr = aligned_addr;

    waketicks = ((uint64_t)(ace_region((SHIM_DSPWCTT0C + 4))) << 32);
    waketicks |= ace_region((SHIM_DSPWCTT0C + 0));

    trace_adsp_dsp_shim_write(addr, final_val);

    /* special case registers */
    switch (addr) {
    case SHIM_DSPWC:
        break;
    case SHIM_DSPWCTTCS:
    /* set timer only in valid */
        if ((final_val & SHIM_DSPWCTTCS_T0A) &&
            waketicks ) {
            ace_rearm_ext_timer0(adsp, info);
            /* Preserve the arm bit so FW can poll timer 0 as armed. */
            ace_region(addr) |= final_val;
    }
        if ((final_val & SHIM_DSPWCTTCS_T1A) &&
            waketicks ) {
            ace_rearm_ext_timer1(adsp, info);
            /* Mirror timer 1's armed state into the control register. */
            ace_region(addr) |= final_val;
    }
    /* clear IRQ */
        if ((final_val & SHIM_DSPWCTTCS_T0T) &&
            (ace_region(addr) & SHIM_DSPWCTTCS_T0T)) {
            /* Timeout bits are W1C, so clear only the bits FW wrote back. */
            ace_region(addr) &= ~final_val;
    }
        if ((final_val & SHIM_DSPWCTTCS_T1T) &&
            (ace_region(addr) & SHIM_DSPWCTTCS_T1T)) {
            /* Timeout bits are W1C, so clear only the bits FW wrote back. */
            ace_region(addr) &= ~final_val;
    }

        if (!(ace_region(addr) & (SHIM_DSPWCTTCS_T0T | SHIM_DSPWCTTCS_T1T))) {
            adsp_set_lvl1_irq(adsp, IRQ_NUM_EXT_LEVEL2, 0);
        }
        break;
    case SHIM_DSPWCTT0C:
        /* Store the low compare word that the timer callback later reconstructs. */
        ace_region(addr) = final_val;
        break;
    case SHIM_DSPWCTT0C + 4:
        /* Store the high compare word to complete the 64-bit deadline. */
        ace_region(addr) = final_val;
        break;
    case SHIM_DSPWCTT1C:
        /* Timer 1 follows the same split compare programming model as timer 0. */
        ace_region(addr) = final_val;
        break;
    case SHIM_DSPWCTT1C + 4:
        /* Retain the upper half of timer 1's programmed wake time. */
        ace_region(addr) = final_val;
        break;
    case SHIM_CLKCTL:
        /* ACE3.x: Enhanced clock control with EOCS (Extended Oscillator Clock Select)
         */
        /* Reflect the requested power-gating state so FW reads back its own request. */
        ace_region(addr) = final_val;
        /* CLKSTS reflects current frequency selection including
         * SoC override via HP clock select wire */
        ace_region(SHIM_CLKSTS) = final_val;
        break;
    case SHIM_PWRCTL: {
        /* ACE3.x: Power Write Control register
         * Controls power gating for all DSP subsystem domains.
         * 
         * Power sequencing requirements:
         * 1. Set PWRCTL.WPDSPxPG=1 to power up DSP core domain
         * 2. Poll PWRSTS.DSPxPGS=1 until hardware confirms power up
         * 3. Set DSPCxCTL.SPA=1 to boot the core
         * 4. Poll DSPCxCTL.CPA=1 for core ready status
         * 
         * Note: gated-HUB-HP must stay on when any HP DSP Core or ML Block is alive.
         *       gated-HUB-ULP must stay on when ULP DSP Core is alive.
         */
        uint32_t old_val = ace_region(addr);
        char domains_buf[512];
        int pos = 0;
        
        /* Build domain list for logging */
        if (final_val & PWRCTL_WPDSP0PG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "DSP0=%d->%d ", 
                                                     !!(old_val & PWRCTL_WPDSP0PG), 1);
        if (final_val & PWRCTL_WPDSP1PG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "DSP1=%d->%d ",
                                                     !!(old_val & PWRCTL_WPDSP1PG), 1);
        if (final_val & PWRCTL_WPDSP2PG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "DSP2=%d->%d ",
                                                     !!(old_val & PWRCTL_WPDSP2PG), 1);
        if (final_val & PWRCTL_WPDSP3PG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "DSP3=%d->%d ",
                                                     !!(old_val & PWRCTL_WPDSP3PG), 1);
        if (final_val & PWRCTL_WPDSP4PG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "DSP4=%d->%d ",
                                                     !!(old_val & PWRCTL_WPDSP4PG), 1);
        if (final_val & PWRCTL_WPML0PG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "ML0=%d->%d ",
                                                    !!(old_val & PWRCTL_WPML0PG), 1);
        if (final_val & PWRCTL_WPML1PG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "ML1=%d->%d ",
                                                    !!(old_val & PWRCTL_WPML1PG), 1);
        if (final_val & PWRCTL_WPHSTPG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "HST=%d->%d ",
                                                    !!(old_val & PWRCTL_WPHSTPG), 1);
        if (final_val & PWRCTL_WPHUBHPPG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "HUB-HP=%d->%d ",
                                                      !!(old_val & PWRCTL_WPHUBHPPG), 1);
        if (final_val & PWRCTL_PHUBULPPG) pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "HUB-ULP=%d->%d ",
                                                      !!(old_val & PWRCTL_PHUBULPPG), 1);
        if (final_val & (PWRCTL_WPIO0PG | PWRCTL_WPIO1PG | PWRCTL_WPIO2PG | PWRCTL_WPIO3PG))
            pos += snprintf(domains_buf + pos, sizeof(domains_buf) - pos, "IO=0x%x ",
                            (uint32_t)((final_val >> 28) & 0xF));
        
        /* TODO: trace_adsp_dsp_pwrctl_write((uint32_t)final_val, domains_buf); */
        qemu_log( "PWRCTL: write 0x%08x [%s]\n", (uint32_t)final_val, domains_buf);
        
        ace_region(addr) = final_val;
        
        /* Update PWRSTS to mirror PWRCTL (instant power-up simulation)
         * In real hardware, there would be a delay and polling required */
        /* Complete the transition synchronously and report all requested domains as ready. */
        ace_region(SHIM_PWRSTS) = final_val;

        /* Apply core power-domain changes to actual secondary core execution.
         * Core 0 remains under existing boot flow; secondaries are runtime controlled. */
        for (uint32_t core_num = 1; core_num < ARRAY_SIZE(adsp->xtensa); core_num++) {
            uint32_t core_bit = 1u << core_num;
            uint32_t dspc_reg = SHIM_DSPCxCTL(core_num);
            uint32_t dspc_ctl;
            bool was_powered = !!(old_val & core_bit);
            bool now_powered = !!((uint32_t)final_val & core_bit);

            if (!adsp->xtensa[core_num]) {
                break;
            }

            dspc_ctl = ace_region(dspc_reg);

            if (was_powered && !now_powered) {
                /* Power-gating a secondary core forces it into reset/off state. */
                ace_secondary_core_set_running(adsp, core_num, false,
                                               "PWRCTL: domain power-gated");
                /* Power-gating drops both the start request and the active acknowledgement. */
                dspc_ctl &= ~(DSPCxCTL_SPA | DSPCxCTL_CPA);
                ace_region(dspc_reg) = dspc_ctl;
            } else if (!was_powered && now_powered) {
                /* If FW requested SPA earlier while unpowered, honor it now. */
                if (dspc_ctl & DSPCxCTL_SPA) {
                    /* Assert CPA before releasing the core so FW sees the documented ready bit. */
                    dspc_ctl |= DSPCxCTL_CPA;
                    ace_region(dspc_reg) = dspc_ctl;
                    ace_secondary_core_set_running(adsp, core_num, true,
                                                   "PWRCTL: domain powered with SPA=1");
                }
            }
        }
        break;
    }
    case SHIM_PWRSTS:
        /* Read-only register - writes are ignored */
        break;
    case SHIM_DSSCS: {
        /* ACE3.x: DSP Subsystem Control/Status
         * Global control for DSP subsystem D-state transitions.
         * 
         * SPA (Set Power Active) bit controls:
         * - SPA=1: Bring DSP subsystem out of reset (D0 entry)
         *   * Hardware sequences power-up of subsystem
         *   * Sets CPA=1 when ready
         *   * Required before any DSP core can boot
         * - SPA=0: Shutdown DSP subsystem (D3 entry)
         *   * All Tensilica cores are power gated
         *   * All subsystem logic is reset
         *   * Hardware clears CPA=0
         * 
         * Boot sequence:
         * 1. Host writes DSSCS.SPA=1 to power up DSP subsystem
         * 2. Hardware powers domains and sets DSSCS.CPA=1
         * 3. Host writes DSPCxCTL.SPA=1 to boot primary core
         * 
         * Shutdown sequence:
         * 1. FW properly shuts down workloads
         * 2. Host writes DSSCS.SPA=0
         * 3. Hardware powers down and clears DSSCS.CPA=0
         */
        uint32_t old_val = ace_region(addr);
        const char *action;
        
        if ((final_val & DSSCS_SPA) && !(old_val & DSSCS_SPA)) {
            action = "Subsystem BOOT (D0 entry)";
            /* CPA tracks that the subsystem-level bring-up has completed. */
            ace_region(addr) = final_val | DSSCS_CPA; /* Set CPA when SPA is set */
        } else if (!(final_val & DSSCS_SPA) && (old_val & DSSCS_SPA)) {
            action = "Subsystem SHUTDOWN (D3 entry)";
            /* Clearing SPA collapses the subsystem and drops the completion status. */
            ace_region(addr) = final_val & ~DSSCS_CPA; /* Clear CPA when SPA is cleared */
            /* Reset all core control registers on subsystem shutdown */
            /* Shutdown clears per-core bring-up state so the next boot restarts cleanly. */
            ace_region(SHIM_DSPC0CTL) = 0;
            ace_region(SHIM_DSPC1CTL) = 0;
            ace_region(SHIM_DSPC2CTL) = 0;
            ace_region(SHIM_DSPC3CTL) = 0;
            ace_region(SHIM_DSPC4CTL) = 0;
            /* Drop the mirrored domain power state alongside the subsystem shutdown. */
            ace_region(SHIM_PWRCTL) = 0;
            ace_region(SHIM_PWRSTS) = 0;
        } else {
            action = "No state change";
            /* Other DSSCS fields are retained without changing the current subsystem phase. */
            ace_region(addr) = final_val;
        }
        
        /* TODO: trace_adsp_dsp_dsscs_write(old_val, final_val, action); */
        qemu_log( "DSSCS: 0x%08x -> 0x%08x (%s)\n", old_val, (uint32_t)final_val, action);
        break;
    }
    case SHIM_DSPC0CTL:
    case SHIM_DSPC1CTL:
    case SHIM_DSPC2CTL:
    case SHIM_DSPC3CTL:
    case SHIM_DSPC4CTL: {
        /* ACE3.x: DSP Core x Control
         * Per-core boot and reset control.
         * 
         * Register fields:
         * - SPA [0]: Set Power Active
         *   * 1 = Boot core (release from reset)
         *   * 0 = Hold core in reset
         * - CPA [8]: Current Power Active (read-only status)
         *   * 1 = Core is running
         *   * 0 = Core is in reset
         * - BYPROM [16]: Bypass ROM boot
         *   * 1 = Use DSPCxBADDR for boot address
         *   * 0 = Use L1 ROM (default)
         * 
         * Boot sequence (per core):
         * 1. Set PWRCTL.WPDSPxPG=1 and poll PWRSTS.DSPxPGS=1
         * 2. Optionally set BYPROM and DSPCxBADDR for alternate boot
         * 3. Set DSPCxCTL.SPA=1 to boot core
         * 4. Poll DSPCxCTL.CPA=1 until core ready
         * 5. Hardware releases reset and stall signals
         * 
         * Reset sequence:
         * 1. Set DSPCxCTL.SPA=0 to reset core
         * 2. Core enters reset, CPA is cleared
         * 3. Optionally set PWRCTL.WPDSPxPG=0 to power gate
         */
        uint32_t old_val = ace_region(addr);
        uint32_t core_num = (addr - SHIM_DSPC0CTL) / 4;
        const char *action;
        uint32_t dsp_power_bit = (1 << core_num);
        
        /* Check if the DSP core domain is powered */
        if (!(ace_region(SHIM_PWRCTL) & dsp_power_bit)) {
            /* Domain not powered - reject boot attempt */
            if (final_val & DSPCxCTL_SPA) {
                action = "BOOT REJECTED (domain not powered)";
                /* Preserve FW's request but keep CPA clear until the domain is actually powered. */
                ace_region(addr) = final_val & ~DSPCxCTL_CPA;
                ace_secondary_core_set_running(adsp, core_num, false,
                                               "DSPCxCTL: SPA set while domain off");
            } else {
                action = "Reset (domain not powered)";
                /* Non-start writes are still latched so FW can pre-program BYPROM and similar bits. */
                ace_region(addr) = final_val;
                ace_secondary_core_set_running(adsp, core_num, false,
                                               "DSPCxCTL: reset while domain off");
            }
        } else if ((final_val & DSPCxCTL_SPA) && !(old_val & DSPCxCTL_SPA)) {
            action = "BOOT (release reset)";
            /* Once the domain is powered, SPA immediately drives CPA high in the simplified model. */
            ace_region(addr) = final_val | DSPCxCTL_CPA; /* Set CPA when SPA is set */
            ace_secondary_core_set_running(adsp, core_num, true,
                                           "DSPCxCTL: SPA rising edge");
        } else if (!(final_val & DSPCxCTL_SPA) && (old_val & DSPCxCTL_SPA)) {
            action = "RESET (hold in reset)";
            /* Clearing SPA models the documented stop path and drops the active acknowledgement. */
            ace_region(addr) = final_val & ~DSPCxCTL_CPA; /* Clear CPA when SPA is cleared */
            ace_secondary_core_set_running(adsp, core_num, false,
                                           "DSPCxCTL: SPA falling edge");
        } else {
            action = "No state change";
            /* Preserve other control fields, such as BYPROM, without changing run state. */
            ace_region(addr) = final_val;
        }
        
        /* TODO: trace_adsp_dsp_dspcctl_write(core_num, old_val, final_val, action); */
        qemu_log( "DSPC%dCTL: 0x%08x -> 0x%08x (%s)\n", core_num, old_val, (uint32_t)final_val, action);
        break;
    }
    case SHIM_D0HIPCIE:
    case SHIM_D1HIPCIE:
    case SHIM_D2HIPCIE:
    case SHIM_D3HIPCIE: {
        /* Host IPC Interrupt Enable - route host IPC to specific DSP core */
        uint32_t core = (addr - SHIM_D0HIPCIE) / 8;
        /* Only architecturally defined routing bits are kept. */
        ace_region(addr) = final_val & HIPCIE_ALL;
        qemu_log( "D%dHIPCIE: write 0x%08x\n", core, (uint32_t)(final_val & HIPCIE_ALL));
        break;
    }
    case SHIM_D0SBIPCIE:
    case SHIM_D1SBIPCIE:
    case SHIM_D2SBIPCIE:
    case SHIM_D3SBIPCIE: {
        /* Sideband IPC Interrupt Enable - route sideband IPC to specific DSP core */
        uint32_t core = (addr - SHIM_D0SBIPCIE) / 8;
        /* Sideband IPC uses the same routing-mask contract as host IPC. */
        ace_region(addr) = final_val & HIPCIE_ALL;
        qemu_log( "D%dSBIPCIE: write 0x%08x\n", core, (uint32_t)(final_val & HIPCIE_ALL));
        break;
    }
    case SHIM_SPSREQ: {
        /* SoC Power State Request - coordinate with SoC sleep states */
        uint32_t ddr = (final_val & SPSREQ_RDDRP) >> 0;
        uint32_t vnn = (final_val & SPSREQ_RVNNP) >> 2;
        uint32_t sleep = (final_val & SPSREQ_SLPLVL) >> 12;
        
        /* Retain FW's request verbatim so polling code sees the last requested SoC state. */
        ace_region(addr) = final_val;
        
        /* Simulate immediate acknowledgment for power requests */
        if (ddr != 0) {
            /* Immediately synthesize the response bits that real PM firmware would raise later. */
            ace_region(SHIM_SPSRSP) |= SPSRSP_DDRSPDRE | SPSRSP_DDRSPDAE;
        }
        if (vnn != 0) {
            /* VNN requests are acknowledged synchronously for the same reason. */
            ace_region(SHIM_SPSRSP) |= SPSRSP_VNNSPDRE | SPSRSP_VNNSPDAE;
        }
        if (sleep != 0) {
            /* Sleep-level requests only need the completion indication FW polls. */
            ace_region(SHIM_SPSRSP) |= SPSRSP_SLPLVRQRE;
        }
        
        qemu_log( "SPSREQ: write 0x%08x (DDR=%d, Vnn=%d, Sleep=%d)\n",
                (uint32_t)final_val, ddr, vnn, sleep);
        break;
    }
    case SHIM_SPSRSP: {
        /* SoC Power State Response - W1C (write 1 to clear) event bits */
        uint32_t old_val = ace_region(addr);
        /* Clear only the response bits FW has acknowledged. */
        ace_region(addr) = old_val & ~final_val;  /* Clear bits where final_val has 1s */
        qemu_log( "SPSRSP: write 0x%08x (clear events)\n", (uint32_t)final_val);
        break;
    }
    case SHIM_LTRC: {
        /* Latency Tolerance Reporting Control */
        uint32_t snoop = (final_val & LTRC_SNOOP_REQ) >> 0;
        uint32_t no_snoop = (final_val & LTRC_NO_SNOOP_REQ) >> 10;
        uint32_t gb = (final_val & LTRC_GB) >> 20;
        
        /* LTRC is policy state only, so the model stores the programmed tolerance unchanged. */
        ace_region(addr) = final_val;
        qemu_log( "LTRC: write 0x%08x (Snoop=%d, NoSnoop=%d, GB=%d)\n",
                (uint32_t)final_val, snoop, no_snoop, gb);
        break;
    }
    case SHIM_L2MPAT: {
        /* L2 Memory Protection Attributes */
        uint32_t base = (final_val & L2MPAT_BASE) >> 0;
        uint32_t mem_size = ((final_val & L2MPAT_SIZE) >> 16) * 4;  /* Convert to KB */
        
        /* Preserve FW's selected L2 partitioning attributes for later readers. */
        ace_region(addr) = final_val;
        qemu_log( "L2MPAT: write 0x%08x (Base=0x%x, Size=%d KB)\n",
                (uint32_t)final_val, base, mem_size);
        break;
    }
    default:
        /* Bounds checking for unmapped register offsets
         * Valid SHIM register range is 0x20-0xFC (256-byte aperture)
         * Unhandled addresses within valid range should log a warning */
        if (aligned_addr >= 0x20 && aligned_addr < 0x100) {
            qemu_log_mask(LOG_UNIMP, "SHIM: unimplemented write at offset 0x%lx val=0x%lx\n", 
                          (unsigned long)aligned_addr, (unsigned long)final_val);
        }
        break;
    }
}

const MemoryRegionOps ace_shim_ops = {
    .read = ace_shim_read,
    .write = ace_shim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void adsp_ace_shim_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    ace_shim_reset(info);
    adsp->shim = info;
    
    /* TODO: Clarify offset 0x94 register identity with ACE 30 Architecture manual.
     * Currently undocumented in SHIM register definitions but initialized to 0x80.
     * Possible related to DFPMCCU_PWRCTL2, but value is incomplete (0x80 vs 0x00FF00FF).
     * This initialization should either:
     * - Be moved to PMCCU init if it belongs there
     * - Have a proper #define SHIM_* macro added to ace-shim.c
     * - Be removed if it's dead code */
    /* ace_region(0x94) = 0x00000080; */
    
    /* Initialize register default values per ACE 30 specification:
     * SHIM_PWRCTL  (0x90): 0x00000000 - all domains OFF
     * SHIM_PWRSTS  (0x92): 0x00000000 - all domains OFF (mirrors PWRCTL)
     * SHIM_DSSCS   (0xF0): 0x00000000 - subsystem OFF (SPA=0, CPA=0=RO)
     * SHIM_CLKCTL  (0x78): 0x00000000 - default clock selection
     * SHIM_CLKSTS  (0x7C): 0x00000000 - RO status register
     * SHIM_DSPCxCTL (0x100-0x112): 0x00000000 - all cores OFF (SPA=0, CPA=0)
     * SHIM_HIPCIS  (0xE0): 0x00000000 - no active IPCs
     * SHIM_SPSREQ  (0xF8): 0x00000000 - no power state requests
     * SHIM_SPSRSP  (0xFC): 0x00000000 - no power state events
     * SHIM_LTRC    (0xE4): 0x00000000 - default latency tolerance
     * SHIM_L2LMCAP (0xE8): initialized in read handler (1MB present)
     * SHIM_L2MPAT  (0xEC): 0x00000000 - default L2 memory partition
     * All others: 0x00000000 (via ace_shim_reset)
     */
    
    adsp->timer[0].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &ace_ext_timer_cb0, info);
    adsp->timer[0].clk_kHz = adsp->clk_kHz;
    adsp->timer[1].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &ace_ext_timer_cb1, info);
    adsp->timer[1].clk_kHz = adsp->clk_kHz;
    adsp->timer[0].start = adsp->timer[1].start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}
