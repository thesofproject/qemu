/*
 * Intel cAVS 2.5 (Tiger Lake / TGL) audio DSP core model.
 *
 * The core configuration (core-isa.h) is the authentic Tensilica
 * "intel_tgl_adsp" LX6 / HiFi3 config imported from the Zephyr SDK
 * toolchain:
 *   ~/zephyr-sdk-1.0.1/xtensa-intel_tgl_adsp_zephyr-elf/.../xtensa/config/core-isa.h
 *
 * ISA decode tables:
 * The HiFi3 instruction decoder (xtensa-modules.c.inc) and gdb register
 * map (gdb-config.c.inc) are imported from the binutils/gdb sources in
 * the Intel CNL Xtensa overlay ("X6H3CNL_2017_8").  CNL and TGL are both
 * LX6 / HiFi3 cores from the identical "2017_8" Tensilica TIE generation
 * (they differ only in HW-version minor and VECBASE reset), so the
 * instruction encodings and register set are ISA-identical to the
 * authentic TGL "cavs2x_LX6HiFi3_2017_8" core whose core-isa.h is used
 * below.  The fixups applied are the same ones target/xtensa/import_core.sh
 * performs.
 *
 * Copyright (C) 2026 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "gdbstub/helpers.h"
#include "qemu/host-utils.h"

#include "core-cavs25/core-isa.h"
#include "core-cavs25/core-matmap.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_cavs25
#include "core-cavs25/xtensa-modules.c.inc"

/*
 * Interrupt naming for the monitor (info ace-irq).  Levels and types are
 * taken from the authentic TGL core-isa.h.  The external interrupts at
 * core IRQs 6/10/13/16 are the cAVS interrupt aggregators cavs_intc0..3
 * (see zephyr dts/xtensa/intel/intel_adsp_cavs25.dtsi).
 */
static const XtensaIrqSource cavs25_irq_sources[] = {
    { 0,  "Software 0", "Internal", 1 },
    { 1,  "Timer 0",    "Internal", 1 },
    { 2,  "External 1", "External", 1 },
    { 5,  "Timer 1",    "Internal", 2 },
    { 6,  "cAVS INTC0", "External", 2 },
    { 9,  "Timer 2",    "Internal", 3 },
    { 10, "cAVS INTC1", "External", 3 },
    { 13, "cAVS INTC2", "External", 4 },
    { 16, "cAVS INTC3", "External", 5 },
    { 20, "NMI",        "Internal", 7 },
};

static XtensaConfig cavs25 __attribute__((unused)) = {
    .name = "cavs25",
    .irq_info = cavs25_irq_sources,
    .num_irq_info = ARRAY_SIZE(cavs25_irq_sources),
    .gdb_regmap = {
        .reg = {
#include "core-cavs25/gdb-config.c.inc"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 400000, /* 400MHz DSP core clock */
    .opcode_translators = (const XtensaOpcodeTranslators *[]){
        &xtensa_core_opcodes,
        &xtensa_fpu_opcodes,
        &xtensa_hifi_opcodes,
        NULL,
    },
    DEFAULT_SECTIONS,
    /* Cached HP SRAM window (sram0 @ 0xbe000000, 2944 KB) */
    .cache_region_base = 0xbe000000,
    .cache_region_size = 2944u * 1024u,
};

REGISTER_CORE(cavs25)
