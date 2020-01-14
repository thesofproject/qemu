/* Core Audio DSP
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
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

#ifndef __HW_ADSP_H__
#define __HW_ADSP_H__

#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "exec/memory.h"
#include "hw/adsp/fw.h"

/* Generic constants */
#define ADSP_MAX_IO                 32
#define ADSP_MAILBOX_SIZE			0x00001000
#define ADSP_MMIO_SIZE				0x00200000
#define ADSP_PCI_SIZE				0x00001000

struct adsp_dev;
struct adsp_gp_dmac;
struct adsp_log;
struct adsp_reg_space;
struct adsp_io_info;

struct adsp_mem_desc {
    const char *name;
	hwaddr base;
	size_t size;
	hwaddr alias;
	void *ptr;
};

/* Register descriptor */
struct adsp_reg_desc {
	const char *name;	/* register name */
	int enable;		/* enable logging for this register */
	uint32_t offset;	/* register offset */
	size_t size;		/* register/area size */
};

/* Device register space */
struct adsp_reg_space {
	const char *name;	/* device name */
	int reg_count;		/* number of registers */
	int irq;
	const struct adsp_reg_desc *reg;	/* array of register descriptors */
	struct adsp_mem_desc desc;
	void (*init)(struct adsp_dev *adsp, MemoryRegion *parent,
	    struct adsp_io_info *info);
	const MemoryRegionOps *ops;
};

struct adsp_io_info {
    MemoryRegion io;
    void *adsp;
    int io_dev;
    uint32_t *region;
    struct adsp_reg_space *space;
    void *private;
};

struct mem_zone {
        uint32_t base;
        uint32_t size;
        uint32_t host_offset;
};

struct adsp_desc {
	const char *name;	/* machine name */

	/* IRQs */
	int ia_irq;
	int ext_timer_irq;
	int pmc_irq;

	/* memory regions */
	int num_mem;
	struct adsp_mem_desc *mem_region;

	/* optional platform data */
	uint32_t imr_boot_ldr_offset;
	uint32_t file_offset;
	struct mem_zone mem_zones[SOF_FW_BLK_TYPE_NUM];

	/* devices */
	int num_io;
	struct adsp_reg_space *io_dev; /* misc device atm */

	struct adsp_dev_ops *ops;
};

int adsp_load_modules(struct adsp_dev *adsp, void *fw, size_t size);

struct adsp_dev;
struct adsp_host;

void adsp_create_memory_regions(struct adsp_dev *adsp);
void adsp_create_io_devices(struct adsp_dev *adsp, const MemoryRegionOps *ops);
void adsp_create_host_memory_regions(struct adsp_host *adsp);
void adsp_create_host_io_devices(struct adsp_host *adsp, const MemoryRegionOps *ops);
struct adsp_reg_space *adsp_get_io_space(struct adsp_dev *adsp, hwaddr addr);
struct adsp_mem_desc *adsp_get_mem_space(struct adsp_dev *adsp, hwaddr addr);

#endif
