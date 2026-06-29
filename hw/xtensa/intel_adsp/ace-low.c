/* ACE low-address DSPMEM blocks
 * IP Region: low DSPMEM sub-blocks (IDC/HFINTIP/SOCCI/PMCCU side windows).
 *
 * Copyright (C) 2026 Intel Corporation
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
#include "qemu/log.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"

/* Moved from ace.h */
#define SHIM_DFIDCPP    0x2020  /* Discovery Feature ID - Device Configuration and Port Parameters */

/* CAP_INST[27:24] describes (instance_count - 1). */
#define ACE_LOW_DFIDCPP_DEFAULT 0x04000000u  /* Default: 5 cores (ACE 2.0/LNL) */
#define ACE15_LOW_DFIDCPP_DEFAULT 0x02000000u /* ACE 1.5/MTL: 3 cores */
#define ACE40_LOW_DFIDCPP_DEFAULT 0x01000000u /* ACE 4.0/NVL: 2 cores */

static uint32_t ace_low_dfidcpp_default(struct adsp_io_info *info)
{
    const struct adsp_desc *desc = info->adsp ? info->adsp->desc : NULL;

    if (!desc || !desc->name) {
        return ACE_LOW_DFIDCPP_DEFAULT;
    }

    /* Return SoC-specific core count in CAP_INST field. */
    if (!strcmp(desc->name, "ace15")) {
        return ACE15_LOW_DFIDCPP_DEFAULT;
    }
    if (!strcmp(desc->name, "ace40")) {
        return ACE40_LOW_DFIDCPP_DEFAULT;
    }

    return ACE_LOW_DFIDCPP_DEFAULT;
}

static void ace_block_common_init(struct adsp_dev *adsp, MemoryRegion *parent,
                                  struct adsp_io_info *info)
{
    hwaddr base = info->space->desc.base;
    hwaddr size = info->space->desc.size;

    if (base <= SHIM_DFIDCPP && SHIM_DFIDCPP < base + size) {
        /* Seed the fixed descriptor pointer value expected by FW probing these low blocks. */
        info->region[(SHIM_DFIDCPP - base) >> 2] = ace_low_dfidcpp_default(info);
    }

    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
}

static uint64_t ace_block_common_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr abs_addr = info->space->desc.base + addr;

    if (abs_addr == SHIM_DFIDCPP) {
        /* DFIDCPP is a discoverability pointer written during init;
         * return 0 on read so firmware can probe capabilities via init writes. */
        return 0x00000000;
    }

    return info->region[addr >> 2];
}

static void ace_block_common_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr abs_addr = info->space->desc.base + addr;

    if (abs_addr == SHIM_DFIDCPP) {
        /* DFIDCPP is fixed by hardware/strap and ignores software writes. */
        return;
    }

    /* All other low-block registers behave as simple scratch/configuration storage. */
    info->region[addr >> 2] = val;
}

#define DEFINE_ACE_BLOCK_IO(name)                                              \
    void ace_##name##_block_init(struct adsp_dev *adsp, MemoryRegion *parent, \
                                 struct adsp_io_info *info)                    \
    {                                                                          \
        ace_block_common_init(adsp, parent, info);                             \
    }                                                                          \
                                                                               \
    const MemoryRegionOps ace_##name##_block_ops = {                          \
        .read = ace_block_common_read,                                         \
        .write = ace_block_common_write,                                       \
        .endianness = DEVICE_NATIVE_ENDIAN,                                    \
    }

DEFINE_ACE_BLOCK_IO(idc_dsp);
DEFINE_ACE_BLOCK_IO(hfintip);
DEFINE_ACE_BLOCK_IO(socci);
DEFINE_ACE_BLOCK_IO(hfpmccu);
DEFINE_ACE_BLOCK_IO(hfpmcch);
DEFINE_ACE_BLOCK_IO(secpol);
DEFINE_ACE_BLOCK_IO(tsocfgu_aon);
DEFINE_ACE_BLOCK_IO(dfcapsts);
DEFINE_ACE_BLOCK_IO(adcip);
DEFINE_ACE_BLOCK_IO(adcs);
