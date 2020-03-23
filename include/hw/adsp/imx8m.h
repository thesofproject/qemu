/* Core Audio DSP
 *
 * Copyright 2020 NXP
 *
 * Author: Diana Cretu <diana.cretu@nxp.com>
 * Author: Daniel Baluta <daniel.baluta@nxp.com>
 */

#ifndef __HW_ADSP_IMX8_H__
#define __HW_ADSP_IMX8_H__

#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "hw.h"

/* i.MX8M */
#define ADSP_IMX8M_DSP_IRAM_BASE            0x3b6f8000
#define ADSP_IMX8M_DSP_IRAM_HOST_OFFSET     0x10000
#define ADSP_IMX8M_DSP_IRAM_SIZE            0x8000
#define ADSP_IMX8M_DSP_DRAM0_BASE      0x3b6e8000
#define ADSP_IMX8M_DSP_DRAM0_SIZE      0x8000
#define ADSP_IMX8M_DSP_DRAM1_BASE      0x3b6f0000
#define ADSP_IMX8M_DSP_DRAM1_SIZE      0x8000
#define ADSP_IMX8M_DSP_SDRAM0_BASE    0x92400000
#define ADSP_IMX8M_DSP_SDRAM0_SIZE    0x800000
#define ADSP_IMX8M_DSP_SDRAM1_BASE    0x92C00000
#define ADSP_IMX8M_DSP_SDRAM1_SIZE    0x800000

#define ADSP_IMX8M_DSP_IRQSTR_BASE    0x30A80000
#define ADSP_IMX8M_DSP_IRQSTR_SIZE  0xCC

#define ADSP_IMX8M_DSP_SDMA_BASE    0x30E10000
#define ADSP_IMX8M_DSP_SDMA_SIZE    0x10000

#define XSHAL_MU2_SIDEB_BYPASS_PADDR 0x30E70000
#define ADSP_IMX8M_DSP_MU_BASE       XSHAL_MU2_SIDEB_BYPASS_PADDR
#define ADSP_IMX8M_DSP_MU_SIZE         0x10000

#define ADSP_IMX8M_SAI_3_BASE     0x30c30000
#define ADSP_IMX8M_SAI_3_SIZE     0x00010000

#define ADSP_IMX8M_DSP_MAILBOX_BASE      0x92C00000

/* Mailbox configuration */
#define ADSP_SRAM_OUTBOX_BASE       ADSP_IMX8M_DSP_MAILBOX_BASE
#define ADSP_SRAM_OUTBOX_SIZE       0x1000
#define ADSP_SRAM_OUTBOX_OFFSET     0

#define ADSP_SRAM_INBOX_BASE        (ADSP_SRAM_OUTBOX_BASE \
                                    + ADSP_SRAM_OUTBOX_SIZE)
#define ADSP_SRAM_INBOX_SIZE        0x1000
#define ADSP_SRAM_INBOX_OFFSET      ADSP_SRAM_OUTBOX_SIZE

#define ADSP_SRAM_DEBUG_BASE        (ADSP_SRAM_INBOX_BASE \
                                    + ADSP_SRAM_INBOX_SIZE)
#define ADSP_SRAM_DEBUG_SIZE        0x800
#define ADSP_SRAM_DEBUG_OFFSET      (ADSP_SRAM_INBOX_OFFSET \
                                    + ADSP_SRAM_INBOX_SIZE)

#define ADSP_SRAM_EXCEPT_BASE       (ADSP_SRAM_DEBUG_BASE \
                                    + ADSP_SRAM_DEBUG_SIZE)
#define ADSP_SRAM_EXCEPT_SIZE       0x800
#define ADSP_SRAM_EXCEPT_OFFSET     (ADSP_SRAM_DEBUG_OFFSET \
                                    + ADSP_SRAM_DEBUG_SIZE)

#define ADSP_SRAM_STREAM_BASE       (ADSP_SRAM_EXCEPT_BASE \
                                    + ADSP_SRAM_EXCEPT_SIZE)
#define ADSP_SRAM_STREAM_SIZE       0x1000
#define ADSP_SRAM_STREAM_OFFSET     (ADSP_SRAM_EXCEPT_OFFSET \
                                    + ADSP_SRAM_EXCEPT_SIZE)

#define ADSP_SRAM_TRACE_BASE        (ADSP_SRAM_STREAM_BASE \
                                    + ADSP_SRAM_STREAM_SIZE)
#define ADSP_SRAM_TRACE_SIZE        0x1000
#define ADSP_SRAM_TRACE_OFFSET      (ADSP_SRAM_STREAM_OFFSET \
                                    + ADSP_SRAM_STREAM_SIZE)

#define ADSP_IMX8M_DSP_MAILBOX_SIZE  (ADSP_SRAM_INBOX_SIZE \
                                    + ADSP_SRAM_OUTBOX_SIZE \
                                    + ADSP_SRAM_DEBUG_SIZE \
                                    + ADSP_SRAM_EXCEPT_SIZE \
                                    + ADSP_SRAM_STREAM_SIZE \
                                    + ADSP_SRAM_TRACE_SIZE)

#endif
