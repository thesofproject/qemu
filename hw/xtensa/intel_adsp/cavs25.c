/* Core DSP support for Intel cAVS 2.5 (Tiger Lake / TGL) audio DSP.
 *
 * cAVS 2.5 ("cavs25") is the audio DSP found in Tiger Lake (TGL) and
 * Tiger Lake-H (TGL-H).  It predates the ACE generation and uses a
 * Tensilica LX6 / HiFi3 core (see target/xtensa/core-cavs25).  Its
 * register map and memory layout differ substantially from ACE:
 *
 *   - HP SRAM at 0xbe000000 (2944 KB), LP SRAM at 0xbe800000 (64 KB)
 *   - IMR (L3) at 0xb0000000 (16 MB) holds the signed firmware image
 *   - SHIM at 0x71f00 (cavs_shim layout, differs from ACE)
 *   - Host IPC doorbell at 0x71e00 (same protocol as the ACE HFIPC)
 *   - cAVS interrupt aggregators (cavs_intc0..3) at 0x78800..0x78830
 *
 * Map derived from:
 *   zephyr/dts/xtensa/intel/intel_adsp_cavs25.dtsi (and _tgph)
 *   zephyr/soc/intel/intel_adsp/cavs/include/cavs25/
 *
 * The firmware boot flow is ROM-less in this model (see adsp-common.c
 * cavs_boot path): the signed image is copied into IMR at offset
 * 0x30000 and execution starts at the BRNGUP module entry point
 * (0xb0038000 for TGL), which then loads BASEFW into HP SRAM.
 *
 * Copyright (C) 2026 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdint.h>

#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "system/memory.h"
#include "qemu/log.h"

#include "qemu/timer.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/ace.h"
#include "hw/ssi/ssp.h"
#include "hw/dma/dw-dma.h"
#include "hw/dma/hda-dma.h"
#include "common.h"
#include "hw/adsp/fw.h"
#include "ace-internal.h"
#include "ace-tlb.h"
#include "qobject/qdict.h"
#include "monitor/monitor.h"
#include "exec/cpu-common.h"

extern struct adsp_dev *g_adsp_dev;

/* -------------------------------------------------------------------------
 * cAVS 2.5 memory map
 * ------------------------------------------------------------------------- */

#define ADSP_CAVS25_DSP_HP_SRAM_BASE    0xbe000000u  /* 2944 KB */
#define ADSP_CAVS25_DSP_LP_SRAM_BASE    0xbe800000u  /* 64 KB   */
#define ADSP_CAVS25_DSP_IMR_BASE        0xb0000000u  /* 16 MB   */

/* Firmware load offset inside IMR (Load offset from the rimage manifest) */
#define ADSP_CAVS25_DSP_IMR_FW_OFFSET   0x30000u

/* ---- IP blocks (DSP address space) ---- */

/* DMIC */
#define ADSP_CAVS25_DSP_DMIC_BASE       0x00010000u
#define ADSP_CAVS25_DSP_DMIC_SIZE       0x8000u

/* IDC (inter-DSP-core doorbells) */
#define ADSP_CAVS25_DSP_IDC_BASE        0x00001200u
#define ADSP_CAVS25_DSP_IDC_SIZE        0x80u

/* L2 host SRAM address translation (TLB) */
#define ADSP_CAVS25_DSP_TLB_BASE        0x00003000u
#define ADSP_CAVS25_DSP_TLB_SIZE        0x1000u

/* ALH */
#define ADSP_CAVS25_DSP_ALH_BASE        0x00071000u
#define ADSP_CAVS25_DSP_ALH_SIZE        0x200u

/* DSP memory windows (4 windows x 8 B) */
#define ADSP_CAVS25_DSP_DMW_BASE        0x00071A00u
#define ADSP_CAVS25_DSP_DMW_STRIDE      0x08u
#define ADSP_CAVS25_DSP_DMW_COUNT       4u
#define ADSP_CAVS25_DSP_DMW_SIZE        (ADSP_CAVS25_DSP_DMW_STRIDE * ADSP_CAVS25_DSP_DMW_COUNT)

/* DSP RAM attribute / LDO control */
#define ADSP_CAVS25_DSP_DSPRA_BASE      0x00071A60u
#define ADSP_CAVS25_DSP_DSPRA_SIZE      0x20u

/* SSP base control */
#define ADSP_CAVS25_DSP_SSP_BASE_CTRL   0x00071C00u
#define ADSP_CAVS25_DSP_SSP_BASE_CTRL_SIZE 0x100u

/* L2 local memory control */
#define ADSP_CAVS25_DSP_L2LM_BASE       0x00071D00u
#define ADSP_CAVS25_DSP_L2LM_SIZE       0x20u

/* HP / LP SRAM bank power management */
#define ADSP_CAVS25_DSP_HSBPM_BASE      0x00071D10u
#define ADSP_CAVS25_DSP_HSBPM_SIZE      0x10u
#define ADSP_CAVS25_DSP_LSBPM_BASE      0x00071D50u
#define ADSP_CAVS25_DSP_LSBPM_SIZE      0x10u

/* Host IPC doorbell (cAVS 1.8+ layout; same protocol as ACE HFIPC) */
#define ADSP_CAVS25_DSP_IPC_BASE        0x00071E00u
#define ADSP_CAVS25_DSP_IPC_SIZE        0x30u

/* DMIC shim */
#define ADSP_CAVS25_DSP_DMIC_SHIM_BASE  0x00071E80u
#define ADSP_CAVS25_DSP_DMIC_SHIM_SIZE  0x80u

/* SHIM (global DSP control) */
#define ADSP_CAVS25_DSP_SHIM_BASE       0x00071F00u
#define ADSP_CAVS25_DSP_SHIM_SIZE       0x100u

/* HDA gateway streams (0x40 per stream) */
#define ADSP_CAVS25_DSP_GTW_STREAM_SIZE 0x40u
#define ADSP_CAVS25_DSP_GTW_LOUT_BASE   0x00072400u  /* link-out, 4 ch  */
#define ADSP_CAVS25_DSP_GTW_LIN_BASE    0x00072600u  /* link-in,  4 ch  */
#define ADSP_CAVS25_DSP_GTW_HOUT_BASE   0x00072800u  /* host-out, 9 ch  */
#define ADSP_CAVS25_DSP_GTW_HIN_BASE    0x00072C00u  /* host-in,  7 ch  */

/* SSP0..5 (each 0x200) + shared host registers */
#define ADSP_CAVS25_DSP_SSP_BASE(x)     (0x00077000u + (x) * 0x200u)
#define ADSP_CAVS25_DSP_SSP_SIZE        0x200u
#define ADSP_CAVS25_DSP_SSP_HOST_BASE   0x00078C00u
#define ADSP_CAVS25_DSP_SSP_HOST_SIZE   0x8u

/* cAVS interrupt aggregators (cavs_intc0..3) */
#define ADSP_CAVS25_DSP_INTC_BASE(x)    (0x00078800u + (x) * 0x10u)
#define ADSP_CAVS25_DSP_INTC_SIZE       0x10u

/* GPDMA controllers + their shims */
#define ADSP_CAVS25_DSP_GPDMA0_BASE     0x0007C000u
#define ADSP_CAVS25_DSP_GPDMA1_BASE     0x0007D000u
#define ADSP_CAVS25_DSP_GPDMA_SIZE      0x1000u
#define ADSP_CAVS25_DSP_GPDMA0_SHIM     0x00078400u
#define ADSP_CAVS25_DSP_GPDMA1_SHIM     0x00078500u
#define ADSP_CAVS25_DSP_GPDMA_SHIM_SIZE 0x100u

/* L1 cache control registers (L1_MEM_REG_BASE / L1CC_ADDR) */
#define ADSP_CAVS25_DSP_L1CC_BASE       0x9F080000u
#define ADSP_CAVS25_DSP_L1CC_SIZE       0x1000u

/* IRQ assignments (TGL core; see core-cavs25.c interrupt table) */
#define IRQ_NUM_SOFTWARE0    0
#define IRQ_NUM_TIMER1       1
#define IRQ_NUM_EXT_IA       6   /* cavs_intc0 (host IPC, IDC) */
#define IRQ_NUM_EXT_TIMER    1
#define IRQ_NUM_NMI          20

/* -------------------------------------------------------------------------
 * IO ops / helpers
 * ------------------------------------------------------------------------- */

static const MemoryRegionOps cavs_io_ops = {
    .read  = ace_shim_read,
    .write = ace_shim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void cavs_simple_io_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
}

/*
 * L1 cache control registers (L1_MEM_REG_BASE / L1CC_ADDR).
 *
 * The BRNGUP loader polls CxL1CCFG bits[31:16] ("all ways active") right
 * after enabling the L1 cache.  Report all bits set so the poll completes;
 * the actual L1 cache is already modelled transparently by QEMU.
 */
static uint64_t cavs_l1cc_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffffu;
}

static void cavs_l1cc_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
}

static const MemoryRegionOps cavs_l1cc_ops = {
    .read  = cavs_l1cc_read,
    .write = cavs_l1cc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* -------------------------------------------------------------------------
 * cAVS host IPC doorbell (0x71e00) + interrupt aggregator cavs_intc0 (0x78800)
 *
 * Host<->DSP IPC uses the cAVS register layout (struct intel_adsp_ipc,
 * zephyr cavs25/adsp_ipc_regs.h):
 *   tdr 0x00, tda 0x04, tdd 0x08, idr 0x10, ida 0x14, idd 0x18,
 *   cst 0x20, csr 0x24, ctl 0x28   (block @ 0x71e00, size 0x30)
 * This differs from ACE (which places tdd at 0x100 and idd at 0x180), so the
 * ACE IPC handler cannot be reused.
 *
 * A host->DSP doorbell (tdr.BUSY) raises child line 7 of cavs_intc0
 * (@0x78800), which aggregates to core external IRQ 6.  The aggregator uses
 * struct cavs_registers (zephyr intc_cavs.h):
 *   disable_il       0x00 (W: 1<<n masks line n)
 *   enable_il        0x04 (W: 1<<n unmasks line n)
 *   disable_state_il 0x08 (R: current mask)
 *   status_il        0x0c (R: pending & enabled lines)
 * ------------------------------------------------------------------------- */

#define CAVS_IPC_TDR    0x00
#define CAVS_IPC_TDA    0x04
#define CAVS_IPC_TDD    0x08
#define CAVS_IPC_IDR    0x10
#define CAVS_IPC_IDA    0x14
#define CAVS_IPC_IDD    0x18
#define CAVS_IPC_BUSY   (1u << 31)
#define CAVS_IPC_DONE   (1u << 31)

#define CAVS_INTC_DISABLE       0x00
#define CAVS_INTC_ENABLE        0x04
#define CAVS_INTC_MASK_STATE    0x08
#define CAVS_INTC_STATUS        0x0c

#define CAVS_INTC0_CORE_IRQ     6   /* cavs_intc0 -> core external IRQ 6 */
#define CAVS_IPC_INTC_LINE      7   /* host IPC -> cavs_intc0 child line 7 */

/* DMW base-address registers: window N base at 0x71a00 + N*8 (MWE = bit 0). */
#define CAVS_DMWBA_WIN1         0x00071a08u

static struct adsp_io_info *g_cavs_ipc_info;
static uint32_t cavs_intc0_pending;   /* raw pending child lines */
static uint32_t cavs_intc0_enabled;   /* unmasked child lines */

static void cavs_intc0_update(struct adsp_dev *adsp)
{
    int active = (cavs_intc0_pending & cavs_intc0_enabled) != 0;
    adsp_set_lvl1_irq(adsp, CAVS_INTC0_CORE_IRQ, active);
}

static void cavs_intc0_set_line(struct adsp_dev *adsp, int line, int active)
{
    if (active) {
        cavs_intc0_pending |= (1u << line);
    } else {
        cavs_intc0_pending &= ~(1u << line);
    }
    cavs_intc0_update(adsp);
}

static uint64_t cavs_intc_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (addr) {
    case CAVS_INTC_MASK_STATE:
        return ~cavs_intc0_enabled;
    case CAVS_INTC_STATUS:
        return cavs_intc0_pending & cavs_intc0_enabled;
    default:
        return 0;
    }
}

static void cavs_intc_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    struct adsp_io_info *info = opaque;

    switch (addr) {
    case CAVS_INTC_DISABLE:
        cavs_intc0_enabled &= ~(uint32_t)val;
        break;
    case CAVS_INTC_ENABLE:
        cavs_intc0_enabled |= (uint32_t)val;
        break;
    default:
        break;
    }
    cavs_intc0_update(info->adsp);
}

static const MemoryRegionOps cavs_intc_ops = {
    .read  = cavs_intc_read,
    .write = cavs_intc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void cavs_intc0_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    cavs_intc0_pending = 0;
    cavs_intc0_enabled = 0;
    ace_log("cavs_intc0: initialized at 0x%x\n", info->space->desc.base);
}

/* -------------------------------------------------------------------------
 * cAVS 2.5 DSP wall-clock timer (SHIM DSPWCTCS @ 0x28, DSPWCTxC @ 0x30/0x38)
 *
 * The cAVS DSPWCTCS control register has a different bit layout from ACE and
 * its comparator interrupt routes through the L2 aggregator (cavs_intc0), not
 * the ACE DINT, so the ACE SHIM timer path cannot be reused verbatim:
 *
 *   - arm timer x        : DSP_WCT_CS_TA(x) = BIT(x)      (RW)
 *   - timeout latch x    : DSP_WCT_CS_TT(x) = BIT(4 + x)  (W1C)
 *   - timer x interrupt  : CAVS_L2_DWCTx = BIT(22 + x) on cavs_intc0 -> IRQ 6
 *
 * (zephyr soc.h DSP_WCT_CS_TA/TT, cavs-idc.h CAVS_L2_DWCT0/1,
 *  drivers/timer/intel_adsp_timer.c set_compare()/compare_isr()).
 *
 * Everything else in the SHIM aperture (the DSPWC free-running counter, power,
 * clock and IPC-enable mirrors) is identical to ACE and is delegated to the
 * shared ace_shim_read()/ace_shim_write() handlers.  The register offsets
 * (DSPWC 0x20, DSPWCTCS 0x28, DSPWCT0C 0x30, DSPWCT1C 0x38) match ACE, so the
 * ace_set_time()/ace_rearm_ext_timer{0,1}() helpers are reused directly.
 * ------------------------------------------------------------------------- */

#define CAVS_DSPWCTCS_TA(x)     (1u << (x))        /* arm timer x (RW)      */
#define CAVS_DSPWCTCS_TT(x)     (1u << (4 + (x)))  /* timeout latch x (W1C) */
#define CAVS_DWCT_INTC_LINE(x)  (22 + (x))         /* cavs_intc0 child line */

static void cavs_ext_timer_cb(struct adsp_dev *adsp, struct adsp_io_info *info,
        int t)
{
    /* The arm bit auto-clears in hardware when the compare value is reached. */
    info->region[SHIM_DSPWCTTCS >> 2] &= ~CAVS_DSPWCTCS_TA(t);
    /* Latch the timeout status until FW acknowledges it with a W1C write. */
    info->region[SHIM_DSPWCTTCS >> 2] |= CAVS_DSPWCTCS_TT(t);
    /* Raise the comparator interrupt via the L2 aggregator (-> core IRQ 6). */
    cavs_intc0_set_line(adsp, CAVS_DWCT_INTC_LINE(t), 1);
}

static void cavs_ext_timer_cb0(void *opaque)
{
    struct adsp_io_info *info = opaque;
    cavs_ext_timer_cb(info->adsp, info, 0);
}

static void cavs_ext_timer_cb1(void *opaque)
{
    struct adsp_io_info *info = opaque;
    cavs_ext_timer_cb(info->adsp, info, 1);
}

static void cavs_shim_arm_timer(struct adsp_dev *adsp,
        struct adsp_io_info *info, int t, bool arm)
{
    if (arm) {
        if (t == 0) {
            ace_rearm_ext_timer0(adsp, info);
        } else {
            ace_rearm_ext_timer1(adsp, info);
        }
    } else {
        /* Disarming cancels any pending comparator deadline. */
        timer_del(adsp->timer[t].timer);
    }
}

static void cavs_shim_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;

    if ((addr & ~3u) == SHIM_DSPWCTTCS) {
        uint32_t cur = info->region[SHIM_DSPWCTTCS >> 2];
        uint32_t v = (uint32_t)val;
        int t;

        for (t = 0; t < 2; t++) {
            /* Timeout latch bits are W1C: writing 1 clears the latch and
             * drops the comparator interrupt line. */
            if (v & CAVS_DSPWCTCS_TT(t)) {
                cur &= ~CAVS_DSPWCTCS_TT(t);
                cavs_intc0_set_line(adsp, CAVS_DWCT_INTC_LINE(t), 0);
            }
            /* Arm bits are RW: track the level FW programmed. */
            if (v & CAVS_DSPWCTCS_TA(t)) {
                cur |= CAVS_DSPWCTCS_TA(t);
            } else {
                cur &= ~CAVS_DSPWCTCS_TA(t);
            }
        }

        info->region[SHIM_DSPWCTTCS >> 2] = cur;

        /* (Re)arm or cancel each backing QEMU timer to match its arm bit. */
        for (t = 0; t < 2; t++) {
            cavs_shim_arm_timer(adsp, info, t, v & CAVS_DSPWCTCS_TA(t));
        }
        return;
    }

    ace_shim_write(opaque, addr, val, size);
}

static const MemoryRegionOps cavs_shim_ops = {
    .read  = ace_shim_read,
    .write = cavs_shim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void cavs_shim_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    ace_shim_reset(info);
    adsp->shim = info;

    adsp->timer[0].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        &cavs_ext_timer_cb0, info);
    adsp->timer[0].clk_kHz = adsp->clk_kHz;
    adsp->timer[1].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        &cavs_ext_timer_cb1, info);
    adsp->timer[1].clk_kHz = adsp->clk_kHz;
    adsp->timer[0].start = adsp->timer[1].start =
        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static uint64_t cavs_ipc_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    return info->region[addr >> 2];
}

static void cavs_ipc_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    uint32_t v = (uint32_t)val;

    switch (addr) {
    case CAVS_IPC_TDR:
        /* Host->DSP doorbell.  DSP firmware clears BUSY (W1C) to accept the
         * message; drop the aggregator line once it does. */
        if (v & CAVS_IPC_BUSY) {
            info->region[CAVS_IPC_TDR >> 2] &= ~CAVS_IPC_BUSY;
            cavs_intc0_set_line(adsp, CAVS_IPC_INTC_LINE, 0);
        }
        break;

    case CAVS_IPC_IDR:
        /* DSP->host doorbell (e.g. FW_READY / IPC4 reply). */
        info->region[CAVS_IPC_IDR >> 2] = v;
        if (v & CAVS_IPC_BUSY) {
            adsp->last_ipc4_reply = v & ~CAVS_IPC_BUSY;
            qemu_log("IPC_REPLY: header=0x%08x ext=0x%08x\n",
                     adsp->last_ipc4_reply,
                     info->region[CAVS_IPC_IDD >> 2]);
            /*
             * Standalone auto-ACK: there is no host driver, so immediately
             * acknowledge the DSP->host message.  Clear the DSP's BUSY, set
             * host-side DONE and re-raise the IPC line so the firmware's ISR
             * runs its completion path and proceeds (past FW_READY).
             */
            info->region[CAVS_IPC_IDR >> 2] &= ~CAVS_IPC_BUSY;
            info->region[CAVS_IPC_IDA >> 2] |= CAVS_IPC_DONE;
            cavs_intc0_set_line(adsp, CAVS_IPC_INTC_LINE, 1);
        }
        break;

    case CAVS_IPC_IDA:
        /* Host completing DSP-initiated message: DONE is W1C on DSP side. */
        info->region[CAVS_IPC_IDA >> 2] = v & ~CAVS_IPC_DONE;
        cavs_intc0_set_line(adsp, CAVS_IPC_INTC_LINE, 0);
        break;

    default:
        info->region[addr >> 2] = v;
        break;
    }
}

static const MemoryRegionOps cavs_ipc_ops = {
    .read  = cavs_ipc_read,
    .write = cavs_ipc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void cavs_ipc_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    g_cavs_ipc_info = info;
    adsp->ipc = info;
    ace_log("cavs_ipc: initialized at 0x%x\n", info->space->desc.base);
}

/*
 * HMP monitor hooks (ace-ipc-tx / ace-ipc-rx) for the cAVS IPC layout.
 * Called from adsp_monitor_ace_ipc_{tx,rx} when the board is cavs_boot.
 */
void cavs_monitor_ipc_tx(Monitor *mon, const QDict *qdict)
{
    struct adsp_io_info *info = g_cavs_ipc_info;
    uint32_t primary = qdict_get_int(qdict, "primary");
    int has_extension = qdict_haskey(qdict, "extension");
    uint32_t extension = has_extension ? qdict_get_int(qdict, "extension") : 0;
    const char *payload_str = qdict_get_try_str(qdict, "payload_bytes");
    int has_sram_addr = qdict_haskey(qdict, "sram_addr");
    uint64_t sram_addr = has_sram_addr ? qdict_get_int(qdict, "sram_addr") : 0;

    if (!info) {
        monitor_printf(mon, "IPC block not initialized.\n");
        return;
    }

    if (info->region[CAVS_IPC_TDR >> 2] & CAVS_IPC_BUSY) {
        monitor_printf(mon,
            "Error: IPC doorbell is currently BUSY. DSP has not acked the "
            "previous message.\n");
        return;
    }

    if (payload_str && payload_str[0] != '\0') {
        if (!has_sram_addr || sram_addr == 0) {
            /* Auto-discover the Host->DSP inbox from DMWBA window 1. */
            uint32_t dmwba_win1 = 0;
            cpu_physical_memory_read(CAVS_DMWBA_WIN1, &dmwba_win1, 4);
            if (dmwba_win1 & 1) {  /* MWE set */
                sram_addr = dmwba_win1 & 0xFFFFF000u;
                monitor_printf(mon,
                    "Auto-discovered SRAM Inbox from DMWBA[1]: 0x%" PRIx64 "\n",
                    sram_addr);
            } else {
                monitor_printf(mon,
                    "Warning: payload provided but no SRAM inbox window "
                    "configured.\n");
            }
        }
        if (sram_addr != 0) {
            const char *p = payload_str;
            uint8_t buffer[4096];
            uint32_t byte_count = 0;
            while (*p && byte_count < sizeof(buffer)) {
                unsigned int b = 0;
                if (sscanf(p, "%x", &b) == 1) {
                    buffer[byte_count++] = (uint8_t)b;
                }
                p = strchr(p, ':');
                if (p) {
                    p++;
                } else {
                    break;
                }
            }
            if (byte_count > 0) {
                cpu_physical_memory_write(sram_addr, buffer, byte_count);
                monitor_printf(mon,
                    "Injected %u bytes to SRAM mailbox at 0x%" PRIx64 "\n",
                    byte_count, sram_addr);
            }
        }
    }

    if (has_extension) {
        info->region[CAVS_IPC_TDD >> 2] = extension;
    }

    primary |= CAVS_IPC_BUSY;
    info->region[CAVS_IPC_TDR >> 2] = primary;
    g_adsp_dev->last_ipc4_reply = 0;

    /* Raise host IPC line into cavs_intc0 -> core IRQ 6. */
    cavs_intc0_set_line(info->adsp, CAVS_IPC_INTC_LINE, 1);

    monitor_printf(mon,
        "Injected Host->DSP IPC via cavs DIPCTDR (Primary: 0x%08x, "
        "Extension: 0x%08x).\n", primary, extension);
}

void cavs_monitor_ipc_rx(Monitor *mon, const QDict *qdict)
{
    struct adsp_io_info *info = g_cavs_ipc_info;
    uint32_t tdr, idr, ida, reply;
    int busy, reply_pending;

    if (!info) {
        monitor_printf(mon, "IPC_RX: error=block_not_initialized\n");
        return;
    }

    tdr = info->region[CAVS_IPC_TDR >> 2];
    idr = info->region[CAVS_IPC_IDR >> 2];
    ida = info->region[CAVS_IPC_IDA >> 2];
    busy = !!(tdr & CAVS_IPC_BUSY);
    reply = g_adsp_dev->last_ipc4_reply;
    reply_pending = (reply != 0);

    monitor_printf(mon,
        "IPC_RX: busy=%d reply_pending=%d reply=0x%08x tdr=0x%08x idr=0x%08x "
        "ida=0x%08x\n", busy, reply_pending, reply, tdr, idr, ida);
}

/* -------------------------------------------------------------------------
 * cAVS 2.5 IO device table
 * ------------------------------------------------------------------------- */

/*
 * NOTE: this is an interim bring-up table.  The cAVS register blocks are
 * backed by plain RAM (cavs_simple_io_init + the default cavs_io_ops) sized
 * exactly to the cAVS layout.  The ACE "smart" handlers (ace30_hfipc_init,
 * ace30_idc_init, ace_dmw_init, ace30_dint_init, ...) are NOT reused here
 * because they assume the larger ACE region sizes/offsets and would write
 * past these smaller cAVS buffers.  cAVS-correct handlers (IPC doorbell,
 * INTC, TLB) can be added incrementally.
 */
static struct adsp_reg_space cavs25_io[] = {
    /*
     * Low DSP register window 0x1000-0xFFFF as one contiguous RAM-backed
     * block.  This absorbs the IDC inter-core doorbells (0x1200), the host
     * SRAM-window TLB (0x3000) and the power/clock/status registers the
     * IMR-resident bring-up loader walks across this range (e.g. 0x12d0,
     * 0x6200).  The firmware accesses these blocks past their nominal
     * device-tree sizes during early multi-core init, so they are modelled
     * as plain RAM up to the DMIC block at 0x10000.
     */
    { .name = "dsp-lo-regs", .init = cavs_simple_io_init,
        .desc = {.base = 0x00001000u, .size = 0xF000u}, },
    /* DMIC */
    { .name = "dmic", .init = cavs_simple_io_init,
        .desc = {.base = ADSP_CAVS25_DSP_DMIC_BASE,
                 .size = ADSP_CAVS25_DSP_DMIC_SIZE}, },
    /*
     * DSP register window 0x71000-0x71eff as RAM-backed blocks, with the
     * host IPC doorbell carved out as a live device at 0x71e00.  This covers
     * ALH, DSP memory windows, DSP RAM attributes, SSP base control, L2 local
     * memory, HP/LP SRAM bank power management and the DMIC shim; the bring-up
     * loader pokes registers across this range, so it is modelled as plain
     * RAM (per-block handlers can be carved out later).
     */
    { .name = "dsp-regs-a", .init = cavs_simple_io_init,
        .desc = {.base = 0x00071000u, .size = 0xE00u}, },
    /* Host IPC doorbell (cAVS layout: tdr/tda/tdd/idr/ida/idd/cst/csr/ctl) */
    { .name = "ipc", .init = cavs_ipc_init, .ops = &cavs_ipc_ops,
        .desc = {.base = ADSP_CAVS25_DSP_IPC_BASE,
                 .size = ADSP_CAVS25_DSP_IPC_SIZE}, },
    { .name = "dsp-regs-b", .init = cavs_simple_io_init,
        .desc = {.base = 0x00071e30u, .size = 0xD0u}, },
    /* SHIM (global DSP control); cAVS wall-clock timer layout differs from ACE */
    { .name = "shim", .init = &cavs_shim_init, .ops = &cavs_shim_ops,
        .desc = {.base = ADSP_CAVS25_DSP_SHIM_BASE,
                 .size = ADSP_CAVS25_DSP_SHIM_SIZE}, },
    /*
     * Peripheral register window 0x72000-0x7BFFF as RAM-backed blocks, with
     * the cavs_intc0 interrupt aggregator carved out as a live device at
     * 0x78800.  Absorbs the DMA gateways (0x72xxx), the SSP ports (0x77xxx),
     * the shared SSP host registers (0x78c00), cavs_intc1..3 (0x78810..0x78830)
     * and the GPDMA shims (0x78400/0x78500), modelled as plain RAM.
     */
    { .name = "dsp-periph-a", .init = cavs_simple_io_init,
        .desc = {.base = 0x00072000u, .size = 0x6800u}, },
    /* cavs_intc0 — interrupt aggregator (host IPC line 7 -> core IRQ 6) */
    { .name = "cavs_intc0", .init = cavs_intc0_init, .ops = &cavs_intc_ops,
        .desc = {.base = ADSP_CAVS25_DSP_INTC_BASE(0),
                 .size = ADSP_CAVS25_DSP_INTC_SIZE}, },
    { .name = "dsp-periph-b", .init = cavs_simple_io_init,
        .desc = {.base = 0x00078810u, .size = 0x37F0u}, },
    /* GPDMA controllers */
    { .name = "lpgpdma0", .init = cavs_simple_io_init, .irq = IRQ_LPGPDMA,
        .desc = {.base = ADSP_CAVS25_DSP_GPDMA0_BASE,
                 .size = ADSP_CAVS25_DSP_GPDMA_SIZE}, },
    { .name = "lpgpdma1", .init = cavs_simple_io_init, .irq = IRQ_LPGPDMA,
        .desc = {.base = ADSP_CAVS25_DSP_GPDMA1_BASE,
                 .size = ADSP_CAVS25_DSP_GPDMA_SIZE}, },
    /* L1 cache control registers (reports all ways active) */
    { .name = "l1cc", .init = cavs_simple_io_init, .ops = &cavs_l1cc_ops,
        .desc = {.base = ADSP_CAVS25_DSP_L1CC_BASE,
                 .size = ADSP_CAVS25_DSP_L1CC_SIZE}, },
};

/* -------------------------------------------------------------------------
 * Machine descriptor
 * ------------------------------------------------------------------------- */

static const struct adsp_desc cavs25_dsp_desc = {
    .name           = "cavs25",
    .ia_irq         = IRQ_NUM_EXT_IA,
    .ext_timer_irq  = IRQ_NUM_EXT_TIMER,

    .cavs_boot          = true,
    .cavs_fw_load_offset = ADSP_CAVS25_DSP_IMR_FW_OFFSET,

    .num_mem    = cavs25_mem_num,
    .mem_region = cavs25_mem,

    .num_io  = ARRAY_SIZE(cavs25_io),
    .io_dev  = cavs25_io,

    .mem_zones = {
        [SOF_FW_BLK_TYPE_IMR] = {
            .base = ADSP_CAVS25_DSP_IMR_BASE,
        },
        [SOF_FW_BLK_TYPE_SRAM] = {
            .base = ADSP_CAVS25_DSP_HP_SRAM_BASE,
        },
    },
};

/* -------------------------------------------------------------------------
 * Machine init
 * ------------------------------------------------------------------------- */

static void cavs25_adsp_init(MachineState *machine)
{
    /* cAVS 2.5 DSP initialisation.
     * Clock: 38.4 MHz system clock.
     * exec_addr=0: firmware boots via the cavs_boot path (load image to IMR,
     * jump to BRNGUP entry).
     */
    adsp_ace_init(&cavs25_dsp_desc, machine, &cavs_io_ops, 0,
                  ADSP_CAVS25_DSP_IMR_BASE, 38400);
}

static void xtensa_cavs25_machine_init(MachineClass *mc)
{
    mc->desc         = "cAVS2.5 HiFi3 (Tiger Lake)";
    mc->is_default   = false;
    mc->init         = cavs25_adsp_init;
    mc->max_cpus     = 4;
    mc->default_cpus = 1;
    mc->default_cpu_type = XTENSA_CPU_TYPE_NAME("cavs25");
    adsp_machine_class_add_options(mc);
}

DEFINE_MACHINE("adsp_cavs25", xtensa_cavs25_machine_init)
