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
    struct qemu_io_msg_irq irq;
    uint64_t aux;

    log_write(adsp->log, space, addr, val, size,
        info->region[addr >> 2]);

    /* Interrupt arrived, check src */
    info->region[addr >> 2] = val;
    
    switch(addr) {
    case IMX_MU_xCR:

        /* send IRQ to parent */
        irq.hdr.type = QEMU_IO_TYPE_IRQ;
        irq.hdr.msg = QEMU_IO_MSG_IRQ;
        irq.hdr.size = sizeof(irq);
        irq.irq = 0;

        /* send a new message to host */
        if (val & IMX_MU_xCR_GIRn(1)) {
            /* activate pending bit on MU Side A */
            /* TODO: currently activates pending bit on MU Side B,
             * so find a way to write to MU Side A regs 
             * aux = info->region[IMX_MU_xSR >> 2] & ~0;
             * aux |= IMX_MU_xSR_GIPn(0);
             * info->region[IMX_MU_xSR >> 2] = aux;
             */
            qemu_io_send_msg(&irq.hdr);
        }

        /* send reply to host */
        if (val & IMX_MU_xCR_GIRn(0)) {
            /* TODO: currently activates pending bit on MU Side B */
            aux = info->region[IMX_MU_xSR >> 2] & ~0;
            aux |= IMX_MU_xSR_GIPn(1);
            info->region[IMX_MU_xSR >> 2] = aux;
            qemu_io_send_msg(&irq.hdr);
        }

        break;
    case IMX_MU_xSR:
        break;
    default:
        break;
    }
}

void adsp_imx8_irq_msg(struct adsp_dev *adsp, struct qemu_io_msg *msg)
{
    struct adsp_io_info *info = adsp->mu;
    uint64_t aux;

    aux = info->region[IMX_MU_xCR >> 2];

    /* reply arrived from host */
    if (aux & IMX_MU_xCR_GIRn(1)) {
        /* set pending bit for reply on DSP */
        aux = info->region[IMX_MU_xSR >> 2] & ~0;
        aux |= IMX_MU_xSR_GIPn(1);
        info->region[IMX_MU_xSR >> 2] = aux;	
    }
    else {
        /* new message arrived from host, so activate pending bit */
        aux = info->region[IMX_MU_xSR >> 2] & ~0;
        aux |= IMX_MU_xSR_GIPn(0);
        info->region[IMX_MU_xSR >> 2] = aux;
    }
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
    adsp->mu = info;    
}
