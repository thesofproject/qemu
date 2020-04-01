/*
 * Virtualization support for SDMA Engine.
 *
 * Copyright (C) 2020 NXP
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

#include "hw/audio/adsp-dev.h"
#include "hw/audio/adsp-host.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/log.h"
#include "hw/dma/sdma.h"

static void dsp_do_irq(struct adsp_sdma *sdma, int enable, uint32_t mask)
{
    struct adsp_io_info *info = sdma->info;

    if (enable) {
        adsp_irq_set(sdma->adsp, info, info->space->irq, mask);
    } else {
        adsp_irq_clear(sdma->adsp, info, info->space->irq, mask);
    }
}

void sdma_init_dev(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    struct adsp_sdma *sdma;
    char name[32];
    int j;

    sdma = g_malloc(sizeof(*sdma));
    sdma->adsp = adsp;
    sdma->id = info->io_dev;
    sdma->irq_assert = 0;
    sdma->do_irq = dsp_do_irq;
    sdma->log = log_init(NULL, NULL);
    sdma->desc = info->space;
    sdma->io = info->region;
    sdma->info = info;

    sprintf(name, "sdma%d.io", info->io_dev);

    /* channels */
    for (j = 0; j < NUM_CHANNELS; j++) {
        sdma->dma_chan[j].sdma = sdma;
        sdma->dma_chan[j].fd = 0;
        sdma->dma_chan[j].chan = j;
        sdma->dma_chan[j].file_idx = 0;
        sprintf(sdma->dma_chan[j].thread_name, "dmac:%d.%d", info->io_dev, j);
    }

    info->private = sdma;
}

