/* ACE host-window register virtualization
 * IP Region: ACE3 DTF core register block split across two 0x20 host apertures.
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
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"

#define ace_region(raddr)    info->region[(raddr) >> 2]

#define DTFXCTL_RESET         (2u << 2)
#define DTFXSTS_RESET         ((2u << 20) | (1u << 17))
#define DTFXSFFSTS_RESET      (0x80u << 16)

static hwaddr ace_hostwin_global_offset(struct adsp_io_info *info, hwaddr addr)
{
    if (info->space->desc.base == ADSP_ACE30_DSP_HOST_WIN_BASE(1)) {
        return addr + ADSP_ACE30_DSP_HOST_WIN_SIZE;
    }

    return addr;
}

static const char *ace_hostwin_reg_name(hwaddr global_addr)
{
    switch (global_addr & ~7ULL) {
    case DTFXCTL_OFFSET:
        return "DTFXCTL";
    case DTFXSTS_OFFSET:
        return "DTFXSTS";
    case DTFXSFFCTL_OFFSET:
        return "DTFXSFFCTL";
    case DTFXSFFSTS_OFFSET:
        return "DTFXSFFSTS";
    case DTFCXD64TS_OFFSET:
        return "DTFCXD64TS";
    case DTFCXD64_OFFSET:
        return "DTFCXD64";
    case DTFCXD64M_OFFSET:
        return "DTFCXD64M";
    case DTFCXD64DMA_OFFSET:
        return "DTFCXD64DMA";
    default:
        return "DTF-RESERVED";
    }
}

static bool ace_hostwin_is_qword_reg(hwaddr global_addr)
{
    switch (global_addr & ~7ULL) {
    case DTFCXD64TS_OFFSET:
    case DTFCXD64_OFFSET:
    case DTFCXD64M_OFFSET:
    case DTFCXD64DMA_OFFSET:
        return true;
    default:
        return false;
    }
}

static bool ace_hostwin_write_qword(struct adsp_io_info *info, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    hwaddr qword_base = addr & ~7ULL;
    uint32_t byte_offset = addr & 7;
    uint64_t qword = ((uint64_t)ace_region(qword_base + 4) << 32) |
                     ace_region(qword_base);
    uint64_t mask;

    if ((size != 1 && size != 2 && size != 4 && size != 8) ||
        byte_offset + size > 8) {
        return false;
    }

    if (size == 8) {
        qword = val;
    } else {
        mask = ((1ULL << (size * 8)) - 1) << (byte_offset * 8);
        qword = (qword & ~mask) | ((val << (byte_offset * 8)) & mask);
    }

    ace_region(qword_base) = (uint32_t)qword;
    ace_region(qword_base + 4) = (uint32_t)(qword >> 32);
    return true;
}

void ace_hostwin_init(struct adsp_dev *adsp, MemoryRegion *parent,
                       struct adsp_io_info *info)
{
    if (info->space->desc.base == ADSP_ACE30_DSP_HOST_WIN_BASE(0)) {
        /* DTF control/status window. */
        ace_region(DTFXCTL_OFFSET) = DTFXCTL_RESET;
        ace_region(DTFXSTS_OFFSET) = DTFXSTS_RESET;
        ace_region(DTFXSFFCTL_OFFSET) = 0;
        ace_region(DTFXSFFSTS_OFFSET) = DTFXSFFSTS_RESET;
    } else if (info->space->desc.base == ADSP_ACE30_DSP_HOST_WIN_BASE(1)) {
        /* DTF message-push window. */
        ace_region(0x00) = 0;
        ace_region(0x04) = 0;
        ace_region(0x08) = 0;
        ace_region(0x0c) = 0;
        ace_region(0x10) = 0;
        ace_region(0x14) = 0;
        ace_region(0x18) = 0;
        ace_region(0x1c) = 0;
    }

    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
}

static uint64_t ace_hostwin_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr global_addr = ace_hostwin_global_offset(info, addr);
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t word;
    uint64_t val;

    if (ace_hostwin_is_qword_reg(global_addr) ||
        ((global_addr & ~3ULL) == DTFXSFFCTL_OFFSET)) {
        ace_log("%s read: %s is write-only addr=0x%lx size=%u\n",
                 info->name, ace_hostwin_reg_name(global_addr),
                 (unsigned long)addr, size);
        return 0;
    }

    word = ace_region(aligned_addr);

    if (!ace_extract_subword_read(word, size, byte_offset, &val)) {
        ace_log("%s read: unsupported size %u at addr=0x%lx\n",
                 info->name, size, (unsigned long)addr);
        return 0;
    }

    ace_log("%s read: %s addr=0x%lx size=%u val=0x%lx\n",
             info->name, ace_hostwin_reg_name(global_addr),
             (unsigned long)addr, size, (unsigned long)val);

    return val;
}

static void ace_hostwin_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr global_addr = ace_hostwin_global_offset(info, addr);
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t old_word = ace_region(aligned_addr);
    uint32_t new_word = old_word;

    if (ace_hostwin_is_qword_reg(global_addr)) {
        if (!ace_hostwin_write_qword(info, addr, val, size)) {
            ace_log("%s write: unsupported size %u for %s at addr=0x%lx\n",
                     info->name, size, ace_hostwin_reg_name(global_addr),
                     (unsigned long)addr);
            return;
        }

        ace_log("%s write: %s addr=0x%lx size=%u val=0x%lx\n",
                 info->name, ace_hostwin_reg_name(global_addr),
                 (unsigned long)addr, size, (unsigned long)val);
        return;
    }

    if (!ace_merge_subword_write(old_word, val, size, byte_offset,
                                 &new_word)) {
        ace_log("%s write: unsupported size %u at addr=0x%lx\n",
                 info->name, size, (unsigned long)addr);
        return;
    }

    switch (global_addr & ~3ULL) {
    case DTFXCTL_OFFSET:
        ace_region(aligned_addr) = new_word & DTFXCTL_RW_MASK;
        break;
    case DTFXSTS_OFFSET:
    case DTFXSFFSTS_OFFSET:
        ace_log("%s write: ignored read-only %s addr=0x%lx size=%u val=0x%lx\n",
                 info->name, ace_hostwin_reg_name(global_addr),
                 (unsigned long)addr, size, (unsigned long)val);
        return;
    case DTFXSFFCTL_OFFSET:
        ace_region(aligned_addr) = new_word & DTFXSFFCTL_RW_MASK;
        break;
    default:
        ace_region(aligned_addr) = new_word;
        break;
    }

    ace_log("%s write: %s addr=0x%lx size=%u val=0x%lx\n",
             info->name, ace_hostwin_reg_name(global_addr),
             (unsigned long)addr, size, (unsigned long)val);
}

const MemoryRegionOps ace_hostwin_io_ops = {
    .read = ace_hostwin_read,
    .write = ace_hostwin_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void hmp_info_ace_window(Monitor *mon, const QDict *qdict)
{
    uint64_t windows[4] = {0};
    const char *reg_names[] = {"DTFCXD64TS", "DTFCXD64", "DTFCXD64M", "DTFCXD64DMA"};
    
    cpu_physical_memory_read(ADSP_ACE30_DSP_HOST_WIN_BASE(0) + DTFCXD64TS_OFFSET, &windows[0], 8);
    cpu_physical_memory_read(ADSP_ACE30_DSP_HOST_WIN_BASE(0) + DTFCXD64_OFFSET, &windows[1], 8);
    cpu_physical_memory_read(ADSP_ACE30_DSP_HOST_WIN_BASE(0) + DTFCXD64M_OFFSET, &windows[2], 8);
    cpu_physical_memory_read(ADSP_ACE30_DSP_HOST_WIN_BASE(0) + DTFCXD64DMA_OFFSET, &windows[3], 8);

    monitor_printf(mon, "Intel ADSP ACE SRAM Host Window Configuration:\n");
    monitor_printf(mon, "  %-4s %-12s %-18s %-12s\n", "WIN", "REGISTER", "RAW QWORD", "MAPPED BASE");

    for (int i = 0; i < 4; i++) {
        uint32_t mapped_addr = (uint32_t)(windows[i] & 0xFFFFFFFF);
        monitor_printf(mon, "  %-4u %-12s 0x%016" PRIx64 " 0x%08x\n", 
                       i, reg_names[i], windows[i], mapped_addr);
    }
}
