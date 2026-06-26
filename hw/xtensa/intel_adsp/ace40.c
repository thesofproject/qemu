/* Core DSP support for Intel ACE4.0 audio DSPs.
 *
 * Copyright (C) 2016 Intel Corporation
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
#define ADSP_ACE40_DSP_HP_SRAM_BASE     0xa0020000
#define ADSP_ACE40_DSP_HP_SRAM_SIZE     0x480000  /* 4608KB = 36 banks × 128KB (NVL) */
#define ADSP_ACE40_DSP_LP_SRAM_BASE     0xa0000000
#define ADSP_ACE40_DSP_LP_SRAM_SIZE     0x10000   /* 64KB = 8 banks × 8KB */
#define ADSP_ACE40_DSP_IMR_BASE         0xa1000000
#define ADSP_ACE40_DSP_IMR_SIZE         0x1000000
#define ADSP_ACE40_DSP_HFDSSGBL_LOW_BASE        0x00001000
#define ADSP_ACE40_DSP_HFDSSGBL_LOW_SIZE        0x100
#define ADSP_ACE40_DSP_HFINT_LOW_BASE           0x00001100
#define ADSP_ACE40_DSP_HFINT_LOW_SIZE           0x20
#define ADSP_ACE40_DSP_HFINTIP_BLOCK_BASE       0x00001800
#define ADSP_ACE40_DSP_HFINTIP_BLOCK_SIZE       0x400
#define ADSP_ACE40_DSP_HFTTS_LOW_BASE           0x00001C00
#define ADSP_ACE40_DSP_HFTTS_LOW_SIZE           0x80
#define ADSP_ACE40_DSP_SOCCI_BLOCK_BASE         0x00001C80
#define ADSP_ACE40_DSP_SOCCI_BLOCK_SIZE         0x80
#define ADSP_ACE40_DSP_HFPMCCU_BLOCK_BASE       0x00001D00
#define ADSP_ACE40_DSP_HFPMCCU_BLOCK_SIZE       0x40
#define ADSP_ACE40_DSP_HFPMCCH_BLOCK_BASE       0x00001D40
#define ADSP_ACE40_DSP_HFPMCCH_BLOCK_SIZE       0x40
#define ADSP_ACE40_DSP_SECPOL_BLOCK_BASE        0x00001D80
#define ADSP_ACE40_DSP_SECPOL_BLOCK_SIZE        0x80
#define ADSP_ACE40_DSP_TSOCFGU_AON_BLOCK_BASE   0x00001E00
#define ADSP_ACE40_DSP_TSOCFGU_AON_BLOCK_SIZE   0x200
#define ADSP_ACE40_DSP_DFCAPSTS_BLOCK_BASE      0x00002000
#define ADSP_ACE40_DSP_DFCAPSTS_BLOCK_SIZE      0x1000
#define ADSP_ACE40_DSP_ADCIP_BLOCK_BASE         0x00003000
#define ADSP_ACE40_DSP_ADCIP_BLOCK_SIZE         0x800
#define ADSP_ACE40_DSP_ADCS_BLOCK_BASE          0x00003800
#define ADSP_ACE40_DSP_ADCS_BLOCK_SIZE          0x800
#define ADSP_ACE40_DSP_DMW_BASE                 0x00070200
#define ADSP_ACE40_DSP_DMW_SIZE                 (ADSP_ACE40_DSP_DMW_STRIDE * ADSP_ACE40_DSP_DMW_COUNT)
#define ADSP_ACE40_DSP_DTFC_BASE                0x00071600
#define ADSP_ACE40_DSP_DTFC_SIZE                0x40
#define ADSP_ACE40_DSP_DFMICPVC_BASE            0x00071A40
#define ADSP_ACE40_DSP_DFMICPVC_SIZE            0x10
#define ADSP_ACE40_DSP_DCR_BASE                 0x00071A50
#define ADSP_ACE40_DSP_DCR_SIZE                 0x70
#define ADSP_ACE40_DSP_DFPMCCH_BASE             0x00071AC0
#define ADSP_ACE40_DSP_DFPMCCH_SIZE             0x40
#define ADSP_ACE40_DSP_DFPMCCU_BASE             0x00071B00
#define ADSP_ACE40_DSP_DFPMCCU_SIZE             0x100
#define ADSP_ACE40_DSP_DFL2MM_BASE              0x00071D00
#define ADSP_ACE40_DSP_DFL2MM_SIZE              0x80
#define ADSP_ACE40_DSP_DFL2USBPM_BASE           0x00071D80
#define ADSP_ACE40_DSP_DFL2USBPM_SIZE           0x80
#define ADSP_ACE40_DSP_HFTTS_BASE               0x00072000
#define ADSP_ACE40_DSP_HFTTS_SIZE               0x200
#define ADSP_ACE40_DSP_GTW_HOST_OUT_STREAM_BASE(x)  (0x00072800 + (x) * 0x40)
#define ADSP_ACE40_DSP_GTW_HOST_OUT_STREAM_SIZE     0x40
#define ADSP_ACE40_DSP_GTW_HOST_IN_STREAM_BASE(x)   (0x00072C00 + (x) * 0x40)
#define ADSP_ACE40_DSP_GTW_HOST_IN_STREAM_SIZE      0x40
#define ADSP_ACE40_DSP_GTW_RSVD_72E_BASE            0x00072E00
#define ADSP_ACE40_DSP_GTW_RSVD_72E_SIZE            0x200
#define ADSP_ACE40_DSP_HFIPC_BASE               0x00073000
#define ADSP_ACE40_DSP_HFIPC_SIZE               0x1000
#define ADSP_ACE40_DSP_DSPCS_BASE               0x00078D00
#define ADSP_ACE40_DSP_DSPCS_SIZE               0x1000
#define ADSP_ACE40_DSP_GTW_LINK_OUT_STREAM_BASE(x)  (0x00079400 + (x) * 0x40)
#define ADSP_ACE40_DSP_GTW_LINK_OUT_STREAM_SIZE     0x40
#define ADSP_ACE40_DSP_GTW_RSVD_796_BASE            0x00079600
#define ADSP_ACE40_DSP_GTW_RSVD_796_SIZE            0x200
#define ADSP_ACE40_DSP_GTW_LINK_IN_STREAM_BASE(x)   (0x00079800 + (x) * 0x40)
#define ADSP_ACE40_DSP_GTW_LINK_IN_STREAM_SIZE      0x40
#define ADSP_ACE40_DSP_GTW_RSVD_79A_BASE            0x00079A00
#define ADSP_ACE40_DSP_GTW_RSVD_79A_SIZE            0x200
#define ADSP_ACE40_DSP_GPDMA_BASE               0x0007C000
#define ADSP_ACE40_DSP_GPDMA_SIZE               0x4000
#define ADSP_ACE40_DSP_SPI_BASE                 0x00080000
#define ADSP_ACE40_DSP_SPI_SIZE                 0x4000
#define ADSP_ACE40_DSP_DFFUSA_BASE              0x00090000
#define ADSP_ACE40_DSP_DFFUSA_SIZE              0x1000
#define ADSP_ACE40_DSP_AONV_BASE                0x000A0000
#define ADSP_ACE40_DSP_AONV_SIZE                0x1000
#define ADSP_ACE40_DSP_I3CD_BASE                0x000F0000
#define ADSP_ACE40_DSP_I3CD_SIZE                0x2000
#define ADSP_ACE40_DSP_ML_BASE                  0x00100000
#define ADSP_ACE40_DSP_ML_SIZE                  0x2000
#define ADSP_ACE40_DSP_DINT_BASE(x)             (0x00091000 + (x) * 0x100)
#define ADSP_ACE40_DSP_DINT_SIZE                0x100
#define ADSP_ACE40_DSP_IDC_BASE(x)              (0x00092000 + (x) * 0x400)
#define ADSP_ACE40_DSP_IDC_SIZE                 0x400
#define ADSP_ACE40_DSP_HFINTIP_BASE             0x00094000
#define ADSP_ACE40_DSP_HFINTIP_SIZE             0x800       /* 2 cores × 0x400 */
#define ADSP_ACE40_DSP_DFDGP_BASE               0x00160000
#define ADSP_ACE40_DSP_DFDGP_SIZE               0x2000
#define ADSP_ACE40_DSP_HFIMR_BASE               0x000162000
#define ADSP_ACE40_DSP_HFIMR_SIZE               0x1000
#define ADSP_ACE40_DSP_DFL2HSBPM_BASE           0x00017A800
#define ADSP_ACE40_DSP_DFL2HSBPM_SIZE           0x1000
#define ADSP_ACE40_DSP_L2UCMRP_BASE             0x0017D600
#define ADSP_ACE40_DSP_L2UCMRP_SIZE             0x200
#define ADSP_ACE40_DSP_L2UCMM_BASE              0x0017D800
#define ADSP_ACE40_DSP_L2UCMM_SIZE              0x100
#define ADSP_ACE40_DSP_DFSHA_BASE               0x0017DF00
#define ADSP_ACE40_DSP_DFSHA_SIZE               0x100
#define ADSP_ACE40_DSP_L2HSTLB_BASE             0x00017E000
#define ADSP_ACE40_DSP_L2HSTLB_SIZE             0x2000
#define ADSP_ACE40_DSP_IMR_MAN_OFFSET           0x42000

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

static void ace_ipc_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
                               struct adsp_io_info *info)
{
    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
    adsp->ipc = info;
}

static struct adsp_mem_desc ace_ace4_mem[] = {
    { .name = "lp-sram", .base = ADSP_ACE40_DSP_LP_SRAM_BASE,
      .size = ADSP_ACE40_DSP_LP_SRAM_SIZE },
    { .name = "hp-sram", .base = ADSP_ACE40_DSP_HP_SRAM_BASE,
      .size = ADSP_ACE40_DSP_HP_SRAM_SIZE,
      .per_core_non_coherent = true },
    { .name = "imr", .base = ADSP_ACE40_DSP_IMR_BASE,
      .size = ADSP_ACE40_DSP_IMR_SIZE },
    { .name = "rom", .base = ADSP_ACE_DSP_ROM_BASE,
      .size = ADSP_ACE_DSP_ROM_SIZE },
};

static struct adsp_reg_space ace_40_io[] = {
    { .name = "shim-low", .init = adsp_ace_shim_init, .ops = &ace_shim_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFDSSGBL_LOW_BASE,
                .size = ADSP_ACE40_DSP_HFDSSGBL_LOW_SIZE }, },
    { .name = "ipc-low", .init = ace_ipc_block_init, .ops = &ace_ipc_ace3_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFINT_LOW_BASE,
                .size = ADSP_ACE40_DSP_HFINT_LOW_SIZE }, },
    { .name = "hfintip-low", .init = ace_hfintip_block_init,
      .ops = &ace_hfintip_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFINTIP_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_HFINTIP_BLOCK_SIZE }, },
    { .name = "timer-low", .init = ace_timer_init, .ops = &ace_timer_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFTTS_LOW_BASE,
                .size = ADSP_ACE40_DSP_HFTTS_LOW_SIZE }, },
    { .name = "socci-low", .init = ace_socci_block_init,
      .ops = &ace_socci_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_SOCCI_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_SOCCI_BLOCK_SIZE }, },
    { .name = "hfpmccu-low", .init = ace_hfpmccu_block_init,
      .ops = &ace_hfpmccu_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFPMCCU_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_HFPMCCU_BLOCK_SIZE }, },
    { .name = "hfpmcch-low", .init = ace_hfpmcch_block_init,
      .ops = &ace_hfpmcch_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFPMCCH_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_HFPMCCH_BLOCK_SIZE }, },
    { .name = "secpol-low", .init = ace_secpol_block_init,
      .ops = &ace_secpol_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_SECPOL_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_SECPOL_BLOCK_SIZE }, },
    { .name = "tsocfgu-low", .init = ace_tsocfgu_aon_block_init,
      .ops = &ace_tsocfgu_aon_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_TSOCFGU_AON_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_TSOCFGU_AON_BLOCK_SIZE }, },
    { .name = "dfcapsts-low", .init = ace_dfcapsts_block_init,
      .ops = &ace_dfcapsts_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_DFCAPSTS_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_DFCAPSTS_BLOCK_SIZE }, },
    { .name = "adcip-low", .init = ace_adcip_block_init,
      .ops = &ace_adcip_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_ADCIP_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_ADCIP_BLOCK_SIZE }, },
    { .name = "adcs-low", .init = ace_adcs_block_init,
      .ops = &ace_adcs_block_ops,
      .desc = { .base = ADSP_ACE40_DSP_ADCS_BLOCK_BASE,
                .size = ADSP_ACE40_DSP_ADCS_BLOCK_SIZE }, },
    { .name = "dmw", .init = ace_dmw_init, .ops = &ace_dmw_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DMW_BASE,
                .size = ADSP_ACE40_DSP_DMW_SIZE }, },
    { .name = "dtfc", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_DTFC_BASE,
                .size = ADSP_ACE40_DSP_DTFC_SIZE }, },
    { .name = "dfmicpvc", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_DFMICPVC_BASE,
                .size = ADSP_ACE40_DSP_DFMICPVC_SIZE }, },
    { .name = "dcr", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_DCR_BASE,
                .size = ADSP_ACE40_DSP_DCR_SIZE }, },
    { .name = "dfpmcch", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_DFPMCCH_BASE,
                .size = ADSP_ACE40_DSP_DFPMCCH_SIZE }, },
    { .name = "pmccu", .init = ace_pmccu_init, .ops = &ace_pmccu_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DFPMCCU_BASE,
                .size = ADSP_ACE40_DSP_DFPMCCU_SIZE }, },
    { .name = "DfL2MM", .init = ace30_dfl2mm_init, .ops = &ace30_dfl2mm_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DFL2MM_BASE,
                .size = ADSP_ACE40_DSP_DFL2MM_SIZE }, },
    { .name = "dfl2usbpm", .init = ace30_dfl2usbpm_init,
      .ops = &ace30_dfl2usbpm_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DFL2USBPM_BASE,
                .size = ADSP_ACE40_DSP_DFL2USBPM_SIZE }, },
    { .name = "timer", .init = ace_timer_init, .ops = &ace_timer_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFTTS_BASE,
                .size = ADSP_ACE40_DSP_HFTTS_SIZE }, },
    { .name = "gtw-hout", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
      .desc = { .base = ADSP_ACE40_DSP_GTW_HOST_OUT_STREAM_BASE(0),
                .size = ADSP_ACE40_DSP_GTW_HOST_OUT_STREAM_SIZE * 8 }, },
    { .name = "gtw-hin", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
      .desc = { .base = ADSP_ACE40_DSP_GTW_HOST_IN_STREAM_BASE(0),
                .size = ADSP_ACE40_DSP_GTW_HOST_IN_STREAM_SIZE * 8 }, },
    { .name = "gtw-rsvd-72e", .init = hda_dma_init_dev, .ops = &unmapped_ops,
      .desc = { .base = ADSP_ACE40_DSP_GTW_RSVD_72E_BASE,
                .size = ADSP_ACE40_DSP_GTW_RSVD_72E_SIZE }, },
    { .name = "hfipc0", .init = ace30_hfipc_init, .ops = &ace30_hfipc_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFIPC_BASE,
                .size = ADSP_ACE40_DSP_HFIPC_SIZE }, },
    { .name = "dspcs", .init = ace30_dspcs_init, .ops = &ace30_dspcs_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DSPCS_BASE,
                .size = ADSP_ACE40_DSP_DSPCS_SIZE }, },
    { .name = "gtw-lout", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
      .desc = { .base = ADSP_ACE40_DSP_GTW_LINK_OUT_STREAM_BASE(0),
                .size = ADSP_ACE40_DSP_GTW_LINK_OUT_STREAM_SIZE * 8 }, },
    { .name = "gtw-rsvd-796", .init = hda_dma_init_dev, .ops = &unmapped_ops,
      .desc = { .base = ADSP_ACE40_DSP_GTW_RSVD_796_BASE,
                .size = ADSP_ACE40_DSP_GTW_RSVD_796_SIZE }, },
    { .name = "gtw-lin", .init = hda_dma_init_dev, .ops = &ace_hda_stream_ops,
      .desc = { .base = ADSP_ACE40_DSP_GTW_LINK_IN_STREAM_BASE(0),
                .size = ADSP_ACE40_DSP_GTW_LINK_IN_STREAM_SIZE * 8 }, },
    { .name = "gtw-rsvd-79a", .init = hda_dma_init_dev, .ops = &unmapped_ops,
      .desc = { .base = ADSP_ACE40_DSP_GTW_RSVD_79A_BASE,
                .size = ADSP_ACE40_DSP_GTW_RSVD_79A_SIZE }, },
    { .name = "gpdma", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_GPDMA_BASE,
                .size = ADSP_ACE40_DSP_GPDMA_SIZE }, },
    { .name = "spi", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_SPI_BASE,
                .size = ADSP_ACE40_DSP_SPI_SIZE }, },
    { .name = "dffusa", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_DFFUSA_BASE,
                .size = ADSP_ACE40_DSP_DFFUSA_SIZE }, },
    { .name = "aonv", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_AONV_BASE,
                .size = ADSP_ACE40_DSP_AONV_SIZE }, },
    { .name = "i3cd", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_I3CD_BASE,
                .size = ADSP_ACE40_DSP_I3CD_SIZE }, },
    { .name = "ml", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_ML_BASE,
                .size = ADSP_ACE40_DSP_ML_SIZE }, },
    { .name = "dint0", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DINT_BASE(0),
                .size = ADSP_ACE40_DSP_DINT_SIZE }, },
    { .name = "dint1", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DINT_BASE(1),
                .size = ADSP_ACE40_DSP_DINT_SIZE }, },
    { .name = "idc0", .init = ace30_idc_init, .ops = &ace_idc_ops,
      .desc = { .base = ADSP_ACE40_DSP_IDC_BASE(0),
                .size = ADSP_ACE40_DSP_IDC_SIZE }, },
    { .name = "idc1", .init = ace30_idc_init, .ops = &ace_idc_ops,
      .desc = { .base = ADSP_ACE40_DSP_IDC_BASE(1),
                .size = ADSP_ACE40_DSP_IDC_SIZE }, },
    { .name = "irq", .init = ace30_dint_init, .ops = &ace30_dint_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFINTIP_BASE,
                .size = ADSP_ACE40_DSP_HFINTIP_SIZE }, },
    { .name = "dfdgp", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_DFDGP_BASE,
                .size = ADSP_ACE40_DSP_DFDGP_SIZE }, },
    { .name = "hfimr", .init = ace30_hfimr_init, .ops = &ace30_hfimr_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_HFIMR_BASE,
                .size = ADSP_ACE40_DSP_HFIMR_SIZE }, },
    { .name = "dfl2hsbpm", .init = ace30_dfl2hsbpm_init,
      .ops = &ace30_dfl2hsbpm_io_ops,
      .desc = { .base = ADSP_ACE40_DSP_DFL2HSBPM_BASE,
                .size = ADSP_ACE40_DSP_DFL2HSBPM_SIZE }, },
    { .name = "l2ucmrp", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_L2UCMRP_BASE,
                .size = ADSP_ACE40_DSP_L2UCMRP_SIZE }, },
    { .name = "l2ucmm", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_L2UCMM_BASE,
                .size = ADSP_ACE40_DSP_L2UCMM_SIZE }, },
    { .name = "dfsha", .init = ace_simple_io_init,
      .desc = { .base = ADSP_ACE40_DSP_DFSHA_BASE,
                .size = ADSP_ACE40_DSP_DFSHA_SIZE }, },
    { .name = "l2hstlb", .init = ace_tlb_mmio_init, .ops = &ace_tlb_ops,
      .desc = { .base = ADSP_ACE40_DSP_L2HSTLB_BASE,
                .size = ADSP_ACE40_DSP_L2HSTLB_SIZE }, },
};

static const struct adsp_desc ace_ace4_dsp_desc = {
    .name = "ace40",
    .ia_irq = IRQ_NUM_EXT_IA,
    .ext_timer_irq = IRQ_NUM_EXT_TIMER,
    .imr_boot_ldr_offset = ADSP_ACE40_DSP_IMR_MAN_OFFSET,
    .num_mem = ARRAY_SIZE(ace_ace4_mem),
    .mem_region = ace_ace4_mem,
    .num_io = ARRAY_SIZE(ace_40_io),
    .io_dev = ace_40_io,
    .mem_zones = {
        [SOF_FW_BLK_TYPE_IMR] = { .base = ADSP_ACE40_DSP_IMR_BASE, },
        [SOF_FW_BLK_TYPE_SRAM] = { .base = ADSP_ACE40_DSP_HP_SRAM_BASE, },
    },
 //   .ops = &ace_ace3_ops,
};

static void ace40_adsp_init(MachineState *machine)
{
    adsp_ace_init(&ace_ace4_dsp_desc, machine, &ace_io_ops, 0,
                  ADSP_ACE40_DSP_IMR_BASE, 38400);
}

static void xtensa_ace40_machine_init(MachineClass *mc)
{
    mc->desc = "ACE4.0 HiFi5";
    mc->is_default = false;
    mc->init = ace40_adsp_init;
    mc->max_cpus = 2;
    mc->default_cpus = 1;
    mc->default_cpu_type = XTENSA_CPU_TYPE_NAME("ace40");
    adsp_machine_class_add_options(mc);
}

DEFINE_MACHINE("adsp_ace40", xtensa_ace40_machine_init)
