/*
 * MU support for audio DSP
 *
 * Copyright (c) 2020 NXP
 *
 * Author: Diana Cretu <diana.cretu@nxp.com>
 */

#ifndef ADSP_IO_H
#define ADSP_IO_H

/* Transmit Register */
#define IMX_MU_xTRn(x)          (0x00 + 4 * (x))
/* Receive Register */
#define IMX_MU_xRRn(x)          (0x10 + 4 * (x))
/* Status Register */
#define IMX_MU_xSR              0x20
#define IMX_MU_xSR_GIPn(x)      BIT(28 + (3 - (x)))
#define IMX_MU_xSR_RFn(x)       BIT(24 + (3 - (x)))
#define IMX_MU_xSR_TEn(x)       BIT(20 + (3 - (x)))
#define IMX_MU_xSR_BRDIP        BIT(9)

/* Control Register */
#define IMX_MU_xCR              0x24
/* General Purpose Interrupt Enable */
#define IMX_MU_xCR_GIEn(x)      BIT(28 + (3 - (x)))
/* Receive Interrupt Enable */
#define IMX_MU_xCR_RIEn(x)      BIT(24 + (3 - (x)))
/* Transmit Interrupt Enable */
#define IMX_MU_xCR_TIEn(x)      BIT(20 + (3 - (x)))
/* General Purpose Interrupt Request */
#define IMX_MU_xCR_GIRn(x)      BIT(16 + (3 - (x)))

#define MU_BASE_SIDEA	0x5D280000

#endif
