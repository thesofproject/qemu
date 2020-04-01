/*
 * Virtualization support for EDMA Engine.
 *
 * Copyright (C) 2016 Intel Corporation
 * Copyright 2020 NXP
 *
 * Author: Diana Cretu <diana.cretu@nxp.com>
 *
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
#include "qemu/thread.h"
#include "qemu/io-bridge.h"

#include "hw/pci/pci.h"
#include "hw/audio/adsp-dev.h"
#include "hw/audio/adsp-host.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/log.h"
#include "hw/ssi/ssp.h"
#include "hw/dma/edma.h"


const struct adsp_reg_desc adsp_edma_map[] = {
    {.name = "edma", .enable = LOG_DMA,
        .offset = 0x00000000, .size = 0x10000},
};

static inline void dma_sleep(long nsec)
{
    struct timespec req;

    req.tv_sec = 0;
    req.tv_nsec = nsec;

    if (nanosleep(&req, NULL) < 0) {
        fprintf(stderr, "failed to sleep %d\n", -errno);
    }

}

void edma_reset(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_edma *edma = info->private;
    const struct adsp_reg_space *edma_dev = edma->desc;

    memset(edma->io, 0, edma_dev->desc.size);
}

static uint64_t edma_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_edma *edma = info->private;
    const struct adsp_reg_space *edma_dev = edma->desc;

    /* only print IO from guest */
    log_read(edma->log, edma_dev, addr, size,
            edma->io[addr >> 2]);

    return edma->io[addr >> 2];
}

static void edma_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_edma *edma = info->private;
    const struct adsp_reg_space *edma_dev = edma->desc;

    log_write(edma->log, edma_dev, addr, val, size,
            edma->io[addr >> 2]);
}

const MemoryRegionOps edma_ops = {
    .read = edma_read,
    .write = edma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
