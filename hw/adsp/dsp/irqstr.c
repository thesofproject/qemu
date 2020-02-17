/*
 * IRQ Steer support for audio DSP
 *
 * Copyright 2020 NXP
 *
 * Author: Diana Cretu <diana.cretu@nxp.com>
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
#include "hw/adsp/mu.h"
#include "hw/adsp/log.h"
#include "imx8.h"
#include "common.h"
#include "irqstr.h"

const struct adsp_reg_desc adsp_imx8_irqstr_map[] = {
    {.name = "control", .enable = LOG_IRQSTR_CONTROL, .offset = 0x0},
    {.name = "mask", .enable = LOG_IRQSTR_MASK, .offset = 0x4},
    {.name = "set", .enable = LOG_IRQSTR_SET, .offset = 0x44},
    {.name = "status", .enable = LOG_IRQSTR_STATUS, .offset = 0x84},
    {.name = "master_disable", .enable = LOG_IRQSTR_MASTER_DISABLE,
        .offset = 0xC4},
    {.name = "master_status", .enable = LOG_IRQSTR_MASTER_STATUS,
        .offset = 0xC8},
};

static void irqstr_reset(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;
    memset(info->region, 0, space->desc.size);
}

/* IRQSTR IO from ADSP */
static uint64_t irqstr_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    log_read(adsp->log, space, addr, size,
        info->region[addr >> 2]);

    return info->region[addr >> 2];
}

static void irqstr_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    log_write(adsp->log, space, addr, val, size,
        info->region[addr >> 2]);

    /* set value via SHM */
    info->region[addr >> 2] = val;
}

const MemoryRegionOps irqstr_io_ops = {
    .read = irqstr_read,
    .write = irqstr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void adsp_imx8_irqstr_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    irqstr_reset(info);
}
