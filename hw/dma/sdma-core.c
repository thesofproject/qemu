/*
 * Virtualization support for DesignWare SDMA Engine.
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
#include "hw/dma/sdma.h"


const struct adsp_reg_desc adsp_sdma_map[] = {
    {.name = "sdma", .enable = LOG_DMA,
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

void sdma_reset(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_sdma *sdma = info->private;
    const struct adsp_reg_space *sdma_dev = sdma->desc;

    memset(sdma->io, 0, sdma_dev->desc.size);
}

static uint64_t sdma_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_sdma *sdma = info->private;
    const struct adsp_reg_space *sdma_dev = sdma->desc;

    /* only print IO from guest */
    log_read(sdma->log, sdma_dev, addr, size,
            sdma->io[addr >> 2]);

    return sdma->io[addr >> 2];
}

static void sdma_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_sdma *sdma = info->private;
    const struct adsp_reg_space *sdma_dev = sdma->desc;

    log_write(sdma->log, sdma_dev, addr, val, size,
            sdma->io[addr >> 2]);
}

const MemoryRegionOps sdma_ops = {
    .read = sdma_read,
    .write = sdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
