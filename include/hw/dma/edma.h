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

#define NUM_CHANNELS                    32

#define EDMA_CH_CSR                     0x00
#define EDMA_CH_ES                      0x04
#define EDMA_CH_INT                     0x08
#define EDMA_CH_SBR                     0x0C
#define EDMA_CH_PRI                     0x10
#define EDMA_TCD_SADDR                  0x20
#define EDMA_TCD_SOFF                   0x24
#define EDMA_TCD_ATTR                   0x26
#define EDMA_TCD_NBYTES                 0x28
#define EDMA_TCD_SLAST                  0x2C
#define EDMA_TCD_DADDR                  0x30
#define EDMA_TCD_DOFF                   0x34
#define EDMA_TCD_CITER_ELINK            0x36
#define EDMA_TCD_CITER                  0x36
#define EDMA_TCD_DLAST_SGA              0x38
#define EDMA_TCD_CSR                    0x3C
#define EDMA_TCD_BITER_ELINK            0x3E
#define EDMA_TCD_BITER                  0x3E

#define EDMA_CSR                        0x00
#define EDMA_ES                         0x04
#define EDMA_INT                        0x08
#define EDMA_HRS                        0x0C

#define EDMA_TCD_ATTR_SSIZE_8BIT        0x0000
#define EDMA_TCD_ATTR_SSIZE_16BIT       0x0100
#define EDMA_TCD_ATTR_SSIZE_32BIT       0x0200
#define EDMA_TCD_ATTR_SSIZE_64BIT       0x0300
#define EDMA_TCD_ATTR_SSIZE_16BYTE      0x0400
#define EDMA_TCD_ATTR_SSIZE_32BYTE      0x0500
#define EDMA_TCD_ATTR_SSIZE_64BYTE      0x0600

#define EDMA_TCD_ATTR_DSIZE_8BIT        0x0000
#define EDMA_TCD_ATTR_DSIZE_16BIT       0x0001
#define EDMA_TCD_ATTR_DSIZE_32BIT       0x0002
#define EDMA_TCD_ATTR_DSIZE_64BIT       0x0003
#define EDMA_TCD_ATTR_DSIZE_16BYTE      0x0004
#define EDMA_TCD_ATTR_DSIZE_32BYTE      0x0005
#define EDMA_TCD_ATTR_DSIZE_64BYTE      0x0006

#define EDMA_BUFFER_PERIOD_COUNT        2

#define EDMA_TCD_ALIGNMENT              32

#define EDMA0_ESAI_CHAN_RX      6
#define EDMA0_ESAI_CHAN_TX      7
#define EDMA0_SAI_CHAN_RX       14
#define EDMA0_SAI_CHAN_TX       15
#define EDMA0_CHAN_MAX          32

#define EDMA0_ESAI_CHAN_RX_IRQ  442
#define EDMA0_ESAI_CHAN_TX_IRQ  442
#define EDMA0_SAI_CHAN_RX_IRQ   349
#define EDMA0_SAI_CHAN_TX_IRQ   349

/* context pointer used by timer callbacks */
struct dma_chan {
    struct adsp_edma *edma;
    int chan;

    /* buffer */
    uint32_t bytes;
    void *ptr;
    void *base;
    uint32_t tbytes;

    /* endpoint */
    struct qemu_io_msg_dma32 dma_msg;
    int sai;
    int esai;

    /* file output/input */
    int fd;
    int file_idx;

    /* threading */
    QemuThread thread;
    char thread_name[32];
    uint32_t stop;
};

struct adsp_edma {
    int id;
    int num_chan;
    uint32_t *io;
    int irq_assert;
    int irq;
    void (*do_irq)(struct adsp_edma *edma, int enable, uint32_t mask);
    struct adsp_log *log;

    struct adsp_io_info *info;
    const struct adsp_reg_space *desc;
    struct adsp_dev *adsp;
    struct dw_host *dw_host;
    struct dma_chan dma_chan[NUM_CHANNELS];
};

struct edma_desc {
    const char *name;    /* device name */
    int num_dmac;
    struct adsp_reg_space edma_dev[6];
};

struct edma_host {
    MemoryRegion *system_memory;
    qemu_irq irq;
    struct adsp_log *log;
    const struct dw_desc *desc;
};

#define ADSP_EDMA_REGS        1
extern const struct adsp_reg_desc adsp_edma_map[ADSP_EDMA_REGS];

extern const MemoryRegionOps edma_ops;

void edma_init_dev(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);
void edma_msg(struct qemu_io_msg *msg);
void edma_reset(void *opaque);

#endif
