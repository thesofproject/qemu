/* ACE HFIMR register virtualization
 * IP Region: Host-facing IMR aperture base/size discovery registers.
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

#define ace_region(raddr)    info->region[(raddr) >> 2]

#define HFIMR_IA1_OFFSET 0x88
#define HFIMR_IS1_OFFSET 0x8c
#define HFIMR_IA1_ADDR   0x1a000000
/* IA1/IS1 register pair:
 * - IA1: aperture base address exposed to host software
 * - IS1: aperture attributes (IU/RS) and SZ size coding.
 */

struct ace_hfimria1 {
    uint64_t addr;
};

struct ace_hfimris1 {
    uint64_t iu : 1;
    uint64_t rs : 3;
    uint64_t rsvd31 : 28;
    uint64_t sz : 32;
};

static struct adsp_io_info *g_hfimr_info;

void ace30_hfimr_init(struct adsp_dev *adsp, MemoryRegion *parent,
                        struct adsp_io_info *info)
{
    struct ace_hfimria1 ia1 = {
        .addr = HFIMR_IA1_ADDR,
    };
    struct ace_hfimris1 is1 = {
        .iu = 1,
        .rs = 0,
        .sz = 16 * 1024 / 1024,
    };
    
    g_hfimr_info = info;

    /* IA1 exposes the aperture base that host firmware uses for the IMR window. */
    memcpy(&ace_region(HFIMR_IA1_OFFSET), &ia1, sizeof(ia1));
    /* IS1 advertises the corresponding aperture size and attributes. */
    memcpy(&ace_region(HFIMR_IS1_OFFSET), &is1, sizeof(is1));

    ace_log("HFIMR: Initialized at 0x%x\n", ADSP_ACE30_DSP_HFIMR_BASE);
}

static uint64_t ace30_hfimr_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t word = ace_region(aligned_addr);
    uint32_t byte_offset = addr & 3;
    uint64_t val;

    if (!ace_extract_subword_read(word, size, byte_offset, &val)) {
        ace_log( "HFIMR read: unsupported size %u\n", size);
        val = 0;
    }

    ace_log( "HFIMR read: addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)addr, size, (unsigned long)val, byte_offset);
    return val;
}

static void ace30_hfimr_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t word;

    ace_log( "HFIMR write: addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)addr, size, (unsigned long)val, byte_offset);

    /* HFIMR words are side-effect free in this model, so writes are direct RMW updates. */
    word = ace_region(aligned_addr);
    if (!ace_merge_subword_write(word, val, size, byte_offset, &word)) {
        ace_log( "HFIMR write: unsupported size %u\n", size);
        return;
    }

    /* Sub-word accesses use read-modify-write so adjacent fields are preserved. */
    ace_region(aligned_addr) = word;
}

const MemoryRegionOps ace30_hfimr_io_ops = {
    .read = ace30_hfimr_read,
    .write = ace30_hfimr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qobject/qdict.h"

void hmp_info_ace_imr(Monitor *mon, const QDict *qdict)
{
    bool scan = qdict_get_try_bool(qdict, "scan", false);
    struct ace_hfimria1 *ia1;
    struct ace_hfimris1 *is1;
    uint32_t sz_mb;
    uint32_t size_bytes;
    uint64_t base;

    if (!g_hfimr_info) {
        monitor_printf(mon, "Intel ADSP ACE IMR Configuration:\n");
        monitor_printf(mon, "  [HFIMR device not initialized on this platform]\n");
        return;
    }

    ia1 = (struct ace_hfimria1 *)&g_hfimr_info->region[HFIMR_IA1_OFFSET >> 2];
    is1 = (struct ace_hfimris1 *)&g_hfimr_info->region[HFIMR_IS1_OFFSET >> 2];
    base = ia1->addr;
    sz_mb = is1->sz;
    size_bytes = sz_mb * 1024 * 1024;

    monitor_printf(mon, "Intel ADSP ACE IMR Configuration:\n");
    monitor_printf(mon, "  IMR 0: Base 0x%08lx, Size %dMB, Attributes: (IU=%d, RS=%d)\n",
                   (unsigned long)base, sz_mb, is1->iu, is1->rs);

    if (scan && size_bytes > 0) {
        int tot_pages = 0, tot_used = 0;
        /* IMR host aperture represents what the host sees. For DSP memory scanning via 
           address_space_read, we must use the DSP-mapped base (ADSP_ACE30_DSP_IMR_BASE) */
        uint64_t scan_base = ADSP_ACE30_DSP_IMR_BASE;
        
        /* Using state=1 (SRAM_POWER_ON Equivalent) to force scanning logic on IMR */
        adsp_scan_and_print_memory(mon, scan_base, size_bytes, &tot_pages, &tot_used, 1, "IMR", 0);
        
        if (tot_pages > 0) {
            monitor_printf(mon, "  IMR Summary: %d / %d pages used (%.1f%%)\n", 
                           tot_used, tot_pages, (float)tot_used * 100.0f / tot_pages);
        }
    }
}

