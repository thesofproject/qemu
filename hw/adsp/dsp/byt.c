/* Core DSP support for Baytrail audio DSP.
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
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/hw.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/io-bridge.h"

#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/log.h"
#include "hw/adsp/byt.h"
#include "hw/ssi/ssp.h"
#include "hw/dma/dw-dma.h"
#include "mbox.h"
#include "byt.h"
#include "common.h"

static void adsp_reset(void *opaque)
{

}

static void adsp_pm_msg(struct adsp_dev *adsp, struct qemu_io_msg *msg)
{
}

static int bridge_cb(void *data, struct qemu_io_msg *msg)
{
    struct adsp_dev *adsp = (struct adsp_dev *)data;

    log_text(adsp->log, LOG_MSGQ,
            "msg: id %d msg %d size %d type %d\n",
            msg->id, msg->msg, msg->size, msg->type);

    switch (msg->type) {
    case QEMU_IO_TYPE_REG:
        /* mostly handled by SHM, some exceptions */
        adsp_byt_shim_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_IRQ:
        adsp_byt_irq_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_PM:
        adsp_pm_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_DMA:
        dw_dma_msg(msg);
        break;
    case QEMU_IO_TYPE_MEM:
    default:
        break;
    }

    return 0;
}

static void byt_irq_set(struct adsp_io_info *info, int irq, uint32_t mask)
{
     struct adsp_dev *adsp = info->adsp;
     adsp_set_lvl1_irq(adsp, irq, 1);
}

static void byt_irq_clear(struct adsp_io_info *info, int irq, uint32_t mask)
{
     struct adsp_dev *adsp = info->adsp;
     adsp_set_lvl1_irq(adsp, irq, 0);
}

struct adsp_dev_ops byt_ops = {
    .irq_set = byt_irq_set,
    .irq_clear = byt_irq_clear,
};

static struct adsp_dev *adsp_init(const struct adsp_desc *board,
    MachineState *machine, const char *name)
{
    struct adsp_dev *adsp;
    uint8_t *ldata;
    size_t lsize;
    int n;

    adsp = g_malloc(sizeof(*adsp));
    adsp->log = log_init(NULL);    /* TODO: add log name to cmd line */
    adsp->desc = board;
    adsp->shm_idx = 0;
    adsp->system_memory = get_system_memory();
    adsp->machine_opts = qemu_get_machine_opts();
    adsp->cpu_model = machine->cpu_model;
    adsp->kernel_filename = qemu_opt_get(adsp->machine_opts, "kernel");
    adsp->ops = &byt_ops;

    /* initialise CPU */
    if (!adsp->cpu_model) {
        adsp->cpu_model = XTENSA_DEFAULT_CPU_MODEL;
    }

    for (n = 0; n < smp_cpus; n++) {

        adsp->xtensa[n] = g_malloc(sizeof(struct adsp_xtensa));
        adsp->xtensa[n]->cpu = XTENSA_CPU(cpu_create(machine->cpu_type));

        if (adsp->xtensa[n]->cpu == NULL) {
            error_report("unable to find CPU definition '%s'",
                adsp->cpu_model);
            exit(EXIT_FAILURE);
        }

        adsp->xtensa[n]->env = &adsp->xtensa[n]->cpu->env;
        adsp->xtensa[n]->env->sregs[PRID] = n;
        qemu_register_reset(adsp_reset, adsp->xtensa[n]->cpu);

        /* Need MMU initialized prior to ELF loading,
        * so that ELF gets loaded into virtual addresses
        */
        cpu_reset(CPU(adsp->xtensa[n]->cpu));
    }

    adsp_create_memory_regions(adsp);
    adsp_create_io_devices(adsp, NULL);

    /* reset all devices to init state */
    qemu_devices_reset();

    /* initialise bridge to x86 host driver */
    qemu_io_register_child(name, &bridge_cb, (void*)adsp);

    /* load binary file if one is specified on cmd line otherwise finish */
    if (adsp->kernel_filename == NULL) {
        printf(" ** Baytrail Xtensa HiFi2 EP DSP initialised.\n"
            " ** Waiting for host to load firmware...\n");
        return adsp;
    }

    /* load the binary image and copy to IRAM */
    ldata = g_malloc(ADSP_BYT_IRAM_SIZE + ADSP_BYT_DRAM_SIZE);
    lsize = load_image_size(adsp->kernel_filename, ldata,
         ADSP_BYT_IRAM_SIZE + ADSP_BYT_DRAM_SIZE);

    adsp_load_modules(adsp, ldata, lsize);

    return adsp;
}

static uint64_t io_read(void *opaque, hwaddr addr,
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
static void io_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    info->region[addr >> 2] = val;

    /* omit 0 writes as it fills mbox log */
    if (val == 0)
        return;

    log_write(adsp->log, space, addr, val, size,
         info->region[addr >> 2]);
}

static const MemoryRegionOps mbox_io_ops = {
    .read = io_read,
    .write = io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static struct adsp_mem_desc byt_mem[] = {
    {.name = "iram", .base = ADSP_BYT_DSP_IRAM_BASE,
        .size = ADSP_BYT_IRAM_SIZE},
    {.name = "dram", .base = ADSP_BYT_DSP_DRAM_BASE,
        .size = ADSP_BYT_DRAM_SIZE},
};

static struct adsp_reg_space byt_io[] = {
    { .name = "dmac0", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC0,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_BYT_DMA0_BASE, .size = ADSP_BYT_DMA0_SIZE},},
    { .name = "dmac1", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC1,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_BYT_DMA1_BASE, .size = ADSP_BYT_DMA1_SIZE},},
    { .name = "ssp0", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP0,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP0_BASE, .size = ADSP_BYT_SSP0_SIZE},},
    { .name = "ssp1", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP1,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP1_BASE, .size = ADSP_BYT_SSP1_SIZE},},
    { .name = "ssp2", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP2,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP2_BASE, .size = ADSP_BYT_SSP2_SIZE},},
    { .name = "shim", .reg_count = ARRAY_SIZE(adsp_byt_shim_map),
        .reg = adsp_byt_shim_map, .init = &adsp_byt_shim_init, .ops = &byt_shim_ops,
        .desc = {.base = ADSP_BYT_DSP_SHIM_BASE, .size = ADSP_BYT_SHIM_SIZE},},
    { .name = "mbox", .reg_count = ARRAY_SIZE(adsp_mbox_map),
        .reg = adsp_mbox_map, .init = &adsp_mbox_init, .ops = &mbox_io_ops,
        .desc = {.base = ADSP_BYT_DSP_MAILBOX_BASE, .size = ADSP_MAILBOX_SIZE},},
};

/* hardware memory map */
static const struct adsp_desc byt_dsp_desc = {
    .ia_irq = IRQ_NUM_EXT_IA,
    .ext_timer_irq = IRQ_NUM_EXT_TIMER,
    .pmc_irq = IRQ_NUM_EXT_PMC,

    .host_iram_offset = ADSP_BYT_HOST_IRAM_OFFSET,
    .host_dram_offset = ADSP_BYT_HOST_DRAM_OFFSET,

    .num_mem = ARRAY_SIZE(byt_mem),
    .mem_region = byt_mem,

    .num_io = ARRAY_SIZE(byt_io),
    .io_dev = byt_io,

   .iram_base = ADSP_BYT_DSP_IRAM_BASE,
   .dram_base = ADSP_BYT_DSP_DRAM_BASE,
};

static struct adsp_reg_space cht_io[] = {
    { .name = "dmac0", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC0,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_BYT_DMA0_BASE, .size = ADSP_BYT_DMA0_SIZE},},
    { .name = "dmac1", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC1,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_BYT_DMA1_BASE, .size = ADSP_BYT_DMA1_SIZE},},
    { .name = "dmac2", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC2,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_BYT_DMA2_BASE, .size = ADSP_BYT_DMA2_SIZE},},
    { .name = "ssp0", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP0,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP0_BASE, .size = ADSP_BYT_SSP0_SIZE},},
    { .name = "ssp1", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP1,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP1_BASE, .size = ADSP_BYT_SSP1_SIZE},},
    { .name = "ssp2", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP2,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP2_BASE, .size = ADSP_BYT_SSP2_SIZE},},
    { .name = "ssp3", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP0,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP3_BASE, .size = ADSP_BYT_SSP3_SIZE},},
    { .name = "ssp4", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP1,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP4_BASE, .size = ADSP_BYT_SSP4_SIZE},},
    { .name = "ssp5", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP2,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BYT_SSP5_BASE, .size = ADSP_BYT_SSP5_SIZE},},
    { .name = "shim", .reg_count = ARRAY_SIZE(adsp_byt_shim_map),
        .reg = adsp_byt_shim_map, .init = &adsp_byt_shim_init, .ops = &byt_shim_ops,
        .desc = {.base = ADSP_BYT_DSP_SHIM_BASE, .size = ADSP_BYT_SHIM_SIZE},},
    { .name = "mbox", .reg_count = ARRAY_SIZE(adsp_mbox_map),
        .reg = adsp_mbox_map, .ops = &mbox_io_ops,
        .desc = {.base = ADSP_BYT_DSP_MAILBOX_BASE, .size = ADSP_MAILBOX_SIZE},},
};

/* hardware memory map */
static const struct adsp_desc cht_dsp_desc = {
    .ia_irq = IRQ_NUM_EXT_IA,
    .ext_timer_irq = IRQ_NUM_EXT_TIMER,
    .pmc_irq = IRQ_NUM_EXT_PMC,

    .host_iram_offset = ADSP_BYT_HOST_IRAM_OFFSET,
    .host_dram_offset = ADSP_BYT_HOST_DRAM_OFFSET,

    .num_mem = ARRAY_SIZE(byt_mem),
    .mem_region = byt_mem,

    .num_io = ARRAY_SIZE(cht_io),
    .io_dev = cht_io,

   .iram_base = ADSP_BYT_DSP_IRAM_BASE,
   .dram_base = ADSP_BYT_DSP_DRAM_BASE,
};

static void byt_adsp_init(MachineState *machine)
{
    adsp_init(&byt_dsp_desc, machine, "byt");
}

static void cht_adsp_init(MachineState *machine)
{
    adsp_init(&cht_dsp_desc, machine, "cht");
}

static void xtensa_byt_machine_init(MachineClass *mc)
{
    mc->desc = "Baytrail HiFi2";
    mc->is_default = true;
    mc->init = byt_adsp_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
}

DEFINE_MACHINE("adsp_byt", xtensa_byt_machine_init)

static void xtensa_cht_machine_init(MachineClass *mc)
{
    mc->desc = "Cherrytrail HiFi2";
    mc->is_default = true;
    mc->init = cht_adsp_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
}

DEFINE_MACHINE("adsp_cht", xtensa_cht_machine_init)
