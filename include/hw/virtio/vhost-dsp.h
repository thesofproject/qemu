/*
 * Vhost DSP virtio device
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

#ifndef QEMU_VHOST_DSP_H
#define QEMU_VHOST_DSP_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"

#define TYPE_VHOST_DSP "vhost-dsp-device"
#define VHOST_DSP(obj) \
        OBJECT_CHECK(VHostDsp, (obj), TYPE_VHOST_DSP)

typedef struct {
    char *vhostfd;
    char *topology;
} VHostDspConf;

typedef struct {
    /*< private >*/
    VirtIODevice parent;
    VHostDspConf conf;
    struct vhost_virtqueue vhost_vqs[3];
    struct vhost_dev vhost_dev;
    bool boot_complete;
//    VirtQueue *event_vq;
//    QEMUTimer *post_load_timer;
} VHostDsp;

#endif /* QEMU_VHOST_DSP_H */
