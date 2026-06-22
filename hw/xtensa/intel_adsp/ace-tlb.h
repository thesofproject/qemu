/*
 * QEMU ACE3.x TLB (Translation Lookaside Buffer) Interface
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

#ifndef HW_XTENSA_ACE_TLB_H
#define HW_XTENSA_ACE_TLB_H

#include "qemu/osdep.h"
#include "system/memory.h"

struct adsp_dev;
struct adsp_io_info;

/* Initialize TLB with identity mapping */
void ace_tlb_init(void);

/* Translate virtual address to physical address
 * Returns physical address and sets enabled flag
 */
hwaddr ace_tlb_translate(hwaddr vaddr, bool *enabled);

/* TLB MMIO register access */
uint32_t ace_tlb_read(hwaddr addr);
void ace_tlb_write(hwaddr addr, uint32_t val);
void ace_tlb_mmio_init(struct adsp_dev *adsp, MemoryRegion *parent,
	struct adsp_io_info *info);
extern const MemoryRegionOps ace_tlb_ops;

/* Print TLB statistics */
void ace_tlb_stats(void);

#endif /* HW_XTENSA_ACE_TLB_H */
