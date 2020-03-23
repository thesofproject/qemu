/* Virtualization support for SAI for i.MX8,i.MX8X and i.MX8M.
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
#include "hw/ssi/sai.h"
#include "hw/adsp/imx8.h"

const struct adsp_reg_desc adsp_sai_map[ADSP_SAI_REGS] = {
    {.name = "sai", .enable = LOG_SAI,
        .offset = 0x00000000, .size = ADSP_IMX8_SAI_1_SIZE},
};

static void sai_reset(void *opaque)
{
     struct adsp_io_info *info = opaque;
     struct adsp_reg_space *space = info->space;

     memset(info->region, 0, space->desc.size);
}

static uint64_t sai_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;
    struct adsp_sai *sai = info->private;

    log_read(sai->log, space, addr, size,
        info->region[addr >> 2]);

    return info->region[addr >> 2];
}

static void sai_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_reg_space *space = info->space;
    struct adsp_sai *sai = info->private;

    log_write(sai->log, space, addr, val, size,
        info->region[addr >> 2]);

}

const MemoryRegionOps sai_ops = {
    .read = sai_read,
    .write = sai_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define MAX_SAI     6
struct adsp_sai *_sai[MAX_SAI] = {NULL, NULL, NULL, NULL, NULL, NULL};

struct adsp_sai *sai_get_port(int port)
{
    if (port >= 0  && port < MAX_SAI)
        return _sai[port];

    // TODO find an alternative
    fprintf(stderr, "cant get SAI port %d\n", port);
    return NULL;
}

void adsp_sai_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    struct adsp_sai *sai;

    sai = g_malloc(sizeof(*sai));

    sai->tx.level = 0;
    sai->rx.level = 0;
    sprintf(sai->name, "%s.io", info->space->name);

    sai->log = log_init(NULL, NULL);
    info->private = sai;
    sai_reset(info);
    _sai[info->io_dev] = sai;
}
