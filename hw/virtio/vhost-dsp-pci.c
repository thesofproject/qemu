/*
 * Vhost DSP PCI Bindings
 *
 * Copyright 2019 Intel, Inc.
 *
 * Authors:
 *  Guennadi Liakhovetski <guennadi.liakhovetski@linux.intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/virtio/vhost-dsp.h"
#include "qemu/module.h"

typedef struct VHostDspPCI VHostDspPCI;

/*
 * vhost-dsp-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VHOST_DSP_PCI "vhost-dsp-pci-base"
#define VHOST_DSP_PCI(obj) \
        OBJECT_CHECK(VHostDspPCI, (obj), TYPE_VHOST_DSP_PCI)

struct VHostDspPCI {
    VirtIOPCIProxy parent_obj;
    VHostDsp vdev;
};

/* vhost-dsp-pci */

static Property vhost_dsp_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_dsp_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostDspPCI *dev = VHOST_DSP_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void vhost_dsp_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = vhost_dsp_pci_realize;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->props = vhost_dsp_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
}

static void vhost_dsp_pci_instance_init(Object *obj)
{
    VHostDspPCI *dev = VHOST_DSP_PCI(obj);

    /* Instantiate a vhost DSP device */
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_DSP);
}

static const VirtioPCIDeviceTypeInfo vhost_dsp_pci_info = {
    .base_name             = TYPE_VHOST_DSP_PCI,
    .non_transitional_name = "vhost-dsp-pci",
    .instance_size = sizeof(VHostDspPCI),
    .instance_init = vhost_dsp_pci_instance_init,
    .class_init    = vhost_dsp_pci_class_init,
};

static void virtio_pci_vhost_register(void)
{
    virtio_pci_types_register(&vhost_dsp_pci_info);
}

type_init(virtio_pci_vhost_register)
