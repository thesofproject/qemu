/* Virtualization support for ESAI.
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

#include "qemu/io-bridge.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/log.h"
#include "hw/ssi/esai.h"
#include "hw/adsp/imx8.h"

const struct adsp_reg_desc adsp_esai_map[ADSP_ESAI_REGS] = {
    {.name = "esai", .enable = LOG_ESAI,
        .offset = 0x00000000, .size = ADSP_IMX8_ESAI_SIZE},
};

static void esai_reset(void *opaque)
{
     struct adsp_io_info *info = opaque;
     struct adsp_reg_space *space = info->space;

     memset(info->region, 0, space->desc.size);
}

static uint64_t esai_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;
    struct adsp_esai *esai = info->private;

    log_read(esai->log, space, addr, size,
        info->region[addr >> 2]);

    return info->region[addr >> 2];
}

static void esai_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;
    struct adsp_esai *esai = info->private;

    log_write(esai->log, space, addr, val, size,
        info->region[addr >> 2]);

}

const MemoryRegionOps esai_ops = {
    .read = esai_read,
    .write = esai_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define MAX_ESAI     6
struct adsp_esai *_esai[MAX_ESAI] = {NULL, NULL, NULL, NULL, NULL, NULL};

struct adsp_esai *esai_get_port(int port)
{
    if (port >= 0  && port < MAX_ESAI)
        return _esai[port];

    // TODO find an alternative
    fprintf(stderr, "cant get ESAI port %d\n", port);
    return NULL;
}

void adsp_esai_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    struct adsp_esai *esai;

    esai = g_malloc(sizeof(*esai));

    esai->tx.level = 0;
    esai->rx.level = 0;
    sprintf(esai->name, "%s.io", info->space->name);

    esai->log = log_init(NULL, NULL);
    info->private = esai;
    esai_reset(info);
    _esai[info->io_dev] = esai;
}
