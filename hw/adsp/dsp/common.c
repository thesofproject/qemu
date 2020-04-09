/* Core common support for audio DSP.
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

#include "qemu/io-bridge.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/fw.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/log.h"
#include "common.h"

/* In ASCII `XMan` */
#define SND_SOF_EXT_MAN_MAGIC_NUMBER	0x6e614d58

void adsp_set_lvl1_irq(struct adsp_dev *adsp, int irq, int active)
{
    /* TODO: allow interrupts other cores than core 0 */
    CPUXtensaState *env = adsp->xtensa[0]->env;
    uint32_t irq_bit = 1 << irq;

    if (active) {
        env->sregs[INTSET] |= irq_bit;
    } else if (env->config->interrupt[irq].inttype == INTTYPE_LEVEL) {
        env->sregs[INTSET] &= ~irq_bit;
    }

    check_interrupts(env);
}

#define SST_FW_SIG_SIZE		4
#define SST_HSW_FW_SIGN		"$SST"
#define SST_HSW_IRAM	1
#define SST_HSW_DRAM	2
#define SST_HSW_REGS	3

/*
 * Firmware module is made up of 1 . N blocks of different types. The
 * Block header is used to determine where and how block is to be copied in the
 * DSP/host memory space.
 */

struct snd_sst_dma_block_info {
	uint32_t type;		/* IRAM/DRAM */
	uint32_t size;		/* Bytes */
	uint32_t offset;	/* Offset in I/DRAM */
	uint32_t rsvd;		/* Reserved field */
} __attribute__((packed));

struct snd_sst_fw_module_info {
	uint32_t persistent_size;
	uint32_t scratch_size;
} __attribute__((packed));

struct snd_sst_fw_header {
	char sig[SST_FW_SIG_SIZE]; /* FW signature */
	uint32_t file_size;		/* size of fw minus this header */
	uint32_t num_modules;		/*  # of modules */
	uint32_t file_format;	/* version of header format */
	uint32_t reserved[4];
} __attribute__((packed));

struct snd_sst_fw_module_header {
	char sig[SST_FW_SIG_SIZE]; /* module signature */
	uint32_t size;	/* size of module */
	uint32_t num_blocks;	/* # of blocks */
	uint16_t padding;
	uint16_t type;	/* codec type, pp lib */
	uint32_t entry_point;
	struct snd_sst_fw_module_info info;
} __attribute__((packed));


/* generic module parser for mmaped DSPs */
static int sof_module_memcpy(struct adsp_dev *adsp,
				struct snd_sof_mod_hdr *module)
{
	const struct adsp_desc *board = adsp->desc;
	struct snd_sof_blk_hdr *block;
	struct adsp_mem_desc *mem;
	int count;

	fprintf(stdout, "new module size 0x%x blocks 0x%x type 0x%x\n",
		module->size, module->num_blocks, module->type);

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->num_blocks; count++) {
		if (block->size == 0) {
			fprintf(stderr,
				 "warning: block %d size zero\n", count);
			fprintf(stderr, " type 0x%x offset 0x%x\n",
				 block->type, block->offset);
			continue;
		}

		switch (block->type) {
		case SOF_FW_BLK_TYPE_RSRVD0:
		case SOF_FW_BLK_TYPE_ROM:
		case SOF_FW_BLK_TYPE_IMR:
		case SOF_FW_BLK_TYPE_RSRVD6:
			continue;	/* not handled atm */
		case SOF_FW_BLK_TYPE_IRAM:
		case SOF_FW_BLK_TYPE_DRAM:
		case SOF_FW_BLK_TYPE_SRAM:
			fprintf(stdout, "data: 0x%x size 0x%x\n",
				board->mem_zones[block->type].base + block->offset - board->mem_zones[block->type].host_offset,
				block->size);

			mem = adsp_get_mem_space(adsp, board->mem_zones[block->type].base + block->offset - board->mem_zones[block->type].host_offset);
			if (!mem)
				goto next;
			memcpy(mem->ptr + block->offset - board->mem_zones[block->type].host_offset,
				(void *)block + sizeof(*block), block->size);
			break;
		default:
			fprintf(stderr, "error: bad type 0x%x for block 0x%x\n",
				block->type, count);
			return -EINVAL;
		}

		fprintf(stdout,
			"block %d type 0x%x size 0x%x ==>  offset 0x%x\n",
			count, block->type, block->size, block->offset);

next:
		/* next block */
		block = (void *)block + sizeof(*block) + block->size;
	}

	return 0;
}

static int sst_module_memcpy(struct adsp_dev *adsp,
				struct snd_sst_fw_module_header *module)
{
	const struct adsp_desc *board = adsp->desc;
	struct snd_sst_dma_block_info *block;
	uint32_t offset;
	int count;

	fprintf(stdout, "new module size 0x%x blocks 0x%x type 0x%x\n",
		module->size, module->num_blocks, module->type);

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->num_blocks; count++) {
		if (block->size == 0) {
			fprintf(stderr,
				 "warning: block %d size zero\n", count);
			fprintf(stderr, " type 0x%x offset 0x%x\n",
				 block->type, block->offset);
			continue;
		}

		switch (block->type) {
		case SST_HSW_REGS:
			continue;	/* not handled atm */
		case SST_HSW_IRAM:
			fprintf(stdout, "text: 0x%x size 0x%x\n",
				block->offset,
				block->size);
			offset = board->mem_zones[block->type].host_offset + block->offset;
			cpu_physical_memory_write(board->mem_zones[block->type].base + block->offset,
				(void *)block + sizeof(*block), block->size);
			break;
		case SST_HSW_DRAM:
			fprintf(stdout, "data: 0x%x size 0x%x\n",
				board->mem_zones[block->type].base + block->offset - board->mem_zones[block->type].host_offset,
				block->size);
			offset = board->mem_zones[block->type].host_offset + block->offset;
			cpu_physical_memory_write(board->mem_zones[block->type].base + block->offset,
				(void *)block + sizeof(*block), block->size);
			break;
		default:
			fprintf(stderr, "error: bad type 0x%x for block 0x%x\n",
				block->type, count);
			return -EINVAL;
		}

		fprintf(stdout,
			"block %d type 0x%x size 0x%x ==>  offset 0x%x\n",
			count, block->type, block->size, offset);


		/* next block */
		block = (void *)block + sizeof(*block) + block->size;
	}

	return 0;
}

static int sof_check_header(struct adsp_dev *adsp,
	struct snd_sof_fw_header *header, size_t size)
{

	/* verify FW sig */
	if (strncmp(header->sig, SND_SOF_FW_SIG, SND_SOF_FW_SIG_SIZE) != 0)
		return -EINVAL;

	/* check size is valid */
	if (size != header->file_size + sizeof(*header)) {
		fprintf(stderr, "error: invalid filesize mismatch got 0x%lx expected 0x%lx\n",
			size, header->file_size + sizeof(*header));
		return -EINVAL;
	}

	fprintf(stdout, "header size=0x%x modules=0x%x abi=0x%x size=%zu\n",
		header->file_size, header->num_modules,
		header->abi, sizeof(*header));

	return 0;
}

static int sst_check_header(struct adsp_dev *adsp,
	struct snd_sst_fw_header *header, size_t size)
{

	/* verify FW sig */
	if (strncmp(header->sig, SST_HSW_FW_SIGN, SST_FW_SIG_SIZE) != 0)
		return -EINVAL;

	/* check size is valid */
	if (size != header->file_size + sizeof(*header)) {
		fprintf(stderr, "error: invalid filesize mismatch got 0x%lx expected 0x%lx\n",
			size, header->file_size + sizeof(*header));
		return -EINVAL;
	}

	fprintf(stdout, "header size=0x%x modules=0x%x size=%zu\n",
		header->file_size, header->num_modules,
		sizeof(*header));

	return 0;
}

static int sof_load_modules(struct adsp_dev *adsp, void *fw,
	struct snd_sof_fw_header *header, size_t size)
{
	struct snd_sof_mod_hdr *module;
	int ret, count;

	/* parse each module */
	module = fw + sizeof(*header);
	for (count = 0; count < header->num_modules; count++) {
		/* module */
		ret = sof_module_memcpy(adsp, module);
		if (ret < 0) {
			fprintf(stderr, "error: invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->size;
	}

	return 0;
}

static int sst_load_modules(struct adsp_dev *adsp, void *fw,
	struct snd_sst_fw_header *header, size_t size)
{
	struct snd_sst_fw_module_header *module;
	int ret, count;

	/* parse each module */
	module = fw + sizeof(*header);
	for (count = 0; count < header->num_modules; count++) {
		/* module */
		ret = sst_module_memcpy(adsp, module);
		if (ret < 0) {
			fprintf(stderr, "error: invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->size;
	}

	return 0;
}

uint32_t adsp_get_ext_man_size(const uint32_t *fw)
{
	/*
	 * When fw points to extended manifest,
	 * then first u32 must be equal SOF_EXT_MAN_MAGIC_NUMBER.
	 */
	if (fw[0] == SOF_EXT_MAN_MAGIC_NUMBER)
		return fw[1];

	/* otherwise given fw don't have an extended manifest */
	return 0;
}

int adsp_load_modules(struct adsp_dev *adsp, void *fw, size_t size)
{
	struct snd_sof_fw_header *sof_header;
	struct snd_sst_fw_header *sst_header;
	int ext_man_size;
	int ret;

	ext_man_size = adsp_get_ext_man_size(fw);
	fw = (uint8_t *)fw + ext_man_size;
	size -= ext_man_size;

	sof_header = (struct snd_sof_fw_header *)fw;
	ret = sof_check_header(adsp, sof_header, size);
	if (ret == 0)
		return sof_load_modules(adsp, fw, sof_header, size);

	sst_header = (struct snd_sst_fw_header *)fw;
	ret = sst_check_header(adsp, sst_header, size);
	if (ret == 0)
		return sst_load_modules(adsp, fw, sst_header, size);

	fprintf(stderr, "error: invalid firmware signature\n");
	return -EINVAL;
}

