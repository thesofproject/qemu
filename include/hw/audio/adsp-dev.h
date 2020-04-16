/* Core register IO tracing
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

#ifndef __ADSP_XTENSA_H__
#define __ADSP_XTENSA_H__

#include "hw/adsp/hw.h"

struct adsp_xtensa;
struct adsp_io_info;

#define ADSP_MAX_CORES	4
#define ADSP_MAX_EXT_TIMERS	4

struct adsp_dev_timer {
	QEMUTimer *timer;
	uint32_t clk_kHz;
	int64_t start;
	struct adsp_io_info *info;
};

struct adsp_dev_ops {
    void (*irq_set)(struct adsp_io_info *info, int irq, uint32_t mask);
    void (*irq_clear)(struct adsp_io_info *info, int irq, uint32_t mask);
};

struct adsp_dev {

    struct adsp_io_info *mu;
    struct adsp_io_info *shim;
    int shm_idx;

	/* runtime CPU */
	struct adsp_xtensa *xtensa[ADSP_MAX_CORES];
	bool cpu_stalled;
	bool in_reset;
	MemoryRegion *system_memory;
	QemuOpts *machine_opts;

	/* machine init data */
	const struct adsp_desc *desc;
	const char *cpu_model;
	const char *kernel_filename;
        const char *rom_filename;
	uint32_t clk_kHz;

	/* logging options */
	struct adsp_log *log;

	/* ext timer */
	struct adsp_dev_timer timer[ADSP_MAX_EXT_TIMERS];

	/* PMC */
	QemuThread pmc_thread;
	uint32_t pmc_cmd;

	/* ops */
	struct adsp_dev_ops *ops;
};

void adsp_set_lvl1_irq(struct adsp_dev *adsp, int irq, int active);

static inline void adsp_irq_set(struct adsp_dev *adsp,
    struct adsp_io_info *info, int irq, uint32_t mask)
{
    if (adsp->ops->irq_set)
        adsp->ops->irq_set(info, irq, mask);
}
static inline void adsp_irq_clear(struct adsp_dev *adsp,
    struct adsp_io_info *info, int irq, uint32_t mask)
{
    if (adsp->ops->irq_clear)
        adsp->ops->irq_clear(info, irq, mask);
}

#endif
