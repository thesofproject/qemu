/* Core DSP support for i.MX8 audio DSP
 *
 * Copyright 2020 NXP
 *
 * Author: Diana Cretu <diana.cretu@nxp.com>
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
#include "migration/vmstate.h"
#include "qemu/main-loop.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"

#include "hw/audio/adsp-dev.h"
#include "hw/adsp/log.h"
#include "hw/adsp/imx8.h"
#include "hw/adsp/dsp/mbox.h"
#include "imx8.h"
#include "common.h"
#include "hw/adsp/fw.h"
#include "irqstr.h"
#include "hw/dma/edma.h"
#include "hw/ssi/sai.h"
#include "hw/ssi/esai.h"

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
        break;
    case QEMU_IO_TYPE_IRQ:
        adsp_imx8_irq_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_PM:
        adsp_pm_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_DMA:
        break;
    case QEMU_IO_TYPE_MEM:
    default:
        break;
    }

    return 0;
}

static struct adsp_dev *adsp_init(const struct adsp_desc *board,
    MachineState *machine, const char *name)
{
    struct adsp_dev *adsp;
    uint8_t *ldata;
    size_t lsize;
    int n;

    adsp = g_malloc(sizeof(*adsp));
    adsp->log = log_init(NULL, NULL);    /* TODO: add log name to cmd line */
    adsp->desc = board;
    adsp->shm_idx = 0;
    adsp->system_memory = get_system_memory();
    adsp->machine_opts = qemu_get_machine_opts();
    adsp->kernel_filename = qemu_opt_get(adsp->machine_opts, "kernel");

    /* initialise CPU */
    if (!adsp->cpu_model) {
        adsp->cpu_model = XTENSA_DEFAULT_CPU_MODEL;
    }

    for (n = 0; n < machine->smp.cpus; n++) {

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
    qemu_io_register_child(name, &bridge_cb, (void *)adsp);

    /* load binary file if one is specified on cmd line otherwise finish */
    if (adsp->kernel_filename == NULL) {
        printf(" ** I.MX8 HiFi4 initialised.\n"
            " ** Waiting for host to load firmware...\n");
        return adsp;
    }
    /* load the binary image and copy to IRAM */
    ldata = g_malloc(ADSP_IMX8_IRAM_SIZE + ADSP_IMX8_SDRAM0_SIZE
        + ADSP_IMX8_SDRAM1_SIZE + ADSP_IMX8_DRAM_SIZE);
    lsize = load_image_size(adsp->kernel_filename, ldata,
        ADSP_IMX8_IRAM_SIZE + ADSP_IMX8_SDRAM0_SIZE
        + ADSP_IMX8_SDRAM1_SIZE + ADSP_IMX8_DRAM_SIZE);

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

/* MBOX IO from ADSP */
static void io_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    info->region[addr >> 2] = val;

    /* omit 0 writes as it fills mbox log */
    if (val == 0) {
        return;
    }

    log_write(adsp->log, space, addr, val, size,
         info->region[addr >> 2]);
}

static const MemoryRegionOps mbox_io_ops = {
    .read = io_read,
    .write = io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static struct adsp_mem_desc imx8_mem[] = {
    {.name = "iram", .base = ADSP_IMX8_DSP_IRAM_BASE,
        .size = ADSP_IMX8_IRAM_SIZE},
    {.name = "dram", .base = ADSP_IMX8_DSP_DRAM_BASE,
        .size = ADSP_IMX8_DRAM_SIZE},
    {.name = "sdram0", .base = ADSP_IMX8_DSP_SDRAM0_BASE,
        .size = ADSP_IMX8_SDRAM0_SIZE},
    {.name = "sdram1", .base = ADSP_IMX8_SDRAM1_BASE,
        .size = ADSP_IMX8_SDRAM1_SIZE},
};

static struct adsp_reg_space imx8x_io[] = {
    { .name = "mbox", .reg_count = ARRAY_SIZE(adsp_imx8_mbox_map),
        .reg = adsp_imx8_mbox_map, .init = &adsp_mbox_init, .ops = &mbox_io_ops,
        .desc = {.base = ADSP_IMX8_DSP_MAILBOX_BASE,
        .size = ADSP_IMX8_DSP_MAILBOX_SIZE},},
    { .name = "mu", .reg_count = ARRAY_SIZE(adsp_imx8_mu_map),
        .reg = adsp_imx8_mu_map, .init = &adsp_imx8_mu_init,
        .ops = &imx8_mu_ops,
        .desc = {.base = ADSP_IMX8_DSP_MU_BASE,
        .size = ADSP_IMX8_DSP_MU_SIZE},},
    { .name = "irqstr", .reg_count = ARRAY_SIZE(adsp_imx8_irqstr_map),
        .reg = adsp_imx8_irqstr_map, .init = &adsp_imx8_irqstr_init,
        .ops = &irqstr_io_ops,
        .desc = {.base = ADSP_IMX8X_DSP_IRQSTR_BASE,
        .size = ADSP_IMX8_DSP_IRQSTR_SIZE},},
    { .name = "edma", .reg_count = ARRAY_SIZE(adsp_edma_map),
        .reg = adsp_edma_map, .irq = EDMA_CH_INT,
        .init = &edma_init_dev, .ops = &edma_ops,
        .desc = {.base = ADSP_IMX8_EDMA0_BASE, .size = ADSP_IMX8_EDMA0_SIZE},},
    { .name = "sai", .reg_count = ARRAY_SIZE(adsp_sai_map),
        .reg = adsp_sai_map, .init = &adsp_sai_init,
        .ops = &sai_ops,
        .desc = {.base = ADSP_IMX8_SAI_1_BASE,
        .size = ADSP_IMX8_SAI_1_SIZE},},
    { .name = "esai", .reg_count = ARRAY_SIZE(adsp_esai_map),
        .reg = adsp_esai_map, .init = &adsp_esai_init,
        .ops = &esai_ops,
        .desc = {.base = ADSP_IMX8_ESAI_BASE,
        .size = ADSP_IMX8_ESAI_SIZE},},
};

/* hardware memory map */
static const struct adsp_desc imx8x_dsp_desc = {
    .num_mem = ARRAY_SIZE(imx8_mem),
    .mem_region = imx8_mem,

    .num_io = ARRAY_SIZE(imx8x_io),
    .io_dev = imx8x_io,

    .mem_zones = {
                [SOF_FW_BLK_TYPE_IRAM] = {
                        .base = ADSP_IMX8_DSP_IRAM_BASE,
                        .size = ADSP_IMX8_IRAM_SIZE,
                        .host_offset = ADSP_IMX8_HOST_IRAM_OFFSET,
                },
                [SOF_FW_BLK_TYPE_DRAM] = {
                        .base = ADSP_IMX8_DSP_DRAM_BASE,
                        .size = ADSP_IMX8_DRAM_SIZE,
                        .host_offset = 0,
                },
                [SOF_FW_BLK_TYPE_SRAM] = {
                        .base = ADSP_IMX8_DSP_SDRAM0_BASE,
                        .size = ADSP_IMX8_SDRAM0_SIZE + ADSP_IMX8_SDRAM1_SIZE,
                        .host_offset = 0,
                },
        },
};

static struct adsp_reg_space imx8_io[] = {
    { .name = "mbox", .reg_count = ARRAY_SIZE(adsp_imx8_mbox_map),
        .reg = adsp_imx8_mbox_map, .init = &adsp_mbox_init, .ops = &mbox_io_ops,
        .desc = {.base = ADSP_IMX8_DSP_MAILBOX_BASE,
        .size = ADSP_IMX8_DSP_MAILBOX_SIZE},},
    { .name = "mu_13a", .reg_count = ARRAY_SIZE(adsp_imx8_mu_map),
        .reg = adsp_imx8_mu_map, .init = &adsp_imx8_mu_init,
        .ops = &imx8_mu_ops,
        .desc = {.base = ADSP_IMX8_DSP_MU_BASE,
        .size = ADSP_IMX8_DSP_MU_SIZE},},
    { .name = "mu_13b", .reg_count = ARRAY_SIZE(adsp_imx8_mu_map),
        .reg = adsp_imx8_mu_map, .init = &adsp_imx8_mu_init,
        .ops = &imx8_mu_ops,
        .desc = {.base = ADSP_IMX8_DSP_MU_BASE,
        .size = ADSP_IMX8_DSP_MU_SIZE},},
    { .name = "irqstr", .reg_count = ARRAY_SIZE(adsp_imx8_irqstr_map),
        .reg = adsp_imx8_irqstr_map, .init = &adsp_imx8_irqstr_init,
        .ops = &irqstr_io_ops,
        .desc = {.base = ADSP_IMX8_DSP_IRQSTR_BASE,
        .size = ADSP_IMX8_DSP_IRQSTR_SIZE},},
    { .name = "edma", .reg_count = ARRAY_SIZE(adsp_edma_map),
        .reg = adsp_edma_map, .irq = EDMA_CH_INT,
        .init = &edma_init_dev, .ops = &edma_ops,
        .desc = {.base = ADSP_IMX8_EDMA0_BASE, .size = ADSP_IMX8_EDMA0_SIZE},},
    { .name = "sai", .reg_count = ARRAY_SIZE(adsp_sai_map),
        .reg = adsp_sai_map, .init = &adsp_sai_init,
        .ops = &sai_ops,
        .desc = {.base = ADSP_IMX8_SAI_1_BASE,
        .size = ADSP_IMX8_SAI_1_SIZE},},
    { .name = "esai", .reg_count = ARRAY_SIZE(adsp_esai_map),
        .reg = adsp_esai_map, .init = &adsp_esai_init,
        .ops = &esai_ops,
        .desc = {.base = ADSP_IMX8_ESAI_BASE,
        .size = ADSP_IMX8_ESAI_SIZE},},
};

/* hardware memory map */
static const struct adsp_desc imx8_dsp_desc = {
    .num_mem = ARRAY_SIZE(imx8_mem),
    .mem_region = imx8_mem,

    .num_io = ARRAY_SIZE(imx8_io),
    .io_dev = imx8_io,

    .mem_zones = {
                [SOF_FW_BLK_TYPE_IRAM] = {
                        .base = ADSP_IMX8_DSP_IRAM_BASE,
                        .size = ADSP_IMX8_IRAM_SIZE,
                        .host_offset = ADSP_IMX8_HOST_IRAM_OFFSET,
                },
                [SOF_FW_BLK_TYPE_DRAM] = {
                        .base = ADSP_IMX8_DSP_DRAM_BASE,
                        .size = ADSP_IMX8_DRAM_SIZE,
                        .host_offset = 0,
                },
                [SOF_FW_BLK_TYPE_SRAM] = {
                        .base = ADSP_IMX8_DSP_SDRAM0_BASE,
                        .size = ADSP_IMX8_SDRAM0_SIZE + ADSP_IMX8_SDRAM1_SIZE,
                        .host_offset = 0,
                },
        },
};

static void imx8x_adsp_init(MachineState *machine)
{
    adsp_init(&imx8x_dsp_desc, machine, "i.MX8X");
}

static void imx8_adsp_init(MachineState *machine)
{
    adsp_init(&imx8_dsp_desc, machine, "i.MX8");
}

static void xtensa_imx8x_machine_init(MachineClass *mc)
{
    mc->desc = "i.MX8X";
    mc->is_default = true;
    mc->init = imx8x_adsp_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
}

DEFINE_MACHINE("adsp_imx8x", xtensa_imx8x_machine_init)

static void xtensa_imx8_machine_init(MachineClass *mc)
{
    mc->desc = "i.MX8";
    mc->is_default = true;
    mc->init = imx8_adsp_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
}

DEFINE_MACHINE("adsp_imx8", xtensa_imx8_machine_init)
