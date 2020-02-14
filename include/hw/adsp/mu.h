/* Core SHIM support for audio DSP.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ADSP_IO_H
#define ADSP_IO_H

/* Transmit Register */
#define IMX_MU_xTRn(x)» »       (0x00 + 4 * (x))
/* Receive Register */
#define IMX_MU_xRRn(x)» »       (0x10 + 4 * (x))
/* Status Register */
#define IMX_MU_xSR»     »       0x20
#define IMX_MU_xSR_GIPn(x)»     BIT(28 + (3 - (x)))
#define IMX_MU_xSR_RFn(x)»      BIT(24 + (3 - (x)))
#define IMX_MU_xSR_TEn(x)»      BIT(20 + (3 - (x)))
#define IMX_MU_xSR_BRDIP»       BIT(9)

/* Control Register */
#define IMX_MU_xCR»     »       0x24
/* General Purpose Interrupt Enable */
#define IMX_MU_xCR_GIEn(x)»     BIT(28 + (3 - (x)))
/* Receive Interrupt Enable */
#define IMX_MU_xCR_RIEn(x)»     BIT(24 + (3 - (x)))
/* Transmit Interrupt Enable */
#define IMX_MU_xCR_TIEn(x)»     BIT(20 + (3 - (x)))
/* General Purpose Interrupt Request */
#define IMX_MU_xCR_GIRn(x)»     BIT(16 + (3 - (x)))


#endif
