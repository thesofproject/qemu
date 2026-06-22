/* Core DSP support for Intel ACE audio DSPs.
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
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
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

/* Moved from ace.h */
#define ADSP_ACE30_DSP_HP_SRAM_BASE     0xa0200000
#define ADSP_ACE30_DSP_LP_GPDMA_SHIM_BASE(x)   (0x00001000 + (x) * 0x40)
#define ADSP_ACE30_DSP_LP_GPDMA_SHIM_SIZE      0x40
#define ADSP_ACE30_DSP_ADCIP_BASE              0x00001100
#define ADSP_ACE30_DSP_ADCIP_SIZE              0x10
#define ADSP_ACE30_DSP_ADCS_BASE               0x00001110
#define ADSP_ACE30_DSP_ADCS_SIZE               0x10
#define ADSP_ACE30_DSP_IDC_DSP_BLOCK_BASE      0x00001200
#define ADSP_ACE30_DSP_IDC_DSP_BLOCK_SIZE      0x50
#define ADSP_ACE30_DSP_HFINTIP_BLOCK_BASE      0x00001800
#define ADSP_ACE30_DSP_HFINTIP_BLOCK_SIZE      0x400
#define ADSP_ACE30_DSP_LP_GP_DMA_LINK_BASE(x)  (0x00001C00 + (x) * 0x40)
#define ADSP_ACE30_DSP_LP_GP_DMA_LINK_SIZE     0x40
#define ADSP_ACE30_DSP_SOCCI_BLOCK_BASE        0x00001C80
#define ADSP_ACE30_DSP_SOCCI_BLOCK_SIZE        0x80
#define ADSP_ACE30_DSP_HFPMCCU_BLOCK_BASE      0x00001D00
#define ADSP_ACE30_DSP_HFPMCCU_BLOCK_SIZE      0x40
#define ADSP_ACE30_DSP_HFPMCCH_BLOCK_BASE      0x00001D40
#define ADSP_ACE30_DSP_HFPMCCH_BLOCK_SIZE      0x40
#define ADSP_ACE30_DSP_SECPOL_BLOCK_BASE       0x00001D80
#define ADSP_ACE30_DSP_SECPOL_BLOCK_SIZE       0x80
#define ADSP_ACE30_DSP_TSOCFGU_AON_BLOCK_BASE  0x00001E00
#define ADSP_ACE30_DSP_TSOCFGU_AON_BLOCK_SIZE  0x200
#define ADSP_ACE30_DSP_DFCAPSTS_BLOCK_BASE     0x00002000
#define ADSP_ACE30_DSP_DFCAPSTS_BLOCK_SIZE     0x1000
#define ADSP_ACE30_DSP_ADCIP_BLOCK_BASE        0x00003000
#define ADSP_ACE30_DSP_ADCIP_BLOCK_SIZE        0x800
#define ADSP_ACE30_DSP_ADCS_BLOCK_BASE         0x00003800
#define ADSP_ACE30_DSP_ADCS_BLOCK_SIZE         0x800
#define ADSP_ACE30_DSP_DMIC_BASE        0x00010000
#define ADSP_ACE30_DSP_DMIC_SIZE        0x8000
#define ADSP_ACE30_DSP_PWMIP_BASE       0x0001F000
#define ADSP_ACE30_DSP_PWMIP_SIZE       0x800
#define ADSP_ACE30_DSP_PWMS_AON_BASE    0x0001F800
#define ADSP_ACE30_DSP_PWMS_AON_SIZE    0x800
#define ADSP_ACE30_DSP_DMW_BASE         0x00070200
#define ADSP_ACE30_DSP_DMW_SIZE         (ADSP_ACE30_DSP_DMW_STRIDE * ADSP_ACE30_DSP_DMW_COUNT)
#define ADSP_ACE30_DSP_DTFC_BASE        0x00071600
#define ADSP_ACE30_DSP_DTFC_SIZE        0x40
#define ADSP_ACE30_DSP_DFMICPVC_BASE    0x00071A40
#define ADSP_ACE30_DSP_DFMICPVC_SIZE    0x10
#define ADSP_ACE30_DSP_DCR_BASE         0x00071A50
#define ADSP_ACE30_DSP_DCR_SIZE         0x70
#define ADSP_ACE30_DSP_DFPMCCH_BASE     0x00071AC0
#define ADSP_ACE30_DSP_DFPMCCH_SIZE     0x40
#define ADSP_ACE30_DSP_DFPMCCU_BASE     0x00071B00
#define ADSP_ACE30_DSP_DFPMCCU_SIZE     0x100
#define ADSP_ACE30_DSP_HP_GPDMA_SHIM_BASE      0x00071C00
#define ADSP_ACE30_DSP_HP_GPDMA_SHIM_SIZE      0x100
#define ADSP_ACE30_DSP_DFL2HSBPM_BASE   0x00017a800
#define ADSP_ACE30_DSP_DFL2HSBPM_SIZE   0x1000
#define ADSP_ACE30_DSP_HFINT_BASE       0x00071E00
#define ADSP_ACE30_DSP_HFINT_SIZE       0x20
#define ADSP_ACE30_DSP_HFDSSGBL_BASE    0x00071F00
#define ADSP_ACE30_DSP_HFDSSGBL_SIZE    0x100   /* ACE30 SHIM: 0x71F00-0x71FFF */
#define ADSP_ACE30_DSP_HFTTS_BASE       0x00072000
#define ADSP_ACE30_DSP_HFTTS_SIZE       0x200
#define ADSP_ACE30_DSP_GTW_HOST_OUT_STREAM_BASE(x)  (0x00072800 + (x) * 0x40)
#define ADSP_ACE30_DSP_GTW_HOST_OUT_STREAM_SIZE     0x40
#define ADSP_ACE30_DSP_GTW_HOST_IN_STREAM_BASE(x)   (0x00072C00 + (x) * 0x40)
#define ADSP_ACE30_DSP_GTW_HOST_IN_STREAM_SIZE      0x40
#define ADSP_ACE30_DSP_GTW_LINK_OUT_STREAM_BASE(x)  (0x00079400 + (x) * 0x40)
#define ADSP_ACE30_DSP_GTW_LINK_OUT_STREAM_SIZE     0x40
#define ADSP_ACE30_DSP_GTW_LINK_IN_STREAM_BASE(x)   (0x00079800 + (x) * 0x40)
#define ADSP_ACE30_DSP_GTW_LINK_IN_STREAM_SIZE      0x40
#define ADSP_ACE30_DSP_HFIPC_BASE       0x00073000
#define ADSP_ACE30_DSP_HFIPC_SIZE       0x1000
#define ADSP_ACE30_DSP_L2C_BASE         0x00074000
#define ADSP_ACE30_DSP_L2C_SIZE         0x1000
#define ADSP_ACE30_DSP_SSP_BASE_CTRL    0x00028000
#define ADSP_ACE30_DSP_SSP_BASE_CTRL_SIZE 0x100
#define ADSP_ACE30_DSP_SSP_BASE(x)      (0x00028100 + (x) * 0x1000)
#define ADSP_ACE30_DSP_SSP_SIZE         0x1000
#define ADSP_ACE30_DSP_SSP_HOST_BASE    0x00079C00
#define ADSP_ACE30_DSP_SSP_HOST_SIZE    0x200
#define ADSP_ACE30_DSP_SOCCI_BASE       0x00078C00
#define ADSP_ACE30_DSP_SOCCI_SIZE       0x200
#define ADSP_ACE30_DSP_DFFUSA_BASE      0x00090000
#define ADSP_ACE30_DSP_DFFUSA_SIZE      0x1000
#define ADSP_ACE30_DSP_DINT_BASE(x)     (0x00091000 + (x) * 0x100)
#define ADSP_ACE30_DSP_DINT_SIZE        0x100
#define ADSP_ACE30_DSP_IDC_BASE(x)      (0x00092000 + (x) * 0x400)
#define ADSP_ACE30_DSP_IDC_SIZE         0x400
#define ADSP_ACE30_DSP_HFINTIP_BASE     0x00094000  /* DfINTCP: DXINTIPPTR base (ACE30) */
#define ADSP_ACE30_DSP_HFINTIP_SIZE     0x1400      /* 5 cores × 0x400 */
#define ADSP_ACE30_DSP_HFIMR_SIZE       0x1000
#define ADSP_ACE30_DSP_DSPCS_BASE       0x000178d00
#define ADSP_ACE30_DSP_DSPCS_SIZE       0x1000
#define ADSP_ACE30_DSP_DFL2USBPM_BASE   0x00071d80
#define ADSP_ACE30_DSP_DFL2USBPM_SIZE   0x80
#define ADSP_ACE30_DSP_DFL2MM_BASE      0x000071d00
#define ADSP_ACE30_DSP_DFL2MM_SIZE      0x80
#define ADSP_ACE30_DSP_L2HSTLB_BASE     0x00017e000
#define ADSP_ACE30_DSP_L2HSTLB_SIZE     0x2000
#define ADSP_ACE30_DSP_IMR_MAN_OFFSET   0x42000

#define IRQ_NUM_SOFTWARE0    0
#define IRQ_NUM_TIMER1       1
#define IRQ_NUM_SOFTWARE1    2
#define IRQ_NUM_SOFTWARE2    3
#define IRQ_NUM_TIMER2       5
#define IRQ_NUM_SOFTWARE3    6
#define IRQ_NUM_TIMER3       7
#define IRQ_NUM_SOFTWARE4    8
#define IRQ_NUM_SOFTWARE5    9
#define IRQ_NUM_EXT_IA       10
#define IRQ_NUM_EXT_PMC      11
#define IRQ_NUM_SOFTWARE6    12
#define IRQ_NUM_EXT_DMAC0    13
#define IRQ_NUM_EXT_DMAC1    14
#define IRQ_NUM_EXT_TIMER    15
#define IRQ_NUM_EXT_SSP0     16
#define IRQ_NUM_EXT_SSP1     17
#define IRQ_NUM_EXT_SSP2     18
#define IRQ_NUM_NMI          20

#define ace_region(raddr)    info->region[(raddr) >> 2]

static const MemoryRegionOps ace_io_ops = {
    .read = ace_shim_read,
    .write = ace_shim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ace_simple_io_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
}

/* ACE 3.0 IO devices */
static struct adsp_reg_space ace_30_io[] = {
        /* LP GPDMA shim windows (low-power DMA register aperture). */
        { .name = "lp-gpda-shim", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_LP_GPDMA_SHIM_BASE(0), .size = ADSP_ACE30_DSP_LP_GPDMA_SHIM_SIZE * 4},},
        /* ADC command/status register block. */
        { .name = "cmd", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_ADCIP_BASE, .size = ADSP_ACE30_DSP_ADCIP_SIZE},},
        /* ADC result register block. */
        { .name = "res", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_ADCS_BASE, .size = ADSP_ACE30_DSP_ADCS_SIZE},},
        /* Low-address IDC aggregate window. */
        { .name = "idc-dsp", .init = ace_idc_dsp_block_init,
            .ops = &ace_idc_dsp_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_IDC_DSP_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_IDC_DSP_BLOCK_SIZE},},
        /* Low-address interrupt proxy aggregate window. */
        { .name = "hfintip", .init = ace_hfintip_block_init,
            .ops = &ace_hfintip_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFINTIP_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_HFINTIP_BLOCK_SIZE},},
        /* LP GP-DMA link controller 0 register window. */
        { .name = "dmac0", .init = ace_simple_io_init, .irq = IRQ_LPGPDMA,
            .desc = {.base = ADSP_ACE30_DSP_LP_GP_DMA_LINK_BASE(0), .size = ADSP_ACE30_DSP_LP_GP_DMA_LINK_SIZE},},
        /* LP GP-DMA link controller 1 register window. */
        { .name = "dmac1", .init = ace_simple_io_init, .irq = IRQ_LPGPDMA,
            .desc = {.base = ADSP_ACE30_DSP_LP_GP_DMA_LINK_BASE(1), .size = ADSP_ACE30_DSP_LP_GP_DMA_LINK_SIZE},},
        /* Low-address mailbox/network node control aggregate window. */
        { .name = "socci", .init = ace_socci_block_init,
            .ops = &ace_socci_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_SOCCI_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_SOCCI_BLOCK_SIZE},},
        /* Low-address L2 memory capability aggregate window. */
        { .name = "hfpmccu", .init = ace_hfpmccu_block_init,
            .ops = &ace_hfpmccu_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFPMCCU_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_HFPMCCU_BLOCK_SIZE},},
        /* Low-address HP SRAM power management aggregate window. */
        { .name = "hfpmcch", .init = ace_hfpmcch_block_init,
            .ops = &ace_hfpmcch_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFPMCCH_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_HFPMCCH_BLOCK_SIZE},},
        /* Security policy aggregate window. */
        { .name = "secpol", .init = ace_secpol_block_init,
            .ops = &ace_secpol_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_SECPOL_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_SECPOL_BLOCK_SIZE},},
        /* Always-on thermal/SOC configuration aggregate window. */
        { .name = "tsocfgu-aon", .init = ace_tsocfgu_aon_block_init,
            .ops = &ace_tsocfgu_aon_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_TSOCFGU_AON_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_TSOCFGU_AON_BLOCK_SIZE},},
        /* Capability/status low-address aggregate window. */
        { .name = "dfcapsts", .init = ace_dfcapsts_block_init,
            .ops = &ace_dfcapsts_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_DFCAPSTS_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_DFCAPSTS_BLOCK_SIZE},},
        /* ADC command block aggregate window. */
        { .name = "adcip", .init = ace_adcip_block_init,
            .ops = &ace_adcip_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_ADCIP_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_ADCIP_BLOCK_SIZE},},
        /* ADC status block aggregate window. */
        { .name = "adcs", .init = ace_adcs_block_init,
            .ops = &ace_adcs_block_ops,
            .desc = {.base = ADSP_ACE30_DSP_ADCS_BLOCK_BASE,
                     .size = ADSP_ACE30_DSP_ADCS_BLOCK_SIZE},},
        /* Digital microphone register block. */
        { .name = "dmic", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_DMIC_BASE, .size = ADSP_ACE30_DSP_DMIC_SIZE},},
        /* PWM IP block. */
        { .name = "pwmip", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_PWMIP_BASE, .size = ADSP_ACE30_DSP_PWMIP_SIZE},},
        /* PWM always-on block. */
        { .name = "pwms-aon", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_PWMS_AON_BASE, .size = ADSP_ACE30_DSP_PWMS_AON_SIZE},},
        /* DMW — DSP memory window configuration block. */
        { .name = "dmw", .init = ace_dmw_init, .ops = &ace_dmw_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_DMW_BASE, .size = ADSP_ACE30_DSP_DMW_SIZE},},
        /* DTFC timing/control block. */
        { .name = "dtfc", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_DTFC_BASE, .size = ADSP_ACE30_DSP_DTFC_SIZE},},
        /* DFMICPVC clock/power control block. */
        { .name = "dfmicpvc", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_DFMICPVC_BASE, .size = ADSP_ACE30_DSP_DFMICPVC_SIZE},},
        /* DCR debug/control registers. */
        { .name = "dcr", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_DCR_BASE, .size = ADSP_ACE30_DSP_DCR_SIZE},},
        /* DFPMCCH register block. */
        { .name = "dfpmcch", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_DFPMCCH_BASE, .size = ADSP_ACE30_DSP_DFPMCCH_SIZE},},
        /* HP GPDMA shim register block. */
        { .name = "hp-gpdma-shim", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_HP_GPDMA_SHIM_BASE, .size = ADSP_ACE30_DSP_HP_GPDMA_SHIM_SIZE},},
        /* DfPMCCU — DSP power and clock control block. */
        { .name = "pmccu", .init = ace_pmccu_init, .ops = &ace_pmccu_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_DFPMCCU_BASE, .size = ADSP_ACE30_DSP_DFPMCCU_SIZE},},
        /* DfL2MM capability and pointer block. */
        { .name = "DfL2MM",  .init = ace30_dfl2mm_init, .ops = &ace30_dfl2mm_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_DFL2MM_BASE, .size = ADSP_ACE30_DSP_DFL2MM_SIZE},},
        /* DfL2USBPM LP SRAM bank power management block. */
        { .name = "dfl2usbpm",  .init = ace30_dfl2usbpm_init, .ops = &ace30_dfl2usbpm_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_DFL2USBPM_BASE, .size = ADSP_ACE30_DSP_DFL2USBPM_SIZE},},
        /* DesignWare Interrupt Controller register mapping. */
        { .name = "dw-intc", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFINT_BASE, .size = ADSP_ACE30_DSP_HFINT_SIZE},},
        /* SHIM global control block. */
        { .name = "shim",
           .init = &adsp_ace_shim_init, .ops = &ace_shim_ops,
              .desc = {.base = ADSP_ACE30_DSP_HFDSSGBL_BASE, .size = ADSP_ACE30_DSP_HFDSSGBL_SIZE},},
        /* HFTTS timer block. */
        { .name = "timer",
            .init = ace_timer_init, .ops = &ace_timer_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFTTS_BASE, .size = ADSP_ACE30_DSP_HFTTS_SIZE},},
        /* Gateway host-output stream windows (9 streams, 0x40 each). */
        { .name = "gtw-hout",  .init = hda_gtw_hout_init, .ops = &ace_hda_gtw_ops,
            .desc = {.base = ADSP_ACE30_DSP_GTW_HOST_OUT_STREAM_BASE(0), .size = ADSP_ACE30_DSP_GTW_HOST_OUT_STREAM_SIZE * 9},},
        /* Gateway host-input stream windows (11 streams, 0x40 each). */
        { .name = "gtw-hin",  .init = hda_gtw_hin_init, .ops = &ace_hda_gtw_ops,
            .desc = {.base = ADSP_ACE30_DSP_GTW_HOST_IN_STREAM_BASE(0), .size = ADSP_ACE30_DSP_GTW_HOST_IN_STREAM_SIZE * 11},},
        /* Host firmware IPC block. */
        { .name = "hfipc0", .init = ace30_hfipc_init, .ops = &ace30_hfipc_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFIPC_BASE, .size = ADSP_ACE30_DSP_HFIPC_SIZE},},
        /* L2 cache control block. */
        { .name = "l2c", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_L2C_BASE, .size = ADSP_ACE30_DSP_L2C_SIZE},},
        /* SSP Base Control */
        { .name = "ssp-base-ctrl", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_SSP_BASE_CTRL, .size = ADSP_ACE30_DSP_SSP_BASE_CTRL_SIZE},},
        /* SSP shared host registers */
        { .name = "ssp-host", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_SSP_HOST_BASE, .size = ADSP_ACE30_DSP_SSP_HOST_SIZE},},
        /* SSP0 serial port block. */
        {.name = "ssp0", .init = ace_simple_io_init, .irq = IRQ_SSP0,
           .desc = {.base = ADSP_ACE30_DSP_SSP_BASE(0), .size = ADSP_ACE30_DSP_SSP_SIZE},},
        /* SSP1 serial port block. */
        {.name = "ssp1", .init = ace_simple_io_init, .irq = IRQ_SSP1,
           .desc = {.base = ADSP_ACE30_DSP_SSP_BASE(1), .size = ADSP_ACE30_DSP_SSP_SIZE},},
        /* Main network node control block. */
        { .name = "mn", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_SOCCI_BASE, .size = ADSP_ACE30_DSP_SOCCI_SIZE},},
        /* SSP2 serial port block. */
        {.name = "ssp2", .init = ace_simple_io_init, .irq = IRQ_SSP2,
           .desc = {.base = ADSP_ACE30_DSP_SSP_BASE(2), .size = ADSP_ACE30_DSP_SSP_SIZE},},
        /* Gateway link-output stream windows (9 streams, 0x40 each). */
        { .name = "gtw-lout",  .init = hda_gtw_lout_init, .ops = &ace_hda_gtw_ops,
            .desc = {.base = ADSP_ACE30_DSP_GTW_LINK_OUT_STREAM_BASE(0), .size = ADSP_ACE30_DSP_GTW_LINK_OUT_STREAM_SIZE * 9},},
        /* Gateway link-input stream windows (11 streams, 0x40 each). */
        { .name = "gtw-lin",  .init = hda_gtw_lin_init, .ops = &ace_hda_gtw_ops,
            .desc = {.base = ADSP_ACE30_DSP_GTW_LINK_IN_STREAM_BASE(0), .size = ADSP_ACE30_DSP_GTW_LINK_IN_STREAM_SIZE * 11},},
        /* SSP3 serial port block. */
        {.name = "ssp3", .init = ace_simple_io_init, .irq = IRQ_SSP3,
           .desc = {.base = ADSP_ACE30_DSP_SSP_BASE(3), .size = ADSP_ACE30_DSP_SSP_SIZE},},
        /* SSP4 serial port block. */
        {.name = "ssp4", .init = ace_simple_io_init, .irq = IRQ_SSP4,
           .desc = {.base = ADSP_ACE30_DSP_SSP_BASE(4), .size = ADSP_ACE30_DSP_SSP_SIZE},},
        /* SSP5 serial port block. */
        {.name = "ssp5", .init = ace_simple_io_init, .irq = IRQ_SSP5,
           .desc = {.base = ADSP_ACE30_DSP_SSP_BASE(5), .size = ADSP_ACE30_DSP_SSP_SIZE},},
        /* DFFUSA fuse/security block. */
        { .name = "dffusa", .init = ace_simple_io_init,
            .desc = {.base = ADSP_ACE30_DSP_DFFUSA_BASE, .size = ADSP_ACE30_DSP_DFFUSA_SIZE},},
        /* DINT core 0 interrupt control block. */
        { .name = "dint0", .init = ace_simple_io_init, .ops = NULL,
            .desc = {.base = ADSP_ACE30_DSP_DINT_BASE(0), .size = ADSP_ACE30_DSP_DINT_SIZE},},
        /* DINT core 1 interrupt control block. */
        { .name = "dint1", .init = ace_simple_io_init, .ops = NULL,
            .desc = {.base = ADSP_ACE30_DSP_DINT_BASE(1), .size = ADSP_ACE30_DSP_DINT_SIZE},},
        /* DINT core 2 interrupt control block. */
        { .name = "dint2", .init = ace_simple_io_init, .ops = NULL,
            .desc = {.base = ADSP_ACE30_DSP_DINT_BASE(2), .size = ADSP_ACE30_DSP_DINT_SIZE},},
        /* DINT core 3 interrupt control block. */
        { .name = "dint3", .init = ace_simple_io_init, .ops = NULL,
            .desc = {.base = ADSP_ACE30_DSP_DINT_BASE(3), .size = ADSP_ACE30_DSP_DINT_SIZE},},
        /* DINT core 4 interrupt control block. */
        { .name = "dint4", .init = ace_simple_io_init, .ops = NULL,
            .desc = {.base = ADSP_ACE30_DSP_DINT_BASE(4), .size = ADSP_ACE30_DSP_DINT_SIZE},},
        /* IDC core 0 mailbox block. */
        { .name = "idc0", .init = ace30_idc_init, .ops = &ace_idc_ops,
            .desc = {.base = ADSP_ACE30_DSP_IDC_BASE(0), .size = ADSP_ACE30_DSP_IDC_SIZE},},
        /* IDC core 1 mailbox block. */
        { .name = "idc1", .init = ace30_idc_init, .ops = &ace_idc_ops,
            .desc = {.base = ADSP_ACE30_DSP_IDC_BASE(1), .size = ADSP_ACE30_DSP_IDC_SIZE},},
        /* IDC core 2 mailbox block. */
        { .name = "idc2", .init = ace30_idc_init, .ops = &ace_idc_ops,
            .desc = {.base = ADSP_ACE30_DSP_IDC_BASE(2), .size = ADSP_ACE30_DSP_IDC_SIZE},},
        /* IDC core 3 mailbox block. */
        { .name = "idc3", .init = ace30_idc_init, .ops = &ace_idc_ops,
            .desc = {.base = ADSP_ACE30_DSP_IDC_BASE(3), .size = ADSP_ACE30_DSP_IDC_SIZE},},
        /* IDC core 4 mailbox block. */
        { .name = "idc4", .init = ace30_idc_init, .ops = &ace_idc_ops,
            .desc = {.base = ADSP_ACE30_DSP_IDC_BASE(4), .size = ADSP_ACE30_DSP_IDC_SIZE},},
        /* IRQ aggregator/programmer block. */
        { .name = "irq",
            .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFINTIP_BASE, .size = ADSP_ACE30_DSP_HFINTIP_SIZE},},
        /* HFIMR image region management block. */
        { .name = "hfimr",  .init = ace30_hfimr_init, .ops = &ace30_hfimr_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_HFIMR_BASE, .size = ADSP_ACE30_DSP_HFIMR_SIZE},},
        /* DSP core status/control block. */
        { .name = "dspcs",  .init = ace30_dspcs_init, .ops = &ace30_dspcs_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_DSPCS_BASE, .size = ADSP_ACE30_DSP_DSPCS_SIZE},},
        /* DfL2HSBPM HP SRAM bank power management block. */
        { .name = "dfl2hsbpm",  .init = ace30_dfl2hsbpm_init, .ops = &ace30_dfl2hsbpm_io_ops,
            .desc = {.base = ADSP_ACE30_DSP_DFL2HSBPM_BASE, .size = ADSP_ACE30_DSP_DFL2HSBPM_SIZE},},
        /* L2 host SRAM TLB block. */
        { .name = "l2hstlb", .init = ace_tlb_mmio_init, .ops = &ace_tlb_ops,
            .desc = {.base = ADSP_ACE30_DSP_L2HSTLB_BASE, .size = ADSP_ACE30_DSP_L2HSTLB_SIZE},},
};

/* ACE3.0 descriptor */
static const struct adsp_desc ace_ace3_dsp_desc = {
    .ia_irq = IRQ_NUM_EXT_IA,
    .ext_timer_irq = IRQ_NUM_EXT_TIMER,

    .imr_boot_ldr_offset = ADSP_ACE30_DSP_IMR_MAN_OFFSET,

    .num_mem = ace_ace3_mem_num,
    .mem_region = ace_ace3_mem,

    .num_io = ARRAY_SIZE(ace_30_io),
    .io_dev = ace_30_io,

    .mem_zones = {
        [SOF_FW_BLK_TYPE_IMR] = {
            .base = ADSP_ACE30_DSP_IMR_BASE,
        },
        [SOF_FW_BLK_TYPE_SRAM] = {
            .base = ADSP_ACE30_DSP_HP_SRAM_BASE,  /* L2 SRAM includes both HP and ULP SRAM */
        },
    },

    //.ops = &ace_ace3_ops,
};

static void ace30_adsp_init(MachineState *machine)
{
    /* ACE3.0 DSP memory initialization */
    adsp_ace_init(&ace_ace3_dsp_desc, machine, &ace_io_ops, 0,
                  ADSP_ACE30_DSP_IMR_BASE, 38420);
}

static void xtensa_ace30_machine_init(MachineClass *mc)
{
    mc->desc = "ACE3.0 HiFi5";
    mc->is_default = false;
    mc->init = ace30_adsp_init;
    mc->max_cpus = 5;
    mc->default_cpus = 1;  /* Start with 1 core but allow up to 5 */
    mc->default_cpu_type = XTENSA_CPU_TYPE_NAME("ace30");
    adsp_machine_class_add_options(mc);
}

DEFINE_MACHINE("adsp_ace30", xtensa_ace30_machine_init)
