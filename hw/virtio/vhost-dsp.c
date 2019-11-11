/*
 * Virtio DSP device
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
#include <sys/ioctl.h>
#include "standard-headers/linux/virtio_ids.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "qemu/error-report.h"
#include "hw/virtio/vhost-dsp.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "monitor/monitor.h"

#include "linux/vhost.h"

#define VHOST_DSP_SAVEVM_VERSION 0
#define VHOST_DSP_QUEUE_SIZE 128

static void vhost_dsp_start(VirtIODevice *vdev)
{
    VHostDsp *dsp = VHOST_DSP(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    unsigned int i;
    int ret;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    /* Delegate all vqueues to vhost */
    ret = vhost_dev_enable_notifiers(&dsp->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, dsp->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    dsp->vhost_dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&dsp->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * Tell the vhost driver, that a guest is booting: send a VHOST_SET_RUNNING
     * ioctl
     */
    if (dsp->vhost_dev.vhost_ops->vhost_set_running)
        dsp->vhost_dev.vhost_ops->vhost_set_running(&dsp->vhost_dev, 1);

    /* Supply the SOF topology file name to the SOF driver on the host */
    if (dsp->conf.topology) {
        if (strlen(dsp->conf.topology) > NAME_MAX) {
            error_report("Topology name %s too long!",
                         dsp->conf.topology);
        } else {
            struct vhost_dsp_topology tplg;

            strcpy(tplg.name, dsp->conf.topology);
            ret = ioctl((uintptr_t)dsp->vhost_dev.opaque, VHOST_DSP_SET_GUEST_TPLG,
                        &tplg);
            if (ret < 0)
                error_report("Failed to set topology %s: %d",
                             dsp->conf.topology, -errno);
        }
    }

    /*
     * guest_notifier_mask/pending not used so far, just unmask everything here.
     * virtio-pci will do the right thing by enabling/disabling irqfd.
     */
    for (i = 0; i < dsp->vhost_dev.nvqs; i++)
        vhost_virtqueue_mask(&dsp->vhost_dev, vdev, i, false);

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, dsp->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&dsp->vhost_dev, vdev);
}

static void vhost_dsp_stop(VirtIODevice *vdev)
{
    VHostDsp *dsp = VHOST_DSP(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers)
        return;

    vhost_dev_stop(&dsp->vhost_dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, dsp->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&dsp->vhost_dev, vdev);
}

static void vhost_dsp_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostDsp *dsp = VHOST_DSP(vdev);
    bool should_start = (status & VIRTIO_CONFIG_S_DRIVER_OK) &&
        vdev->vm_running;

    /*
     * Status
     * 0xf == VIRTIO_CONFIG_S_FEATURES_OK | VIRTIO_CONFIG_S_DRIVER_OK |
     * VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_ACKNOWLEDGE
     * is only set once late during the start up process, hence it's suitable as
     * a boot completion indicator
     */
    if (status == 0xf)
        dsp->boot_complete = true;

    if (dsp->vhost_dev.started == should_start)
        return;

    if (should_start)
        vhost_dsp_start(vdev);
    else
        vhost_dsp_stop(vdev);
}

static void vhost_dsp_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostDsp *dsp = VHOST_DSP(dev);
    unsigned int i;
    int vhostfd, ret;

    if (dsp->conf.vhostfd) {
        vhostfd = monitor_fd_param(cur_mon, dsp->conf.vhostfd, errp);
        if (vhostfd == -1) {
            error_prepend(errp, "vhost-dsp: unable to parse vhostfd: ");
            return;
        }
    } else {
        vhostfd = open("/dev/vhost-dsp", O_RDWR);
        if (vhostfd < 0) {
            error_setg_errno(errp, errno,
                             "vhost-dsp: failed to open vhost device");
            return;
        }
    }

    /* Initialise the vdev object */
    virtio_init(vdev, "vhost-dsp", VIRTIO_ID_DSP, 0);

    /* All queues belong to the vhost */
    for (i = 0; i < ARRAY_SIZE(dsp->vhost_vqs); i++)
        virtio_add_queue(vdev, VHOST_DSP_QUEUE_SIZE, NULL);

    dsp->vhost_dev.nvqs = ARRAY_SIZE(dsp->vhost_vqs);
    dsp->vhost_dev.vqs = dsp->vhost_vqs;
    /* Initialise the vhost device for the kernel backend */
    ret = vhost_dev_init(&dsp->vhost_dev, (void *)(uintptr_t)vhostfd,
                         VHOST_BACKEND_TYPE_KERNEL, 0);
    if (!ret)
        return;

    error_setg_errno(errp, -ret, "vhost-dsp: vhost_dev_init failed");

    virtio_cleanup(vdev);
    close(vhostfd);
}

static void vhost_dsp_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostDsp *dsp = VHOST_DSP(dev);

    /* This will stop vhost backend if appropriate. */
    vhost_dsp_set_status(vdev, 0);

    vhost_dev_cleanup(&dsp->vhost_dev);
    virtio_cleanup(vdev);
}

static uint64_t vhost_dsp_get_features(VirtIODevice *vdev,
                                         uint64_t requested_features,
                                         Error **errp)
{
    /* No feature bits used yet */
    return requested_features;
}

static void vhost_dsp_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                            bool mask)
{
    VHostDsp *dsp = VHOST_DSP(vdev);

    vhost_virtqueue_mask(&dsp->vhost_dev, vdev, idx, mask);
}

static bool vhost_dsp_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostDsp *dsp = VHOST_DSP(vdev);

    return vhost_virtqueue_pending(&dsp->vhost_dev, idx);
}

static void vhost_dsp_reset(VirtIODevice *vdev)
{
    VHostDsp *dsp = VHOST_DSP(vdev);

    if (dsp->boot_complete) {
        /* Guest has rebooted */
        dsp->boot_complete = false;
        if (dsp->vhost_dev.vhost_ops->vhost_set_running)
            dsp->vhost_dev.vhost_ops->vhost_set_running(&dsp->vhost_dev, 0);
    }
}

static const VMStateDescription vmstate_virtio_vhost_dsp = {
    .name = "virtio-vhost_dsp",
    .minimum_version_id = VHOST_DSP_SAVEVM_VERSION,
    .version_id = VHOST_DSP_SAVEVM_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_dsp_properties[] = {
    DEFINE_PROP_STRING("vhostfd", VHostDsp, conf.vhostfd),
    DEFINE_PROP_STRING("topology", VHostDsp, conf.topology),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_dsp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_dsp_properties;
    dc->vmsd = &vmstate_virtio_vhost_dsp;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    vdc->realize = vhost_dsp_device_realize;
    vdc->unrealize = vhost_dsp_device_unrealize;
    vdc->get_features = vhost_dsp_get_features;
    vdc->set_status = vhost_dsp_set_status;
    vdc->reset = vhost_dsp_reset;
    vdc->guest_notifier_mask = vhost_dsp_guest_notifier_mask;
    vdc->guest_notifier_pending = vhost_dsp_guest_notifier_pending;
}

static const TypeInfo vhost_dsp_info = {
    .name = TYPE_VHOST_DSP,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostDsp),
    .class_init = vhost_dsp_class_init,
};

static void vhost_dsp_register_types(void)
{
    type_register_static(&vhost_dsp_info);
}

type_init(vhost_dsp_register_types)
