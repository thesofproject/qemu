/* Core DSP support for i.MX8 audio DSP.
 *
 * Copyright 2020 NXP
 * Author: Diana Cretu <diana.cretu@nxp.com>
 *
 */

#ifndef __ADSP_IMX8_H__
#define __ADSP_IMX8_H__

/* IRQ numbers */
#define IRQ_NUM_SOFTWARE0    8      /* Level 1 */

#define IRQ_MASK_SOFTWARE0    BIT(IRQ_NUM_SOFTWARE0)

#define IRQ_NUM_TIMER0       2      /* Level 2 */
#define IRQ_NUM_MU           7      /* Level 2 */
#define IRQ_NUM_SOFTWARE1    9      /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP0  19     /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP1  20     /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP2  21     /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP3  22     /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP4  23     /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP5  24     /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP6  25     /* Level 2 */
#define IRQ_NUM_IRQSTR_DSP7  26     /* Level 2 */

#define IRQ_NUM_TIMER1       3      /* Level 3 */

#define IRQ_MASK_TIMER0        BIT(IRQ_NUM_TIMER0)
#define IRQ_MASK_MU            BIT(IRQ_NUM_MU)
#define IRQ_MASK_SOFTWARE1     BIT(IRQ_NUM_SOFTWARE1)
#define IRQ_MASK_IRQSTR_DSP0   BIT(IRQ_NUM_IRQSTR_DSP0)
#define IRQ_MASK_IRQSTR_DSP1   BIT(IRQ_NUM_IRQSTR_DSP1)
#define IRQ_MASK_IRQSTR_DSP2   BIT(IRQ_NUM_IRQSTR_DSP2)
#define IRQ_MASK_IRQSTR_DSP3   BIT(IRQ_NUM_IRQSTR_DSP3)
#define IRQ_MASK_IRQSTR_DSP4   BIT(IRQ_NUM_IRQSTR_DSP4)
#define IRQ_MASK_IRQSTR_DSP5   BIT(IRQ_NUM_IRQSTR_DSP5)
#define IRQ_MASK_IRQSTR_DSP6   BIT(IRQ_NUM_IRQSTR_DSP6)
#define IRQ_MASK_IRQSTR_DSP7   BIT(IRQ_NUM_IRQSTR_DSP7)

#define IRQ_MASK_TIMER1       BIT(IRQ_NUM_TIMER1)

struct adsp_dev;

void adsp_imx8_mu_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);
void adsp_imx8_mu_msg(struct adsp_dev *adsp, struct qemu_io_msg *msg);
void adsp_imx8_irq_msg(struct adsp_dev *adsp, struct qemu_io_msg *msg);
void imx8_ext_timer_cb(void *opaque);
extern const MemoryRegionOps imx8_mu_ops;
extern const MemoryRegionOps adsp_mbox_ops;

#endif
