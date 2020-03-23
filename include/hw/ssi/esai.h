/*
 * BSD 3 Clause - See LICENCE file for details.
 *
 * Copyright (c) 2015, Intel Corporation
 * Copyright 2020 NXP
 *
 * All rights reserved.
 */

#ifndef __ADSP_ESAI_H__
#define __ADSP_ESAI_H__

/* ESAI Register Map */
#define REG_ESAI_ETDR           0x00
#define REG_ESAI_ERDR           0x04
#define REG_ESAI_ECR            0x08
#define REG_ESAI_ESR            0x0C
#define REG_ESAI_TFCR           0x10
#define REG_ESAI_TFSR           0x14
#define REG_ESAI_RFCR           0x18
#define REG_ESAI_RFSR           0x1C
#define REG_ESAI_TX0            0x80
#define REG_ESAI_TX1            0x84
#define REG_ESAI_TX2            0x88
#define REG_ESAI_TX3            0x8C
#define REG_ESAI_TX4            0x90
#define REG_ESAI_TX5            0x94
#define REG_ESAI_TSR            0x98
#define REG_ESAI_RX0            0xA0
#define REG_ESAI_RX1            0xA4
#define REG_ESAI_RX2            0xA8
#define REG_ESAI_RX3            0xAC
#define REG_ESAI_SAISR          0xCC
#define REG_ESAI_SAICR          0xD0
#define REG_ESAI_TCR            0xD4
#define REG_ESAI_TCCR           0xD8
#define REG_ESAI_RCR            0xDC
#define REG_ESAI_RCCR           0xE0

struct adsp_dev;
struct adsp_gp_dmac;
struct adsp_log;
struct adsp_reg_space;

struct esai_fifo {
	uint32_t total_frames;
	uint32_t index;
	int fd;
	char file_name[64];
	uint32_t data[16];
	uint32_t level;
};

struct adsp_esai {
	char name[32];
	uint32_t *io;

	struct esai_fifo tx;
	struct esai_fifo rx;

	struct adsp_log *log;
	const struct adsp_reg_space *esai_dev;
};

#define ADSP_ESAI_REGS		1
extern const struct adsp_reg_desc adsp_esai_map[ADSP_ESAI_REGS];

struct adsp_esai *esai_get_port(int port);
void adsp_esai_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);
extern const MemoryRegionOps esai_ops;

#endif
