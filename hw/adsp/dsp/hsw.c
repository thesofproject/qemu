/* Core DSP Haswell support for audio DSP.
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
#include "hw/adsp/hsw.h"
#include "hw/ssi/ssp.h"
#include "hw/dma/dw-dma.h"
#include "mbox.h"
#include "hsw.h"
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

    switch (msg->type) {
    case QEMU_IO_TYPE_REG:
        /* mostly handled by SHM, some exceptions */
        adsp_bdw_shim_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_IRQ:
        adsp_bdw_irq_msg(adsp, msg);
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

static void hsw_irq_set(struct adsp_io_info *info, int irq, uint32_t mask)
{
     struct adsp_dev *adsp = info->adsp;
     adsp_set_lvl1_irq(adsp, irq, 1);
}

static void hsw_irq_clear(struct adsp_io_info *info, int irq, uint32_t mask)
{
     struct adsp_dev *adsp = info->adsp;
     adsp_set_lvl1_irq(adsp, irq, 0);
}

struct adsp_dev_ops hsw_ops = {
    .irq_set = hsw_irq_set,
    .irq_clear = hsw_irq_clear,
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
    adsp->ops = &hsw_ops;

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
        printf(" ** %s Xtensa HiFi2 EP initalised.\n"
            " ** Waiting for host to load firmware...\n",
            board->name);
        return adsp;
    }

    /* load the binary image and copy to IRAM */
    ldata = g_malloc(ADSP_BDW_DSP_IRAM_BASE + ADSP_BDW_DSP_DRAM_BASE);
    lsize = load_image_size(adsp->kernel_filename, ldata,
        ADSP_BDW_DSP_IRAM_BASE + ADSP_BDW_DSP_DRAM_BASE);

    adsp_load_modules(adsp, ldata, lsize);

    return adsp;
}

static struct adsp_mem_desc hsw_mem[] = {
    {.name = "iram", .base = ADSP_HSW_DSP_IRAM_BASE,
        .size = ADSP_HSW_IRAM_SIZE},
    {.name = "dram", .base = ADSP_HSW_DSP_DRAM_BASE,
        .size = ADSP_HSW_DRAM_SIZE},
};

static struct adsp_reg_space hsw_io[] = {
    { .name = "dmac0", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC0,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_HSW_DMA0_BASE, .size = ADSP_HSW_DMA0_SIZE},},
    { .name = "dmac1", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC1,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_HSW_DMA1_BASE, .size = ADSP_HSW_DMA1_SIZE},},
    { .name = "ssp0", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP0,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_HSW_SSP0_BASE, .size = ADSP_HSW_SSP0_SIZE},},
    { .name = "ssp1", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP1,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_HSW_SSP1_BASE, .size = ADSP_HSW_SSP1_SIZE},},
    { .name = "shim", .reg_count = ARRAY_SIZE(adsp_hsw_shim_map),
        .reg = adsp_hsw_shim_map, .init = &adsp_bdw_shim_init, .ops = &hsw_shim_ops,
        .desc = {.base = ADSP_HSW_DSP_SHIM_BASE, .size = ADSP_HSW_SHIM_SIZE},},
};

/* hardware memory map - TODO: update MBOX from BDW */
static const struct adsp_desc hsw_dsp_desc = {
    .name = "Haswell",
    .ia_irq = 4,

    .num_mem = ARRAY_SIZE(hsw_mem),
    .mem_region = hsw_mem,

    .num_io = ARRAY_SIZE(hsw_io),
    .io_dev = hsw_io,

    .mem_zones = {
	    [SOF_FW_BLK_TYPE_IRAM] = {
		.base = ADSP_HSW_DSP_IRAM_BASE,
		.size = ADSP_HSW_IRAM_SIZE,
		.host_offset = ADSP_HSW_HOST_IRAM_OFFSET,
	    },
	    [SOF_FW_BLK_TYPE_DRAM] = {
		.base = ADSP_HSW_DSP_DRAM_BASE,
		.size = ADSP_HSW_DRAM_SIZE,
		.host_offset = ADSP_HSW_HOST_DRAM_OFFSET,
	    },
    },

};

static struct adsp_mem_desc bdw_mem[] = {
    {.name = "iram", .base = ADSP_BDW_DSP_IRAM_BASE,
        .size = ADSP_BDW_IRAM_SIZE},
    {.name = "dram", .base = ADSP_BDW_DSP_DRAM_BASE,
        .size = ADSP_BDW_DRAM_SIZE},
};

static struct adsp_reg_space bdw_io[] = {
    { .name = "dmac0", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC0,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_BDW_DMA0_BASE, .size = ADSP_HSW_DMA0_SIZE},},
    { .name = "dmac1", .reg_count = ARRAY_SIZE(adsp_gp_dma_map),
        .reg = adsp_gp_dma_map, .irq = IRQ_NUM_EXT_DMAC1,
        .init = &dw_dma_init_dev, .ops = &dw_dmac_ops,
        .desc = {.base = ADSP_BDW_DMA1_BASE, .size = ADSP_HSW_DMA1_SIZE},},
    { .name = "ssp0", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP0,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BDW_SSP0_BASE, .size = ADSP_HSW_SSP0_SIZE},},
    { .name = "ssp1", .reg_count = ARRAY_SIZE(adsp_ssp_map),
        .reg = adsp_ssp_map, .irq = IRQ_NUM_EXT_SSP1,
        .init = &adsp_ssp_init, .ops = &ssp_ops,
        .desc = {.base = ADSP_BDW_SSP1_BASE, .size = ADSP_HSW_SSP1_SIZE},},
    { .name = "shim", .reg_count = ARRAY_SIZE(adsp_hsw_shim_map),
        .reg = adsp_hsw_shim_map, .init = &adsp_bdw_shim_init, .ops = &hsw_shim_ops,
        .desc = {.base = ADSP_BDW_DSP_SHIM_BASE, .size = ADSP_HSW_SHIM_SIZE},},
};

/* hardware memory map - TODO: update MBOX from BDW */
static const struct adsp_desc bdw_dsp_desc = {
    .name = "Broadwell",
    .ia_irq = 4,

    .num_mem = ARRAY_SIZE(bdw_mem),
    .mem_region = bdw_mem,

    .num_io = ARRAY_SIZE(bdw_io),
    .io_dev = bdw_io,

    .mem_zones = {
	[SOF_FW_BLK_TYPE_IRAM] = {
		.base = ADSP_BDW_DSP_IRAM_BASE,
		.size = ADSP_BDW_IRAM_SIZE,
		.host_offset = ADSP_BDW_HOST_IRAM_OFFSET,
	},
	[SOF_FW_BLK_TYPE_DRAM] = {
		.base = ADSP_BDW_DSP_DRAM_BASE,
		.size = ADSP_BDW_DRAM_SIZE,
		.host_offset = ADSP_BDW_HOST_DRAM_OFFSET,
	},
    },

};

static void hsw_adsp_init(MachineState *machine)
{
    adsp_init(&hsw_dsp_desc, machine, "hsw");
}

static void bdw_adsp_init(MachineState *machine)
{
    adsp_init(&bdw_dsp_desc, machine, "bdw");
}

static void xtensa_bdw_machine_init(MachineClass *mc)
{
    mc->desc = "Broadwell HiFi2";
    mc->is_default = true;
    mc->init = bdw_adsp_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
}

DEFINE_MACHINE("adsp_bdw", xtensa_bdw_machine_init)

static void xtensa_hsw_machine_init(MachineClass *mc)
{
    mc->desc = "Haswell HiFi2";
    mc->is_default = true;
    mc->init = hsw_adsp_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
}

DEFINE_MACHINE("adsp_hsw", xtensa_hsw_machine_init)
