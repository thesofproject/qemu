/* Core DSP i.MX8 support for audio DSP.
 *
 * Copyright (C) 2020 NXP
 *
 * Author: Diana Cretu  <diana.cretu@nxp.com>
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
#include "hw/adsp/mu.h"
#include "hw/adsp/log.h"
#include "imx8.h"
#include "common.h"

static void mu_reset(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;

    memset(info->region, 0, space->desc.size);
}

static uint64_t mu_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;


    log_read(adsp->log, space, addr, size,
        info->region[addr >> 2]);

    return info->region[addr >> 2];
}

/* SHIM IO from ADSP */
static void mu_write(void *opaque, hwaddr addr,
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

const MemoryRegionOps imx8_mu_ops = {
    .read = mu_read,
    .write = mu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void adsp_imx8_mu_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    mu_reset(info);
}
