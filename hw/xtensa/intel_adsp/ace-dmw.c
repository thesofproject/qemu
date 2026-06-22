/* ACE DMW (DSP Memory Window) register virtualization
 * IP Region: DSPMEM DMW window descriptors mapping DSP views into L2/system ranges.
 *
 * Copyright (C) 2026 Intel Corporation
 * Base: DfDMWCP.PTR = 0x070200, stride 0x08 per window, up to DMWC=16 windows.
 *
 * Per-window registers:
 *   DMWXBA  +0x00  Base address (BA[23:12], ISEL[7:4], RSSEL[3:2], RO[1], MWE[0])
 *   DMWXLO  +0x04  Limit offset / curtain (QW-aligned)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"
#include "exec/cpu-common.h"

#define ace_region(raddr)    info->region[(raddr) >> 2]

/* Register offsets within each DMW instance (stride ADSP_ACE30_DSP_DMW_STRIDE) */
#define DMW_BA  0x00   /* DMWXBA — base address + config */
#define DMW_LO  0x04   /* DMWXLO — limit offset (curtain) */

/* DMWXBA field masks */
#define DMW_BA_BA_MASK   0xFFFFF000u   /* 4 KB aligned base address (full 32-bit) */
#define DMW_BA_ISEL_MASK 0x000000F0u   /* ISEL[7:4]  — initiator select */
#define DMW_BA_RSSEL_MASK 0x0000000Cu  /* RSSEL[3:2] — routing select */
#define DMW_BA_RO_BIT    0x00000002u   /* RO[1]      — read-only attribute */
#define DMW_BA_MWE_BIT   0x00000001u   /* MWE[0]     — Memory Window Enable */
#define DMW_BA_VALID_MASK (DMW_BA_BA_MASK | DMW_BA_ISEL_MASK | \
                           DMW_BA_RSSEL_MASK | DMW_BA_RO_BIT | DMW_BA_MWE_BIT)

/* DMWXLO is QW-aligned, so lower 3 bits are reserved/zero. */
#define DMW_LO_VALID_MASK 0xFFFFFFF8u

static struct adsp_io_info *g_dmw_info;

void ace_dmw_init(struct adsp_dev *adsp, MemoryRegion *parent,
                   struct adsp_io_info *info)
{
    unsigned int w;

    g_dmw_info = info;

    /* Reset all windows: disabled, base=0, limit=0 */
    for (w = 0; w < ADSP_ACE30_DSP_DMW_COUNT; w++) {
        /* Clearing BA drops MWE and any previous target selection for this window. */
        ace_region(w * ADSP_ACE30_DSP_DMW_STRIDE + DMW_BA) = 0;
        /* Clearing LO removes the programmed window curtain until FW sets one up. */
        ace_region(w * ADSP_ACE30_DSP_DMW_STRIDE + DMW_LO) = 0;
    }

    qemu_log("%s: initialized %u windows at 0x%x size=0x%x\n",
             info->name, ADSP_ACE30_DSP_DMW_COUNT,
             info->space->desc.base, info->space->desc.size);
}

static uint64_t ace_dmw_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    unsigned int win  = addr / ADSP_ACE30_DSP_DMW_STRIDE;
    hwaddr       reg  = addr % ADSP_ACE30_DSP_DMW_STRIDE;
    hwaddr       reg_aligned = reg & ~3u;
    uint32_t     byte_offset = addr & 3u;
    uint32_t     word;
    uint64_t     val;

    if (win >= ADSP_ACE30_DSP_DMW_COUNT) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW read: out-of-range window %u addr=0x%lx size=%u\n",
                      win, (unsigned long)addr, size);
        return 0;
    }

    if (size != 1 && size != 2 && size != 4) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] read: unsupported size %u at addr=0x%lx\n",
                      win, size, (unsigned long)addr);
        return 0;
    }

    if (byte_offset + size > 4) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] read: unaligned access addr=0x%lx size=%u crosses register\n",
                      win, (unsigned long)addr, size);
        return 0;
    }

    switch (reg_aligned) {
    case DMW_BA:
        word = ace_region(addr & ~3u);
        break;
    case DMW_LO:
        word = ace_region(addr & ~3u);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] read: unhandled reg 0x%lx\n",
                      win, (unsigned long)reg);
        return 0;
    }

    if (!ace_extract_subword_read(word, size, byte_offset, &val)) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] read: unsupported size %u at addr=0x%lx\n",
                      win, size, (unsigned long)addr);
        return 0;
    }

    qemu_log("DMW[%u] read:  %s addr=0x%lx size=%u val=0x%lx\n",
             win,
             (reg_aligned == DMW_BA) ? "DMWBA" :
             (reg_aligned == DMW_LO) ? "DMWLO" : "?",
             (unsigned long)addr, size, (unsigned long)val);
    return val;
}

static void ace_dmw_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    struct adsp_io_info *info = opaque;
    unsigned int win  = addr / ADSP_ACE30_DSP_DMW_STRIDE;
    hwaddr       reg  = addr % ADSP_ACE30_DSP_DMW_STRIDE;
    hwaddr       reg_aligned = reg & ~3u;
    uint32_t     byte_offset = addr & 3u;
    uint32_t     old_word;
    uint32_t     word;

    if (win >= ADSP_ACE30_DSP_DMW_COUNT) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW write: out-of-range window %u addr=0x%lx size=%u val=0x%llx\n",
                      win, (unsigned long)addr, size, (unsigned long long)val);
        return;
    }

    if (size != 1 && size != 2 && size != 4) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] write: unsupported size %u at addr=0x%lx\n",
                      win, size, (unsigned long)addr);
        return;
    }

    if (byte_offset + size > 4) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] write: unaligned access addr=0x%lx size=%u crosses register\n",
                      win, (unsigned long)addr, size);
        return;
    }

    old_word = ace_region(addr & ~3u);
    if (!ace_merge_subword_write(old_word, val, size, byte_offset, &word)) {
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] write: unsupported size %u at addr=0x%lx\n",
                      win, size, (unsigned long)addr);
        return;
    }

    switch (reg_aligned) {
    case DMW_BA:
        /* Keep only documented DMWBA fields; drop reserved bits on write. */
        word &= DMW_BA_VALID_MASK;
        ace_region(addr & ~3u) = word;
        qemu_log("DMW[%u] write: DMWBA addr=0x%lx size=%u val=0x%08x%s\n",
                 win, (unsigned long)addr, size, word,
                 (word & DMW_BA_MWE_BIT) ? " [ENABLED]" : " [DISABLED]");
        /* Propagate the window base address to the host-facing DTFCXD64 register
         * so the IPC TX handler can discover the SRAM inbox address.  In real
         * hardware the host reads DTFCXD64 to find where the firmware mapped
         * each window; QEMU keeps the two register banks separate so we mirror
         * the write here.  Window N maps to ADSP_ACE30_DSP_HOST_WIN_BASE(N). */
        if (win < 4) {
            uint32_t base = word & ~(DMW_BA_MWE_BIT | DMW_BA_RO_BIT | DMW_BA_RSSEL_MASK | DMW_BA_ISEL_MASK);
            uint64_t dtfcxd64 = (uint64_t)base;
            cpu_physical_memory_write(ADSP_ACE30_DSP_HOST_WIN_BASE(win) + DTFCXD64_OFFSET,
                                      &dtfcxd64, sizeof(dtfcxd64));
        }
        break;
    case DMW_LO:
        /* DMWLO is QW-aligned; enforce lower 3 bits as zero. */
        word &= DMW_LO_VALID_MASK;
        ace_region(addr & ~3u) = word;
        qemu_log("DMW[%u] write: DMWLO addr=0x%lx size=%u val=0x%08x\n",
                 win, (unsigned long)addr, size, word);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "DMW[%u] write: unhandled reg 0x%lx val=0x%llx\n",
                      win, (unsigned long)reg, (unsigned long long)val);
        break;
    }
}

const MemoryRegionOps ace_dmw_io_ops = {
    .read       = ace_dmw_read,
    .write      = ace_dmw_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void hmp_info_ace_win(Monitor *mon, const QDict *qdict)
{
    unsigned int w;
    int enabled = 0;

    (void)qdict;

    monitor_printf(mon, "Intel ADSP ACE DMW Window Configuration:\n");
    if (!g_dmw_info) {
        monitor_printf(mon, "  [DMW device not initialized on this platform]\n");
        return;
    }

    monitor_printf(mon,
                   "  %-4s %-10s %-10s %-8s %-8s %-4s %-4s %-3s\n",
                   "WIN", "DMWBA", "DMWLO", "BASE", "LIMIT", "ISEL",
                   "RSS", "FLG");

    for (w = 0; w < ADSP_ACE30_DSP_DMW_COUNT; w++) {
        hwaddr off = w * ADSP_ACE30_DSP_DMW_STRIDE;
        uint32_t ba = g_dmw_info->region[(off + DMW_BA) >> 2];
        uint32_t lo = g_dmw_info->region[(off + DMW_LO) >> 2];
        uint32_t base = ba & DMW_BA_BA_MASK;
        uint32_t limit = lo & DMW_LO_VALID_MASK;
        uint32_t isel = (ba & DMW_BA_ISEL_MASK) >> 4;
        uint32_t rssel = (ba & DMW_BA_RSSEL_MASK) >> 2;
        char flags[4] = "---";

        if (ba & DMW_BA_MWE_BIT) {
            flags[0] = 'E';
            enabled++;
        }
        if (ba & DMW_BA_RO_BIT) {
            flags[1] = 'R';
        }

        monitor_printf(mon,
                       "  %-4u 0x%08x 0x%08x 0x%06x 0x%08x %-4u %-4u %s\n",
                       w, ba, lo, base, limit, isel, rssel, flags);
    }

    monitor_printf(mon, "  Summary: %d/%d windows enabled\n",
                   enabled, ADSP_ACE30_DSP_DMW_COUNT);
}
