/* CAVS Manifest handling
 * IP Region: firmware manifest parsing/loading into IMR and memory segments.
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
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "manifest.h"
#include "ace-internal.h"
#include "monitor/monitor.h"

extern struct adsp_dev *g_adsp_dev;

#define NUM_SEGMENTS    3
#define PAGE_SIZE   4096
#define FW_MAN_DESC_OFFSET 0x2000

static const struct adsp_mem_desc *ace_mem_desc_by_name(const struct adsp_desc *board,
                                                        const char *name)
{
    int i;

    for (i = 0; i < board->num_mem; i++) {
        if (strcmp(board->mem_region[i].name, name) == 0) {
            return &board->mem_region[i];
        }
    }

    return NULL;
}

void copy_man_modules(const struct adsp_desc *board, struct adsp_dev *adsp,
    struct adsp_fw_desc *desc, void *file_start)
{
    struct module *mod;
    struct adsp_mem_desc *mem;
    struct adsp_fw_header *hdr = &desc->header;
    const struct adsp_mem_desc *imr_mem = ace_mem_desc_by_name(board, "imr");
    const struct adsp_mem_desc *lp_mem = ace_mem_desc_by_name(board, "lp-sram");
    const struct adsp_mem_desc *hp_mem = ace_mem_desc_by_name(board, "hp-sram");
    uint32_t imr_base = imr_mem ? imr_mem->base : ADSP_ACE30_DSP_IMR_BASE;
    uint32_t lp_base = lp_mem ? lp_mem->base : ADSP_ACE30_DSP_LP_SRAM_BASE;
    uint32_t hp_base = hp_mem ? hp_mem->base : 0xa0020000;
    uint32_t l2_addr_class = lp_base & 0xff000000;
    uint32_t lp_window_size = hp_base - lp_base;
    unsigned long foffset, soffset, ssize;
    void *base_ptr = (void *)desc - board->file_offset - FW_MAN_DESC_OFFSET;
    uint32_t phys_addr;
    int i, j;

    ace_log("\n=== Loading Firmware Modules ===\n");
    ace_log("Total modules: %d\n\n", hdr->num_module_entries);

    /* Load all modules from the manifest
     * Map virtual addresses to physical memory:
     * - 0xa1xxxxxx -> IMR at physical 0xa1000000
     * - 0xa0000000-0xa001ffff -> LP-SRAM at physical 0xa0000000 (64KB)
     * - 0xa0020000+ -> HP-SRAM at physical 0xa0020000 (8MB)
     */
    
    for (i = 0; i < hdr->num_module_entries; i++) {
        mod = &desc->module[i];
        
        /* Check if this module has any segments in IMR */
        int has_imr_segments = 0;
        for (j = 0; j < NUM_SEGMENTS; j++) {
            if (mod->segment[j].flags.r.load == 0)
                continue;
            phys_addr = mod->segment[j].v_base_addr;
            if ((phys_addr & 0xff000000) == imr_base) {
                has_imr_segments = 1;
                break;
            }
        }
        
        /* Skip modules without IMR segments */
        if (!has_imr_segments) {
            continue;
        }
        
        ace_log("Module %d: %s\n", i, mod->name);

        for (j = 0; j < NUM_SEGMENTS; j++) {

            if (mod->segment[j].flags.r.load == 0)
                continue;

            foffset = mod->segment[j].file_offset;
            ssize = mod->segment[j].flags.r.length * PAGE_SIZE;
            phys_addr = mod->segment[j].v_base_addr;
            uint32_t lookup_addr = phys_addr;
            const char *mem_type = "Unknown";
            const char *seg_type;
            
            /* Determine segment type from flags */
            switch (mod->segment[j].flags.r.type) {
                case 0: seg_type = "TEXT  "; break;  /* MAN_SEGMENT_TEXT */
                case 1: seg_type = "RODATA"; break;  /* MAN_SEGMENT_RODATA */
                case 2: seg_type = "BSS   "; break;  /* MAN_SEGMENT_BSS */
                default: seg_type = "UNKNWN"; break;
            }
            
            /* Map virtual address to physical memory based on ACE3.0 memory map */
            if ((phys_addr & 0xff000000) == imr_base) {
                /* Virtual IMR addresses map directly into the IMR window. */
                lookup_addr = phys_addr;
                mem_type = "IMR    ";
            } else if ((phys_addr & 0xff000000) == l2_addr_class) {
                /* Virtual L2 addresses use the LP window first, then HP SRAM. */
                uint32_t offset = phys_addr & 0x00ffffff;
                if (offset < lp_window_size) {
                    lookup_addr = lp_base + offset;
                    mem_type = "LP-SRAM";
                } else {
                    lookup_addr = phys_addr;
                    mem_type = "HP-SRAM";
                }
            }
            
            mem = adsp_get_mem_space(adsp, lookup_addr);

            if (mem) {
                soffset = lookup_addr - mem->base;
                void *src = (void*)base_ptr + foffset;
                void *dst = mem->ptr + soffset;
                uint32_t end_addr = lookup_addr + ssize - 1;

                ace_log("  Seg %d [%s] 0x%08x-0x%08x (%s) size: %6lu KB\n",
                        j, seg_type, lookup_addr, end_addr, mem_type, ssize / 1024);

                /* copy module segment to destination memory */
                memcpy(dst, src, ssize);

            } else {
                ace_log("  Seg %d [%s] 0x%08x-0x%08lx (UNMAPPED) size: %6lu KB\n",
                        j, seg_type, phys_addr, (unsigned long)(phys_addr + ssize - 1), ssize / 1024);
            }
        }
    }
    
    ace_log("\n=== Firmware Load Complete ===\n");
    ace_log("Loaded %d modules successfully\n\n", hdr->num_module_entries);
}

void copy_man_to_imr(const struct adsp_desc *board, struct adsp_dev *adsp,
    struct adsp_fw_desc *desc, uint32_t imr_addr)
{
    struct adsp_fw_header *hdr = &desc->header;
    struct adsp_mem_desc *mem;

    mem = adsp_get_mem_space(adsp, imr_addr);
    if (!mem)
        return;

    /* Initialize IMR memory to 0xFF (simulating erased state) before loading */
    memset(mem->ptr, 0xff, mem->size);

    /* copy manifest to IMR */
    memcpy(mem->ptr + board->imr_boot_ldr_offset, (void*)hdr,
                 hdr->preload_page_count * PAGE_SIZE);
    /* The ROM loader path expects the manifest/header pages staged at the IMR boot-loader offset. */

    ace_log("ROM loader: copy %d kernel pages to IMR\n",
        hdr->preload_page_count);
}

#define MANIFEST_HEADER_MAGIC 0x314d4124

static void adsp_print_manifest(Monitor *mon, uint8_t *desc_ptr, size_t size, uint32_t fw_imr_start, uint32_t imr_base, uint32_t lp_base, uint32_t hp_base, uint32_t l2_addr_class, uint32_t lp_window_size)
{
    struct adsp_fw_desc *desc = (struct adsp_fw_desc *)desc_ptr;
    struct adsp_fw_header *hdr = &desc->header;

    monitor_printf(mon, "Intel ADSP Firmware Manifest (Version %d.%d.%d Build %d)\n",
                   hdr->major_version, hdr->minor_version, hdr->hotfix_version, hdr->build_version);
    monitor_printf(mon, "  Name: %.8s\n", hdr->name);
    monitor_printf(mon, "  Firmware Kernel Blob (IMR Physical/Relative Start): 0x%08x\n", fw_imr_start);
    monitor_printf(mon, "  Flags: 0x%08lx, Features: 0x%08lx\n", (unsigned long)hdr->fw_image_flags, (unsigned long)hdr->feature_mask);
    monitor_printf(mon, "  Load Offset: 0x%08lx\n", (unsigned long)hdr->load_offset);
    monitor_printf(mon, "  Preload Pages: %d (%d KB)\n", hdr->preload_page_count, (hdr->preload_page_count * 4096) / 1024);
    monitor_printf(mon, "  Total Extracted Modules: %d\n", hdr->num_module_entries);

    for (int i = 0; i < hdr->num_module_entries; i++) {
        struct module *mod = (struct module *)((uint8_t *)&desc->module[0] + (i * sizeof(struct module)));
        
        monitor_printf(mon, "\n--- Module %d: %.8s ---\n", i, mod->name);
        monitor_printf(mon, "  Load Type: %d (Built-In = 0, Loadable = 1)\n", mod->type.load_type);
        monitor_printf(mon, "  Auto-Start: %s\n", mod->type.auto_start ? "True" : "False");
        monitor_printf(mon, "  Entry Point: 0x%08lx\n", (unsigned long)mod->entry_point);
        monitor_printf(mon, "  Affinity Routing Mask: 0x%08lx\n", (unsigned long)mod->affinity_mask);

        uint32_t mod_imr_start = 0xFFFFFFFF;
        uint32_t mod_imr_end = 0;
        for (int j = 0; j < NUM_SEGMENTS; j++) {
            if (mod->segment[j].flags.r.load && mod->segment[j].flags.r.length > 0) {
                uint32_t seg_start = fw_imr_start + mod->segment[j].file_offset;
                uint32_t seg_end = seg_start + (mod->segment[j].flags.r.length * 4096);
                if (seg_start < mod_imr_start) mod_imr_start = seg_start;
                if (seg_end > mod_imr_end) mod_imr_end = seg_end;
            }
        }
        if (mod_imr_start != 0xFFFFFFFF) {
            monitor_printf(mon, "  IMR Storage Source: 0x%08x - 0x%08x (%d pages / %d KB)\n",
                           mod_imr_start, mod_imr_end, 
                           (mod_imr_end - mod_imr_start) / 4096, 
                           (mod_imr_end - mod_imr_start) / 1024);
        }

        monitor_printf(mon, "  Segment Allocation:\n");

        bool has_imr = false;
        bool has_segments = false;

        for (int j = 0; j < NUM_SEGMENTS; j++) {
            if (mod->segment[j].flags.r.load) {
                has_segments = true;
                const char *seg_type = "UNKNOWN";
                switch (mod->segment[j].flags.r.type) {
                    case 0: seg_type = "TEXT  "; break;
                    case 1: seg_type = "RODATA"; break;
                    case 2: seg_type = "BSS   "; break;
                }
                
                uint32_t phys_addr = mod->segment[j].v_base_addr;
                const char *mem_type = "UNMAPPED";

                if ((phys_addr & 0xff000000) == imr_base) {
                    mem_type = "IMR    ";
                    has_imr = true;
                } else if ((phys_addr & 0xff000000) == l2_addr_class) {
                    uint32_t offset = phys_addr & 0x00ffffff;
                    if (offset < lp_window_size) {
                        mem_type = "LP-SRAM";
                    } else {
                        mem_type = "HP-SRAM";
                    }
                }

                monitor_printf(mon, "   [%d] %s: VAddr 0x%08lx (%s), FileOffset 0x%08lx, Size = %d pages (%d KB)\n",
                       j, seg_type, (unsigned long)phys_addr, mem_type, 
                       (unsigned long)mod->segment[j].file_offset,
                       mod->segment[j].flags.r.length, 
                       (mod->segment[j].flags.r.length * 4096) / 1024);
            }
        }

        if (!has_segments) {
            monitor_printf(mon, "   (No segments matched 'load' flag)\n");
        } else if (!has_imr) {
            monitor_printf(mon, "   * Note: This module has no text or data mapped in IMR\n");
        }
    }
}

static void scan_region_for_manifest(Monitor *mon, const char *region_name, const struct adsp_mem_desc *mem, uint32_t imr_base, uint32_t lp_base, uint32_t hp_base, uint32_t l2_addr_class, uint32_t lp_window_size)
{
    if (!mem || !mem->ptr || mem->size < sizeof(struct adsp_fw_header)) return;

    /* Scan every 4-bytes for MANIFEST_HEADER_MAGIC */
    uint32_t *ptr = (uint32_t *)mem->ptr;
    for (size_t skip = 0; skip <= mem->size - sizeof(struct adsp_fw_header); skip += 4) {
        if (*(uint32_t *)((uint8_t *)ptr + skip) == MANIFEST_HEADER_MAGIC) {
            uint8_t *desc_ptr = (uint8_t *)ptr + skip;
            struct adsp_fw_desc *desc = (struct adsp_fw_desc *)desc_ptr;
            /* Filter false positives by validating standard header traits */
            if (desc->header.num_module_entries > 0 && desc->header.num_module_entries < 100 &&
                desc->header.major_version < 10) {
                 monitor_printf(mon, "\n=======================================================\n");
                 monitor_printf(mon, ">> Found Manifest in [ %s ] at physical offset 0x%zx\n", region_name, skip);
                 monitor_printf(mon, "=======================================================\n");
                 adsp_print_manifest(mon, desc_ptr, mem->size - skip, mem->base + skip, imr_base, lp_base, hp_base, l2_addr_class, lp_window_size);
            }
        }
    }
}

void adsp_monitor_ace_manifest(Monitor *mon, const QDict *qdict)
{
    if (!g_adsp_dev) {
        monitor_printf(mon, "No ADSP device available.\n");
        return;
    }

    const struct adsp_desc *board = g_adsp_dev->desc;
    const struct adsp_mem_desc *imr_mem = ace_mem_desc_by_name(board, "imr");
    const struct adsp_mem_desc *lp_mem = ace_mem_desc_by_name(board, "lp-sram");
    const struct adsp_mem_desc *hp_mem = ace_mem_desc_by_name(board, "hp-sram");

    uint32_t imr_base = imr_mem ? imr_mem->base : ADSP_ACE30_DSP_IMR_BASE;
    uint32_t lp_base = lp_mem ? lp_mem->base : ADSP_ACE30_DSP_LP_SRAM_BASE;
    uint32_t hp_base = hp_mem ? hp_mem->base : 0xa0020000;
    uint32_t l2_addr_class = lp_base & 0xff000000;
    uint32_t lp_window_size = hp_base - lp_base;

    /* Scan standard regions for dynamically loaded module manifests ($AM1 signatures) */
    if (g_adsp_dev->fw_manifest) {
        monitor_printf(mon, "\nScanning Base Firmware Boot Buffer for Extracted Modules...\n");
        /* Manually route the statically passed firmware block */
        void *man_ptr = g_adsp_dev->fw_manifest;
        size_t size = g_adsp_dev->fw_manifest_size;
        int skip = adsp_get_ext_man_size(man_ptr);
        uint8_t *desc_ptr = (uint8_t *)man_ptr + skip;

        while (*((uint32_t *)desc_ptr) != MANIFEST_HEADER_MAGIC && skip < size) {
            desc_ptr += 4;
            skip += 4;
        }

        if (skip < size) {
            monitor_printf(mon, ">> Found Boot Base Manifest:\n");
            adsp_print_manifest(mon, desc_ptr, size - skip, imr_base + board->imr_boot_ldr_offset, imr_base, lp_base, hp_base, l2_addr_class, lp_window_size);
        }
    }

    monitor_printf(mon, "\nScanning Hardware RAM for runtime LLEXT / Dynamic Manifests...\n");
    scan_region_for_manifest(mon, "IMR RAM", imr_mem, imr_base, lp_base, hp_base, l2_addr_class, lp_window_size);
    scan_region_for_manifest(mon, "LP-SRAM", lp_mem, imr_base, lp_base, hp_base, l2_addr_class, lp_window_size);
    scan_region_for_manifest(mon, "HP-SRAM", hp_mem, imr_base, lp_base, hp_base, l2_addr_class, lp_window_size);
}
