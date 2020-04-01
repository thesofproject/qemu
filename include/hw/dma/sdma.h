/*
 * Core Audio DSP
 *
 * Copyright (C) 2016 Intel Corporation
 * Copyright 2020 NXP
 *
 * Author: Diana Cretu <diana.cretu@nxp.com>
 *
 */

#ifndef __EDMA_H__
#define __EDMA_H__

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/acpi/aml-build.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/pci/pci.h"
#include "elf.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/thread.h"
#include "qemu/io-bridge.h"
#include "hw/adsp/hw.h"

struct adsp_dev;
struct adsp_host;
struct adsp_edma;
struct adsp_log;
struct adsp_io_info;

#define NUM_CHANNELS                  32

/* context pointer used by timer callbacks */
struct dma_chan {
    struct adsp_sdma *sdma;
    int chan;

    /* buffer */
    uint32_t bytes;
    void *ptr;
    void *base;
    uint32_t tbytes;

    /* endpoint */
    struct qemu_io_msg_dma32 dma_msg;
    int sai;

    /* file output/input */
    int fd;
    int file_idx;

    /* threading */
    QemuThread thread;
    char thread_name[32];
    uint32_t stop;
};

struct adsp_sdma {
    int id;
    int num_chan;
    uint32_t *io;
    int irq_assert;
    int irq;
    void (*do_irq)(struct adsp_sdma *sdma, int enable, uint32_t mask);
    struct adsp_log *log;

    struct adsp_io_info *info;
    const struct adsp_reg_space *desc;
    struct adsp_dev *adsp;
    struct sdma_host *sdma_host;
    struct dma_chan dma_chan[NUM_CHANNELS];
};

struct sdma_desc {
    const char *name;    /* device name */
    int num_dmac;
    struct adsp_reg_space sdma_dev[6];
};

struct sdma_host {
    MemoryRegion *system_memory;
    qemu_irq irq;
    struct adsp_log *log;
    const struct sdma_desc *desc;
};

#define ADSP_SDMA_REGS        1
extern const struct adsp_reg_desc adsp_sdma_map[ADSP_SDMA_REGS];

extern const MemoryRegionOps sdma_ops;

void sdma_init_dev(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);
void sdma_msg(struct qemu_io_msg *msg);
void sdma_reset(void *opaque);

#endif
