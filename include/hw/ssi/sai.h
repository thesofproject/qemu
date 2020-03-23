/*
 * BSD 3 Clause - See LICENCE file for details.
 *
 * Copyright (c) 2020 NXP
 * All rights reserved.
 */

#ifndef __SAI_H__
#define __SAI_H__

#ifdef CONFIG_IMX8M
#define SAI_OFS         8
#else
#define SAI_OFS         0
#endif

/* SAI registers start */
/* SAI register offsets */
#define REG_SAI_VERID  0x00 /* SAI Version ID Register */
#define REG_SAI_PARAM  0x04 /* SAI Parameter Register */

#define REG_SAI_TCSR   (0x00 + SAI_OFS) /* SAI Transmit Control */
#define REG_SAI_TCR1   (0x04 + SAI_OFS) /* SAI Transmit Configuration 1 */
#define REG_SAI_TCR2   (0x08 + SAI_OFS) /* SAI Transmit Configuration 2 */
#define REG_SAI_TCR3   (0x0c + SAI_OFS) /* SAI Transmit Configuration 3 */
#define REG_SAI_TCR4   (0x10 + SAI_OFS) /* SAI Transmit Configuration 4 */
#define REG_SAI_TCR5   (0x14 + SAI_OFS) /* SAI Transmit Configuration 5 */

#define REG_SAI_TDR0   0x20 /* SAI Transmit Data */
#define REG_SAI_TDR1   0x24 /* SAI Transmit Data */
#define REG_SAI_TDR2   0x28 /* SAI Transmit Data */
#define REG_SAI_TDR3   0x2C /* SAI Transmit Data */
#define REG_SAI_TDR4   0x30 /* SAI Transmit Data */
#define REG_SAI_TDR5   0x34 /* SAI Transmit Data */
#define REG_SAI_TDR6   0x38 /* SAI Transmit Data */
#define REG_SAI_TDR7   0x3C /* SAI Transmit Data */
#define REG_SAI_TFR0   0x40 /* SAI Transmit FIFO */
#define REG_SAI_TFR1   0x44 /* SAI Transmit FIFO */
#define REG_SAI_TFR2   0x48 /* SAI Transmit FIFO */
#define REG_SAI_TFR3   0x4C /* SAI Transmit FIFO */
#define REG_SAI_TFR4   0x50 /* SAI Transmit FIFO */
#define REG_SAI_TFR5   0x54 /* SAI Transmit FIFO */
#define REG_SAI_TFR6   0x58 /* SAI Transmit FIFO */
#define REG_SAI_TFR7   0x5C /* SAI Transmit FIFO */
#define REG_SAI_TMR    0x60 /* SAI Transmit Mask */
#define REG_SAI_TTCTL  0x70 /* SAI Transmit Timestamp Control Register */
#define REG_SAI_TTCTN  0x74 /* SAI Transmit Timestamp Counter Register */
#define REG_SAI_TBCTN  0x78 /* SAI Transmit Bit Counter Register */
#define REG_SAI_TTCAP  0x7C /* SAI Transmit Timestamp Capture */

#define REG_SAI_RCSR   (0x80 + SAI_OFS) /* SAI Receive Control */
#define REG_SAI_RCR1   (0x84 + SAI_OFS) /* SAI Receive Configuration 1 */
#define REG_SAI_RCR2   (0x88 + SAI_OFS) /* SAI Receive Configuration 2 */
#define REG_SAI_RCR3   (0x8c + SAI_OFS) /* SAI Receive Configuration 3 */
#define REG_SAI_RCR4   (0x90 + SAI_OFS) /* SAI Receive Configuration 4 */
#define REG_SAI_RCR5   (0x94 + SAI_OFS) /* SAI Receive Configuration 5 */

#define REG_SAI_RDR0   0xa0 /* SAI Receive Data */
#define REG_SAI_RDR1   0xa4 /* SAI Receive Data */
#define REG_SAI_RDR2   0xa8 /* SAI Receive Data */
#define REG_SAI_RDR3   0xac /* SAI Receive Data */
#define REG_SAI_RDR4   0xb0 /* SAI Receive Data */
#define REG_SAI_RDR5   0xb4 /* SAI Receive Data */
#define REG_SAI_RDR6   0xb8 /* SAI Receive Data */
#define REG_SAI_RDR7   0xbc /* SAI Receive Data */
#define REG_SAI_RFR0   0xc0 /* SAI Receive FIFO */
#define REG_SAI_RFR1   0xc4 /* SAI Receive FIFO */
#define REG_SAI_RFR2   0xc8 /* SAI Receive FIFO */
#define REG_SAI_RFR3   0xcc /* SAI Receive FIFO */
#define REG_SAI_RFR4   0xd0 /* SAI Receive FIFO */
#define REG_SAI_RFR5   0xd4 /* SAI Receive FIFO */
#define REG_SAI_RFR6   0xd8 /* SAI Receive FIFO */
#define REG_SAI_RFR7   0xdc /* SAI Receive FIFO */
#define REG_SAI_RMR    0xe0 /* SAI Receive Mask */

struct adsp_dev;
struct adsp_gp_dmac;
struct adsp_log;
struct adsp_reg_space;

struct sai_fifo {
	uint32_t total_frames;
	uint32_t index;
	int fd;
	char file_name[64];
	uint32_t data[16];
	uint32_t level;
};

struct adsp_sai {
	char name[32];
	uint32_t *io;

	struct sai_fifo tx;
	struct sai_fifo rx;

	struct adsp_log *log;
	const struct adsp_reg_space *sai_dev;
};

#define ADSP_SAI_REGS		1
extern const struct adsp_reg_desc adsp_sai_map[ADSP_SAI_REGS];

extern struct adsp_sai *sai_get_port(int port);
void adsp_sai_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);
extern const MemoryRegionOps sai_ops;

#endif
