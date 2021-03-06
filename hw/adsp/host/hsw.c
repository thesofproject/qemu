/* Core IA host support for Haswell audio DSP.
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

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/audio/adsp-host.h"
#include "hw/adsp/hsw.h"

static struct adsp_mem_desc hsw_mem[] = {
    {.name = "iram", .base = ADSP_HSW_HOST_IRAM_BASE,
        .size = ADSP_HSW_IRAM_SIZE},
    {.name = "dram", .base = ADSP_HSW_HOST_DRAM_BASE,
        .size = ADSP_HSW_DRAM_SIZE},
};

static struct adsp_reg_space hsw_io[] = {
    { .name = "pci",
        .desc = {.base = ADSP_HSW_PCI_BASE, .size = ADSP_PCI_SIZE},},
    { .name = "shim", .reg_count = ARRAY_SIZE(adsp_hsw_shim_map),
        .reg = adsp_hsw_shim_map,
        .desc = {.base = ADSP_HSW_DSP_SHIM_BASE, .size = ADSP_HSW_SHIM_SIZE},},
    { .name = "mbox", .reg_count = ARRAY_SIZE(adsp_host_mbox_map),
        .reg = adsp_host_mbox_map,
        .desc = {.base = ADSP_HSW_DSP_MAILBOX_BASE, .size = ADSP_MAILBOX_SIZE},},
};

static const struct adsp_desc hsw_board = {
    .num_mem = ARRAY_SIZE(hsw_mem),
    .mem_region = hsw_mem,

    .num_io = ARRAY_SIZE(hsw_io),
    .io_dev = hsw_io,
};

static struct adsp_mem_desc bdw_mem[] = {
    {.name = "iram", .base = ADSP_BDW_HOST_IRAM_BASE,
        .size = ADSP_BDW_IRAM_SIZE},
    {.name = "dram", .base = ADSP_BDW_HOST_DRAM_BASE,
        .size = ADSP_BDW_DRAM_SIZE},
};

static struct adsp_reg_space bdw_io[] = {
    { .name = "pci",
        .desc = {.base = ADSP_BDW_PCI_BASE, .size = ADSP_PCI_SIZE},},
    { .name = "shim", .reg_count = ARRAY_SIZE(adsp_hsw_shim_map),
        .reg = adsp_hsw_shim_map,
        .desc = {.base = ADSP_BDW_DSP_SHIM_BASE, .size = ADSP_HSW_SHIM_SIZE},},
    { .name = "mbox", .reg_count = ARRAY_SIZE(adsp_host_mbox_map),
        .reg = adsp_host_mbox_map,
        .desc = {.base = ADSP_BDW_DSP_MAILBOX_BASE, .size = ADSP_MAILBOX_SIZE},},
};

static const struct adsp_desc bdw_board = {
    .num_mem = ARRAY_SIZE(bdw_mem),
    .mem_region = bdw_mem,

    .num_io = ARRAY_SIZE(bdw_io),
    .io_dev = bdw_io,
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

static int hsw_bridge_cb(void *data, struct qemu_io_msg *msg)
{
    struct adsp_host *adsp = (struct adsp_host *)data;

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

void adsp_hsw_host_init(struct adsp_host *adsp, const char *name)
{
    adsp->desc = &hsw_board;
    adsp->system_memory = get_system_memory();
    adsp->machine_opts = qemu_get_machine_opts();
    adsp->shm_idx = 0;

    adsp->log = log_init(NULL, NULL);    /* TODO: add log name to cmd line */

    adsp_create_host_memory_regions(adsp);
    adsp_create_host_io_devices(adsp, NULL);

    /* initialise bridge to x86 host driver */
    qemu_io_register_parent(name, &hsw_bridge_cb, (void*)adsp);
}

void adsp_bdw_host_init(struct adsp_host *adsp, const char *name)
{
    adsp->desc = &bdw_board;
    adsp->system_memory = get_system_memory();
    adsp->machine_opts = qemu_get_machine_opts();
    adsp->shm_idx = 0;

    adsp->log = log_init(NULL, NULL);    /* TODO: add log name to cmd line */

    adsp_create_host_memory_regions(adsp);
    adsp_create_host_io_devices(adsp, NULL);

    /* initialise bridge to x86 host driver */
    qemu_io_register_parent(name, &hsw_bridge_cb, (void*)adsp);
}

static void hsw_reset(DeviceState *dev)
{
}

static Property hsw_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void hsw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = adsp_hsw_pci_realize;
    k->exit = adsp_hsw_pci_exit;

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = SST_DEV_ID_LYNX_POINT;
    k->revision = 1;

    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);

    dc->desc = "Intel Audio DSP Haswell";
    dc->reset = hsw_reset;
    dc->props = hsw_properties;
}

static void bdw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = adsp_bdw_pci_realize;
    k->exit = adsp_hsw_pci_exit;

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = SST_DEV_ID_WILDCAT_POINT;
    k->revision = 1;

    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);

    dc->desc = "Intel Audio DSP Broadwell";
    dc->reset = hsw_reset;
    dc->props = hsw_properties;
}

static void hsw_instance_init(Object *obj)
{

}

static const TypeInfo hsw_base_info = {
    .name          = ADSP_HOST_HSW_NAME,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct adsp_host),
    .instance_init = hsw_instance_init,
    .class_init    = hsw_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const TypeInfo bdw_base_info = {
    .name          = ADSP_HOST_BDW_NAME,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct adsp_host),
    .instance_init = hsw_instance_init,
    .class_init    = bdw_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void adsp_hsw_register_types(void)
{
    type_register_static(&hsw_base_info);
    type_register_static(&bdw_base_info);
}

type_init(adsp_hsw_register_types)
