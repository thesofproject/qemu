/* Core common support for audio DSP mailbox.
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

#include "qemu/io-bridge.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/log.h"
#include "hw/adsp/imx8.h"
#include "mbox.h"

/* mailbox map - must be aligned with reef platform/mailbox.h */
const struct adsp_reg_desc adsp_mbox_map[ADSP_MBOX_AREAS] = {
    {.name = "outbox", .enable = LOG_MBOX_OUTBOX,
        .offset = 0x00000000, .size = 0x400},
    {.name = "inbox", .enable = LOG_MBOX_INBOX,
        .offset = 0x00000400, .size = 0x400},
    {.name = "exception", .enable = LOG_MBOX_EXCEPTION,
        .offset = 0x00000800, .size = 0x100},
    {.name = "debug", .enable = LOG_MBOX_DEBUG,
        .offset = 0x00000900, .size = 0x100},
    {.name = "stream", .enable = LOG_MBOX_STREAM,
        .offset = 0x00000a00, .size = 0x200},
    {.name = "trace", .enable = LOG_MBOX_TRACE,
        .offset = 0x00000c00, .size = 0x400},
};

/* mailbox map - must be aligned with SOF platform/mailbox.h */
/* TODO: unify adsp_imx8_mbox_map with adsp_mbox_map */
const struct adsp_reg_desc adsp_imx8_mbox_map[ADSP_MBOX_AREAS] = {
    {.name = "outbox", .enable = LOG_MBOX_OUTBOX,
        .offset = ADSP_SRAM_OUTBOX_OFFSET, .size = ADSP_SRAM_OUTBOX_SIZE},
    {.name = "inbox", .enable = LOG_MBOX_INBOX,
        .offset = ADSP_SRAM_INBOX_OFFSET, .size = ADSP_SRAM_INBOX_SIZE},
    {.name = "exception", .enable = LOG_MBOX_EXCEPTION,
        .offset = ADSP_SRAM_EXCEPT_OFFSET, .size = ADSP_SRAM_EXCEPT_SIZE},
    {.name = "debug", .enable = LOG_MBOX_DEBUG,
        .offset = ADSP_SRAM_DEBUG_OFFSET, .size = ADSP_SRAM_DEBUG_SIZE},
    {.name = "stream", .enable = LOG_MBOX_STREAM,
        .offset = ADSP_SRAM_STREAM_OFFSET, .size = ADSP_SRAM_TRACE_SIZE},
    {.name = "trace", .enable = LOG_MBOX_TRACE,
        .offset = ADSP_SRAM_TRACE_OFFSET, .size = ADSP_SRAM_TRACE_SIZE},
};

static void mbox_reset(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;

     memset(info->region, 0, space->desc.size);
}

static uint64_t adsp_mbox_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    log_area_read(adsp->log, space, addr, size,
        info->region[addr >> 2]);

    return info->region[addr >> 2];
}

static void adsp_mbox_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    log_area_write(adsp->log, space, addr, val, size,
                info->region[addr >> 2]);

    info->region[addr >> 2] = val;
}

const MemoryRegionOps adsp_mbox_ops = {
    .read = adsp_mbox_read,
    .write = adsp_mbox_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void adsp_mbox_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
     mbox_reset(info);
}
