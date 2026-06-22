/* CAVS IRQ handling
 * IP Region: Global interrupt controller/mask/status routing for DSP and host events.
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
 * 
 * ACE3.x (Audio/Context/Engine IP 3.0+):
 *   - Two-level interrupt structure:
 *     1. Root level: Inside Tensilica Core (3 inputs: low priority, high priority, NMI)
 *     2. Second level: Synopsys DesignWare interrupt aggregator/router
 *   - Up to 5 interrupt controllers (one per Tensilica Core)
 *   - Each controller has:
 *     * FIQ output (high priority) -> Tensilica Core high priority input
 *     * IRQ output (normal priority) -> Tensilica Core low priority input
 *     * NMI: Connected to watchdog timer (1st timeout)
 *   - Interrupt routing registers:
 *     * DxHIPCIE: Route host IPC interrupt to specific DSP Core
 *     * DxSBIPCIE: Route Sideband IPC interrupt to specific DSP Core
 *     * DxIDCAIE: Route IDC agent A interrupt to specific DSP Core
 *   - Per-instance interrupt masking (HIPCIE, HIPCIS)
 *   - Synopsys DesignWare interrupt controller registers:
 *     * IRQ_INTEN: Enable low priority interrupts
 *     * IRQ_INTMASK: Mask individual low priority interrupt sources
 *     * IRQ_FINALSTATUS: Low priority interrupt status
 *     * FIQ_INTEN: Enable high priority interrupts
 *     * FIQ_INTMASK: Mask individual high priority interrupt sources
 *     * FIQ_FINALSTATUS: High priority interrupt status
 *   - Interrupt sources can be assigned to:
 *     * Any single Tensilica Core
 *     * Multiple Tensilica Cores
 *     * No Tensilica Core (disabled)
 *   - Enhanced interrupt sources:
 *     * Timers and Time Stamping (high priority)
 *     * IPC (host and sideband)
 *     * IDC (intra-DSP communication)
 *     * GPDMA channels
 *     * Audio links (DMIC, SoundWire, I2S, etc.)
 *     * L2 memory errors
 *     * Privacy microphone interrupts
 *   - ACPI interrupt routing support (via HxPCICFGCTL.ACPIIE)
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"
#include "../trace.h"


#define ace_region(raddr)    info->region[(raddr) >> 2]

