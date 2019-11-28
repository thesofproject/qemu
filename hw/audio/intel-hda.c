/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * written by Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "hw/audio/adsp-host.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/audio/soundhw.h"
#include "hw/audio/intel-hda.h"
#include "migration/vmstate.h"
#include "hw/audio/intel-hda-defs.h"
#include "sysemu/dma.h"
#include "qapi/error.h"

/* --------------------------------------------------------------------- */
/* hda bus                                                               */

static Property hda_props[] = {
    DEFINE_PROP_UINT32("cad", HDACodecDevice, cad, -1),
    DEFINE_PROP_END_OF_LIST()
};

static const TypeInfo hda_codec_bus_info = {
    .name = TYPE_HDA_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(HDACodecBus),
};

void hda_codec_bus_init(DeviceState *dev, HDACodecBus *bus, size_t bus_size,
                        hda_codec_response_func response,
                        hda_codec_xfer_func xfer)
{
    qbus_create_inplace(bus, bus_size, TYPE_HDA_BUS, dev, NULL);
    bus->response = response;
    bus->xfer = xfer;
}

static void hda_codec_dev_realize(DeviceState *qdev, Error **errp)
{
    HDACodecBus *bus = HDA_BUS(qdev->parent_bus);
    HDACodecDevice *dev = HDA_CODEC_DEVICE(qdev);
    HDACodecDeviceClass *cdc = HDA_CODEC_DEVICE_GET_CLASS(dev);

    if (dev->cad == -1) {
        dev->cad = bus->next_cad;
    }
    if (dev->cad >= 15) {
        error_setg(errp, "HDA audio codec address is full");
        return;
    }
    bus->next_cad = dev->cad + 1;
    if (cdc->init(dev) != 0) {
        error_setg(errp, "HDA audio init failed");
    }
}

static void hda_codec_dev_unrealize(DeviceState *qdev, Error **errp)
{
    HDACodecDevice *dev = HDA_CODEC_DEVICE(qdev);
    HDACodecDeviceClass *cdc = HDA_CODEC_DEVICE_GET_CLASS(dev);

    if (cdc->exit) {
        cdc->exit(dev);
    }
}

HDACodecDevice *hda_codec_find(HDACodecBus *bus, uint32_t cad)
{
    BusChild *kid;
    HDACodecDevice *cdev;

    QTAILQ_FOREACH(kid, &bus->qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        cdev = HDA_CODEC_DEVICE(qdev);
        if (cdev->cad == cad) {
            return cdev;
        }
    }
    return NULL;
}

void hda_codec_response(HDACodecDevice *dev, bool solicited, uint32_t response)
{
    HDACodecBus *bus = HDA_BUS(dev->qdev.parent_bus);
    bus->response(dev, solicited, response);
}

bool hda_codec_xfer(HDACodecDevice *dev, uint32_t stnr, bool output,
                    uint8_t *buf, uint32_t len)
{
    HDACodecBus *bus = HDA_BUS(dev->qdev.parent_bus);
    return bus->xfer(dev, stnr, output, buf, len);
}

/* --------------------------------------------------------------------- */
/* intel hda emulation                                                   */

#define TYPE_INTEL_HDA_GENERIC "intel-hda-generic"

#define INTEL_HDA(obj) \
    OBJECT_CHECK(IntelHDAState, (obj), TYPE_INTEL_HDA_GENERIC)

struct IntelHDAReg {
    const char *name;      /* register name */
    uint32_t   size;       /* size in bytes */
    uint32_t   reset;      /* reset value */
    uint32_t   wmask;      /* write mask */
    uint32_t   wclear;     /* write 1 to clear bits */
    uint32_t   offset;     /* location in IntelHDAState */
    uint32_t   shift;      /* byte access entries for dwords */
    uint32_t   stream;
    void       (*whandler)(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old);
    void       (*rhandler)(IntelHDAState *d, const IntelHDAReg *reg);
};

static void intel_hda_reset(DeviceState *dev);

/* --------------------------------------------------------------------- */

static hwaddr intel_hda_addr(uint32_t lbase, uint32_t ubase)
{
    return ((uint64_t)ubase << 32) | lbase;
}

static void intel_hda_update_int_sts(IntelHDAState *d)
{
    uint32_t sts = 0;
    uint32_t i;

    /* update controller status */
    if (d->rirb_sts & ICH6_RBSTS_IRQ) {
        sts |= (1 << 30);
    }
    if (d->rirb_sts & ICH6_RBSTS_OVERRUN) {
        sts |= (1 << 30);
    }
    if (d->state_sts & d->wake_en) {
        sts |= (1 << 30);
    }

    /* update stream status */
    for (i = 0; i < 8; i++) {
        /* buffer completion interrupt */
        if (d->st[i].ctl & (1 << 26)) {
            sts |= (1 << i);
        }
    }

    /* update global status */
    if (sts & d->int_ctl) {
        sts |= (1U << 31);
    }

    d->int_sts = sts;
}

static void intel_hda_update_irq(IntelHDAState *d)
{
    bool msi = msi_enabled(&d->pci);
    int level;

    intel_hda_update_int_sts(d);
    if (d->int_sts & (1U << 31) && d->int_ctl & (1U << 31)) {
        level = 1;
    } else {
        level = 0;
    }
    dprint(d, 2, "%s: level %d [%s]\n", __func__,
           level, msi ? "msi" : "intx");
    if (msi) {
        if (level) {
            msi_notify(&d->pci, 0);
        }
    } else {
        pci_set_irq(&d->pci, level);
    }
}

static int intel_hda_send_command(IntelHDAState *d, uint32_t verb)
{
    uint32_t cad, nid, data;
    HDACodecDevice *codec;
    HDACodecDeviceClass *cdc;

    cad = (verb >> 28) & 0x0f;
    if (verb & (1 << 27)) {
        /* indirect node addressing, not specified in HDA 1.0 */
        dprint(d, 1, "%s: indirect node addressing (guest bug?)\n", __func__);
        return -1;
    }
    nid = (verb >> 20) & 0x7f;
    data = verb & 0xfffff;

    codec = hda_codec_find(&d->codecs, cad);
    if (codec == NULL) {
        dprint(d, 1, "%s: addressed non-existing codec\n", __func__);
        return -1;
    }
    cdc = HDA_CODEC_DEVICE_GET_CLASS(codec);
    cdc->command(codec, nid, data);
    return 0;
}

static void intel_hda_corb_run(IntelHDAState *d)
{
    hwaddr addr;
    uint32_t rp, verb;

    if (d->ics & ICH6_IRS_BUSY) {
        dprint(d, 2, "%s: [icw] verb 0x%08x\n", __func__, d->icw);
        intel_hda_send_command(d, d->icw);
        return;
    }

    for (;;) {
        if (!(d->corb_ctl & ICH6_CORBCTL_RUN)) {
            dprint(d, 2, "%s: !run\n", __func__);
            return;
        }
        if ((d->corb_rp & 0xff) == d->corb_wp) {
            dprint(d, 2, "%s: corb ring empty\n", __func__);
            return;
        }
        if (d->rirb_count == d->rirb_cnt) {
            dprint(d, 2, "%s: rirb count reached\n", __func__);
            return;
        }

        rp = (d->corb_rp + 1) & 0xff;
        addr = intel_hda_addr(d->corb_lbase, d->corb_ubase);
        verb = ldl_le_pci_dma(&d->pci, addr + 4*rp);
        d->corb_rp = rp;

        dprint(d, 2, "%s: [rp 0x%x] verb 0x%08x\n", __func__, rp, verb);
        intel_hda_send_command(d, verb);
    }
}

static void intel_hda_response(HDACodecDevice *dev, bool solicited, uint32_t response)
{
    HDACodecBus *bus = HDA_BUS(dev->qdev.parent_bus);
    IntelHDAState *d = container_of(bus, IntelHDAState, codecs);
    hwaddr addr;
    uint32_t wp, ex;

    if (d->ics & ICH6_IRS_BUSY) {
        dprint(d, 2, "%s: [irr] response 0x%x, cad 0x%x\n",
               __func__, response, dev->cad);
        d->irr = response;
        d->ics &= ~(ICH6_IRS_BUSY | 0xf0);
        d->ics |= (ICH6_IRS_VALID | (dev->cad << 4));
        return;
    }

    if (!(d->rirb_ctl & ICH6_RBCTL_DMA_EN)) {
        dprint(d, 1, "%s: rirb dma disabled, drop codec response\n", __func__);
        return;
    }

    ex = (solicited ? 0 : (1 << 4)) | dev->cad;
    wp = (d->rirb_wp + 1) & 0xff;
    addr = intel_hda_addr(d->rirb_lbase, d->rirb_ubase);
    stl_le_pci_dma(&d->pci, addr + 8*wp, response);
    stl_le_pci_dma(&d->pci, addr + 8*wp + 4, ex);
    d->rirb_wp = wp;

    dprint(d, 2, "%s: [wp 0x%x] response 0x%x, extra 0x%x\n",
           __func__, wp, response, ex);

    d->rirb_count++;
    if (d->rirb_count == d->rirb_cnt) {
        dprint(d, 2, "%s: rirb count reached (%d)\n", __func__, d->rirb_count);
        if (d->rirb_ctl & ICH6_RBCTL_IRQ_EN) {
            d->rirb_sts |= ICH6_RBSTS_IRQ;
            intel_hda_update_irq(d);
        }
    } else if ((d->corb_rp & 0xff) == d->corb_wp) {
        dprint(d, 2, "%s: corb ring empty (%d/%d)\n", __func__,
               d->rirb_count, d->rirb_cnt);
        if (d->rirb_ctl & ICH6_RBCTL_IRQ_EN) {
            d->rirb_sts |= ICH6_RBSTS_IRQ;
            intel_hda_update_irq(d);
        }
    }
}

static bool intel_hda_xfer(HDACodecDevice *dev, uint32_t stnr, bool output,
                           uint8_t *buf, uint32_t len)
{
    HDACodecBus *bus = HDA_BUS(dev->qdev.parent_bus);
    IntelHDAState *d = container_of(bus, IntelHDAState, codecs);
    hwaddr addr;
    uint32_t s, copy, left;
    IntelHDAStream *st;
    bool irq = false;

    st = output ? d->st + 4 : d->st;
    for (s = 0; s < 4; s++) {
        if (stnr == ((st[s].ctl >> 20) & 0x0f)) {
            st = st + s;
            break;
        }
    }
    if (s == 4) {
        return false;
    }
    if (st->bpl == NULL) {
        return false;
    }

    left = len;
    s = st->bentries;
    while (left > 0 && s-- > 0) {
        copy = left;
        if (copy > st->bsize - st->lpib)
            copy = st->bsize - st->lpib;
        if (copy > st->bpl[st->be].len - st->bp)
            copy = st->bpl[st->be].len - st->bp;

        dprint(d, 3, "dma: entry %d, pos %d/%d, copy %d\n",
               st->be, st->bp, st->bpl[st->be].len, copy);

        pci_dma_rw(&d->pci, st->bpl[st->be].addr + st->bp, buf, copy, !output);
        st->lpib += copy;
        st->bp += copy;
        buf += copy;
        left -= copy;

        if (st->bpl[st->be].len == st->bp) {
            /* bpl entry filled */
            if (st->bpl[st->be].flags & 0x01) {
                irq = true;
            }
            st->bp = 0;
            st->be++;
            if (st->be == st->bentries) {
                /* bpl wrap around */
                st->be = 0;
                st->lpib = 0;
            }
        }
    }
    if (d->dp_lbase & 0x01) {
        s = st - d->st;
        addr = intel_hda_addr(d->dp_lbase & ~0x01, d->dp_ubase);
        stl_le_pci_dma(&d->pci, addr + 8*s, st->lpib);
    }
    dprint(d, 3, "dma: --\n");

    if (irq) {
        st->ctl |= (1 << 26); /* buffer completion interrupt */
        intel_hda_update_irq(d);
    }
    return true;
}

static void intel_hda_parse_bdl(IntelHDAState *d, IntelHDAStream *st)
{
    hwaddr addr;
    uint8_t buf[16];
    uint32_t i;

    addr = intel_hda_addr(st->bdlp_lbase, st->bdlp_ubase);
    st->bentries = st->lvi +1;
    g_free(st->bpl);
    st->bpl = g_malloc(sizeof(bpl) * st->bentries);
    for (i = 0; i < st->bentries; i++, addr += 16) {
        pci_dma_read(&d->pci, addr, buf, 16);
        st->bpl[i].addr  = le64_to_cpu(*(uint64_t *)buf);
        st->bpl[i].len   = le32_to_cpu(*(uint32_t *)(buf + 8));
        st->bpl[i].flags = le32_to_cpu(*(uint32_t *)(buf + 12));
        dprint(d, 1, "bdl/%d: 0x%" PRIx64 " +0x%x, 0x%x\n",
               i, st->bpl[i].addr, st->bpl[i].len, st->bpl[i].flags);
    }

    st->bsize = st->cbl;
    st->lpib  = 0;
    st->be    = 0;
    st->bp    = 0;
}

static void intel_hda_notify_codecs(IntelHDAState *d, uint32_t stream, bool running, bool output)
{
    BusChild *kid;
    HDACodecDevice *cdev;

    QTAILQ_FOREACH(kid, &d->codecs.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        HDACodecDeviceClass *cdc;

        cdev = HDA_CODEC_DEVICE(qdev);
        cdc = HDA_CODEC_DEVICE_GET_CLASS(cdev);
        if (cdc->stream) {
            cdc->stream(cdev, stream, running, output);
        }
    }
}

/* --------------------------------------------------------------------- */

static void intel_hda_set_g_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    if ((d->g_ctl & ICH6_GCTL_RESET) == 0) {
        intel_hda_reset(DEVICE(d));
    }
}

static void intel_hda_set_wake_en(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);
}

static void intel_hda_set_state_sts(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);
}

static void intel_hda_set_int_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);
}

static void intel_hda_get_wall_clk(IntelHDAState *d, const IntelHDAReg *reg)
{
    int64_t ns;

    ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - d->wall_base_ns;
    d->wall_clk = (uint32_t)(ns * 24 / 1000);  /* 24 MHz */
}

static void intel_hda_set_adspcs(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    uint32_t pwr = (d->adspcs >> 16) & 0xff;

    d->adspcs &= 0x00ffffff;
    d->adspcs |= (pwr << 24);

    /* core 0 powered ON ? */
    if ((((old >> 16) & 1) == 0) && (((d->adspcs >> 16) & 1) == 1)) {
        /* reset ROM state */
        d->hipcie = HDA_DSP_REG_HIPCIE_DONE;
        d->fw_boot_count = 0;
        d->romsts = HDA_DSP_ROM_STATUS_INIT;
    }
}

static void intel_hda_set_adspic(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{

}

static void intel_hda_get_adspis(IntelHDAState *d, const IntelHDAReg *reg)
{

}

static void intel_hda_set_adspic2(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{

}

static void intel_hda_get_adspis2(IntelHDAState *d, const IntelHDAReg *reg)
{

}

static void intel_hda_set_hipct(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{

}

static void intel_hda_set_hipcte(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{

}

static void intel_hda_set_hipci(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{

}

static void intel_hda_set_hipcie(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{

}

static void intel_hda_set_hipcctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{

}

static void intel_hda_get_rom_status(IntelHDAState *d, const IntelHDAReg *reg)
{
    if (d->fw_boot_count++ > 10)
        d->romsts = HDA_DSP_ROM_FW_ENTERED;
}

static void intel_hda_set_corb_wp(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_corb_run(d);
}

static void intel_hda_set_corb_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_corb_run(d);
}

static void intel_hda_set_rirb_wp(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    if (d->rirb_wp & ICH6_RIRBWP_RST) {
        d->rirb_wp = 0;
    }
}

static void intel_hda_set_rirb_sts(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);

    if ((old & ICH6_RBSTS_IRQ) && !(d->rirb_sts & ICH6_RBSTS_IRQ)) {
        /* cleared ICH6_RBSTS_IRQ */
        d->rirb_count = 0;
        intel_hda_corb_run(d);
    }
}

static void intel_hda_set_ics(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    if (d->ics & ICH6_IRS_BUSY) {
        intel_hda_corb_run(d);
    }
}

static void intel_hda_set_st_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    bool output = reg->stream >= 4;
    IntelHDAStream *st = d->st + reg->stream;

    if (st->ctl & 0x01) {
        /* reset */
        dprint(d, 1, "st #%d: reset\n", reg->stream);

        /* codeloader holds it current state */
        if (st->is_codeloader)
            st->ctl |= SD_STS_FIFO_READY << 24;
        else
            st->ctl = SD_STS_FIFO_READY << 24;
    }
    if ((st->ctl & 0x02) != (old & 0x02)) {
        uint32_t stnr = (st->ctl >> 20) & 0x0f;
        /* run bit flipped */
        if (st->ctl & 0x02) {
            /* start */
            dprint(d, 1, "st #%d: start %d (ring buf %d bytes)\n",
                   reg->stream, stnr, st->cbl);
            intel_hda_parse_bdl(d, st);
            intel_hda_notify_codecs(d, stnr, true, output);
        } else {
            /* stop */
            dprint(d, 1, "st #%d: stop %d\n", reg->stream, stnr);
            intel_hda_notify_codecs(d, stnr, false, output);
        }
    }

    intel_hda_update_irq(d);
}

/* --------------------------------------------------------------------- */

#define ST_REG(_n, _o) (0x80 + (_n) * 0x20 + (_o))
#define SD_REG(_n, _o) (((_n) * 0x8) + (_o))

static const struct IntelHDAReg regtab[] = {
    /* global */
    [ ICH6_REG_GCAP ] = {
        .name     = "GCAP",
        .size     = 2,
        .reset    = 0x8801,
    },
    [ ICH6_REG_VMIN ] = {
        .name     = "VMIN",
        .size     = 1,
    },
    [ ICH6_REG_VMAJ ] = {
        .name     = "VMAJ",
        .size     = 1,
        .reset    = 1,
    },
    [ ICH6_REG_OUTPAY ] = {
        .name     = "OUTPAY",
        .size     = 2,
        .reset    = 0x3c,
        .offset   = offsetof(IntelHDAState, outpay),
    },
    [ ICH6_REG_INPAY ] = {
        .name     = "INPAY",
        .size     = 2,
        .reset    = 0x1d,
        .offset   = offsetof(IntelHDAState, inpay),
    },
    [ ICH6_REG_GCTL ] = {
        .name     = "GCTL",
        .size     = 4,
        .wmask    = 0x0103,
        .offset   = offsetof(IntelHDAState, g_ctl),
        .whandler = intel_hda_set_g_ctl,
    },
    [ ICH6_REG_WAKEEN ] = {
        .name     = "WAKEEN",
        .size     = 2,
        .wmask    = 0x7fff,
        .offset   = offsetof(IntelHDAState, wake_en),
        .whandler = intel_hda_set_wake_en,
    },
    [ ICH6_REG_STATESTS ] = {
        .name     = "STATESTS",
        .size     = 2,
        .wmask    = 0x7fff,
        .wclear   = 0x7fff,
        .offset   = offsetof(IntelHDAState, state_sts),
        .whandler = intel_hda_set_state_sts,
    },

    [ ICH6_REG_GSTS ] = {
        .name     = "GSTS",
        .size     = 2,
         .wmask    = 0x2,
        .offset   = offsetof(IntelHDAState, gsts),
    },

    [ ICH6_REG_LLCH ] = {
        .name     = "LLCH",
        .size     = 2,
        .reset    = ICH6_REG_ALLCH,
    },
    /* interrupts */
    [ ICH6_REG_INTCTL ] = {
        .name     = "INTCTL",
        .size     = 4,
        .wmask    = 0xc00000ff,
        .offset   = offsetof(IntelHDAState, int_ctl),
        .whandler = intel_hda_set_int_ctl,
    },
    [ ICH6_REG_INTSTS ] = {
        .name     = "INTSTS",
        .size     = 4,
        .wmask    = 0xc00000ff,
        .wclear   = 0xc00000ff,
        .offset   = offsetof(IntelHDAState, int_sts),
    },

    /* misc */
    [ ICH6_REG_WALLCLK ] = {
        .name     = "WALLCLK",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, wall_clk),
        .rhandler = intel_hda_get_wall_clk,
    },
    [ ICH6_REG_WALLCLK + 0x2000 ] = {
        .name     = "WALLCLK(alias)",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, wall_clk),
        .rhandler = intel_hda_get_wall_clk,
    },

    /* dma engine */
    [ ICH6_REG_CORBLBASE ] = {
        .name     = "CORBLBASE",
        .size     = 4,
        .wmask    = 0xffffff80,
        .offset   = offsetof(IntelHDAState, corb_lbase),
    },
    [ ICH6_REG_CORBUBASE ] = {
        .name     = "CORBUBASE",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, corb_ubase),
    },
    [ ICH6_REG_CORBWP ] = {
        .name     = "CORBWP",
        .size     = 2,
        .wmask    = 0xff,
        .offset   = offsetof(IntelHDAState, corb_wp),
        .whandler = intel_hda_set_corb_wp,
    },
    [ ICH6_REG_CORBRP ] = {
        .name     = "CORBRP",
        .size     = 2,
        .wmask    = 0x80ff,
        .offset   = offsetof(IntelHDAState, corb_rp),
    },
    [ ICH6_REG_CORBCTL ] = {
        .name     = "CORBCTL",
        .size     = 1,
        .wmask    = 0x03,
        .offset   = offsetof(IntelHDAState, corb_ctl),
        .whandler = intel_hda_set_corb_ctl,
    },
    [ ICH6_REG_CORBSTS ] = {
        .name     = "CORBSTS",
        .size     = 1,
        .wmask    = 0x01,
        .wclear   = 0x01,
        .offset   = offsetof(IntelHDAState, corb_sts),
    },
    [ ICH6_REG_CORBSIZE ] = {
        .name     = "CORBSIZE",
        .size     = 1,
        .reset    = 0x42,
        .offset   = offsetof(IntelHDAState, corb_size),
    },
    [ ICH6_REG_RIRBLBASE ] = {
        .name     = "RIRBLBASE",
        .size     = 4,
        .wmask    = 0xffffff80,
        .offset   = offsetof(IntelHDAState, rirb_lbase),
    },
    [ ICH6_REG_RIRBUBASE ] = {
        .name     = "RIRBUBASE",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, rirb_ubase),
    },
    [ ICH6_REG_RIRBWP ] = {
        .name     = "RIRBWP",
        .size     = 2,
        .wmask    = 0x8000,
        .offset   = offsetof(IntelHDAState, rirb_wp),
        .whandler = intel_hda_set_rirb_wp,
    },
    [ ICH6_REG_RINTCNT ] = {
        .name     = "RINTCNT",
        .size     = 2,
        .wmask    = 0xff,
        .offset   = offsetof(IntelHDAState, rirb_cnt),
    },
    [ ICH6_REG_RIRBCTL ] = {
        .name     = "RIRBCTL",
        .size     = 1,
        .wmask    = 0x07,
        .offset   = offsetof(IntelHDAState, rirb_ctl),
    },
    [ ICH6_REG_RIRBSTS ] = {
        .name     = "RIRBSTS",
        .size     = 1,
        .wmask    = 0x05,
        .wclear   = 0x05,
        .offset   = offsetof(IntelHDAState, rirb_sts),
        .whandler = intel_hda_set_rirb_sts,
    },
    [ ICH6_REG_RIRBSIZE ] = {
        .name     = "RIRBSIZE",
        .size     = 1,
        .reset    = 0x42,
        .offset   = offsetof(IntelHDAState, rirb_size),
    },

    [ ICH6_REG_DPLBASE ] = {
        .name     = "DPLBASE",
        .size     = 4,
        .wmask    = 0xffffff81,
        .offset   = offsetof(IntelHDAState, dp_lbase),
    },
    [ ICH6_REG_DPUBASE ] = {
        .name     = "DPUBASE",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, dp_ubase),
    },

    [ ICH6_REG_IC ] = {
        .name     = "ICW",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, icw),
    },
    [ ICH6_REG_IR ] = {
        .name     = "IRR",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, irr),
    },
    [ ICH6_REG_IRS ] = {
        .name     = "ICS",
        .size     = 2,
        .wmask    = 0x0003,
        .wclear   = 0x0002,
        .offset   = offsetof(IntelHDAState, ics),
        .whandler = intel_hda_set_ics,
    },

#define HDA_STREAM(_t, _i)                                            \
    [ ST_REG(_i, ICH6_REG_SD_CTL) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL",                          \
        .size     = 4,                                                \
        .wmask    = 0x1cff001f,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_CTL) + 2] = {                            \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL(stnr)",                    \
        .size     = 1,                                                \
        .shift    = 16,                                               \
        .wmask    = 0x00ff0000,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_STS)] = {                                \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL(sts)",                     \
        .size     = 1,                                                \
        .shift    = 24,                                               \
        .wmask    = 0x1c000000,                                       \
        .wclear   = 0x1c000000,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
        .reset    = SD_STS_FIFO_READY << 24                           \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_LPIB) ] = {                              \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LPIB",                         \
        .size     = 4,                                                \
        .offset   = offsetof(IntelHDAState, st[_i].lpib),             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_LPIB) + 0x2000 ] = {                     \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LPIB(alias)",                  \
        .size     = 4,                                                \
        .offset   = offsetof(IntelHDAState, st[_i].lpib),             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_CBL) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CBL",                          \
        .size     = 4,                                                \
        .wmask    = 0xffffffff,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].cbl),              \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_LVI) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LVI",                          \
        .size     = 2,                                                \
        .wmask    = 0x00ff,                                           \
        .offset   = offsetof(IntelHDAState, st[_i].lvi),              \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_FIFOSIZE) ] = {                          \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " FIFOS",                        \
        .size     = 2,                                                \
        .reset    = HDA_BUFFER_SIZE,                                  \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_FORMAT) ] = {                            \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " FMT",                          \
        .size     = 2,                                                \
        .wmask    = 0x7f7f,                                           \
        .offset   = offsetof(IntelHDAState, st[_i].fmt),              \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_BDLPL) ] = {                             \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " BDLPL",                        \
        .size     = 4,                                                \
        .wmask    = 0xffffff80,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].bdlp_lbase),       \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_BDLPU) ] = {                             \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " BDLPU",                        \
        .size     = 4,                                                \
        .wmask    = 0xffffffff,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].bdlp_ubase),       \
    },                                                                \

    HDA_STREAM("IN", 0)
    HDA_STREAM("IN", 1)
    HDA_STREAM("IN", 2)
    HDA_STREAM("IN", 3)
    HDA_STREAM("IN", 4)
    HDA_STREAM("IN", 5)
    HDA_STREAM("IN", 6)
    HDA_STREAM("IN", 7)

    HDA_STREAM("OUT", 8)
    HDA_STREAM("OUT", 9)
    HDA_STREAM("OUT", 10)
    HDA_STREAM("OUT", 11)
    HDA_STREAM("OUT", 12)
    HDA_STREAM("OUT", 13)
    HDA_STREAM("OUT", 14)
    HDA_STREAM("OUT", 15)


    /* capabilities */
    [ ICH6_REG_ALLCH ] = {
        .name     = "ALLCH",
        .size     = 2,
        .reset    = (ICH6_PP_CAP_ID << ICH6_CAP_SHIFT) | ICH6_GTS_CAP_BASE,
    },

    /* PP regs */
    [ ICH6_REG_PP_CTL ] = {
        .name     = "PPCTL",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, ppctl),
    },
    [ ICH6_REG_PP_STS ] = {
        .name     = "PPSTS",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, ppsts),
    },

    /* GTS caps */
    [ ICH6_GTS_CAP_BASE ] = {
        .name     = "GTSCH",
        .size     = 4,
        .reset    = (0x1 << ICH6_CAP_SHIFT) | ICH6_DRSM_CAP_BASE,
    },

    /* DRSM caps */
    [ ICH6_DRSM_CAP_BASE ] = {
        .name     = "DRSMCH",
        .size     = 4,
        .reset    = (0x5 << ICH6_CAP_SHIFT) | ICH6_SPIB_CAP_BASE,
    },

    /* SPIB caps */
    [ ICH6_SPIB_CAP_BASE ] = {
        .name     = "SBPFCH",
        .size     = 4,
        .reset    = (0x4 << ICH6_CAP_SHIFT),
    },

    [ ICH6_REG_SPBFCCTL ] = {
        .name     = "SBPFCCTL",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, sbpfcctl),
    },

    [ 0x1030 ] = {
            .name     = "EM2",
            .size     = 4,
            .wmask    = 0xffffffff,
            .offset   = offsetof(IntelHDAState, em2),
        },

#define SPIB_STREAM(_t, _i)                                            \
    [ SD_REG(_i, ICH6_REG_SPIB_CTL) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " SPIB",                          \
        .size     = 4,                                                \
        .wmask    = 0xffffffff,                                       \
        .offset   = offsetof(IntelHDAState, sd[_i].spib),              \
    },                                                                \
    [ SD_REG(_i, ICH6_REG_SPIB_CTL) + 4] = {                            \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " MAXFIFO(stnr)",                    \
        .size     = 4,                                                \
        .offset   = offsetof(IntelHDAState, sd[_i].maxfifos),         \
    }

    SPIB_STREAM("IN", 0),
    SPIB_STREAM("IN", 1),
    SPIB_STREAM("IN", 2),
    SPIB_STREAM("IN", 3),
    SPIB_STREAM("IN", 4),
    SPIB_STREAM("IN", 5),
    SPIB_STREAM("IN", 6),
    SPIB_STREAM("IN", 7),

    SPIB_STREAM("OUT", 8),
    SPIB_STREAM("OUT", 9),
    SPIB_STREAM("OUT", 10),
    SPIB_STREAM("OUT", 11),
    SPIB_STREAM("OUT", 12),
    SPIB_STREAM("OUT", 13),
    SPIB_STREAM("OUT", 14),
    SPIB_STREAM("OUT", 15),
};

static const IntelHDAReg *intel_hda_reg_find(IntelHDAState *d, hwaddr addr)
{
    const IntelHDAReg *reg;

    if (addr >= ARRAY_SIZE(regtab)) {
        goto noreg;
    }
    reg = regtab + addr;
    if (reg->name == NULL) {
        goto noreg;
    }
    return reg;

noreg:
    dprint(d, 1, "unknown HDA register, addr 0x%x\n", (int) addr);
    return NULL;
}

#define CL_REG(_n, _o) (HDA_ADSP_LOADER_BASE + (_o))

/* CAVS v1.5 */
static const struct IntelHDAReg regtabdsp[] = {

   /* Control and Status */
   [ HDA_DSP_REG_ADSPCS ] = {
        .name     = "ADSPCS",
        .size     = 4,
	.whandler = intel_hda_set_adspcs,
        .reset    = 0x0000ffff,
        .offset   = offsetof(IntelHDAState, adspcs),
        .wmask    = 0x00ffffff,
    },
   [ HDA_DSP_REG_ADSPIC ] = {
        .name     = "ADSPIC",
        .size     = 4,
	.whandler = intel_hda_set_adspic,
        .offset   = offsetof(IntelHDAState, adspic),
    },
   [ HDA_DSP_REG_ADSPIS ] = {
        .name     = "ADSPIS",
        .size     = 4,
	.rhandler = intel_hda_get_adspis,
        .offset   = offsetof(IntelHDAState, adspis),
    },
    [ HDA_DSP_REG_ADSPIC2 ] = {
        .name     = "ADSPIC2",
        .size     = 4,
	.whandler = intel_hda_set_adspic2,
        .offset   = offsetof(IntelHDAState, adspic2),
    },
   [ HDA_DSP_REG_ADSPIS2 ] = {
        .name     = "ADSPIS2",
        .size     = 4,
        .rhandler = intel_hda_get_adspis2,
        .offset   = offsetof(IntelHDAState, adspis2),
    },

    /* codeloader */
#define HDA_CL_STREAM(_t, _i)                                            \
    [ CL_REG(_i, ICH6_REG_SD_CTL) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL",                          \
        .size     = 4,                                                \
        .wmask    = 0x1cff001f,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_CTL) + 2] = {                            \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL(stnr)",                    \
        .size     = 1,                                                \
        .shift    = 16,                                               \
        .wmask    = 0x00ff0000,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_STS)] = {                                \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL(sts)",                     \
        .size     = 1,                                                \
        .shift    = 24,                                               \
        .wmask    = 0x1c000000,                                       \
        .wclear   = 0x1c000000,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
        .reset    = SD_STS_FIFO_READY << 24                           \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_LPIB) ] = {                              \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LPIB",                         \
        .size     = 4,                                                \
        .offset   = offsetof(IntelHDAState, st[_i].lpib),             \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_LPIB) + 0x2000 ] = {                     \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LPIB(alias)",                  \
        .size     = 4,                                                \
        .offset   = offsetof(IntelHDAState, st[_i].lpib),             \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_CBL) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CBL",                          \
        .size     = 4,                                                \
        .wmask    = 0xffffffff,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].cbl),              \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_LVI) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LVI",                          \
        .size     = 2,                                                \
        .wmask    = 0x00ff,                                           \
        .offset   = offsetof(IntelHDAState, st[_i].lvi),              \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_FIFOSIZE) ] = {                          \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " FIFOS",                        \
        .size     = 2,                                                \
        .reset    = HDA_BUFFER_SIZE,                                  \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_FORMAT) ] = {                            \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " FMT",                          \
        .size     = 2,                                                \
        .wmask    = 0x7f7f,                                           \
        .offset   = offsetof(IntelHDAState, st[_i].fmt),              \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_BDLPL) ] = {                             \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " BDLPL",                        \
        .size     = 4,                                                \
        .wmask    = 0xffffff80,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].bdlp_lbase),       \
    },                                                                \
    [ CL_REG(_i, ICH6_REG_SD_BDLPU) ] = {                             \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " BDLPU",                        \
        .size     = 4,                                                \
        .wmask    = 0xffffffff,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].bdlp_ubase),       \
    },                                                                \

    HDA_CL_STREAM("CL", 15)

    /* IPC */
    [ HDA_DSP_REG_HIPCT ] = {
        .name     = "HIPCT",
        .size     = 4,
	.whandler = intel_hda_set_hipct,
        .offset   = offsetof(IntelHDAState, hipct),
    },
    [ HDA_DSP_REG_HIPCTE ] = {
        .name     = "HIPCTE",
        .size     = 4,
	.whandler = intel_hda_set_hipcte,
        .offset   = offsetof(IntelHDAState, hipcte),
    },
    [ HDA_DSP_REG_HIPCI ] = {
        .name     = "HIPCI",
        .size     = 4,
	.whandler = intel_hda_set_hipci,
        .offset   = offsetof(IntelHDAState, hipci),
    },
    [ HDA_DSP_REG_HIPCIE ] = {
        .name     = "HIPCIE",
        .size     = 4,
	.whandler = intel_hda_set_hipcie,
        .offset   = offsetof(IntelHDAState, hipcie),
    },
    [ HDA_DSP_REG_HIPCCTL ] = {
        .name     = "HIPCCTL",
        .size     = 4,
	.whandler = intel_hda_set_hipcctl,
        .offset   = offsetof(IntelHDAState, hipcctl),
    },

    /* rom regs */
    [ HDA_DSP_REG_ROM_STATUS ] = {
        .name    = "ROMSTS",
        .size    = 4,
        .reset   = HDA_DSP_ROM_STATUS_INIT,
        .rhandler = intel_hda_get_rom_status,
        .offset   = offsetof(IntelHDAState, romsts),
    },
    [ HDA_DSP_REG_ROM_ERROR ] = {
        .name    = "ROMERR",
        .size    = 4,
        .reset   = 0,
    },
    [ HDA_DSP_REG_ROM_END ] = {
        .name    = "ROMEND",
        .size    = 4,
        .reset   = 0,
    },
};

static const IntelHDAReg *intel_hda_dsp_reg_find(IntelHDAState *d, hwaddr addr)
{
    const IntelHDAReg *reg;

    if (addr >= ARRAY_SIZE(regtabdsp)) {
        goto noreg;
    }
    reg = regtabdsp+addr;
    if (reg->name == NULL) {
        goto noreg;
    }
    return reg;

noreg:
    dprint(d, 1, "unknown DSP register, addr 0x%x\n", (int) addr);
    return NULL;
}

static uint32_t *intel_hda_reg_addr(IntelHDAState *d, const IntelHDAReg *reg)
{
    uint8_t *addr = (void*)d;

    addr += reg->offset;
    return (uint32_t*)addr;
}

static void intel_hda_reg_write(IntelHDAState *d, const IntelHDAReg *reg, uint32_t val,
                                uint32_t wmask)
{
    uint32_t *addr;
    uint32_t old;

    if (!reg) {
        return;
    }
    if (!reg->wmask) {
        qemu_log_mask(LOG_GUEST_ERROR, "intel-hda: write to r/o reg %s\n",
                      reg->name);
        return;
    }

    if (d->debug) {
        time_t now = time(NULL);
        if (d->last_write && d->last_reg == reg && d->last_val == val) {
            d->repeat_count++;
            if (d->last_sec != now) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
                d->last_sec = now;
                d->repeat_count = 0;
            }
        } else {
            if (d->repeat_count) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
            }
            dprint(d, 2, "write %-16s: 0x%x (%x)\n", reg->name, val, wmask);
            d->last_write = 1;
            d->last_reg   = reg;
            d->last_val   = val;
            d->last_sec   = now;
            d->repeat_count = 0;
        }
    }

    assert(reg->offset != 0);

    addr = intel_hda_reg_addr(d, reg);
    old = *addr;

    if (reg->shift) {
        val <<= reg->shift;
        wmask <<= reg->shift;
    }
    wmask &= reg->wmask;
    *addr &= ~wmask;
    *addr |= wmask & val;
    *addr &= ~(val & reg->wclear);

    if (reg->whandler) {
        reg->whandler(d, reg, old);
    }
}

static uint32_t intel_hda_reg_read(IntelHDAState *d, const IntelHDAReg *reg,
                                   uint32_t rmask)
{
    uint32_t *addr, ret;

    if (!reg) {
        return 0;
    }

    if (reg->rhandler) {
        reg->rhandler(d, reg);
    }

    if (reg->offset == 0) {
        /* constant read-only register */
        ret = reg->reset;
    } else {
        addr = intel_hda_reg_addr(d, reg);
        ret = *addr;
        if (reg->shift) {
            ret >>= reg->shift;
        }
        ret &= rmask;
    }
    if (d->debug) {
        time_t now = time(NULL);
        if (!d->last_write && d->last_reg == reg && d->last_val == ret) {
            d->repeat_count++;
            if (d->last_sec != now) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
                d->last_sec = now;
                d->repeat_count = 0;
            }
        } else {
            if (d->repeat_count) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
            }
            dprint(d, 2, "read  %-16s: 0x%x (%x)\n", reg->name, ret, rmask);
            d->last_write = 0;
            d->last_reg   = reg;
            d->last_val   = ret;
            d->last_sec   = now;
            d->repeat_count = 0;
        }
    }

    return ret;
}

static void intel_hda_regs_reset(IntelHDAState *d)
{
    uint32_t *addr;
    int i;

    for (i = 0; i < ARRAY_SIZE(regtab); i++) {
        if (regtab[i].name == NULL) {
            continue;
        }
        if (regtab[i].offset == 0) {
            continue;
        }
        addr = intel_hda_reg_addr(d, regtab + i);
        *addr = regtab[i].reset;
    }
}

/* --------------------------------------------------------------------- */

static void intel_hda_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    if (!reg)
        fprintf(stderr, "hda: no register to write at 0x%lx value 0x%lx size %d\n",
                addr, val, size);

    intel_hda_reg_write(d, reg, val, MAKE_64BIT_MASK(0, size * 8));
}

static uint64_t intel_hda_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    if (!reg)
        fprintf(stderr, "hda: no register to read at 0x%lx size %d\n", addr, size);

    return intel_hda_reg_read(d, reg, MAKE_64BIT_MASK(0, size * 8));
}

static const MemoryRegionOps intel_hda_mmio_ops = {
    .read = intel_hda_mmio_read,
    .write = intel_hda_mmio_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void intel_hda_dsp_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_dsp_reg_find(d, addr);

    if (!reg)
        fprintf(stderr, "hda-dsp: no register to write at 0x%lx value 0x%lx size %d\n",
                addr, val, size);

    intel_hda_reg_write(d, reg, val, MAKE_64BIT_MASK(0, size * 8));
}

static uint64_t intel_hda_dsp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_dsp_reg_find(d, addr);

    if (!reg)
        fprintf(stderr, "hda-dsp: no register to read at 0x%lx size %d\n", addr, size);

    return intel_hda_reg_read(d, reg, MAKE_64BIT_MASK(0, size * 8));
}

static const MemoryRegionOps intel_hda_dsp_mmio_ops = {
    .read = intel_hda_dsp_mmio_read,
    .write = intel_hda_dsp_mmio_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* --------------------------------------------------------------------- */

static void intel_hda_reset(DeviceState *dev)
{
    BusChild *kid;
    IntelHDAState *d = INTEL_HDA(dev);
    HDACodecDevice *cdev;

    intel_hda_regs_reset(d);
    d->wall_base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* reset codecs */
    QTAILQ_FOREACH(kid, &d->codecs.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        cdev = HDA_CODEC_DEVICE(qdev);
        device_reset(DEVICE(cdev));
        d->state_sts |= (1 << cdev->cad);
    }
    intel_hda_update_irq(d);
}

static void intel_hda_realize(PCIDevice *pci, Error **errp)
{
    IntelHDAState *d = INTEL_HDA(pci);
    uint8_t *conf = d->pci.config;
    Error *err = NULL;
    int ret;

    d->name = object_get_typename(OBJECT(d));

    pci_config_set_interrupt_pin(conf, 1);

    /* HDCTL off 0x40 bit 0 selects signaling mode (1-HDA, 0 - Ac97) 18.1.19 */
    conf[0x40] = 0x01;

    if (d->msi != ON_OFF_AUTO_OFF) {
        ret = msi_init(&d->pci, d->old_msi_addr ? 0x50 : 0x60,
                       1, true, false, &err);
        /* Any error other than -ENOTSUP(board's MSI support is broken)
         * is a programming error */
        assert(!ret || ret == -ENOTSUP);
        if (ret && d->msi == ON_OFF_AUTO_ON) {
            /* Can't satisfy user's explicit msi=on request, fail */
            error_append_hint(&err, "You have to use msi=auto (default) or "
                    "msi=off with this machine type.\n");
            error_propagate(errp, err);
            return;
        }
        assert(!err || d->msi == ON_OFF_AUTO_AUTO);
        /* With msi=auto, we fall back to MSI off silently */
        error_free(err);
    }

    memory_region_init_io(&d->mmio, OBJECT(d), &intel_hda_mmio_ops, d,
                          "intel-hda", 0x4000);
    pci_register_bar(&d->pci, 0, 0, &d->mmio);

    hda_codec_bus_init(DEVICE(pci), &d->codecs, sizeof(d->codecs),
                       intel_hda_response, intel_hda_xfer);
}

static void intel_hda_dsp_realize(PCIDevice *pci, Error **errp,
		int version, const char *name)
{
    IntelHDAState *d = INTEL_HDA(pci);
    uint8_t *conf = d->pci.config;
    IntelHDAStream *st = d->st + 8;
    Error *err = NULL;
    int ret;

    d->name = object_get_typename(OBJECT(d));

    pci_config_set_interrupt_pin(conf, 1);

    /* HDCTL off 0x40 bit 0 selects signaling mode (1-HDA, 0 - Ac97) 18.1.19 */
    conf[0x40] = 0x01;

    if (d->msi != ON_OFF_AUTO_OFF) {
        ret = msi_init(&d->pci, d->old_msi_addr ? 0x50 : 0x60,
                       1, true, false, &err);
        /* Any error other than -ENOTSUP(board's MSI support is broken)
         * is a programming error */
        assert(!ret || ret == -ENOTSUP);
        if (ret && d->msi == ON_OFF_AUTO_ON) {
            /* Can't satisfy user's explicit msi=on request, fail */
            error_append_hint(&err, "You have to use msi=auto (default) or "
                    "msi=off with this machine type.\n");
            error_propagate(errp, err);
            return;
        }
        assert(!err || d->msi == ON_OFF_AUTO_AUTO);
        /* With msi=auto, we fall back to MSI off silently */
        error_free(err);
    }

    /* codeloader is */
    st->is_codeloader = true;

    /* HDA Legacy BAR 0 */
    memory_region_init_io(&d->mmio, OBJECT(d), &intel_hda_mmio_ops, d,
                          "intel-hda", 0x4000);
    pci_register_bar(&d->pci, 0, 0, &d->mmio);

    /* DSP BAR 4 */
    memory_region_init_io(&d->mmio_dsp, OBJECT(d), &intel_hda_dsp_mmio_ops, d,
                          "intel-hda-dsp", 0x100000);
    pci_register_bar(&d->pci, 4, 0, &d->mmio_dsp);

    hda_codec_bus_init(DEVICE(pci), &d->codecs, sizeof(d->codecs),
                       intel_hda_response, intel_hda_xfer);
    adsp_hda_init(d, version,  name);
}

static void intel_hda_dsp_realize_bxt(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 15, "bxt");
}

static void intel_hda_dsp_realize_apl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 15, "apl");
}

static void intel_hda_dsp_realize_glk(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 15, "glk");
}

static void intel_hda_dsp_realize_kbl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 15, "kbl");
}

static void intel_hda_dsp_realize_skl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 15, "skl");
}

static void intel_hda_dsp_realize_cnl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 18, "cnl");
}

static void intel_hda_dsp_realize_cfl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 18, "cfl");
}

static void intel_hda_dsp_realize_icl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 18, "icl");
}

static void intel_hda_dsp_realize_tgl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 18, "tgl");
}

static void intel_hda_dsp_realize_cml(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 15, "cml");
}

static void intel_hda_dsp_realize_ehl(PCIDevice *pci, Error **errp)
{
	intel_hda_dsp_realize(pci, errp, 18, "ehl");
}

static void intel_hda_exit(PCIDevice *pci)
{
    IntelHDAState *d = INTEL_HDA(pci);

    msi_uninit(&d->pci);
}

static int intel_hda_post_load(void *opaque, int version)
{
    IntelHDAState* d = opaque;
    int i;

    dprint(d, 1, "%s\n", __func__);
    for (i = 0; i < ARRAY_SIZE(d->st); i++) {
        if (d->st[i].ctl & 0x02) {
            intel_hda_parse_bdl(d, &d->st[i]);
        }
    }
    intel_hda_update_irq(d);
    return 0;
}

static const VMStateDescription vmstate_intel_hda_stream = {
    .name = "intel-hda-stream",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctl, IntelHDAStream),
        VMSTATE_UINT32(lpib, IntelHDAStream),
        VMSTATE_UINT32(cbl, IntelHDAStream),
        VMSTATE_UINT32(lvi, IntelHDAStream),
        VMSTATE_UINT32(fmt, IntelHDAStream),
        VMSTATE_UINT32(bdlp_lbase, IntelHDAStream),
        VMSTATE_UINT32(bdlp_ubase, IntelHDAStream),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_intel_hda = {
    .name = "intel-hda",
    .version_id = 1,
    .post_load = intel_hda_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pci, IntelHDAState),

        /* registers */
        VMSTATE_UINT32(g_ctl, IntelHDAState),
        VMSTATE_UINT32(wake_en, IntelHDAState),
        VMSTATE_UINT32(state_sts, IntelHDAState),
        VMSTATE_UINT32(int_ctl, IntelHDAState),
        VMSTATE_UINT32(int_sts, IntelHDAState),
        VMSTATE_UINT32(wall_clk, IntelHDAState),
        VMSTATE_UINT32(corb_lbase, IntelHDAState),
        VMSTATE_UINT32(corb_ubase, IntelHDAState),
        VMSTATE_UINT32(corb_rp, IntelHDAState),
        VMSTATE_UINT32(corb_wp, IntelHDAState),
        VMSTATE_UINT32(corb_ctl, IntelHDAState),
        VMSTATE_UINT32(corb_sts, IntelHDAState),
        VMSTATE_UINT32(corb_size, IntelHDAState),
        VMSTATE_UINT32(rirb_lbase, IntelHDAState),
        VMSTATE_UINT32(rirb_ubase, IntelHDAState),
        VMSTATE_UINT32(rirb_wp, IntelHDAState),
        VMSTATE_UINT32(rirb_cnt, IntelHDAState),
        VMSTATE_UINT32(rirb_ctl, IntelHDAState),
        VMSTATE_UINT32(rirb_sts, IntelHDAState),
        VMSTATE_UINT32(rirb_size, IntelHDAState),
        VMSTATE_UINT32(dp_lbase, IntelHDAState),
        VMSTATE_UINT32(dp_ubase, IntelHDAState),
        VMSTATE_UINT32(icw, IntelHDAState),
        VMSTATE_UINT32(irr, IntelHDAState),
        VMSTATE_UINT32(ics, IntelHDAState),
        VMSTATE_STRUCT_ARRAY(st, IntelHDAState, 16, 0,
                             vmstate_intel_hda_stream,
                             IntelHDAStream),

        /* additional state info */
        VMSTATE_UINT32(rirb_count, IntelHDAState),
        VMSTATE_INT64(wall_base_ns, IntelHDAState),

        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_intel_dsp_hda = {
    .name = "intel-hda",
    .version_id = 1,
    .post_load = intel_hda_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pci, IntelHDAState),

        /* registers */
        VMSTATE_UINT32(g_ctl, IntelHDAState),
        VMSTATE_UINT32(wake_en, IntelHDAState),
        VMSTATE_UINT32(state_sts, IntelHDAState),
        VMSTATE_UINT32(int_ctl, IntelHDAState),
        VMSTATE_UINT32(int_sts, IntelHDAState),
        VMSTATE_UINT32(wall_clk, IntelHDAState),
        VMSTATE_UINT32(corb_lbase, IntelHDAState),
        VMSTATE_UINT32(corb_ubase, IntelHDAState),
        VMSTATE_UINT32(corb_rp, IntelHDAState),
        VMSTATE_UINT32(corb_wp, IntelHDAState),
        VMSTATE_UINT32(corb_ctl, IntelHDAState),
        VMSTATE_UINT32(corb_sts, IntelHDAState),
        VMSTATE_UINT32(corb_size, IntelHDAState),
        VMSTATE_UINT32(rirb_lbase, IntelHDAState),
        VMSTATE_UINT32(rirb_ubase, IntelHDAState),
        VMSTATE_UINT32(rirb_wp, IntelHDAState),
        VMSTATE_UINT32(rirb_cnt, IntelHDAState),
        VMSTATE_UINT32(rirb_ctl, IntelHDAState),
        VMSTATE_UINT32(rirb_sts, IntelHDAState),
        VMSTATE_UINT32(rirb_size, IntelHDAState),
        VMSTATE_UINT32(dp_lbase, IntelHDAState),
        VMSTATE_UINT32(dp_ubase, IntelHDAState),
        VMSTATE_UINT32(icw, IntelHDAState),
        VMSTATE_UINT32(irr, IntelHDAState),
        VMSTATE_UINT32(ics, IntelHDAState),
        VMSTATE_STRUCT_ARRAY(st, IntelHDAState, 16, 0,
                             vmstate_intel_hda_stream,
                             IntelHDAStream),

        /* additional state info */
        VMSTATE_UINT32(rirb_count, IntelHDAState),
        VMSTATE_INT64(wall_base_ns, IntelHDAState),

        VMSTATE_END_OF_LIST()
    }
};

static Property intel_hda_properties[] = {
    DEFINE_PROP_UINT32("debug", IntelHDAState, debug, 0),
    DEFINE_PROP_ON_OFF_AUTO("msi", IntelHDAState, msi, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BOOL("old_msi_addr", IntelHDAState, old_msi_addr, false),
    DEFINE_PROP_END_OF_LIST(),
};

static Property intel_hda_dsp_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void intel_hda_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = intel_hda_realize;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_hda;
    dc->props = intel_hda_properties;
}

static void intel_hda_class_init_ich6(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x2668;
    k->revision = 1;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (ich6)";
}

static void intel_hda_class_init_ich9(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x293e;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (ich9)";
}

static void intel_hda_class_init_bxt(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);


    k->device_id = 0x5a98;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Broxton)";

    k->realize = intel_hda_dsp_realize_bxt;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_apl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x1a98;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Apollolake)";

    k->realize = intel_hda_dsp_realize_apl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_glk(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x3198;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Geminilake)";

    k->realize = intel_hda_dsp_realize_glk;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_cnl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x9dc8;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Cannonlake)";

    k->realize = intel_hda_dsp_realize_cnl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_cfl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0xa348;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Coffeelake)";

    k->realize = intel_hda_dsp_realize_cfl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_kbl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x9d71;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Kabylake)";

    k->realize = intel_hda_dsp_realize_kbl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_skl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x9d70;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Skylake)";

    k->realize = intel_hda_dsp_realize_skl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_icl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x34C8;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Icelake)";

    k->realize = intel_hda_dsp_realize_icl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_cml_lp(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x02c8;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Cometlake LP)";

    k->realize = intel_hda_dsp_realize_cml;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_cml_h(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x06c8;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Cometlake H)";

    k->realize = intel_hda_dsp_realize_cml;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_tgl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0xa0c8;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Tigerlake)";

    k->realize = intel_hda_dsp_realize_tgl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static void intel_hda_class_init_ehl(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x4b55;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (Elkhartlake)";

    k->realize = intel_hda_dsp_realize_ehl;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_DSP_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_dsp_hda;
    dc->props = intel_hda_dsp_properties;
}

static const TypeInfo intel_hda_info = {
    .name          = TYPE_INTEL_HDA_GENERIC,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IntelHDAState),
    .class_init    = intel_hda_class_init,
    .abstract      = true,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const TypeInfo intel_hda_info_ich6 = {
    .name          = "intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_ich6,
};

static const TypeInfo intel_hda_info_ich9 = {
    .name          = "ich9-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_ich9,
};

static const TypeInfo intel_hda_info_bxt = {
    .name          = "bxt-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_bxt,
};

static const TypeInfo intel_hda_info_apl = {
    .name          = "apl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_apl,
};

static const TypeInfo intel_hda_info_glk = {
    .name          = "glk-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_glk,
};

static const TypeInfo intel_hda_info_cnl = {
    .name          = "cnl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_cnl,
};

static const TypeInfo intel_hda_info_cfl = {
    .name          = "cfl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_cfl,
};

static const TypeInfo intel_hda_info_kbl = {
    .name          = "kbl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_kbl,
};

static const TypeInfo intel_hda_info_skl = {
    .name          = "skl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_skl,
};

static const TypeInfo intel_hda_info_icl = {
    .name          = "icl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_icl,
};

static const TypeInfo intel_hda_info_cml_lp = {
    .name          = "cml-lp-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_cml_lp,
};

static const TypeInfo intel_hda_info_cml_h = {
    .name          = "cml_h-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_cml_h,
};

static const TypeInfo intel_hda_info_tgl = {
    .name          = "tgl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_tgl,
};

static const TypeInfo intel_hda_info_ehl = {
    .name          = "ehl-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_ehl,
};

static void hda_codec_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = hda_codec_dev_realize;
    k->unrealize = hda_codec_dev_unrealize;
    set_bit(DEVICE_CATEGORY_SOUND, k->categories);
    k->bus_type = TYPE_HDA_BUS;
    k->props = hda_props;
}

static const TypeInfo hda_codec_device_type_info = {
    .name = TYPE_HDA_CODEC_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(HDACodecDevice),
    .abstract = true,
    .class_size = sizeof(HDACodecDeviceClass),
    .class_init = hda_codec_device_class_init,
};

/*
 * create intel hda controller with codec attached to it,
 * so '-soundhw hda' works.
 */
static int intel_hda_and_codec_init(PCIBus *bus)
{
    DeviceState *controller;
    BusState *hdabus;
    DeviceState *codec;

    controller = DEVICE(pci_create_simple(bus, -1, "intel-hda"));
    hdabus = QLIST_FIRST(&controller->child_bus);
    codec = qdev_create(hdabus, "hda-duplex");
    qdev_init_nofail(codec);
    return 0;
}

static void intel_hda_register_types(void)
{
    type_register_static(&hda_codec_bus_info);
    type_register_static(&intel_hda_info);
    type_register_static(&intel_hda_info_ich6);
    type_register_static(&intel_hda_info_ich9);
    type_register_static(&intel_hda_info_bxt);
    type_register_static(&intel_hda_info_apl);
    type_register_static(&intel_hda_info_glk);
    type_register_static(&intel_hda_info_cnl);
    type_register_static(&intel_hda_info_cfl);
    type_register_static(&intel_hda_info_skl);
    type_register_static(&intel_hda_info_kbl);
    type_register_static(&intel_hda_info_icl);
    type_register_static(&intel_hda_info_cml_lp);
    type_register_static(&intel_hda_info_cml_h);
    type_register_static(&intel_hda_info_tgl);
    type_register_static(&intel_hda_info_ehl);
    type_register_static(&hda_codec_device_type_info);
    pci_register_soundhw("hda", "Intel HD Audio", intel_hda_and_codec_init);
}

type_init(intel_hda_register_types)
