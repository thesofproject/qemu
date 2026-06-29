/* Core DSP support for Intel ACE 2.0 (Lunar Lake / LNL) audio DSP.
 *
 * ACE 2.0 is the second generation of the Audio DSP Engine (ACE) IP,
 * introduced in Lunar Lake (LNL).  It has the same LX7 HiFi5 ISA and
 * peripheral structure as ACE 1.5 (MTL) but with a number of differences
 * in block addresses and added peripherals.
 *
 * Key differences from ACE 1.5 (MTL)
 * ====================================
 * - 5 DSP cores (LX7 HiFi5) vs 3 in ACE 1.5
 * - HDA link-out streams at 0x79400 (ACE 1.5: 0x72400)
 * - HDA link-in  streams at 0x79800 (ACE 1.5: 0x72600)
 * - No LP GPDMA controllers (ACE 1.5 had 3 at 0x7C000/7D000/7E000)
 * - sspbase at 0x28000 (ACE 1.5: 0x28800); SSP IP regs shifted +0x100 per slot
 * - DMIC IP registers at 0x10100 (ACE 1.5: 0x10000)
 * - New DMICVSS block at 0x16000
 * - New UAOL block at 0xF000
 * - New L1 cache cap/cfg/pcfg at 0x1FE80080
 * - HP SRAM stays at 2816 KB (same physical size as ACE 1.5)
 *
 * Addresses identical to ACE 1.5:
 *   IDC (0x70400), DMW (0x70200), DFPMCCH (0x71AC0), DFPMCCU (0x71B00),
 *   HSBCAP (0x71D00), LSBPM (0x71D80), SHIM (0x71F00), timer (0x72000),
 *   host-out (0x72800), host-in (0x72C00), HFIPC (0x73000),
 *   ACE INTC (0x7AC00), imria1 (0x162080), DSPCS (0x178D00),
 *   HSBPM (0x17A800), TLB (0x17E000), DINT (0x78840).
 *
 * References:
 *   zephyr/dts/xtensa/intel/intel_adsp_ace20_lnl.dtsi
 *   zephyr/soc/intel/intel_adsp/ace/include/ace20_lnl/
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
 * ACE 2.0 memory map
 * Derived from:
 *   zephyr/dts/xtensa/intel/intel_adsp_ace20_lnl.dtsi
 *   zephyr/soc/intel/intel_adsp/ace/include/ace20_lnl/
 * ------------------------------------------------------------------------- */

/* SRAM (same physical sizes as ACE 1.5) */
#define ADSP_ACE20_DSP_LP_SRAM_BASE     0xa0000000u  /* 64 KB  */
#define ADSP_ACE20_DSP_LP_SRAM_SIZE     0x10000u
#define ADSP_ACE20_DSP_HP_SRAM_BASE     0xa0020000u  /* 2816 KB physical */
#define ADSP_ACE20_DSP_HP_SRAM_SIZE     (2816u * 1024u)
#define ADSP_ACE20_DSP_IMR_BASE         0xa1000000u  /* 16 MB */
#define ADSP_ACE20_DSP_IMR_SIZE         0x1000000u

/* Manifest offset inside IMR (same as ACE 1.5) */
#define ADSP_ACE20_DSP_IMR_MAN_OFFSET   0x42000u

/* Low capability/status blocks (SHIM registers including DFIDCPP) */
#define ADSP_ACE20_DSP_DFCAPSTS_BLOCK_BASE 0x00002000u
#define ADSP_ACE20_DSP_DFCAPSTS_BLOCK_SIZE 0x1000u
#define ADSP_ACE20_DSP_ADCIP_BLOCK_BASE    0x00003000u
#define ADSP_ACE20_DSP_ADCIP_BLOCK_SIZE    0x800u
#define ADSP_ACE20_DSP_ADCS_BLOCK_BASE     0x00003800u
#define ADSP_ACE20_DSP_ADCS_BLOCK_SIZE     0x800u

/* ---- IP blocks (DSP address space) ---- */

/* UAOL — USB Audio Offload Link (new in ACE 2.0) */
#define ADSP_ACE20_DSP_UAOL_BASE        0x0000f000u
#define ADSP_ACE20_DSP_UAOL_SIZE        0x1000u

/* DMIC */
#define ADSP_ACE20_DSP_DMIC_BASE        0x00010100u  /* +0x100 vs ACE 1.5 */
#define ADSP_ACE20_DSP_DMIC_SIZE        0x8000u

/* DMICVSS — DMIC VSS control (new in ACE 2.0) */
#define ADSP_ACE20_DSP_DMICVSS_BASE     0x00016000u
#define ADSP_ACE20_DSP_DMICVSS_SIZE     0x2000u

/* ALH */
#define ADSP_ACE20_DSP_ALH_BASE         0x00024400u
#define ADSP_ACE20_DSP_ALH_SIZE         0x200u

/* SSP: sspbase at 0x28000, per-SSP regs at base+N*0x1000+0x100 */
#define ADSP_ACE20_DSP_SSP_BASE_CTRL    0x00028000u  /* 0x28800 in ACE 1.5 */
#define ADSP_ACE20_DSP_SSP_BASE_CTRL_SIZE 0x1000u
#define ADSP_ACE20_DSP_SSP_BASE(x)      (0x00028100u + (x) * 0x1000u)
#define ADSP_ACE20_DSP_SSP_SIZE         0x1000u
#define ADSP_ACE20_DSP_SSP_HOST_BASE    0x00079c00u
#define ADSP_ACE20_DSP_SSP_HOST_SIZE    0x200u

/* IDC — same location as ACE 1.5 */
#define ADSP_ACE20_DSP_IDC_BASE         0x00070400u
#define ADSP_ACE20_DSP_IDC_SIZE         0x400u

/* DMW — memory windows (4 windows × 8 B, identical to ACE 1.5) */
#define ADSP_ACE20_DSP_DMW_BASE         0x00070200u
#define ADSP_ACE20_DSP_DMW_STRIDE       0x08u
#define ADSP_ACE20_DSP_DMW_COUNT        4u
#define ADSP_ACE20_DSP_DMW_SIZE         (ADSP_ACE20_DSP_DMW_STRIDE * ADSP_ACE20_DSP_DMW_COUNT)

/* DFPMCCH (HST domain power/clock) */
#define ADSP_ACE20_DSP_DFPMCCH_BASE     0x00071AC0u
#define ADSP_ACE20_DSP_DFPMCCH_SIZE     0x40u

/* DFPMCCU (ULP domain power/clock) */
#define ADSP_ACE20_DSP_DFPMCCU_BASE     0x00071B00u
#define ADSP_ACE20_DSP_DFPMCCU_SIZE     0x100u

/* HSBCAP — HP SRAM bank capability */
#define ADSP_ACE20_DSP_HSBCAP_BASE      0x00071D00u
#define ADSP_ACE20_DSP_HSBCAP_SIZE      0x80u

/* LSBPM — LP SRAM bank power management */
#define ADSP_ACE20_DSP_LSBPM_BASE       0x00071D80u
#define ADSP_ACE20_DSP_LSBPM_SIZE       0x80u

/* Host-window mirror registers (DTFCXD64 etc.) */
#define ADSP_ACE20_DSP_HOST_WIN_BASE(x) (0x00071A00u + (x) * 0x20u)
#define ADSP_ACE20_DSP_HOST_WIN_SIZE    0x80u        /* 4 windows × 0x20 */

/* SHIM (global DSP control) */
#define ADSP_ACE20_DSP_SHIM_BASE        0x00071F00u
#define ADSP_ACE20_DSP_SHIM_SIZE        0x100u

/* HFTTS (timer/timestamp block) */
#define ADSP_ACE20_DSP_HFTTS_BASE       0x00072000u
#define ADSP_ACE20_DSP_HFTTS_SIZE       0x200u

/* HDA gateway streams
 * NOTE: link streams moved to 0x79400/0x79800 in ACE 2.0
 */
#define ADSP_ACE20_DSP_GTW_HOUT_BASE(x) (0x00072800u + (x) * 0x40u)  /* host-out */
#define ADSP_ACE20_DSP_GTW_HIN_BASE(x)  (0x00072C00u + (x) * 0x40u)  /* host-in  */
#define ADSP_ACE20_DSP_GTW_LOUT_BASE(x) (0x00079400u + (x) * 0x40u)  /* link-out */
#define ADSP_ACE20_DSP_GTW_LIN_BASE(x)  (0x00079800u + (x) * 0x40u)  /* link-in  */
#define ADSP_ACE20_DSP_GTW_STREAM_SIZE  0x40u

/* HFIPC (host IPC block) */
#define ADSP_ACE20_DSP_HFIPC_BASE       0x00073000u
#define ADSP_ACE20_DSP_HFIPC_SIZE       0x1000u

/* L2 cache control */
#define ADSP_ACE20_DSP_L2C_BASE         0x00074000u
#define ADSP_ACE20_DSP_L2C_SIZE         0x1000u

/* SOCCI (MN node) */
#define ADSP_ACE20_DSP_SOCCI_BASE       0x00078C00u
#define ADSP_ACE20_DSP_SOCCI_SIZE       0x200u

/* Per-core DINT interrupt enable/status (same as ACE 1.5: DXHIPCIE_REG) */
#define ADSP_ACE20_DSP_DINT_BASE        0x00078840u
#define ADSP_ACE20_DSP_DINT_SIZE        0x400u   /* 5 cores × struct ace_dint */

/* ACE INTC (DesignWare interrupt controller) */
#define ADSP_ACE20_DSP_ACE_INTC_BASE    0x0007AC00u
#define ADSP_ACE20_DSP_ACE_INTC_SIZE    0x0C00u  /* LNL DTS: reg = <0x7ac00 0xc00> */

/* imria1 — IMR attribute register (same as ACE 1.5) */
#define ADSP_ACE20_DSP_IMRIA1_BASE      0x00162080u
#define ADSP_ACE20_DSP_IMRIA1_SIZE      0x80u

/* DSPCS — DSP Core Shim (same as ACE 1.5) */
#define ADSP_ACE20_DSP_DSPCS_BASE       0x000178D00u
#define ADSP_ACE20_DSP_DSPCS_SIZE       0x1000u

/* HSBPM — HP SRAM bank power management (same as ACE 1.5) */
#define ADSP_ACE20_DSP_HSBPM_BASE       0x0017A800u
#define ADSP_ACE20_DSP_HSBPM_SIZE       0x1000u

/* TLB (L2 host SRAM TLB, same as ACE 1.5) */
#define ADSP_ACE20_DSP_L2HSTLB_BASE     0x00017E000u
#define ADSP_ACE20_DSP_L2HSTLB_SIZE     0x1000u

/* L1 cache capability / config (new in ACE 2.0, in high address space) */
#define ADSP_ACE20_DSP_L1CCAP_BASE      0x1FE80080u
#define ADSP_ACE20_DSP_L1CCAP_SIZE      0x10u   /* l1ccap + l1ccfg + l1pcfg */

/* IRQ assignments (from adsp_interrupt.h ACE 2.0 — same numbering as ACE 1.5) */
#define IRQ_NUM_EXT_IA       10
#define IRQ_NUM_EXT_TIMER    15

/* -------------------------------------------------------------------------
 * IO ops / helpers shared with ACE 3.0/1.5 code
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
 * ACE 2.0 IO device table
 * ------------------------------------------------------------------------- */

static struct adsp_reg_space ace_20_io[] = {
    /* Low capability/status window (includes DFIDCPP). */
    { .name = "dfcapsts-low", .init = ace_dfcapsts_block_init,
        .ops = &ace_dfcapsts_block_ops,
        .desc = {.base = ADSP_ACE20_DSP_DFCAPSTS_BLOCK_BASE,
                 .size = ADSP_ACE20_DSP_DFCAPSTS_BLOCK_SIZE}, },
    { .name = "adcip-low", .init = ace_adcip_block_init,
        .ops = &ace_adcip_block_ops,
        .desc = {.base = ADSP_ACE20_DSP_ADCIP_BLOCK_BASE,
                 .size = ADSP_ACE20_DSP_ADCIP_BLOCK_SIZE}, },
    { .name = "adcs-low", .init = ace_adcs_block_init,
        .ops = &ace_adcs_block_ops,
        .desc = {.base = ADSP_ACE20_DSP_ADCS_BLOCK_BASE,
                 .size = ADSP_ACE20_DSP_ADCS_BLOCK_SIZE}, },
    /* UAOL — USB Audio Offload Link (new in ACE 2.0) */
    { .name = "uaol", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_UAOL_BASE,
                 .size = ADSP_ACE20_DSP_UAOL_SIZE}, },
    /* DMIC (at 0x10100 in ACE 2.0) */
    { .name = "dmic", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_DMIC_BASE,
                 .size = ADSP_ACE20_DSP_DMIC_SIZE}, },
    /* DMICVSS (new in ACE 2.0) */
    { .name = "dmicvss", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_DMICVSS_BASE,
                 .size = ADSP_ACE20_DSP_DMICVSS_SIZE}, },
    /* ALH */
    { .name = "alh", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_ALH_BASE,
                 .size = ADSP_ACE20_DSP_ALH_SIZE}, },
    /* SSP base ctrl (at 0x28000 in ACE 2.0, was 0x28800 in ACE 1.5) */
    { .name = "ssp-base-ctrl", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_SSP_BASE_CTRL,
                 .size = ADSP_ACE20_DSP_SSP_BASE_CTRL_SIZE}, },
    /* SSP0 (reg at 0x28100 in ACE 2.0) */
    { .name = "ssp0", .init = ace_simple_io_init, .irq = IRQ_SSP0,
        .desc = {.base = ADSP_ACE20_DSP_SSP_BASE(0),
                 .size = ADSP_ACE20_DSP_SSP_SIZE}, },
    /* SSP1 (reg at 0x29100) */
    { .name = "ssp1", .init = ace_simple_io_init, .irq = IRQ_SSP1,
        .desc = {.base = ADSP_ACE20_DSP_SSP_BASE(1),
                 .size = ADSP_ACE20_DSP_SSP_SIZE}, },
    /* SSP2 (reg at 0x2a100) */
    { .name = "ssp2", .init = ace_simple_io_init, .irq = IRQ_SSP2,
        .desc = {.base = ADSP_ACE20_DSP_SSP_BASE(2),
                 .size = ADSP_ACE20_DSP_SSP_SIZE}, },
    /* SSP shared host registers */
    { .name = "ssp-host", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_SSP_HOST_BASE,
                 .size = ADSP_ACE20_DSP_SSP_HOST_SIZE}, },
    /* IDC — aggregate block at 0x70400 (same as ACE 1.5) */
    { .name = "idc", .init = ace30_idc_init, .ops = &ace_idc_ops,
        .desc = {.base = ADSP_ACE20_DSP_IDC_BASE,
                 .size = ADSP_ACE20_DSP_IDC_SIZE}, },
    /* DMW — DSP memory window configuration */
    { .name = "dmw", .init = ace_dmw_init, .ops = &ace_dmw_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_DMW_BASE,
                 .size = ADSP_ACE20_DSP_DMW_SIZE}, },
    /* Host-window mirror registers (DTFCXD64 etc.) */
    { .name = "hostwin", .init = ace_hostwin_init, .ops = &ace_hostwin_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_HOST_WIN_BASE(0),
                 .size = ADSP_ACE20_DSP_HOST_WIN_SIZE}, },
    /* DFPMCCH (HST power/clock) */
    { .name = "dfpmcch", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_DFPMCCH_BASE,
                 .size = ADSP_ACE20_DSP_DFPMCCH_SIZE}, },
    /* DFPMCCU (ULP power/clock) */
    { .name = "pmccu", .init = ace_pmccu_init, .ops = &ace_pmccu_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_DFPMCCU_BASE,
                 .size = ADSP_ACE20_DSP_DFPMCCU_SIZE}, },
    /* HSBCAP (HP SRAM bank capability) */
    { .name = "hsbcap", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_HSBCAP_BASE,
                 .size = ADSP_ACE20_DSP_HSBCAP_SIZE}, },
    /* LSBPM (LP SRAM bank power mgmt) */
    { .name = "lsbpm", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_LSBPM_BASE,
                 .size = ADSP_ACE20_DSP_LSBPM_SIZE}, },
    /* SHIM (global DSP control) */
    { .name = "shim",
        .init = &adsp_ace_shim_init, .ops = &ace_shim_ops,
        .desc = {.base = ADSP_ACE20_DSP_SHIM_BASE,
                 .size = ADSP_ACE20_DSP_SHIM_SIZE}, },
    /* HFTTS (timer / timestamp) */
    { .name = "timer",
        .init = ace_timer_init, .ops = &ace_timer_ops,
        .desc = {.base = ADSP_ACE20_DSP_HFTTS_BASE,
                 .size = ADSP_ACE20_DSP_HFTTS_SIZE}, },
    /* Gateway: host-out streams */
    { .name = "gtw-hout", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE20_DSP_GTW_HOUT_BASE(0),
                 .size = ADSP_ACE20_DSP_GTW_STREAM_SIZE * 9}, },
    /* Gateway: host-in streams */
    { .name = "gtw-hin", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE20_DSP_GTW_HIN_BASE(0),
                 .size = ADSP_ACE20_DSP_GTW_STREAM_SIZE * 11}, },
    /* HFIPC (host IPC) */
    { .name = "hfipc0", .init = ace30_hfipc_init, .ops = &ace30_hfipc_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_HFIPC_BASE,
                 .size = ADSP_ACE20_DSP_HFIPC_SIZE}, },
    /* L2 cache control */
    { .name = "l2c", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_L2C_BASE,
                 .size = ADSP_ACE20_DSP_L2C_SIZE}, },
    /* SOCCI (MN node / network control) */
    { .name = "mn", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_SOCCI_BASE,
                 .size = ADSP_ACE20_DSP_SOCCI_SIZE}, },
    /* DINT — per-core interrupt enable/status (same as ACE 1.5: 0x78840) */
    { .name = "dint", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_DINT_BASE,
                 .size = ADSP_ACE20_DSP_DINT_SIZE}, },
    /* SSP shared host registers (in 0x79xxx region) */
    /* Gateway: link-out streams (at 0x79400 in ACE 2.0) */
    { .name = "gtw-lout", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE20_DSP_GTW_LOUT_BASE(0),
                 .size = ADSP_ACE20_DSP_GTW_STREAM_SIZE * 9}, },
    /* Gateway: link-in streams (at 0x79800 in ACE 2.0) */
    { .name = "gtw-lin", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
        .desc = {.base = ADSP_ACE20_DSP_GTW_LIN_BASE(0),
                 .size = ADSP_ACE20_DSP_GTW_STREAM_SIZE * 11}, },
    /* ACE INTC (DesignWare interrupt controller) */
    { .name = "irq", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_ACE_INTC_BASE,
                 .size = ADSP_ACE20_DSP_ACE_INTC_SIZE}, },
    /* imria1 (IMR attribute region, same as ACE 1.5) */
    { .name = "imria1", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_IMRIA1_BASE,
                 .size = ADSP_ACE20_DSP_IMRIA1_SIZE}, },
    /* DSPCS (DSP Core Shim — boot/reset/power per core) */
    { .name = "dspcs", .init = ace30_dspcs_init, .ops = &ace30_dspcs_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_DSPCS_BASE,
                 .size = ADSP_ACE20_DSP_DSPCS_SIZE}, },
    /* HSBPM (HP SRAM bank power management) */
    { .name = "dfl2hsbpm", .init = ace30_dfl2hsbpm_init,
        .ops = &ace30_dfl2hsbpm_io_ops,
        .desc = {.base = ADSP_ACE20_DSP_HSBPM_BASE,
                 .size = ADSP_ACE20_DSP_HSBPM_SIZE}, },
    /* TLB (L2 host SRAM address translation) */
    { .name = "l2hstlb", .init = ace_tlb_mmio_init, .ops = &ace_tlb_ops,
        .desc = {.base = ADSP_ACE20_DSP_L2HSTLB_BASE,
                 .size = ADSP_ACE20_DSP_L2HSTLB_SIZE}, },
    /* L1 cache capability/config/pcfg (new in ACE 2.0, in high address space) */
    { .name = "l1ccap", .init = ace_simple_io_init,
        .desc = {.base = ADSP_ACE20_DSP_L1CCAP_BASE,
                 .size = ADSP_ACE20_DSP_L1CCAP_SIZE}, },
};

/* -------------------------------------------------------------------------
 * Machine descriptor
 * ------------------------------------------------------------------------- */

static const struct adsp_desc ace_ace20_dsp_desc = {
    .name           = "ace20",
    .ia_irq         = IRQ_NUM_EXT_IA,
    .ext_timer_irq  = IRQ_NUM_EXT_TIMER,

    .imr_boot_ldr_offset = ADSP_ACE20_DSP_IMR_MAN_OFFSET,

    .num_mem    = ace_ace20_mem_num,
    .mem_region = ace_ace20_mem,

    .num_io  = ARRAY_SIZE(ace_20_io),
    .io_dev  = ace_20_io,

    .mem_zones = {
        [SOF_FW_BLK_TYPE_IMR] = {
            .base = ADSP_ACE20_DSP_IMR_BASE,
        },
        [SOF_FW_BLK_TYPE_SRAM] = {
            .base = ADSP_ACE20_DSP_HP_SRAM_BASE,
        },
    },
};

/* -------------------------------------------------------------------------
 * Machine init
 * ------------------------------------------------------------------------- */

static void ace20_adsp_init(MachineState *machine)
{
    /* ACE 2.0 DSP initialisation.
     * Clock: 38.4 MHz system clock (same as ACE 1.5).
     */
    adsp_ace_init(&ace_ace20_dsp_desc, machine, &ace_io_ops, 0,
                  ADSP_ACE20_DSP_IMR_BASE, 38400);
}

static void xtensa_ace20_machine_init(MachineClass *mc)
{
    mc->desc         = "ACE2.0 HiFi5 (Lunar Lake)";
    mc->is_default   = false;
    mc->init         = ace20_adsp_init;
    mc->max_cpus     = 5;
    mc->default_cpus = 1;
    /* ACE 2.0 uses the same LX7 HiFi5 ISA as ACE 3.0 */
    mc->default_cpu_type = XTENSA_CPU_TYPE_NAME("ace30");
    adsp_machine_class_add_options(mc);
}

DEFINE_MACHINE("adsp_ace20", xtensa_ace20_machine_init)
