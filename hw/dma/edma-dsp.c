/*
 * Virtualization support for EDMA Engine.
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
#include "hw/dma/edma.h"

static void dsp_do_irq(struct adsp_edma *edma, int enable, uint32_t mask)
{
    struct adsp_io_info *info = edma->info;

    if (enable) {
        adsp_irq_set(edma->adsp, info, info->space->irq, mask);
    } else {
        adsp_irq_clear(edma->adsp, info, info->space->irq, mask);
    }
}

void edma_init_dev(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    struct adsp_edma *edma;
    char name[32];
    int j;

    edma = g_malloc(sizeof(*edma));
    edma->adsp = adsp;
    edma->id = info->io_dev;
    edma->irq_assert = 0;
    edma->do_irq = dsp_do_irq;
    edma->log = log_init(NULL, NULL);
    edma->desc = info->space;
    edma->io = info->region;
    edma->info = info;

    sprintf(name, "edma%d.io", info->io_dev);

    /* channels */
    for (j = 0; j < NUM_CHANNELS; j++) {
        edma->dma_chan[j].edma = edma;
        edma->dma_chan[j].fd = 0;
        edma->dma_chan[j].chan = j;
        edma->dma_chan[j].file_idx = 0;
        sprintf(edma->dma_chan[j].thread_name, "dmac:%d.%d", info->io_dev, j);
    }

    info->private = edma;
}

