/* ACE Wall Clock and Timer virtualization
 * IP Region: DSP wall-clock, compare timers, and watchdog/RTC-facing timer control.
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This provides register-level virtualization for ACE3.x timers/clocks:
 * - DSP Wall Clock (DSPWC) for audio time stamping
 * - Programmable Timers (2x) for scheduled events
 * - RTC Wall Clock for real-time tracking
 * - Time stamping with SoC ART synchronization
 * - Watch Dog Timer (WDT) for firmware hang detection
 *
 * Reference: ACE3.x IP HAS Section 8.5.7 (Timers)
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"
#include "../trace.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"

/* Moved from ace.h */
#define SHIM_DSPWCL            0x20   /* DSP Wall Clock Counter Low */
#define SHIM_DSPWCH            0x24   /* DSP Wall Clock Counter High */
#define SHIM_DSPWCCTL          0x10   /* DSP Wall Clock Control */
#define SHIM_DSPWCAV           0x18   /* DSP Wall Clock Adjust Value */
#define DSPWCCTL_DWCS          (1 << 0)   /* DSP Wall Clock Select (0=XTAL, 1=Resume) */
#define DSPWCCTL_DWCADJ        (1 << 1)   /* DSP Wall Clock Adjust (start sync) */
#define DSPWCCTL_DWCADIR       (1 << 2)   /* Adjust Direction (0=hold, 1=advance) */
#define SHIM_RTCWCL            0x60   /* RTC Wall Clock Counter Low */
#define SHIM_RTCWCH            0x64   /* RTC Wall Clock Counter High */
#define SHIM_DSPWCTT0CL        0x30   /* Timer 0 Compare Value Low */
#define SHIM_DSPWCTT0CH        0x34   /* Timer 0 Compare Value High */
#define SHIM_DSPWCTT1CL        0x38   /* Timer 1 Compare Value Low */
#define SHIM_DSPWCTT1CH        0x3C   /* Timer 1 Compare Value High */
#define DSPWCTTCS_T0EN         (1 << 0)   /* Timer 0 Enable */
#define DSPWCTTCS_T1EN         (1 << 1)   /* Timer 1 Enable */
#define DSPWCTTCS_T0INT        (1 << 4)   /* Timer 0 Interrupt Status (W1C) */
#define DSPWCTTCS_T1INT        (1 << 5)   /* Timer 1 Interrupt Status (W1C) */
#define SHIM_TSCTRL            0x48   /* Time Stamp Control */
#define TSCTRL_HHTSE           (1 << 0)   /* Hammock Harbor Time Stamp Enable */
#define TSCTRL_ODTS            (1 << 1)   /* On-Demand Time Stamp */
#define TSCTRL_CDMAS_SHIFT     8          /* Capture DMA Stream Select */
#define TSCTRL_CDMAS_MASK      (0x1F << 8)
#define SHIM_WDTCS             0x68   /* WDT Control/Status */
#define WDTCS_EN               (1 << 0)   /* Enable */
#define WDTCS_RSTTYPE          (1 << 1)   /* Reset Type (0=interrupt, 1=reset) */
#define WDTCS_RSTWARN          (1 << 2)   /* Reset Warning (1st timeout occurred) */

#define ace_region(raddr)    info->region[(raddr) >> 2]

/* Timer state tracking */
struct timer_state {
    uint64_t compare_value;     /* Compare value for timer */
    bool enabled;               /* Timer enabled */
    bool interrupt_pending;     /* Interrupt status */
};

/* Wall clock heuristic state */
static uint64_t past_ticks[8];
static int past_idx = 0;
uint64_t ace_timer_ticks_adjustment = 0;

/* Wall clock state */
struct wallclock_state {
    uint64_t base_ticks;        /* Base value at last reset */
    int64_t base_ns;            /* QEMU time at last reset */
    uint32_t control;           /* DSPWCCTL */
    uint32_t adjust_value;      /* DSPWCAV - for ART sync */
    bool use_resume_clock;      /* Clock source selection */
};

void ace_timer_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);

}

/* Wall Clock and Timer read handler
 * Provides 64-bit free-running counter and timer compare values.
 * We need to fake this as a 32kkHz, 19.2MHz or 38.4MHz clock. FW will read and write
 * assuming a 32kkHz, 19.2MHz or 38.4MHz clock depending on the clock source.
 * i.e. 1ms of FW ticks corresponds to 32k, 19.2k or 38.4k ticks of the wall clock.
 * 1ms will be smallest unit of time we care about, but we will convert 1ms
 * of FW time to ACE_TIMER_MULTIPLIER ms of qemu time or 32k, 19.2k or 38.4k ticks of the wall clock.
 */
uint64_t ace_timer_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    uint32_t offset = addr;
    uint64_t value = 0;
    (void)size;

    switch (offset) {
    case SHIM_DSPWCL:  /* DSP Wall Clock Low */
    case SHIM_DSPWCH:  /* DSP Wall Clock High */
    {
        /* Calculate current wall clock value based on elapsed time */
        uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - adsp->timer[0].start;
        /* Conver time_ns to FW ticks, knowing ACE_TIMER_MULTIPLIER ms of QEMU time = 1ms of FW time */
        uint64_t ticks = (time_ns * adsp->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000);

        ticks += ace_timer_ticks_adjustment;

        if (offset == SHIM_DSPWCL) {
            past_ticks[past_idx % 8] = ticks;
            past_idx++;

            if (past_idx >= 8) {
                uint64_t oldest = past_ticks[past_idx % 8];
                uint64_t threshold = adsp->timer[0].clk_kHz / 32;
                if (threshold == 0) threshold = 1;

                /* roughly 1/32 ms corresponds to 25000 CCOUNTs dynamically simulating busy-spins */
                if (ticks - oldest < threshold) {
                    uint64_t jump = adsp->timer[0].clk_kHz * 2; /* 2ms FW time */
                    ace_log("FW TIGHT POLL DETECTED! Jumping timer forward by %llu ticks.\n", (unsigned long long)jump);
                    ace_timer_ticks_adjustment += jump;
                    ticks += jump;
                    past_idx = 0;
                }
            }
            value = (uint32_t)(ticks & 0xFFFFFFFF);
        } else {
            value = (uint32_t)(ticks >> 32);
        }

        ace_log( "DSP Wall Clock: read %s = 0x%08x (full=0x%016llx, %lld ns, ctimer=%u kHz)\n",
                (offset == SHIM_DSPWCL) ? "LOW" : "HIGH",
                (uint32_t)value, (unsigned long long)ticks,
                (long long)time_ns, (uint32_t)adsp->timer[0].clk_kHz);
        break;
    }

    case SHIM_DSPWCCTL:  /* DSP Wall Clock Control */
        value = ace_region(addr);
        ace_log( "DSPWCCTL: read 0x%08x [clk=%s adj=%s dir=%s]\n",
                (uint32_t)value,
                (value & DSPWCCTL_DWCS) ? "RESUME" : "XTAL",
                (value & DSPWCCTL_DWCADJ) ? "active" : "idle",
                (value & DSPWCCTL_DWCADIR) ? "advance" : "hold");
        break;

    case SHIM_DSPWCAV:  /* DSP Wall Clock Adjust Value */
        value = ace_region(addr);
        ace_log("ace: read :DSPWCAV@0x%lx: %u: 0x%08x (adjust delta=%d ticks)\n", (unsigned long)offset, size, (uint32_t)value, (uint32_t)value);
        break;

    case SHIM_DSPWCTT0CL:  /* Timer 0 Compare Low */
    case SHIM_DSPWCTT0CH:  /* Timer 0 Compare High */
        value = ace_region(addr);
        ace_log( "Timer 0: read compare %s = 0x%08x\n",
                (offset == SHIM_DSPWCTT0CL) ? "LOW" : "HIGH",
                (uint32_t)value);
        break;

    case SHIM_DSPWCTT1CL:  /* Timer 1 Compare Low */
    case SHIM_DSPWCTT1CH:  /* Timer 1 Compare High */
        value = ace_region(addr);
        ace_log( "Timer 1: read compare %s = 0x%08x\n",
                (offset == SHIM_DSPWCTT1CL) ? "LOW" : "HIGH",
                (uint32_t)value);
        break;

    case SHIM_DSPWCTTCS:  /* Timer Control/Status */
        value = ace_region(addr);
        ace_log( "Timer CTL/STS: read 0x%08x [T0=%s T1=%s T0INT=%d T1INT=%d]\n",
                (uint32_t)value,
                (value & DSPWCTTCS_T0EN) ? "EN" : "DIS",
                (value & DSPWCTTCS_T1EN) ? "EN" : "DIS",
                !!(value & DSPWCTTCS_T0INT),
                !!(value & DSPWCTTCS_T1INT));
        break;

    case SHIM_RTCWCL:  /* RTC Wall Clock Low */
    case SHIM_RTCWCH:  /* RTC Wall Clock High */
    {
        /* RTC clock runs on 32.768 KHz */
        uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint64_t rtc_ticks = time_ns * 32768 / 1000000000;  /* Convert to 32.768 KHz ticks */

        if (offset == SHIM_RTCWCL) {
            value = (uint32_t)(rtc_ticks & 0xFFFFFFFF);
        } else {
            value = (uint32_t)(rtc_ticks >> 32);
        }

        ace_log( "RTC Wall Clock: read %s = 0x%08x (full=0x%016llx)\n",
                (offset == SHIM_RTCWCL) ? "LOW" : "HIGH",
                (uint32_t)value, (unsigned long long)rtc_ticks);
        break;
    }

    case SHIM_TSCTRL:  /* Time Stamp Control */
        value = ace_region(addr);
        ace_log( "TSCTRL: read 0x%08x [HHTSE=%d ODTS=%d DMA=%u]\n",
                (uint32_t)value,
                !!(value & TSCTRL_HHTSE),
                !!(value & TSCTRL_ODTS),
                (unsigned)((value & TSCTRL_CDMAS_MASK) >> TSCTRL_CDMAS_SHIFT));
        break;

    case SHIM_WDTCS:  /* Watch Dog Timer Control/Status */
        value = ace_region(addr);
        ace_log( "WDT: read 0x%08x [EN=%d TYPE=%s WARN=%d]\n",
                (uint32_t)value,
                !!(value & WDTCS_EN),
                (value & WDTCS_RSTTYPE) ? "RESET" : "INT",
                !!(value & WDTCS_RSTWARN));
        break;

    default:
        value = ace_region(addr);
        ace_log( "Timer: read unknown offset 0x%04x = 0x%08x\n",
                offset, (uint32_t)value);
        break;
    }

    return value;
}

/* Wall Clock and Timer write handler
 * Handles timer arming, clock synchronization, and control.
 */
void ace_timer_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    uint32_t offset = addr;
    uint32_t old_value = ace_region(addr);
    (void)size;

    ace_region(addr) = val;

    switch (offset) {
    case SHIM_DSPWCL:  /* DSP Wall Clock Low */
    case SHIM_DSPWCH:  /* DSP Wall Clock High */
        ace_log( "DSP Wall Clock: write %s = 0x%08x (READ-ONLY, ignored)\n",
                (offset == SHIM_DSPWCL) ? "LOW" : "HIGH",
                (uint32_t)val);
        /* DSPWC is read-only, so restore the synthetic counter value after logging the write. */
        ace_region(addr) = old_value;  /* Restore - counter is read-only */
        break;

    case SHIM_DSPWCCTL:  /* DSP Wall Clock Control */
        ace_log( "DSPWCCTL: write 0x%08x [clk=%s adj=%s dir=%s]\n",
                (uint32_t)val,
                (val & DSPWCCTL_DWCS) ? "RESUME(32KHz)" : "XTAL(19.2-38.4MHz)",
                (val & DSPWCCTL_DWCADJ) ? "START" : "idle",
                (val & DSPWCCTL_DWCADIR) ? "ADVANCE" : "HOLD");

        if ((val & DSPWCCTL_DWCADJ) && !(old_value & DSPWCCTL_DWCADJ)) {
            uint32_t adjust_val = ace_region(SHIM_DSPWCAV);
            ace_log( "     Initiating clock sync: delta=%u ticks, direction=%s\n",
                    adjust_val,
                    (val & DSPWCCTL_DWCADIR) ? "ADVANCE" : "HOLD");
            ace_log( "     (Would adjust wall clock to match SoC ART)\n");
        }
        break;

    case SHIM_DSPWCAV:  /* DSP Wall Clock Adjust Value */
        ace_log("ace: write :DSPWCAV@0x%lx: %u: 0x%08x (set adjust delta to %u ticks)\n", (unsigned long)offset, size, (uint32_t)val, (uint32_t)val);
        break;

    case SHIM_DSPWCTT0CL:  /* Timer 0 Compare Low */
    case SHIM_DSPWCTT0CH:  /* Timer 0 Compare High */
    {
        uint64_t compare = ((uint64_t)ace_region(SHIM_DSPWCTT0CH) << 32) |
                           ace_region(SHIM_DSPWCTT0CL);
        uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - adsp->timer[0].start;
        uint64_t now_ticks = (time_ns * adsp->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000) + ace_timer_ticks_adjustment;
        long long delta = compare - now_ticks;
        long long delta_ms = (delta * (long long)ACE_TIMER_MULTIPLIER) / (long long)adsp->timer[0].clk_kHz;
        
        ace_log( "Timer 0: write compare %s = 0x%08x (full=0x%016llx now=0x%016llx delta=%lld ticks, %lld host ms, ctimer=%u kHz)\n",
                (offset == SHIM_DSPWCTT0CL) ? "LOW" : "HIGH",
                (uint32_t)val, (unsigned long long)compare, (unsigned long long)now_ticks, delta, delta_ms, (uint32_t)adsp->timer[0].clk_kHz);

        /* Rearm timer if enabled */
        if (ace_region(SHIM_DSPWCTTCS) & DSPWCTTCS_T0EN) {
            ace_log( "     Timer 0 is ENABLED, rearming...\n");
            ace_rearm_ext_timer0(adsp, info);
        }
        break;
    }

    case SHIM_DSPWCTT1CL:  /* Timer 1 Compare Low */
    case SHIM_DSPWCTT1CH:  /* Timer 1 Compare High */
    {
        uint64_t compare = ((uint64_t)ace_region(SHIM_DSPWCTT1CH) << 32) |
                           ace_region(SHIM_DSPWCTT1CL);
        uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - adsp->timer[0].start;
        uint64_t now_ticks = (time_ns * adsp->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000) + ace_timer_ticks_adjustment;
        long long delta = compare - now_ticks;
        long long delta_ms = (delta * (long long)ACE_TIMER_MULTIPLIER) / (long long)adsp->timer[0].clk_kHz;

        ace_log( "Timer 1: write compare %s = 0x%08x (full=0x%016llx now=0x%016llx delta=%lld ticks, %lld host ms, ctimer=%u kHz)\n",
                (offset == SHIM_DSPWCTT1CL) ? "LOW" : "HIGH",
                (uint32_t)val, (unsigned long long)compare, (unsigned long long)now_ticks, delta, delta_ms, (uint32_t)adsp->timer[0].clk_kHz);
        ace_log( "     (Timer 1 rearm not yet implemented)\n");
        break;
    }

    case SHIM_DSPWCTTCS:  /* Timer Control/Status */
        ace_log("ace: write :Timer CTL/STS@0x%lx: %u: 0x%08x -> 0x%08x\n", (unsigned long)offset, size, old_value, (uint32_t)val);

        if ((val & DSPWCTTCS_T0EN) && !(old_value & DSPWCTTCS_T0EN)) {
            uint64_t compare = ((uint64_t)ace_region(SHIM_DSPWCTT0CH) << 32) |
                               ace_region(SHIM_DSPWCTT0CL);
            uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - adsp->timer[0].start;
            uint64_t now_ticks = (time_ns * adsp->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000) + ace_timer_ticks_adjustment;
            long long delta = compare - now_ticks;
            long long delta_ms = (delta * (long long)ACE_TIMER_MULTIPLIER) / (long long)adsp->timer[0].clk_kHz;
            ace_log( "     Timer 0 ENABLED, compare=0x%016llx (now=0x%016llx delta=%lld ticks, %lld host ms, ctimer=%u kHz)\n",
                    (unsigned long long)compare, (unsigned long long)now_ticks, delta, delta_ms, (uint32_t)adsp->timer[0].clk_kHz);
            ace_rearm_ext_timer0(adsp, info);
        } else if (!(val & DSPWCTTCS_T0EN) && (old_value & DSPWCTTCS_T0EN)) {
			//ace_irq_clear(info->adsp, IRQ_DWCT0, 0);	
            ace_log( "     Timer 0 DISABLED\n");
        }

        if ((val & DSPWCTTCS_T1EN) && !(old_value & DSPWCTTCS_T1EN)) {
            ace_log( "     Timer 1 ENABLED\n");
        } else if (!(val & DSPWCTTCS_T1EN) && (old_value & DSPWCTTCS_T1EN)) {
			//ace_irq_clear(info->adsp, IRQ_DWCT1, 0);	
            ace_log( "     Timer 1 DISABLED\n");
        }

        /* W1C interrupt status bits */
        if (val & DSPWCTTCS_T0INT) {
            /* Timer status bits are W1C. Writing 1 clears the bit. */
            val &= ~DSPWCTTCS_T0INT;
            ace_log( "     Timer 0 interrupt CLEARED\n");
            ace_irq_clear(info->adsp, IRQ_DWCT0, 0);
        } else {
            val |= (old_value & DSPWCTTCS_T0INT);
        }
        
        if (val & DSPWCTTCS_T1INT) {
            /* Timer 1 uses the same W1C acknowledge path. */
            val &= ~DSPWCTTCS_T1INT;
            ace_log( "     Timer 1 interrupt CLEARED\n");
            ace_irq_clear(info->adsp, IRQ_DWCT1, 0);
        } else {
            val |= (old_value & DSPWCTTCS_T1INT);
        }
        
        ace_region(addr) = val;
        break;

    case SHIM_TSCTRL:  /* Time Stamp Control */
        ace_log( "TSCTRL: write 0x%08x [HHTSE=%d ODTS=%d DMA=%u]\n",
                (uint32_t)val,
                !!(val & TSCTRL_HHTSE),
                !!(val & TSCTRL_ODTS),
                (unsigned)((val & TSCTRL_CDMAS_MASK) >> TSCTRL_CDMAS_SHIFT));

        if (val & TSCTRL_HHTSE) {
            ace_log( "     Hammock Harbor time stamp: Capture DSPWC and SoC ART\n");
            /* Would capture both clocks atomically for synchronization */
        }
        if (val & TSCTRL_ODTS) {
            ace_log( "     On-demand time stamp: Capture audio link position\n");
        }
        break;

    case SHIM_WDTCS:  /* Watch Dog Timer Control/Status */
        ace_log( "WDT: write 0x%08x [EN=%d TYPE=%s]\n",
                (uint32_t)val,
                !!(val & WDTCS_EN),
                (val & WDTCS_RSTTYPE) ? "RESET" : "INT");

        if ((val & WDTCS_EN) && !(old_value & WDTCS_EN)) {
            ace_log( "     Watch Dog Timer ENABLED\n");
        } else if (!(val & WDTCS_EN) && (old_value & WDTCS_EN)) {
            ace_log( "     Watch Dog Timer DISABLED\n");
        }

        /* W1C warning bit */
        if (val & WDTCS_RSTWARN) {
            /* WDT warning is also W1C so FW can acknowledge a first-timeout indication. */
            ace_region(addr) = old_value & ~WDTCS_RSTWARN;
            ace_log( "     WDT warning (1st timeout) CLEARED\n");
        }
        break;

    default:
        ace_log( "Timer: write unknown offset 0x%04x = 0x%08x\n",
                offset, (uint32_t)val);
        break;
    }
}

/* Memory region operations for timers/clocks */
const MemoryRegionOps ace_timer_ops = {
    .read = ace_timer_read,
    .write = ace_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

extern struct adsp_dev *g_adsp_dev;

void hmp_info_ace_timer(Monitor *mon, const QDict *qdict)
{
    struct adsp_dev *adsp = g_adsp_dev;
    if (!adsp || !adsp->shim) {
        monitor_printf(mon, "ace-timer: not available\n");
        return;
    }

    struct adsp_io_info *shim_info = adsp->shim;

    uint32_t t0cl = shim_info->region[(SHIM_DSPWCTT0CL) >> 2];
    uint32_t t0ch = shim_info->region[(SHIM_DSPWCTT0CH) >> 2];
    uint32_t t1cl = shim_info->region[(SHIM_DSPWCTT1CL) >> 2];
    uint32_t t1ch = shim_info->region[(SHIM_DSPWCTT1CH) >> 2];
    uint32_t tcs  = shim_info->region[(SHIM_DSPWCTTCS) >> 2];

    uint64_t t0_compare = ((uint64_t)t0ch << 32) | t0cl;
    uint64_t t1_compare = ((uint64_t)t1ch << 32) | t1cl;

    uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - adsp->timer[0].start;
    uint64_t now_ticks = (time_ns * adsp->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000) + ace_timer_ticks_adjustment;

    monitor_printf(mon, "ace-timer: DSP Wall Clock state\n");
    monitor_printf(mon, "  clk_kHz    : %llu (base %u)\n", (unsigned long long)adsp->timer[0].clk_kHz, adsp->clk_kHz);
    monitor_printf(mon, "  adjustment : %llu\n", (unsigned long long)ace_timer_ticks_adjustment);
    monitor_printf(mon, "  now_ticks  : %llu (0x%016llx)\n", (unsigned long long)now_ticks, (unsigned long long)now_ticks);
    monitor_printf(mon, "  Timer 0    : %s (INT %d) compare = %llu (0x%016llx)\n",
                   (tcs & DSPWCTTCS_T0EN) ? "ENABLED " : "DISABLED",
                   !!(tcs & DSPWCTTCS_T0INT),
                   (unsigned long long)t0_compare, (unsigned long long)t0_compare);

    if (tcs & DSPWCTTCS_T0EN) {
        int64_t delta = t0_compare - now_ticks;
        monitor_printf(mon, "    T0 delta : %ld ticks\n", delta);
    }

    monitor_printf(mon, "  Timer 1    : %s (INT %d) compare = %llu (0x%016llx)\n",
                   (tcs & DSPWCTTCS_T1EN) ? "ENABLED " : "DISABLED",
                   !!(tcs & DSPWCTTCS_T1INT),
                   (unsigned long long)t1_compare, (unsigned long long)t1_compare);

    if (tcs & DSPWCTTCS_T1EN) {
        int64_t delta = t1_compare - now_ticks;
        monitor_printf(mon, "    T1 delta : %ld ticks\n", delta);
    }
}

