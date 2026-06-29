/* Core DSP support for Intel ACE 1.5 (Meteor Lake / MTL/MTM) audio DSP.
 *
 * ACE 1.5 is the first generation of the Audio DSP Engine (ACE) IP
 * introduced in Meteor Lake (MTL).  It shares the same high-level
 * architecture as ACE 3.0 (PTL/WCL) but has a number of differences
 * in peripheral block addresses and core count.
 *
 * Key differences from ACE 3.0
 * =============================
 * - 3 DSP cores (LX7 HiFi5) vs 5 in ACE 3.0
 * - HP SRAM 2816 KB (not 8 MB)
 * - IDC aggregate block at 0x70400 (ACE 3.0: 0x92000+)
 * - Per-core DINT block at 0x78840 (ACE 3.0: 0x91000+)
 * - No DFFUSA fuse block
 * - No HFINTIP IP block (0x94000)
 * - HSBCAP/LSBPM blocks at 0x71D00/0x71D80
 * - imria1 (IMR attribute) at 0x162080 replaces the ACE 3.0 HFIMR
 * - Comm Widget at 0x71C00 (ACE 3.0 has HP GPDMA shim there)
 *
 * Addresses that are identical to ACE 3.0:
 *   DMW (0x70200), DFPMCCH (0x71AC0), DFPMCCU (0x71B00), SHIM (0x71F00),
 *   Timer (0x72000), HDA streams (0x72400…), HFIPC (0x73000),
 *   ACE INTC (0x7AC00), LP GPDMA (0x7C000…), DSPCS (0x178D00),
 *   HSBPM (0x17A800), TLB (0x17E000).
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

/* -------------------------------------------------------------------------
 * ACE 1.5 memory map
 * Derived from:
 *   zephyr/dts/xtensa/intel/intel_adsp_ace15_mtpm.dtsi
 *   zephyr/soc/intel/intel_adsp/ace/include/ace15_mtpm/
 * ------------------------------------------------------------------------- */

/* SRAM */
#define ADSP_ACE15_DSP_LP_SRAM_BASE     0xa0000000u  /* 64 KB  */
#define ADSP_ACE15_DSP_LP_SRAM_SIZE     0x10000u
#define ADSP_ACE15_DSP_HP_SRAM_BASE     0xa0020000u  /* 2816 KB */
#define ADSP_ACE15_DSP_HP_SRAM_SIZE     (2816u * 1024u)
#define ADSP_ACE15_DSP_IMR_BASE         0xa1000000u  /* 16 MB */
#define ADSP_ACE15_DSP_IMR_SIZE         0x1000000u

/* Manifest offset inside IMR (same as ACE 3.0) */
#define ADSP_ACE15_DSP_IMR_MAN_OFFSET   0x42000u

/* Low capability/status blocks (SHIM registers including DFIDCPP) */
#define ADSP_ACE15_DSP_DFCAPSTS_BLOCK_BASE 0x00002000u
#define ADSP_ACE15_DSP_DFCAPSTS_BLOCK_SIZE 0x1000u
#define ADSP_ACE15_DSP_ADCIP_BLOCK_BASE    0x00003000u
#define ADSP_ACE15_DSP_ADCIP_BLOCK_SIZE    0x800u
#define ADSP_ACE15_DSP_ADCS_BLOCK_BASE     0x00003800u
#define ADSP_ACE15_DSP_ADCS_BLOCK_SIZE     0x800u

/* ---- IP blocks (DSP address space) ---- */

/* DMIC */
#define ADSP_ACE15_DSP_DMIC_BASE        0x00010000u
#define ADSP_ACE15_DSP_DMIC_SIZE        0x8000u

/* ALH */
#define ADSP_ACE15_DSP_ALH_BASE         0x00024400u
#define ADSP_ACE15_DSP_ALH_SIZE         0x200u

/* SSP */
#define ADSP_ACE15_DSP_SSP_BASE_CTRL    0x00028800u
#define ADSP_ACE15_DSP_SSP_BASE_CTRL_SIZE 0x1000u
#define ADSP_ACE15_DSP_SSP_BASE(x)      (0x00028000u + (x) * 0x1000u)
#define ADSP_ACE15_DSP_SSP_SIZE         0x1000u
#define ADSP_ACE15_DSP_SSP_HOST_BASE    0x00079C00u
#define ADSP_ACE15_DSP_SSP_HOST_SIZE    0x200u

/* IDC — single aggregate block (ACE 1.5 has IDC at 0x70400, not 0x92000+) */
#define ADSP_ACE15_DSP_IDC_BASE         0x00070400u
#define ADSP_ACE15_DSP_IDC_SIZE         0x400u

/* DMW — memory windows (identical to ACE 3.0: 4 windows × 8 B) */
#define ADSP_ACE15_DSP_DMW_BASE         0x00070200u
#define ADSP_ACE15_DSP_DMW_STRIDE       0x08u
#define ADSP_ACE15_DSP_DMW_COUNT        4u
#define ADSP_ACE15_DSP_DMW_SIZE         (ADSP_ACE15_DSP_DMW_STRIDE * ADSP_ACE15_DSP_DMW_COUNT)

/* DFPMCCH (HST domain power/clock) */
#define ADSP_ACE15_DSP_DFPMCCH_BASE     0x00071AC0u
#define ADSP_ACE15_DSP_DFPMCCH_SIZE     0x40u

/* DFPMCCU (ULP domain power/clock) */
#define ADSP_ACE15_DSP_DFPMCCU_BASE     0x00071B00u
#define ADSP_ACE15_DSP_DFPMCCU_SIZE     0x100u

/* Communication Widget (at 0x71C00; ACE 3.0 uses this for HP GPDMA shim) */
#define ADSP_ACE15_DSP_COMM_WIDGET_BASE 0x00071C00u
#define ADSP_ACE15_DSP_COMM_WIDGET_SIZE 0x100u

/* HSBCAP — HP SRAM bank capability (ACE 1.5 specific) */
#define ADSP_ACE15_DSP_HSBCAP_BASE      0x00071D00u
#define ADSP_ACE15_DSP_HSBCAP_SIZE      0x80u

/* LSBPM — LP SRAM bank power management (ACE 1.5 specific) */
#define ADSP_ACE15_DSP_LSBPM_BASE       0x00071D80u
#define ADSP_ACE15_DSP_LSBPM_SIZE       0x80u

/* Host-window mirror registers (DTFCXD64 etc., same base as ACE 3.0) */
#define ADSP_ACE15_DSP_HOST_WIN_BASE(x) (0x00071A00u + (x) * 0x20u)
#define ADSP_ACE15_DSP_HOST_WIN_SIZE    0x80u        /* 4 windows × 0x20 */

/* SHIM (global DSP control) */
#define ADSP_ACE15_DSP_SHIM_BASE        0x00071F00u
#define ADSP_ACE15_DSP_SHIM_SIZE        0x100u

/* HFTTS (timer/timestamp block) */
#define ADSP_ACE15_DSP_HFTTS_BASE       0x00072000u
#define ADSP_ACE15_DSP_HFTTS_SIZE       0x200u

/* HDA gateway streams (addresses identical to ACE 3.0) */
#define ADSP_ACE15_DSP_GTW_LOUT_BASE(x) (0x00072400u + (x) * 0x40u)  /* link-out  */
#define ADSP_ACE15_DSP_GTW_LIN_BASE(x)  (0x00072600u + (x) * 0x40u)  /* link-in   */
#define ADSP_ACE15_DSP_GTW_HOUT_BASE(x) (0x00072800u + (x) * 0x40u)  /* host-out  */
#define ADSP_ACE15_DSP_GTW_HIN_BASE(x)  (0x00072C00u + (x) * 0x40u)  /* host-in   */
#define ADSP_ACE15_DSP_GTW_STREAM_SIZE  0x40u

/* HFIPC (host IPC block — same as ACE 3.0) */
#define ADSP_ACE15_DSP_HFIPC_BASE       0x00073000u
#define ADSP_ACE15_DSP_HFIPC_SIZE       0x1000u

/* L2 cache control */
#define ADSP_ACE15_DSP_L2C_BASE         0x00074000u
#define ADSP_ACE15_DSP_L2C_SIZE         0x1000u

/* SOCCI (MN node) */
#define ADSP_ACE15_DSP_SOCCI_BASE       0x00078C00u
#define ADSP_ACE15_DSP_SOCCI_SIZE       0x200u

/* Per-core DINT interrupt enable/status (ACE 1.5: 0x78840 / DXHIPCIE_REG) */
#define ADSP_ACE15_DSP_DINT_BASE        0x00078840u
#define ADSP_ACE15_DSP_DINT_SIZE        0x400u   /* 3 cores × struct ace_dint */

/* ACE INTC (DesignWare interrupt controller, same address as ACE 3.0) */
#define ADSP_ACE15_DSP_ACE_INTC_BASE    0x0007AC00u
#define ADSP_ACE15_DSP_ACE_INTC_SIZE    0x1000u

/* LP GPDMA (0x7C000, 0x7D000, 0x7E000 — same as ACE 3.0) */
#define ADSP_ACE15_DSP_LP_GPDMA_BASE(x) (0x0007C000u + (x) * 0x1000u)
#define ADSP_ACE15_DSP_LP_GPDMA_SIZE    0x1000u

/* imria1 — IMR attribute register (replaces HFIMR in ACE 3.0) */
#define ADSP_ACE15_DSP_IMRIA1_BASE      0x00162080u
#define ADSP_ACE15_DSP_IMRIA1_SIZE      0x80u

/* DSPCS — DSP Core Shim (same address as ACE 3.0) */
#define ADSP_ACE15_DSP_DSPCS_BASE       0x000178D00u
#define ADSP_ACE15_DSP_DSPCS_SIZE       0x1000u

/* HSBPM — HP SRAM bank power management (same address as ACE 3.0 DFL2HSBPM) */
#define ADSP_ACE15_DSP_HSBPM_BASE       0x0017A800u
#define ADSP_ACE15_DSP_HSBPM_SIZE       0x1000u

/* TLB (L2 host SRAM TLB, same as ACE 3.0) */
#define ADSP_ACE15_DSP_L2HSTLB_BASE     0x00017E000u
#define ADSP_ACE15_DSP_L2HSTLB_SIZE     0x1000u

/* IRQ assignments (from adsp_interrupt.h ACE 1.5) */
#define IRQ_NUM_SOFTWARE0    0
#define IRQ_NUM_TIMER1       1
#define IRQ_NUM_SOFTWARE1    2
#define IRQ_NUM_SOFTWARE2    3
#define IRQ_NUM_TIMER2       5
#define IRQ_NUM_SOFTWARE3    6
#define IRQ_NUM_TIMER3       7
#define IRQ_NUM_EXT_IA       10
#define IRQ_NUM_EXT_PMC      11
#define IRQ_NUM_EXT_TIMER    15
#define IRQ_NUM_NMI          20

/* -------------------------------------------------------------------------
 * IO ops / helpers shared with ACE 3.0 code
 * ------------------------------------------------------------------------- */

static const MemoryRegionOps ace_io_ops = {
    .read  = ace_shim_read,
    .write = ace_shim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ace_simple_io_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
}

/* -------------------------------------------------------------------------
 * ACE 1.5 IO device table
 * ------------------------------------------------------------------------- */

static struct adsp_reg_space ace_15_io[] = {
    /* Low capability/status window (includes DFIDCPP). */
    { .name = "dfcapsts-low", .init = ace_dfcapsts_block_init,
        .ops = &ace_dfcapsts_block_ops,
        .desc = {.base = ADSP_ACE15_DSP_DFCAPSTS_BLOCK_BASE,
                 .size = ADSP_ACE15_DSP_DFCAPSTS_BLOCK_SIZE}, },
    { .name = "adcip-low", .init = ace_adcip_block_init,
        .ops = &ace_adcip_block_ops,
        .desc = {.base = ADSP_ACE15_DSP_ADCIP_BLOCK_BASE,
                 .size = ADSP_ACE15_DSP_ADCIP_BLOCK_SIZE}, },
    { .name = "adcs-low", .init = ace_adcs_block_init,
        .ops = &ace_adcs_block_ops,
        .desc = {.base = ADSP_ACE15_DSP_ADCS_BLOCK_BASE,
                 .size = ADSP_ACE15_DSP_ADCS_BLOCK_SIZE}, },
    /* DMIC */
    { .name = "dmic", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_DMIC_BASE,
                 .size = ADSP_ACE15_DSP_DMIC_SIZE}, },
    /* ALH */
    { .name = "alh", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_ALH_BASE,
                 .size = ADSP_ACE15_DSP_ALH_SIZE}, },
    /* SSP base ctrl */
    { .name = "ssp-base-ctrl", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_SSP_BASE_CTRL,
                 .size = ADSP_ACE15_DSP_SSP_BASE_CTRL_SIZE}, },
    /* SSP0 */
    { .name = "ssp0", .init = ace_simple_io_init, .irq = IRQ_SSP0,
        .desc = {.base = ADSP_ACE15_DSP_SSP_BASE(0),
                 .size = ADSP_ACE15_DSP_SSP_SIZE}, },
    /* SSP1 */
    { .name = "ssp1", .init = ace_simple_io_init, .irq = IRQ_SSP1,
        .desc = {.base = ADSP_ACE15_DSP_SSP_BASE(1),
                 .size = ADSP_ACE15_DSP_SSP_SIZE}, },
    /* SSP2 */
    { .name = "ssp2", .init = ace_simple_io_init, .irq = IRQ_SSP2,
        .desc = {.base = ADSP_ACE15_DSP_SSP_BASE(2),
                 .size = ADSP_ACE15_DSP_SSP_SIZE}, },
    /* SSP shared host registers */
    { .name = "ssp-host", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_SSP_HOST_BASE,
                 .size = ADSP_ACE15_DSP_SSP_HOST_SIZE}, },
    /* IDC — aggregate block at 0x70400 (ACE 1.5 specific) */
    { .name = "idc", .init = ace30_idc_init, .ops = &ace_idc_ops,
        .desc = {.base = ADSP_ACE15_DSP_IDC_BASE,
                 .size = ADSP_ACE15_DSP_IDC_SIZE}, },
    /* DMW — DSP memory window configuration */
    { .name = "dmw", .init = ace_dmw_init, .ops = &ace_dmw_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_DMW_BASE,
                 .size = ADSP_ACE15_DSP_DMW_SIZE}, },
    /* Host-window mirror registers (DTFCXD64 etc.) */
    { .name = "hostwin", .init = ace_hostwin_init, .ops = &ace_hostwin_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_HOST_WIN_BASE(0),
                 .size = ADSP_ACE15_DSP_HOST_WIN_SIZE}, },
    /* DFPMCCH (HST power/clock) */
    { .name = "dfpmcch", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_DFPMCCH_BASE,
                 .size = ADSP_ACE15_DSP_DFPMCCH_SIZE}, },
    /* DFPMCCU (ULP power/clock) */
    { .name = "pmccu", .init = ace_pmccu_init, .ops = &ace_pmccu_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_DFPMCCU_BASE,
                 .size = ADSP_ACE15_DSP_DFPMCCU_SIZE}, },
    /* Comm Widget */
    { .name = "comm-widget", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_COMM_WIDGET_BASE,
                 .size = ADSP_ACE15_DSP_COMM_WIDGET_SIZE}, },
    /* HSBCAP (HP SRAM bank capability) */
    { .name = "hsbcap", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_HSBCAP_BASE,
                 .size = ADSP_ACE15_DSP_HSBCAP_SIZE}, },
    /* LSBPM (LP SRAM bank power mgmt) */
    { .name = "lsbpm", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_LSBPM_BASE,
                 .size = ADSP_ACE15_DSP_LSBPM_SIZE}, },
    /* SHIM (global DSP control) */
    { .name = "shim",
        .init = &adsp_ace_shim_init, .ops = &ace_shim_ops,
        .desc = {.base = ADSP_ACE15_DSP_SHIM_BASE,
                 .size = ADSP_ACE15_DSP_SHIM_SIZE}, },
    /* HFTTS (timer / timestamp) */
    { .name = "timer",
        .init = ace_timer_init, .ops = &ace_timer_ops,
        .desc = {.base = ADSP_ACE15_DSP_HFTTS_BASE,
                 .size = ADSP_ACE15_DSP_HFTTS_SIZE}, },
    /* Gateway: link-out streams (hda_link_out in ACE 1.5 DTS) */
    { .name = "gtw-lout", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE15_DSP_GTW_LOUT_BASE(0),
                 .size = ADSP_ACE15_DSP_GTW_STREAM_SIZE * 9}, },
    /* Gateway: link-in streams (hda_link_in) */
    { .name = "gtw-lin", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE15_DSP_GTW_LIN_BASE(0),
                 .size = ADSP_ACE15_DSP_GTW_STREAM_SIZE * 9}, },
    /* Gateway: host-out streams (hda_host_out) */
    { .name = "gtw-hout", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE15_DSP_GTW_HOUT_BASE(0),
                 .size = ADSP_ACE15_DSP_GTW_STREAM_SIZE * 9}, },
    /* Gateway: host-in streams (hda_host_in) — note: 0x72C00 in ACE 1.5 */
    { .name = "gtw-hin", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE15_DSP_GTW_HIN_BASE(0),
                 .size = ADSP_ACE15_DSP_GTW_STREAM_SIZE * 10}, },
    /* HFIPC (host IPC) */
    { .name = "hfipc0", .init = ace30_hfipc_init, .ops = &ace30_hfipc_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_HFIPC_BASE,
                 .size = ADSP_ACE15_DSP_HFIPC_SIZE}, },
    /* L2 cache control */
    { .name = "l2c", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_L2C_BASE,
                 .size = ADSP_ACE15_DSP_L2C_SIZE}, },
    /* SOCCI (MN node / network control) */
    { .name = "mn", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_SOCCI_BASE,
                 .size = ADSP_ACE15_DSP_SOCCI_SIZE}, },
    /* DINT — per-core interrupt enable/status (ACE 1.5: 0x78840) */
    { .name = "dint", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_DINT_BASE,
                 .size = ADSP_ACE15_DSP_DINT_SIZE}, },
    /* ACE INTC (DesignWare interrupt controller) */
    { .name = "irq", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_ACE_INTC_BASE,
                 .size = ADSP_ACE15_DSP_ACE_INTC_SIZE}, },
    /* LP GPDMA controllers */
    { .name = "lpgpdma0", .init = ace_simple_io_init, .irq = IRQ_LPGPDMA,
        .desc = {.base = ADSP_ACE15_DSP_LP_GPDMA_BASE(0),
                 .size = ADSP_ACE15_DSP_LP_GPDMA_SIZE}, },
    { .name = "lpgpdma1", .init = ace_simple_io_init, .irq = IRQ_LPGPDMA,
        .desc = {.base = ADSP_ACE15_DSP_LP_GPDMA_BASE(1),
                 .size = ADSP_ACE15_DSP_LP_GPDMA_SIZE}, },
    { .name = "lpgpdma2", .init = ace_simple_io_init, .irq = IRQ_LPGPDMA,
        .desc = {.base = ADSP_ACE15_DSP_LP_GPDMA_BASE(2),
                 .size = ADSP_ACE15_DSP_LP_GPDMA_SIZE}, },
    /* imria1 (IMR attribute region — replaces HFIMR in ACE 3.0) */
    { .name = "imria1", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE15_DSP_IMRIA1_BASE,
                 .size = ADSP_ACE15_DSP_IMRIA1_SIZE}, },
    /* DSPCS (DSP Core Shim — boot/reset/power per core) */
    { .name = "dspcs", .init = ace30_dspcs_init, .ops = &ace30_dspcs_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_DSPCS_BASE,
                 .size = ADSP_ACE15_DSP_DSPCS_SIZE}, },
    /* HSBPM (HP SRAM bank power management) */
    { .name = "dfl2hsbpm", .init = ace30_dfl2hsbpm_init,
        .ops = &ace30_dfl2hsbpm_io_ops,
        .desc = {.base = ADSP_ACE15_DSP_HSBPM_BASE,
                 .size = ADSP_ACE15_DSP_HSBPM_SIZE}, },
    /* TLB (L2 host SRAM address translation) */
    { .name = "l2hstlb", .init = ace_tlb_mmio_init, .ops = &ace_tlb_ops,
        .desc = {.base = ADSP_ACE15_DSP_L2HSTLB_BASE,
                 .size = ADSP_ACE15_DSP_L2HSTLB_SIZE}, },
};

/* -------------------------------------------------------------------------
 * Machine descriptor
 * ------------------------------------------------------------------------- */

static const struct adsp_desc ace_ace15_dsp_desc = {
    .name           = "ace15",
    .ia_irq         = IRQ_NUM_EXT_IA,
    .ext_timer_irq  = IRQ_NUM_EXT_TIMER,

    .imr_boot_ldr_offset = ADSP_ACE15_DSP_IMR_MAN_OFFSET,

    .num_mem    = ace_ace15_mem_num,
    .mem_region = ace_ace15_mem,

    .num_io  = ARRAY_SIZE(ace_15_io),
    .io_dev  = ace_15_io,

    .mem_zones = {
        [SOF_FW_BLK_TYPE_IMR] = {
            .base = ADSP_ACE15_DSP_IMR_BASE,
        },
        [SOF_FW_BLK_TYPE_SRAM] = {
            .base = ADSP_ACE15_DSP_HP_SRAM_BASE,
        },
    },
};

/* -------------------------------------------------------------------------
 * Machine init
 * ------------------------------------------------------------------------- */

static void ace15_adsp_init(MachineState *machine)
{
    /* ACE 1.5 DSP initialisation.
     * Clock: 38.4 MHz system clock (ADSP_CLKCTL_OSC at 38400 kHz).
     * exec_addr=0: firmware is loaded to IMR and executed from there.
     */
    adsp_ace_init(&ace_ace15_dsp_desc, machine, &ace_io_ops, 0,
                  ADSP_ACE15_DSP_IMR_BASE, 38400);
}

static void xtensa_ace15_machine_init(MachineClass *mc)
{
    mc->desc         = "ACE1.5 HiFi5 (Meteor Lake)";
    mc->is_default   = false;
    mc->init         = ace15_adsp_init;
    mc->max_cpus     = 3;
    mc->default_cpus = 1;
    /* ACE 1.5 uses the same LX7 HiFi5 ISA as ACE 3.0 */
    mc->default_cpu_type = XTENSA_CPU_TYPE_NAME("ace30");
    adsp_machine_class_add_options(mc);
}

DEFINE_MACHINE("adsp_ace15", xtensa_ace15_machine_init)
