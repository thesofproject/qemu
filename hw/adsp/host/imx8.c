/* Core IA host support for Baytrail audio DSP.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/audio/adsp-host.h"
#include "hw/adsp/byt.h"
#include "hw/adsp/imx8.h"
#include "hw/adsp/log.h"
#include "hw/adsp/dsp/mbox.h"

extern const MemoryRegionOps adsp_imx8_host_shim_ops;
extern const MemoryRegionOps adsp_host_mbox_ops;
extern const MemoryRegionOps imx8_host_pci_ops;

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

static struct adsp_reg_space imx8_io[] = {
    /*{ .name = "mbox", .reg_count = ARRAY_SIZE(adsp_imx8_mbox_map),
       .reg = adsp_imx8_mbox_map, .init = &adsp_mbox_init, .ops = &mbox_io_ops,
       .desc = {.base = ADSP_IMX8_DSP_MAILBOX_BASE, .size = ADSP_MAILBOX_SIZE},},
*/
       };


static const struct adsp_desc imx8_board = {
    .num_mem = ARRAY_SIZE(imx8_mem),
    .mem_region = imx8_mem,

    .num_io = ARRAY_SIZE(imx8_io),
    .io_dev = imx8_io,
};

static void do_irq(struct adsp_host *adsp, struct qemu_io_msg *msg)
{
    uint32_t active;

    active = adsp->shim_io[SHIM_ISRX >> 2] & ~(adsp->shim_io[SHIM_IMRX >> 2]);

    log_text(adsp->log, LOG_IRQ_ACTIVE,
        "DSP IRQ: status %x mask %x active %x cmd %x\n",
        adsp->shim_io[SHIM_ISRX >> 2],
        adsp->shim_io[SHIM_IMRX >> 2], active,
        adsp->shim_io[SHIM_IPCD >> 2]);

    if (active) {
        pci_set_irq(&adsp->dev, 1);
    }
}

static void do_pm(struct qemu_io_msg *msg)
{
}

static int imx8_bridge_cb(void *data, struct qemu_io_msg *msg)
{
    struct adsp_host *adsp = (struct adsp_host *)data;

    log_text(adsp->log, LOG_MSGQ,
        "msg: id %d msg %d size %d type %d\n",
        msg->id, msg->msg, msg->size, msg->type);

    switch (msg->type) {
    case QEMU_IO_TYPE_REG:
        /* mostly handled by SHM, some exceptions */
        break;
    case QEMU_IO_TYPE_IRQ:
        do_irq(adsp, msg);
        break;
    case QEMU_IO_TYPE_PM:
        do_pm(msg);
        break;
    case QEMU_IO_TYPE_DMA:
        adsp_host_do_dma(adsp, msg);
        break;
    case QEMU_IO_TYPE_MEM:
    default:
        break;
    }
    return 0;
}

void adsp_imx8_host_init(struct adsp_host *adsp, const char *name)
{
    adsp->desc = &imx8_board;
    adsp->system_memory = get_system_memory();
    adsp->machine_opts = qemu_get_machine_opts();

    adsp->log = log_init(NULL);    /* TODO: add log name to cmd line */
    adsp->shm_idx = 0;

    adsp_create_host_memory_regions(adsp);
    adsp_create_host_io_devices(adsp, NULL);

    /* initialise bridge to x86 host driver */
    qemu_io_register_parent(name, &imx8_bridge_cb, (void*)adsp);
}

static void imx8_reset(DeviceState *dev)
{
}

static void imx8_instance_init(Object *obj)
{

}

static Property imx8_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void imx8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = adsp_imx8_pci_realize;
    k->exit = adsp_imx8_pci_exit;

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = SST_DEV_ID_BAYTRAIL;
    k->revision = 1;

    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel Audio DSP Baytrail";
    dc->reset = imx8_reset;
    dc->props = imx8_properties;
}

static const TypeInfo imx8_base_info = {
    .name          = ADSP_HOST_IMX8_NAME,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct adsp_host),
    .instance_init = imx8_instance_init,
    .class_init    = imx8_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void adsp_imx8_register_types(void)
{
    type_register_static(&imx8_base_info);
}

type_init(adsp_imx8_register_types)
