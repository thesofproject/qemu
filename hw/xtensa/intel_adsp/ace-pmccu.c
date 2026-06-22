/* ACE DfPMCCU — DSP Power Management and Clock Control Unit
 * IP Region: PMCCU clock/power capability, status, and AON control registers.
 *
 * Copyright (C) 2026 Intel Corporation
 *
 * Base offset in DSPMEM: DfPMCCUCP.PTR = 0x071B00, size 0x100.
 *
 * The block combines two logical sub-domains at the same base address:
 *   DfPMCCU     (ULP domain)      — RO capability / status registers
 *   DfPMCCU_AON (ULP_AON domain)  — RW clock/power control registers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"

#define ace_region(raddr)    info->region[(raddr) >> 2]

/* Register offsets from DfPMCCU base */
#define DFPMCCU_PMCCAP      0x00
#define DFPMCCU_HROSCF      0x04
#define DFPMCCU_XOSCF       0x08
#define DFPMCCU_LROSCF      0x0C
#define DFPMCCU_SIOROSCF    0x10
#define DFPMCCU_HSIOROSCF   0x14
#define DFPMCCU_IPLLROSCF   0x18
#define DFPMCCU_IROSCV      0x1C
#define DFPMCCU_FBRCFD      0x20
#define DFPMCCU_APLLPTR     0x24
#define DFPMCCU_CLKCTL      0x78
#define DFPMCCU_CLKSTS      0x7C
#define DFPMCCU_INTCLKCTL   0x80
#define DFPMCCU_CROSTS      0x84
#define DFPMCCU_CRODIV      0x88
#define DFPMCCU_PWRCTL      0x90  /* 16-bit */
#define DFPMCCU_PWRSTS      0x92  /* 16-bit, RO */
#define DFPMCCU_PWRCTL2     0x94  /* 16-bit */
#define DFPMCCU_PWRSTS2     0x96  /* 16-bit, RO */
#define DFPMCCU_LPSDMAS0    0x98
#define DFPMCCU_LPSDMAS0E   0x9C
#define DFPMCCU_LPSDMAS1    0x9E
#define DFPMCCU_LDOCTL      0xA4

/* DfPMCCAP capability flags (reported to firmware) */
#define PMCCAP_ACC          (1u <<  0)   /* ACE clock connected */
#define PMCCAP_APLLC        (1u <<  1)   /* ACE PLL connected */
#define PMCCAP_HROSCC       (1u <<  2)   /* HP ROSC connected */
#define PMCCAP_SIOROSCC     (1u <<  3)   /* SIO ROSC connected */
#define PMCCAP_HSIOROSCC    (1u <<  4)   /* HSIO ROSC connected */
#define PMCCAP_WOVROSCC     (1u <<  5)   /* WoV ROSC connected */
#define PMCCAP_IPLLROSCC    (1u <<  7)   /* Integrated PLL/ROSC connected */
#define PMCCAP_DSPHPPG(n)   ((n) << 16) /* HP DSP PG domain count */
#define PMCCAP_CHASSIS_20   (0x1u << 14) /* Intel Chassis 2.0 compliance */

/* DfXOSCCF — XTAL oscillator 24 MHz: SCALE=01 (×1KHz), VAL=24000 */
#define XOSCF_24MHZ         ((0x1u << 30) | 24000u)

/* DfAPLLPTR — ACE PLL HIP pointer */
#define APLLPTR_DEFAULT     0x000A8000u

/* DfCLKSTS — all clocks running at boot */
#define CLKSTS_ALL_RUNNING  0x3FFF0002u   /* RHROSCCS..ACEPLLCS + OCS=01 (HP ROSC) */

/* DfPWRSTS — all power domains active at boot */
#define PWRSTS_ALL_UP       0x00FFu

/* DfCROSTS — calibration done */
#define CROSTS_CALDONE      (1u << 0)

void ace_pmccu_init(struct adsp_dev *adsp, MemoryRegion *parent,
                     struct adsp_io_info *info)
{
    /* Capability register: advertise HP ROSC, XOSC, SIO ROSC, IPLL,
     * ACE clock/PLL, Chassis 2.0, 4 HP DSP PG domains. */
    ace_region(DFPMCCU_PMCCAP) =
        PMCCAP_ACC | PMCCAP_APLLC | PMCCAP_HROSCC |
        PMCCAP_SIOROSCC | PMCCAP_IPLLROSCC |
        PMCCAP_CHASSIS_20 | PMCCAP_DSPHPPG(4);

    /* XTAL = 24 MHz (SCALE=×1KHz, VAL=24000) */
    ace_region(DFPMCCU_XOSCF) = XOSCF_24MHZ;

    /* Oscillator Calibration Registers (HROSCF, LROSCF, SIOROSCF, HSIOROSCF, IPLLROSCF)
     * These are initialized to typical/placeholder values. Real ACE hardware firmware
     * performs runtime calibration using OTP trim values. QEMU simulator accepts
     * firmware calibration writes but does not simulate actual timing changes.
     *
     * Frequencies initialized here for firmware reference:
     * - HP ROSC: 600 MHz (typical high-speed reference)
     * - LP ROSC: 32.768 KHz (typical low-power reference for RTC)
     * If firmware calibrates these registers, values are stored but not used for
     * simulated timer/clock behavior.
     */
    ace_region(DFPMCCU_HROSCF)  = (0x2u << 30) | 600u;  /* 600 MHz */
    ace_region(DFPMCCU_LROSCF)  = (0x0u << 30) | 32768u; /* 32.768 KHz */

    /* ACE PLL HIP pointer */
    /* FW uses this pointer to discover the PLL control block elsewhere in the map. */
    ace_region(DFPMCCU_APLLPTR) = APLLPTR_DEFAULT;

    /* Report all clocks running and OCS=HP ROSC */
    ace_region(DFPMCCU_CLKSTS) = CLKSTS_ALL_RUNNING;

    /* CRO calibration done */
    ace_region(DFPMCCU_CROSTS) = CROSTS_CALDONE;

    /* Power domains: all up */
    /* Report an already-powered reset state so memory and clocks are immediately usable. */
    /* Match both CTL (lower 16) and STS (upper 16) bounds securely */
    ace_region(DFPMCCU_PWRCTL)  = PWRSTS_ALL_UP | (PWRSTS_ALL_UP << 16);
    ace_region(DFPMCCU_PWRCTL2) = PWRSTS_ALL_UP | (PWRSTS_ALL_UP << 16);

    /* LDO defaults: HP SRAM LDO ON, ULP SRAM LDO ON */
    /* Keeping both rails enabled matches the memory availability advertised elsewhere. */
    ace_region(DFPMCCU_LDOCTL)  = (0x2u << 16) | (0x2u << 2) | 0x2u;

    qemu_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
}

static const char *pmccu_reg_name(hwaddr reg)
{
    switch (reg) {
    case DFPMCCU_PMCCAP:    return "DfPMCCAP";
    case DFPMCCU_HROSCF:    return "DfHROSCCF";
    case DFPMCCU_XOSCF:     return "DfXOSCCF";
    case DFPMCCU_LROSCF:    return "DfLROSCCF";
    case DFPMCCU_SIOROSCF:  return "DfSIOROSCCF";
    case DFPMCCU_HSIOROSCF: return "DfHSIOROSCCF";
    case DFPMCCU_IPLLROSCF: return "DfIPLLROSCCF";
    case DFPMCCU_IROSCV:    return "DfIROSCCV";
    case DFPMCCU_FBRCFD:    return "DfFBRCFD";
    case DFPMCCU_APLLPTR:   return "DfAPLLPTR";
    case DFPMCCU_CLKCTL:    return "DfCLKCTL";
    case DFPMCCU_CLKSTS:    return "DfCLKSTS";
    case DFPMCCU_INTCLKCTL: return "DfINTCLKCTL";
    case DFPMCCU_CROSTS:    return "DfCROSTS";
    case DFPMCCU_CRODIV:    return "DfCRODIV";
    case DFPMCCU_PWRCTL:    return "DfPWRCTL";
    case DFPMCCU_PWRSTS:    return "DfPWRSTS";
    case DFPMCCU_PWRCTL2:   return "DfPWRCTL2";
    case DFPMCCU_PWRSTS2:   return "DfPWRSTS2";
    case DFPMCCU_LPSDMAS0:  return "DfLPSDMAS0";
    case DFPMCCU_LPSDMAS0E: return "DfLPSDMAS0E";
    case DFPMCCU_LPSDMAS1:  return "DfLPSDMAS1";
    case DFPMCCU_LDOCTL:    return "DfLDOCTL";
    default:                return "?";
    }
}

/* Check if offset is a known mapped register in PMCCU region [0x00-0xFF] */
static bool pmccu_offset_is_mapped(hwaddr aligned)
{
    switch (aligned) {
    case DFPMCCU_PMCCAP:
    case DFPMCCU_HROSCF:
    case DFPMCCU_XOSCF:
    case DFPMCCU_LROSCF:
    case DFPMCCU_SIOROSCF:
    case DFPMCCU_HSIOROSCF:
    case DFPMCCU_IPLLROSCF:
    case DFPMCCU_IROSCV:
    case DFPMCCU_FBRCFD:
    case DFPMCCU_APLLPTR:
    case DFPMCCU_CLKCTL:
    case DFPMCCU_CLKSTS:
    case DFPMCCU_INTCLKCTL:
    case DFPMCCU_CROSTS:
    case DFPMCCU_CRODIV:
    case DFPMCCU_PWRCTL:
    case DFPMCCU_PWRCTL2:
    case DFPMCCU_LPSDMAS0:
    case DFPMCCU_LPSDMAS0E:
    case DFPMCCU_LPSDMAS1:
    case DFPMCCU_LDOCTL:
        return true;
    default:
        return false;
    }
}

static uint64_t ace_pmccu_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned = addr & ~3u;
    uint32_t word = ace_region(aligned);
    uint64_t val;

    switch (size) {
    case 1: val = (word >> ((addr & 3) * 8)) & 0xFFu;   break;
    case 2: val = (word >> ((addr & 3) * 8)) & 0xFFFFu; break;
    default: val = word; break;
    }

    qemu_log("PMCCU read:  %s (0x%lx) size=%u = 0x%lx\n",
             pmccu_reg_name(aligned), (unsigned long)addr,
             size, (unsigned long)val);
    
    /* Log unmapped register access for debugging */
    if (!pmccu_offset_is_mapped(aligned)) {
        qemu_log_mask(LOG_UNIMP,
                      "PMCCU: read from unmapped offset 0x%lx (size=%u)\n",
                      (unsigned long)addr, size);
    }
    
    return val;
}

static void ace_pmccu_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned = addr & ~3u;
    uint32_t old = ace_region(aligned);
    uint32_t word;
    uint32_t shift = (addr & 3) * 8;
    uint32_t mask  = (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;

    /* Protect read-only registers and fields */
    
    /* Block writes to read-only whole registers (32-bit aligned) */
    switch (aligned) {
    case DFPMCCU_PMCCAP:    /* Capability register is RO */
    case DFPMCCU_CLKSTS:    /* Clock status is RO hardware-driven */
    case DFPMCCU_CROSTS:    /* CRO calibration status is RO */
        qemu_log_mask(LOG_UNIMP,
                      "PMCCU write: %s (0x%lx) is read-only, ignoring val=0x%llx\n",
                      pmccu_reg_name(aligned), (unsigned long)addr,
                      (unsigned long long)val);
        return;
    default:
        break;
    }

    /* Block writes to RO status fields (upper 16 bits of PWRCTL/PWRCTL2).
     * PWRCTL (0x90): CTL=[15:0], STS=[31:16]
     * PWRCTL2 (0x94): CTL=[15:0], STS=[31:16]
     * When firmware writes to 0x92 (PWRSTS) or 0x96 (PWRSTS2), the aligned
     * address becomes 0x90 or 0x94 respectively, with shift=16 for upper field.
     */
    if (((aligned == DFPMCCU_PWRCTL || aligned == DFPMCCU_PWRCTL2) && shift == 16) &&
        (mask & 0xFFFF0000u)) {
        qemu_log_mask(LOG_UNIMP,
                      "PMCCU write: %s status field (0x%lx) is read-only, ignoring\n",
                      pmccu_reg_name(aligned), (unsigned long)addr);
        /* Apply mask to only lower 16 bits (CTL field), ignore upper 16 (STS) */
        mask &= 0xFFFFu;  /* Restrict to lower 16 bits only */
        if (mask == 0) {
            return;  /* Write was entirely to RO status field */
        }
    }

    word = (old & ~(mask << shift)) | (((uint32_t)val & mask) << shift);
    /* Most PMCCU registers are simple FW-owned state, so retain the programmed value first. */
    ace_region(aligned) = word;

    /* Mirror write-side effects: when firmware updates PWRCTL, reflect into PWRSTS.
     * QEMU simplified model: Power domain state transitions are instant.
     * Real hardware: Power sequencer gradually ramps up domains as supplies stabilize;
     * firmware observes state transition via PWRSTS (e.g., 0x00 → 0x05 → 0x0F).
     * Simulator: Setting PWRCTL=0x0F results in immediate PWRSTS=0x0F (no gradual ramp).
     * Note: Firmware should not depend on power state machine timing in simulation.
     */
    if (aligned == DFPMCCU_PWRCTL || aligned == DFPMCCU_PWRCTL2) {
        /* Domains wake when FW sets WPxPG bits; reflect purely matching the command dynamically. */
        /* STS is the upper 16 bits, CTL is the lower 16 bits. Mirror them precisely. */
        uint32_t ctl = word & 0xFFFFu;
        ace_region(aligned) = ctl | (ctl << 16);
    }

    /* When CLKCTL is written, update CLKSTS to match the OCS selection */
    if (aligned == DFPMCCU_CLKCTL) {
        /* OCS field (Oscillator Clock Select) selects active clock source:
         * 0 = LP ROSC (low-power, ~32 KHz)
         * 1 = HP ROSC (high-performance, ~600 MHz)
         * 2 = XTAL (external crystal, 24 MHz)
         * 3 = Reserved (invalid)
         */
        uint32_t ocs = word & 0x3u;
        if (ocs > 2) {
            qemu_log_mask(LOG_UNIMP,
                          "PMCCU: Invalid OCS value 0x%x (reserved); using previous\n", ocs);
        }
        /* Keep all-running flags; update OCS field from written value */
        /* CLKSTS reports the selected oscillator while preserving the static-ready bits. */
        ace_region(DFPMCCU_CLKSTS) =
            (CLKSTS_ALL_RUNNING & ~0x3u) | ocs;
    }

    qemu_log("PMCCU write: %s (0x%lx) size=%u = 0x%llx -> 0x%08x\n",
             pmccu_reg_name(aligned), (unsigned long)addr,
             size, (unsigned long long)val, word);
    
    /* Log unmapped register access for debugging */
    if (!pmccu_offset_is_mapped(aligned)) {
        qemu_log_mask(LOG_UNIMP,
                      "PMCCU: write to unmapped offset 0x%lx (size=%u, val=0x%llx)\n",
                      (unsigned long)addr, size, (unsigned long long)val);
    }
}

const MemoryRegionOps ace_pmccu_io_ops = {
    .read       = ace_pmccu_read,
    .write      = ace_pmccu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
