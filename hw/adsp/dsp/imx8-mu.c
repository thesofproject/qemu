/* Core DSP SHIM support for Baytrail audio DSP.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
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
#include "byt.h"
#include "imx8.h"
#include "common.h"

#if 0
static void rearm_ext_timer(struct adsp_dev *adsp, struct adsp_io_info *info)
{
    uint32_t wake = info->region[SHIM_EXT_TIMER_CNTLL >> 2];

    info->region[SHIM_EXT_TIMER_STAT >> 2] =
        (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - adsp->timer[0].start) /
        (1000000 / adsp->timer[0].clk_kHz);

    timer_mod(adsp->timer[0].timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
            muldiv64(wake - info->region[SHIM_EXT_TIMER_STAT >> 2],
                1000000, adsp->timer[0].clk_kHz));
}


static void *pmc_work(void *data)
{
    struct adsp_io_info *info = data;
    struct adsp_dev *adsp = info->adsp;

    /* delay for PMC to do the work */
    usleep(50);

    /* perform any action after IRQ - ideally we should do this in thread*/
    switch (adsp->pmc_cmd) {
    case PMC_SET_LPECLK:

        /* set the clock bits */
        info->region[SHIM_CLKCTL >> 2] &= ~SHIM_FR_LAT_CLK_MASK;
        info->region[SHIM_CLKCTL >> 2] |=
            info->region[SHIM_FR_LAT_REQ >> 2] & SHIM_FR_LAT_CLK_MASK;

        /* tell the DSP clock has been updated */
        info->region[SHIM_CLKCTL >> 2] &= ~SHIM_CLKCTL_FRCHNGGO;
        info->region[SHIM_CLKCTL >> 2] |= SHIM_CLKCTL_FRCHNGACK;

        break;
    default:
        break;
    }

    log_text(adsp->log, LOG_IRQ_BUSY,
        "irq: SC send busy interrupt 0x%x\n", adsp->pmc_cmd);

    /* now send IRQ to DSP from SC completion */
    info->region[SHIM_IPCLPESCH >> 2] &= ~SHIM_IPCLPESCH_BUSY;
    info->region[SHIM_IPCLPESCH >> 2] |= SHIM_IPCLPESCH_DONE;

    info->region[SHIM_ISRLPESC >> 2] &= ~SHIM_ISRLPESC_BUSY;
    info->region[SHIM_ISRLPESC >> 2] |= SHIM_ISRLPESC_DONE;

    qemu_mutex_lock_iothread();
    adsp_set_lvl1_irq(adsp, adsp->desc->pmc_irq, 1);
    qemu_mutex_unlock_iothread();
    return NULL;
}

void imx8_ext_timer_cb(void *opaque)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    uint32_t pisr = info->region[SHIM_PISR >> 2];

    pisr |= SHIM_PISR_EXTT;
    info->region[SHIM_PISR >> 2] = pisr;
    adsp_set_lvl1_irq(adsp, adsp->desc->ext_timer_irq, 1);
}
#endif

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

#if 0
/* 32 bit SHIM IO from host */
static void do_shim32(struct adsp_dev *adsp, struct qemu_io_msg *msg)
{
    struct qemu_io_msg_reg32 *m = (struct qemu_io_msg_reg32 *)msg;

    switch (m->reg) {
    case SHIM_CSR:
        /* check for reset bit and stall bit */
        if (!adsp->in_reset && (m->val & SHIM_CSR_RST)) {

            log_text(adsp->log, LOG_CPU_RESET, "cpu: reset\n");

            cpu_reset(CPU(adsp->xtensa[0]->cpu));
            //vm_stop(RUN_STATE_SHUTDOWN); TODO: fix, causes hang
            adsp->in_reset = 1;

        } else if (adsp->in_reset && !(m->val & SHIM_CSR_STALL)) {

            log_text(adsp->log, LOG_CPU_RESET, "cpu: running\n");

            cpu_resume(CPU(adsp->xtensa[0]->cpu));
            vm_start();
            adsp->in_reset = 0;
        }
        break;
    default:
        break;
    }
}

/* 64 bit SHIM IO from host */
static void do_shim64(struct adsp_dev *adsp, struct qemu_io_msg *msg)
{
    struct qemu_io_msg_reg64 *m = (struct qemu_io_msg_reg64 *)msg;

    switch (m->reg) {
    case SHIM_CSR:
        /* check for reset bit and stall bit */
        if (!adsp->in_reset && (m->val & SHIM_CSR_RST)) {

            log_text(adsp->log, LOG_CPU_RESET, "cpu: reset\n");

            cpu_reset(CPU(adsp->xtensa[0]->cpu));
            //vm_stop(RUN_STATE_SHUTDOWN); TODO: fix, causes hang
            adsp->in_reset = 1;

        } else if (adsp->in_reset && !(m->val & SHIM_CSR_STALL)) {

            log_text(adsp->log, LOG_CPU_RESET, "cpu: running\n");

            cpu_resume(CPU(adsp->xtensa[0]->cpu));
            vm_start();
            adsp->in_reset = 0;
        }
        break;
    default:
        break;
    }
}

void adsp_imx8_mu_msg(struct adsp_dev *adsp, struct qemu_io_msg *msg)
{
    /*switch (msg->msg) {
    case QEMU_IO_MSG_REG32W:
        do_shim32(adsp, msg);
        break;
    case QEMU_IO_MSG_REG32R:
        break;
    case QEMU_IO_MSG_REG64W:
        do_shim64(adsp, msg);
        break;
    case QEMU_IO_MSG_REG64R:
        break;
    default:
        fprintf(stderr, "unknown register msg %d\n", msg->msg);
        break;
    }*/
}

void adsp_imx8_irq_msg(struct adsp_dev *adsp, struct qemu_io_msg *msg)
{
    struct adsp_io_info *info = adsp->shim;
    uint32_t active;

    active = info->region[SHIM_ISRD >> 2] & ~(info->region[SHIM_IMRD >> 2]);

    log_text(adsp->log, LOG_IRQ_ACTIVE,
        "IRQ: from HOST status %x mask %x active %x cmd %x\n",
        info->region[SHIM_ISRD >> 2],
        info->region[SHIM_IMRD >> 2], active,
        info->region[SHIM_IPCX >> 2]);

    if (active) {
        qemu_mutex_lock_iothread();
        adsp_set_lvl1_irq(adsp, adsp->desc->ia_irq, 1);
        qemu_mutex_unlock_iothread();
    }
}
#endif

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
