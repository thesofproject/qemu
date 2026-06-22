/*
 * QEMU ACE3.x TLB (Translation Lookaside Buffer) Implementation
 * IP Region: DfL2HSTLB entry RAM used for HP SRAM virtual-to-physical translation.
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
 *
 * The TLB provides 4096 entries for 4KB page translation
 * Virtual address range: 0xA0020000 - 0xA1XXXXXX
 * Physical address base: 0xBE000000 (HP SRAM)
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "qemu/log.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qobject/qdict.h"
#include "ace-tlb.h"
#include "ace-internal.h"

#define NUM_TLB_ENTRIES 4096
#define TLB_PAGE_SIZE 0x1000  /* 4KB pages */
#define TLB_PAGE_SHIFT 12

/* DfL2HSTLB is a 16-bit register array: L2HPSRAMTLBx @ 0x000 + 0x002 * x */
#define TLB_ENTRY_STRIDE  0x2
#define TLB_WRA           (1u << 15)
#define TLB_EXA           (1u << 14)
#define TLB_LOCK          (1u << 13)
#define TLB_ENABLE        (1u << 12)
#define TLB_PA_MASK       0x0fff
#define TLB_PHYS_PAGE_BASE 0xA0000

struct ace_tlb_state {
    uint16_t entries[NUM_TLB_ENTRIES];
    uint64_t ok_access_count;
    uint64_t invalid_access_count;
};

static struct ace_tlb_state tlb_state;

void ace_tlb_init(void)
{
    memset(&tlb_state, 0, sizeof(tlb_state));
    
    /* DfL2HSTLB default per spec:
     * WRA=1, EXA=1, LOCK=0, TLBE=1, TLBPA = X + 32 for implemented pages.
     * For page 0 this gives physical address 0xA0020000.
     */
    for (int i = 0; i < NUM_TLB_ENTRIES; i++) {
        tlb_state.entries[i] = TLB_WRA | TLB_EXA | TLB_ENABLE |
                               ((i + 32) & TLB_PA_MASK);
    }
    
    ace_log("TLB: Initialized %d entries with virt=phys identity mapping\n", NUM_TLB_ENTRIES);
    ace_log("TLB: Entry 0 maps virt 0xA0020000 -> phys 0xA0020000\n");
}

hwaddr ace_tlb_translate(hwaddr vaddr, bool *enabled)
{
    /* Check if address is in virtual HP SRAM range starting at 0xA0020000
     * Virtual range: 0xA0020000 - 0xA1020000 (16MB via TLB)
     */
    if (vaddr < 0xA0020000 || vaddr >= 0xA1020000) {
        /* Not in TLB HP-SRAM range, return unchanged */
        *enabled = false;
        return vaddr;
    }
    
    /* Calculate virtual page index from HP-SRAM base (0xA0020000)
     * TLB entries start at virtual address 0xA0020000
     */
    uint32_t virt_offset = vaddr - 0xA0020000;
    uint32_t v_page_idx = virt_offset >> TLB_PAGE_SHIFT;
    
    if (v_page_idx >= NUM_TLB_ENTRIES) {
        ace_log("TLB: Virtual page index %u exceeds max %d for vaddr 0x%lx\n",
                 v_page_idx, NUM_TLB_ENTRIES - 1, vaddr);
        tlb_state.invalid_access_count++;
        *enabled = false;
        return vaddr;  /* Return original address */
    }
    
    uint16_t entry = tlb_state.entries[v_page_idx];
    
    if (!(entry & TLB_ENABLE)) {
        ace_log("TLB: Accessing disabled TLB entry %u for vaddr 0x%lx\n",
                 v_page_idx, vaddr);
        tlb_state.invalid_access_count++;
        *enabled = false;
        return vaddr;
    }
    
    /* Translate using TLB entry
     * The pa field stores the physical page number in the full 32-bit address space
     * Physical address = (pa << 12) + page_offset
     * 
     * At boot with identity mapping:
     *   TLB entry 0: pa = 0xA0020, maps virt 0xA0020000 -> phys 0xA0020000
     *   TLB entry i: pa = 0xA0020+i, maps virt 0xA0020000+(i*4KB) -> phys 0xA0020000+(i*4KB)
     */
    uint32_t p_page_num = TLB_PHYS_PAGE_BASE + (entry & TLB_PA_MASK);
    uint32_t page_offset = vaddr & (TLB_PAGE_SIZE - 1);
    
    /* Construct physical address from page number */
    hwaddr paddr = ((hwaddr)p_page_num << TLB_PAGE_SHIFT) + page_offset;
    
    tlb_state.ok_access_count++;
    *enabled = true;
    
    /* Log first page or non-identity mappings */
    if (v_page_idx == 0 || p_page_num != (0xA0020 + v_page_idx)) {
        ace_log("TLB: Translated vaddr 0x%lx -> paddr 0x%lx (entry=%u, pa_page=0x%x, offset=0x%x)\n",
                 vaddr, paddr, v_page_idx, p_page_num, page_offset);
    }
    
    return paddr;
}

uint32_t ace_tlb_read(hwaddr addr)
{
    uint32_t entry_idx = addr / TLB_ENTRY_STRIDE;
    
    if (entry_idx >= NUM_TLB_ENTRIES) {
        ace_log("TLB: Read from invalid entry %u\n", entry_idx);
        return 0;
    }

    return tlb_state.entries[entry_idx];
}

void ace_tlb_write(hwaddr addr, uint32_t val)
{
    uint32_t entry_idx = addr / TLB_ENTRY_STRIDE;
    
    if (entry_idx >= NUM_TLB_ENTRIES) {
        ace_log("TLB: Write to invalid entry %u\n", entry_idx);
        return;
    }
    
    uint16_t old_entry = tlb_state.entries[entry_idx];
    
    /* Check if locked */
    if ((old_entry & TLB_LOCK) && !(val & TLB_LOCK)) {
        ace_log("TLB: Attempted write to locked entry %u\n", entry_idx);
        return;
    }

    tlb_state.entries[entry_idx] = val & 0xffff;

    ace_log("TLB: Entry %u write=0x%04x [WRA=%u EXA=%u LOCK=%u TLBE=%u TLBPA=0x%x]\n",
             entry_idx, tlb_state.entries[entry_idx],
             !!(tlb_state.entries[entry_idx] & TLB_WRA),
             !!(tlb_state.entries[entry_idx] & TLB_EXA),
             !!(tlb_state.entries[entry_idx] & TLB_LOCK),
             !!(tlb_state.entries[entry_idx] & TLB_ENABLE),
             tlb_state.entries[entry_idx] & TLB_PA_MASK);
}

static uint64_t ace_tlb_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    hwaddr aligned_addr = addr & ~1;
    uint16_t entry = ace_tlb_read(aligned_addr);

    switch (size) {
    case 1:
        return (addr & 1) ? (entry >> 8) & 0xff : entry & 0xff;
    case 2:
        return entry;
    case 4:
        return entry | ((uint32_t)ace_tlb_read(aligned_addr + 2) << 16);
    default:
        ace_log("TLB: unsupported read size %u at 0x%lx\n", size,
                 (unsigned long)addr);
        return 0;
    }
}

static void ace_tlb_mmio_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    hwaddr aligned_addr = addr & ~1;
    uint16_t old_entry;
    uint16_t new_entry;

    switch (size) {
    case 1:
        old_entry = ace_tlb_read(aligned_addr);
        if (addr & 1) {
            new_entry = (old_entry & 0x00ff) | (((uint16_t)val & 0xff) << 8);
        } else {
            new_entry = (old_entry & 0xff00) | ((uint16_t)val & 0xff);
        }
        ace_tlb_write(aligned_addr, new_entry);
        break;
    case 2:
        ace_tlb_write(aligned_addr, (uint16_t)val);
        break;
    case 4:
        ace_tlb_write(aligned_addr, (uint16_t)(val & 0xffff));
        ace_tlb_write(aligned_addr + 2, (uint16_t)((val >> 16) & 0xffff));
        break;
    default:
        ace_log("TLB: unsupported write size %u at 0x%lx\n", size,
                 (unsigned long)addr);
        break;
    }
}

void ace_tlb_mmio_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    ace_log("%s: initialized at 0x%x size=0x%x\n",
             info->name, info->space->desc.base, info->space->desc.size);
}

const MemoryRegionOps ace_tlb_ops = {
    .read = ace_tlb_mmio_read,
    .write = ace_tlb_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void ace_tlb_stats(void)
{
    if (tlb_state.ok_access_count + tlb_state.invalid_access_count > 0) {
        ace_log("TLB Statistics:\n");
        ace_log("  Valid accesses: %lu\n", tlb_state.ok_access_count);
        ace_log("  Invalid accesses: %lu\n", tlb_state.invalid_access_count);
        
        if (tlb_state.invalid_access_count > 0) {
            double ratio = (double)tlb_state.invalid_access_count / 
                          (double)(tlb_state.ok_access_count + tlb_state.invalid_access_count);
            ace_log("  Invalid access ratio: %.2f%%\n", ratio * 100.0);
        }
    }
}

void hmp_info_ace_tlb(Monitor *mon, const QDict *qdict)
{
    int i;
    monitor_printf(mon, "Intel ADSP ACE TLB Mappings (%d entries):\n", NUM_TLB_ENTRIES);
    for (i = 0; i < NUM_TLB_ENTRIES; i++) {
        uint16_t entry = tlb_state.entries[i];
        if (entry & TLB_ENABLE) {
            uint32_t p_page_num = TLB_PHYS_PAGE_BASE + (entry & TLB_PA_MASK);
            hwaddr vaddr = 0xA0020000 + (i * TLB_PAGE_SIZE);
            hwaddr paddr = (hwaddr)p_page_num << TLB_PAGE_SHIFT;
            monitor_printf(mon, "  entry %4d: virt 0x%08lx -> phys 0x%08lx "
                           "[WRA=%u EXA=%u LOCK=%u]%s\n",
                           i, (unsigned long)vaddr, (unsigned long)paddr,
                           !!(entry & TLB_WRA), !!(entry & TLB_EXA), !!(entry & TLB_LOCK),
                           (vaddr == paddr) ? " (direct)" : "");
        }
    }
}
