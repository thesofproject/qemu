/* CAVS memory and module loading
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/core/loader.h"
#include "elf.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/fw.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"

#define ACE30_HP_SRAM_BASE 0xa0020000
#define ACE30_HP_SRAM_SIZE 0x800000
#include "manifest.h"

/* Moved from ace.h */
#define ADSP_ACE30_DSP_LP_SRAM_SIZE     0x10000   /* 64KB = 8 banks x 8KB */
#define ADSP_ACE30_DSP_IMR_SIZE         0x1000000 /* 16MB IMR */

/* ACE 1.5 (MTL) SRAM sizes */
#define ACE15_HP_SRAM_BASE  0xa0020000u
#define ACE15_HP_SRAM_SIZE  (2816u * 1024u)   /* 2816 KB */
#define ACE15_LP_SRAM_BASE  0xa0000000u
#define ACE15_LP_SRAM_SIZE  0x10000u           /* 64 KB */
#define ACE15_IMR_BASE      0xa1000000u
#define ACE15_IMR_SIZE      0x1000000u         /* 16 MB */

/* ACE 2.0 (LNL) SRAM sizes — same physical sizes as ACE 1.5 */
#define ACE20_HP_SRAM_BASE  0xa0020000u
#define ACE20_HP_SRAM_SIZE  (2816u * 1024u)   /* 2816 KB physical */
#define ACE20_LP_SRAM_BASE  0xa0000000u
#define ACE20_LP_SRAM_SIZE  0x10000u           /* 64 KB */
#define ACE20_IMR_BASE      0xa1000000u
#define ACE20_IMR_SIZE      0x1000000u         /* 16 MB */

#define MAX_IMAGE_SIZE (1024 * 1024 * 4)

/* Load firmware image from file */
void *ace_load_firmware(const char *filename)
{
    size_t size = 0;
    void *fw = NULL;

    if (!filename) {
        return NULL;
    }

    fw = g_malloc(MAX_IMAGE_SIZE);
    if (!fw) {
        return NULL;
    }

    size = load_image_size(filename, fw, MAX_IMAGE_SIZE);
    if (size <= 0) {
        error_report("failed to load firmware %s", filename);
        g_free(fw);
        return NULL;
    }

    ace_log("loaded firmware image %s size 0x%zx\n", filename, size);
    return fw;
}

/* Memory descriptors for ACE3.x
 * 
 * ACE3.x memory map (corrected):
 * - LP-SRAM: 64KB at 0xa0000000
 * - HP-SRAM: 8MB at 0xa0020000
 * - IMR: 16MB at 0xa1000000
 * - ROM: Boot ROM
 */
struct adsp_mem_desc ace_ace3_mem[] = {
    {.name = "lp-sram", .base = ADSP_ACE30_DSP_LP_SRAM_BASE,
        .size = ADSP_ACE30_DSP_LP_SRAM_SIZE},  /* 64KB at 0xa0000000 */
    {.name = "hp-sram", .base = ACE30_HP_SRAM_BASE,
        .size = ACE30_HP_SRAM_SIZE,
        .per_core_non_coherent = true},  /* 8MB at 0xa0020000, non-coherent between cores */
    {.name = "imr", .base = ADSP_ACE30_DSP_IMR_BASE,
        .size = ADSP_ACE30_DSP_IMR_SIZE},  /* 16MB at 0xa1000000 */
    {.name = "rom", .base = ADSP_ACE_DSP_ROM_BASE,
        .size = ADSP_ACE_DSP_ROM_SIZE},
};

/* Memory descriptors for ACE 1.5 (Meteor Lake)
 *
 * ACE 1.5 memory map:
 * - LP-SRAM:  64 KB  at 0xa0000000
 * - HP-SRAM: 2816 KB at 0xa0020000 (smaller than ACE 3.0's 8 MB)
 * - IMR:       16 MB at 0xa1000000
 * - ROM: Boot ROM
 */
struct adsp_mem_desc ace_ace15_mem[] = {
    {.name = "lp-sram", .base = ACE15_LP_SRAM_BASE,
        .size = ACE15_LP_SRAM_SIZE},
    {.name = "hp-sram", .base = ACE15_HP_SRAM_BASE,
        .size = ACE15_HP_SRAM_SIZE,
        .per_core_non_coherent = true},
    {.name = "imr", .base = ACE15_IMR_BASE,
        .size = ACE15_IMR_SIZE},
    {.name = "rom", .base = ADSP_ACE_DSP_ROM_BASE,
        .size = ADSP_ACE_DSP_ROM_SIZE},
};

/* Memory descriptors for ACE 2.0 (Lunar Lake)
 *
 * ACE 2.0 memory map:
 * - LP-SRAM:  64 KB  at 0xa0000000 (same as ACE 1.5)
 * - HP-SRAM: 2816 KB at 0xa0020000 (same physical size as ACE 1.5)
 * - IMR:       16 MB at 0xa1000000 (same as ACE 1.5)
 * - ROM: Boot ROM
 */
struct adsp_mem_desc ace_ace20_mem[] = {
    {.name = "lp-sram", .base = ACE20_LP_SRAM_BASE,
        .size = ACE20_LP_SRAM_SIZE},
    {.name = "hp-sram", .base = ACE20_HP_SRAM_BASE,
        .size = ACE20_HP_SRAM_SIZE,
        .per_core_non_coherent = true},
    {.name = "imr", .base = ACE20_IMR_BASE,
        .size = ACE20_IMR_SIZE},
    {.name = "rom", .base = ADSP_ACE_DSP_ROM_BASE,
        .size = ADSP_ACE_DSP_ROM_SIZE},
};

/* Memory descriptors for cAVS 2.5 (Tiger Lake) — see cavs25.c
 *
 * cAVS 2.5 memory map (differs from ACE — different SRAM/IMR bases):
 * - HP-SRAM: 2944 KB at 0xbe000000 (sram0 / L2)
 * - LP-SRAM:   64 KB at 0xbe800000 (sram1)
 * - IMR:       16 MB at 0xb0000000 (L3, holds the signed FW image)
 * - ROM: Boot ROM
 */
#define CAVS25_HP_SRAM_BASE 0xbe000000u
#define CAVS25_HP_SRAM_SIZE (2944u * 1024u)
#define CAVS25_LP_SRAM_BASE 0xbe800000u
#define CAVS25_LP_SRAM_SIZE 0x10000u
#define CAVS25_IMR_BASE     0xb0000000u
#define CAVS25_IMR_SIZE     0x1000000u

struct adsp_mem_desc cavs25_mem[] = {
    {.name = "lp-sram", .base = CAVS25_LP_SRAM_BASE,
        .size = CAVS25_LP_SRAM_SIZE},
    {.name = "hp-sram", .base = CAVS25_HP_SRAM_BASE,
        .size = CAVS25_HP_SRAM_SIZE,
        .per_core_non_coherent = true},
    {.name = "imr", .base = CAVS25_IMR_BASE,
        .size = CAVS25_IMR_SIZE},
    {.name = "rom", .base = ADSP_ACE_DSP_ROM_BASE,
        .size = ADSP_ACE_DSP_ROM_SIZE},
};
