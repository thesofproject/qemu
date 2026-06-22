/*
 * Copyright (c) 2016-2023, Cadence Design Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "gdbstub/helpers.h"
#include "qemu/host-utils.h"

#include "core-ace30/core-isa.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_ace30
#include "core-ace30/xtensa-modules.c.inc"

static const XtensaIrqSource ace30_irq_sources[] = {
    { 0, "Software 0",  "Internal", 1 },
    { 1, "Timer 0",     "Internal", 1 },
    { 2, "Software 1",  "Internal", 2 },
    { 3, "Timer 1",     "Internal", 2 },
    { 4, "ACE INTC",    "External", 2 },
    { 5, "Software 2",  "Internal", 3 },
    { 6, "External L3", "External", 3 },
    { 7, "Profiling",   "Internal", 3 },
    { 8, "NMI",         "Internal", 5 },
};

static XtensaConfig ace30 __attribute__((unused)) = {
    .name = "ace30",
    .irq_info = ace30_irq_sources,
    .num_irq_info = ARRAY_SIZE(ace30_irq_sources),
    .gdb_regmap = {
        .reg = {
#include "core-ace30/gdb-config.c.inc"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 400000, /* 400MHz */
    .opcode_translators = (const XtensaOpcodeTranslators *[]){
        &xtensa_core_opcodes,
        &xtensa_fpu_opcodes,
        &xtensa_hifi_opcodes,
        NULL,
    },
    DEFAULT_SECTIONS,
    .cache_region_base = 0xa0020000,
    .cache_region_size = 0x00800000,
};

REGISTER_CORE(ace30)
