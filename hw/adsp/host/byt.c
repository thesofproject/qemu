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
#include "hw/adsp/log.h"

extern const MemoryRegionOps adsp_byt_host_shim_ops;
extern const MemoryRegionOps adsp_host_mbox_ops;
extern const MemoryRegionOps byt_host_pci_ops;

static struct adsp_mem_desc byt_mem[] = {
    {.name = "iram", .base = ADSP_BYT_HOST_IRAM_BASE,
        .size = ADSP_BYT_IRAM_SIZE},
    {.name = "dram", .base = ADSP_BYT_HOST_DRAM_BASE,
        .size = ADSP_BYT_DRAM_SIZE},
};

static struct adsp_reg_space byt_io[] = {
    { .name = "pci",
        .desc = {.base = ADSP_BYT_PCI_BASE, .size = ADSP_PCI_SIZE},
        .init = (void*)adsp_byt_init_pci,
        .ops = &byt_host_pci_ops,
    },
    { .name = "shim", .reg_count = ARRAY_SIZE(adsp_byt_shim_map),
        .reg = adsp_byt_shim_map,
        .desc = {.base = ADSP_BYT_HOST_SHIM_BASE, .size = ADSP_BYT_SHIM_SIZE},
        .init = (void*)adsp_byt_init_shim,
        .ops = &adsp_byt_host_shim_ops,
    },
    { .name = "mbox", .reg_count = ARRAY_SIZE(adsp_host_mbox_map),
        .reg = adsp_host_mbox_map,
        .desc = {.base = ADSP_BYT_HOST_MAILBOX_BASE, .size = ADSP_MAILBOX_SIZE},
        .init = (void*)adsp_host_init_mbox,
        .ops = &adsp_host_mbox_ops,
    }
};

static const struct adsp_desc byt_board = {
    .num_mem = ARRAY_SIZE(byt_mem),
    .mem_region = byt_mem,

    .num_io = ARRAY_SIZE(byt_io),
    .io_dev = byt_io,
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

static int byt_bridge_cb(void *data, struct qemu_io_msg *msg)
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

void adsp_byt_host_init(struct adsp_host *adsp, const char *name)
{
    adsp->desc = &byt_board;
    adsp->system_memory = get_system_memory();
    adsp->machine_opts = qemu_get_machine_opts();

    adsp->log = log_init(NULL);    /* TODO: add log name to cmd line */
    adsp->shm_idx = 0;

    adsp_create_host_memory_regions(adsp);
    adsp_create_host_io_devices(adsp, NULL);

    /* initialise bridge to x86 host driver */
    qemu_io_register_parent(name, &byt_bridge_cb, (void*)adsp);
}

static void byt_reset(DeviceState *dev)
{
}

static void byt_instance_init(Object *obj)
{

}

static Property byt_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void byt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = adsp_byt_pci_realize;
    k->exit = adsp_byt_pci_exit;

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = SST_DEV_ID_BAYTRAIL;
    k->revision = 1;

    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);

    dc->desc = "Intel Audio DSP Baytrail";
    dc->reset = byt_reset;
    dc->props = byt_properties;
}

static void cht_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = adsp_cht_pci_realize;
    k->exit = adsp_byt_pci_exit;

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = SST_DEV_ID_CHERRYTRAIL;
    k->revision = 1;

    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);

    dc->desc = "Intel Audio DSP Cherrytrail/Braswell";
    dc->reset = byt_reset;
    dc->props = byt_properties;
}

static const TypeInfo byt_base_info = {
    .name          = ADSP_HOST_BYT_NAME,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct adsp_host),
    .instance_init = byt_instance_init,
    .class_init    = byt_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const TypeInfo cht_base_info = {
    .name          = ADSP_HOST_CHT_NAME,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct adsp_host),
    .instance_init = byt_instance_init,
    .class_init    = cht_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void adsp_byt_register_types(void)
{
    type_register_static(&byt_base_info);
    type_register_static(&cht_base_info);
}

type_init(adsp_byt_register_types)
