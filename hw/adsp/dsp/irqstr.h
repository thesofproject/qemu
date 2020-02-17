/*
 * IRQ Steer support for audio DSP
 *
 * Copyright 2020 NXP
 *
 * Author: Diana Cretu <diana.cretu@nxp.com>
 */

#define ADSP_IRQSTR_CONTROL_OFFSET          0x00
#define ADSP_IRQSTR_MASK_OFFSET             0x04
#define ADSP_IRQSTR_SET_OFFSET              0x44
#define ADSP_IRQSTR_STATUS_OFFSET           0x84
#define ADSP_IRQSTR_MASTER_DISABLE_OFFSET   0xC4
#define ADSP_IRQSTR_MASTER_STATUS_OFFSET    0xC8

#define IMX8_IRQSTR_REGS    6
extern const struct adsp_reg_desc adsp_imx8_irqstr_map[IMX8_IRQSTR_REGS];

void adsp_imx8_irqstr_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);
extern const MemoryRegionOps irqstr_io_ops;
