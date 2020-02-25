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

/* i.MX8/i.MX8X */
#define ADSP_IMX8_DSP_MAILBOX_BASE      0x92C00000
#define ADSP_IMX8_HOST_IRAM_OFFSET      0x10000
#define ADSP_IMX8_HOST_IRAM_BASE        0x596f8000
#define ADSP_IMX8_HOST_DRAM_BASE        0x596e8000

/* Mailbox configuration */
#define ADSP_SRAM_OUTBOX_BASE       ADSP_IMX8_DSP_MAILBOX_BASE
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

#define ADSP_IMX8_DSP_MAILBOX_SIZE  (ADSP_SRAM_INBOX_SIZE \
                                    + ADSP_SRAM_OUTBOX_SIZE \
                                    + ADSP_SRAM_DEBUG_SIZE \
                                    + ADSP_SRAM_EXCEPT_SIZE \
                                    + ADSP_SRAM_STREAM_SIZE \
                                    + ADSP_SRAM_TRACE_SIZE)

#define ADSP_IMX8_EDMA0_BASE         0x59200000
#define ADSP_IMX8_EDMA0_SIZE         0x10000

#define ADSP_IMX8_DSP_IRAM_BASE      0x596f8000
#define ADSP_IMX8_DSP_DRAM_BASE      0x596e8000
#define ADSP_IMX8_IRAM_SIZE          0x8000
#define ADSP_IMX8_DRAM_SIZE          0x8000
#define ADSP_IMX8_DSP_SDRAM0_BASE     0x92400000
#define ADSP_IMX8_SDRAM0_SIZE     0x800000
#define ADSP_IMX8_SDRAM1_BASE     0x92C00000
#define ADSP_IMX8_SDRAM1_SIZE     0x800000

#define ADSP_IMX8_ESAI_BASE       0x59010000
#define ADSP_IMX8_ESAI_SIZE       0x00010000

#define ADSP_IMX8_SAI_1_BASE      0x59050000
#define ADSP_IMX8_SAI_1_SIZE      0x00010000

#define XSHAL_MU13_SIDEB_BYPASS_PADDR 0x5D310000
#define ADSP_IMX8_DSP_MU_BASE         XSHAL_MU13_SIDEB_BYPASS_PADDR
#define ADSP_IMX8_DSP_MU_SIZE         0x10000

#define ADSP_IMX8_DSP_IRQSTR_BASE  0x510A0000
#define ADSP_IMX8_DSP_IRQSTR_SIZE  0xCC

#define ADSP_IMX8X_DSP_IRQSTR_BASE  0x51080000

#endif
