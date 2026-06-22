/* Core common support for audio DSP.
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
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/thread.h"
#include "hw/core/boards.h"
#include "hw/core/cpu.h"
#include "hw/core/loader.h"
#include "elf.h"
#include "system/memory.h"
#include "system/address-spaces.h"

#include "hw/audio/adsp-dev.h"
#include "hw/adsp/fw.h"
#include "hw/adsp/ace.h"
#include "hw/adsp/shim.h"
#include "common.h"
#include "manifest.h"
#include "ace-internal.h"
#include "ace-tlb.h"
#include "target/xtensa/cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"

/* Moved from ace.h */
#define ADSP_ACE40_DSP_LP_UNCACHE_BASE  0x40020000

/* Not prototyped by headers in this tree, but provided by system/runstate.c. */
void qemu_add_exit_notifier(Notifier *notify);
void qemu_remove_exit_notifier(Notifier *notify);

#include "hw/core/boards.h"

struct adsp_dev *g_adsp_dev;

void adsp_monitor_ace_core(Monitor *mon, const QDict *qdict)
{
    struct adsp_dev *adsp = g_adsp_dev;
    int i;
    int max_cores;
    
    if (!adsp) {
        monitor_printf(mon, "No ADSP core found.\n");
        return;
    }

    max_cores = current_machine ? current_machine->smp.max_cpus : ADSP_MAX_CORES;

    for (i = 0; i < max_cores; i++) {
        uint32_t dspc_ctl = 0;
        uint32_t shim_ctl = 0;
        bool has_dspcs = false, has_shim = false;

        if (adsp->dspcs) {
            uint32_t ctl_addr = (i * 8 + 4); /* offsetof(struct dspcs, capctl[i].ctl) */
            dspc_ctl = adsp->dspcs->region[ctl_addr >> 2];
            has_dspcs = true;
        }
        
        if (adsp->shim) {
            shim_ctl = adsp->shim->region[(0x100 + i * 4) >> 2]; /* SHIM_DSPCxCTL(i) */
            has_shim = true;
        }

        monitor_printf(mon, "Core %d:\n", i);

        if (!adsp->xtensa[i]) {
            monitor_printf(mon, "  Run State   : OFF (vCPU not allocated by QEMU -smp flag)\n");
        } else {
            CPUXtensaState *env = &adsp->xtensa[i]->cpu->env;
            monitor_printf(mon, "  Run State   : %s\n", env->runstall ? "STALLED" : "RUNNING");
            monitor_printf(mon, "  PC          : 0x%08x\n", env->pc);
            monitor_printf(mon, "  PS          : 0x%08x\n", env->sregs[230]);
            monitor_printf(mon, "  INTENABLE   : 0x%08x\n", env->sregs[228]);
            monitor_printf(mon, "  INTERRUPT   : 0x%08x\n", env->sregs[226]);
            monitor_printf(mon, "  EXCCAUSE    : 0x%08x\n", env->sregs[232]);
        }
        
        if (has_dspcs) {
            monitor_printf(mon, "  DSPCS Power : SPA=%u CPA=%u\n",
                           !!(dspc_ctl & (1 << 0)), !!(dspc_ctl & (1 << 8)));
        } else if (has_shim) {
            monitor_printf(mon, "  SHIM Power  : SPA=%u CPA=%u\n",
                           !!(shim_ctl & (1 << 0)), !!(shim_ctl & (1 << 8)));
        }
        
        monitor_printf(mon, "\n");
    }
}

#include "target/xtensa/cpu.h"

void ace_log_prefix(void)
{
    uint32_t ccount = 0;
    uint32_t twl = 0;
    int core_id = 0;

    CPUState *cs = current_cpu;
    if (cs) {
        core_id = cs->cpu_index;
        CPUXtensaState *env = cpu_env(cs);
        if (env) {
            ccount = env->sregs[234 /* CCOUNT */];
        }
    }

    static uint32_t last_twl = 0;

    if (g_adsp_dev && g_adsp_dev->timer[0].clk_kHz) {
        bool safe_to_read = true;
        if (cs && cs->running && !cs->neg.can_do_io) {
            safe_to_read = false;
        }
        
        if (safe_to_read) {
            uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - g_adsp_dev->timer[0].start;
            uint64_t ticks = (time_ns * g_adsp_dev->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000) + ace_timer_ticks_adjustment;
            twl = (uint32_t)(ticks & 0xFFFFFFFF);
            last_twl = twl;
        } else {
            twl = last_twl;
        }
    }

    qemu_log("[c:%d T:0x%08x C:0x%08x] ", core_id, twl, ccount);
}

/* In ASCII `XMan` */
#define SND_SOF_EXT_MAN_MAGIC_NUMBER	0x6e614d58
/* In ASCII `$AE1` */
#define EXT_MANIFEST_HEADER_MAGIC_AE1   0x31454124
#define HEADER_MAGIC    0x314d4124
#define MAX_IMAGE_SIZE (1024 * 1024 * 4)
#define ADSP_DW_PAGE0_OFFSET 0x4000
#define ADSP_MTRACE_SLOT_HDR_SIZE 8
#define ADSP_MTRACE_SLOT_SIZE 0x1000
#define ADSP_MTRACE_BUF_SIZE (ADSP_MTRACE_SLOT_SIZE - ADSP_MTRACE_SLOT_HDR_SIZE)
#define ADSP_DW_SLOT_COUNT 15
#define ADSP_DW_DESC_SIZE 12
#define ADSP_DW_SLOT_DEBUG_LOG 0x474f4c00u

typedef struct AdspDwDesc {
    uint32_t resource_id;
    uint32_t type;
    uint32_t vma;
} QEMU_PACKED AdspDwDesc;

static uint8_t *adsp_mtrace_find_slot(struct adsp_dev *adsp, uint32_t *buf_size,
                                      unsigned *slot_index)
{
    uint8_t *mem = adsp->hpsram->coherent;
    uint32_t size = adsp->hpsram->size;
    uint32_t off;

    for (off = 0; off < size - sizeof(AdspDwDesc); off += 4) {
        AdspDwDesc *desc = (AdspDwDesc *)(mem + off);
        if ((desc->type & 0xffffff00u) == ADSP_DW_SLOT_DEBUG_LOG &&
            desc->resource_id == 0 && desc->vma == 0) {
            
            /* 
             * In Zephyr, the MTrace struct is initialized at `ADSP_DW->descs[i]`.
             * The `adsp_debug_window` is always page-aligned (4096-bytes).
             * We can find the base of `adsp_debug_window` by rounding down `off`
             * to the nearest 4096 byte boundary.
             */
            uint32_t base_offset = off & ~(ADSP_MTRACE_SLOT_SIZE - 1);
            
            /* 
             * The index `i` of the descriptor is given by how far `off` is from `base_offset`.
             */
            uint32_t desc_index = (off - base_offset) / sizeof(AdspDwDesc);
            
            /*
             * The corresponding slot buffer is located at `base + ADSP_DW_SLOT_SIZE * (index + 1)`.
             */
            uint32_t mem_offset = base_offset + ADSP_MTRACE_SLOT_SIZE * (desc_index + 1);

            ace_log("mtrace: Found DEBUG_LOG desc at offset 0x%x (index %u). Ring buffer at 0x%x\n",
                     off, desc_index, mem_offset);

            if (slot_index) {
                *slot_index = desc_index;
            }
            if (buf_size) {
                *buf_size = ADSP_MTRACE_BUF_SIZE;
            }
            
            return mem + mem_offset;
        }
    }

    return NULL;
}

static void adsp_mtrace_exit_notify(Notifier *notifier, void *data)
{
    struct adsp_dev *adsp = container_of(notifier, struct adsp_dev, mtrace_exit);

    if (!adsp->mtrace_thread_running) {
        return;
    }

    qatomic_set(&adsp->mtrace_thread_stop, true);
    qemu_thread_join(&adsp->mtrace_thread);
    adsp->mtrace_thread_running = false;

    if (adsp->mtrace_file) {
        fclose(adsp->mtrace_file);
        adsp->mtrace_file = NULL;
    }

    g_clear_pointer(&adsp->mtrace_file_path, g_free);
    qemu_remove_exit_notifier(&adsp->mtrace_exit);
}

static void *adsp_mtrace_thread_fn(void *opaque)
{
    struct adsp_dev *adsp = opaque;
    volatile uint32_t *host_ptr = NULL;
    volatile uint32_t *dsp_ptr = NULL;
    uint8_t *data = NULL;
    uint32_t read_pos = 0;
    unsigned slot_index = 0;

    while (!qatomic_read(&adsp->mtrace_thread_stop)) {
        if (!adsp->mtrace_slot) {
            adsp->mtrace_slot = adsp_mtrace_find_slot(adsp, &adsp->mtrace_buf_size,
                                                      &slot_index);
            if (!adsp->mtrace_slot) {
                g_usleep(1000 * 10);
                continue;
            }
            adsp->mtrace_slot_index = slot_index;
            host_ptr = (uint32_t *)adsp->mtrace_slot;
            dsp_ptr = (uint32_t *)(adsp->mtrace_slot + sizeof(uint32_t));
            data = adsp->mtrace_slot + ADSP_MTRACE_SLOT_HDR_SIZE;
            read_pos = *host_ptr;
        }

        uint32_t write_pos = qatomic_read(dsp_ptr);

        if (write_pos >= adsp->mtrace_buf_size || read_pos >= adsp->mtrace_buf_size) {
            ace_log("mtrace: invalid ring pointers read=%u write=%u size=%u\n",
                     read_pos, write_pos, adsp->mtrace_buf_size);
            read_pos = 0;
            * (uint32_t *)host_ptr = 0;
            g_usleep(1000 * 10);
            continue;
        }

        if (read_pos != write_pos) {
            if (write_pos > read_pos) {
                fwrite(data + read_pos, 1, write_pos - read_pos, adsp->mtrace_file);
            } else {
                fwrite(data + read_pos, 1, adsp->mtrace_buf_size - read_pos,
                       adsp->mtrace_file);
                if (write_pos) {
                    fwrite(data, 1, write_pos, adsp->mtrace_file);
                }
            }
            fflush(adsp->mtrace_file);
            read_pos = write_pos;
            *(uint32_t *)host_ptr = read_pos;
        }

        g_usleep(1000 * 10);
    }

    return NULL;
}

static char *global_mtrace_file;

static char *get_mtrace_file(Object *obj, Error **errp)
{
    return g_strdup(global_mtrace_file);
}

static void set_mtrace_file(Object *obj, const char *value, Error **errp)
{
    g_free(global_mtrace_file);
    global_mtrace_file = g_strdup(value);
}

void adsp_machine_class_add_options(MachineClass *mc)
{
    object_class_property_add_str(OBJECT_CLASS(mc), "mtrace-file",
                                  get_mtrace_file, set_mtrace_file);
    object_class_property_set_description(OBJECT_CLASS(mc), "mtrace-file",
                                          "Path to output ace-mtrace log file");
}


static void adsp_mtrace_start(struct adsp_dev *adsp)
{
    const char *path;

    if (!adsp->hpsram || adsp->mtrace_thread_running) {
        return;
    }

    g_adsp_dev = adsp;

    path = g_getenv("QEMU_ACE_MTRACE_FILE");
    if (!path && global_mtrace_file && *global_mtrace_file) {
        path = global_mtrace_file;
    }
    adsp->mtrace_file_path = g_strdup(path && *path ? path : "/tmp/ace-mtrace.log");
    adsp->mtrace_file = fopen(adsp->mtrace_file_path, "ab");
    if (!adsp->mtrace_file) {
        error_report("mtrace: failed to open %s", adsp->mtrace_file_path);
        g_clear_pointer(&adsp->mtrace_file_path, g_free);
        return;
    }

    adsp->mtrace_thread_stop = false;
    qemu_thread_create(&adsp->mtrace_thread, "ace-mtrace",
                       adsp_mtrace_thread_fn, adsp, QEMU_THREAD_JOINABLE);
    adsp->mtrace_thread_running = true;
    adsp->mtrace_exit.notify = adsp_mtrace_exit_notify;
    qemu_add_exit_notifier(&adsp->mtrace_exit);
    ace_log("mtrace: waiting for debug log slot for %s\n",
             adsp->mtrace_file_path);
}

void hmp_info_adsp_mtrace(Monitor *mon, const QDict *qdict)
{
    struct adsp_dev *adsp = g_adsp_dev;
    uint32_t host_ptr, dsp_ptr, pending;

    if (!adsp || !adsp->mtrace_thread_running) {
        monitor_printf(mon, "mtrace: not active\n");
        return;
    }

    host_ptr = qatomic_read((uint32_t *)adsp->mtrace_slot);
    dsp_ptr  = qatomic_read((uint32_t *)(adsp->mtrace_slot + sizeof(uint32_t)));
    pending  = (dsp_ptr >= host_ptr) ? (dsp_ptr - host_ptr)
                                     : (adsp->mtrace_buf_size - host_ptr + dsp_ptr);

    monitor_printf(mon, "mtrace: active\n");
    monitor_printf(mon, "  slot index : %u\n", adsp->mtrace_slot_index);
    monitor_printf(mon, "  output file: %s\n", adsp->mtrace_file_path);
    monitor_printf(mon, "  buf size   : %u bytes\n", adsp->mtrace_buf_size);
    monitor_printf(mon, "  host_ptr   : %u\n", host_ptr);
    monitor_printf(mon, "  dsp_ptr    : %u\n", dsp_ptr);
    monitor_printf(mon, "  pending    : %u bytes\n", pending);
}

static uint64_t adsp_unmapped_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr full_addr = info->space->desc.base + addr;

    ace_log("unmapped_read: %s full=0x%" HWADDR_PRIx
             " offset=0x%" HWADDR_PRIx " size=%u\n",
             info->name, full_addr, addr, size);
    return 0;
}

static void adsp_unmapped_write(void *opaque, hwaddr addr,
    uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr full_addr = info->space->desc.base + addr;

    ace_log("unmapped_write: %s full=0x%" HWADDR_PRIx
             " offset=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
             info->name, full_addr, addr, size, val);
}

const MemoryRegionOps unmapped_ops = {
    .read = adsp_unmapped_read,
    .write = adsp_unmapped_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * HP-SRAM non-coherent per-core dispatcher.
 *
 * Reads/writes to 0xa0020000 are routed to the active CPU's private shadow
 * buffer.  The shared coherent backing at 0x40020000 is only updated when
 * dirty cache lines are evicted or explicitly flushed (dhwb/dhwbi).
 */
static uint64_t hpsram_local_read(void *opaque, hwaddr addr, unsigned size)
{
    adsp_hpsram_state *hp = opaque;
    int idx = (current_cpu && current_cpu->cpu_index < hp->num_cores)
              ? current_cpu->cpu_index : 0;
    uint8_t *base = hp->per_core[idx] + addr;

    switch (size) {
    case 1: return *(uint8_t  *)base;
    case 2: return *(uint16_t *)base;
    case 4: return *(uint32_t *)base;
    case 8: return *(uint64_t *)base;
    default: return 0;
    }
}

static void hpsram_local_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    adsp_hpsram_state *hp = opaque;
    int idx;
    uint8_t *base;

    if (current_cpu && current_cpu->cpu_index < hp->num_cores) {
        /* Runtime write: goes to this core's shadow only (non-coherent). */
        idx = current_cpu->cpu_index;
        base = hp->per_core[idx] + addr;
        switch (size) {
        case 1: *(uint8_t  *)base = (uint8_t)val;  break;
        case 2: *(uint16_t *)base = (uint16_t)val; break;
        case 4: *(uint32_t *)base = (uint32_t)val; break;
        case 8: *(uint64_t *)base = val;            break;
        }
    } else {
        /*
         * Boot-time write (DMA, ELF loader, firmware memcpy via mem->ptr):
         * current_cpu == NULL.  Write to coherent store AND all per-core
         * shadows so every core has consistent initial state.
         */
        for (idx = 0; idx < hp->num_cores; idx++) {
            base = hp->per_core[idx] + addr;
            switch (size) {
            case 1: *(uint8_t  *)base = (uint8_t)val;  break;
            case 2: *(uint16_t *)base = (uint16_t)val; break;
            case 4: *(uint32_t *)base = (uint32_t)val; break;
            case 8: *(uint64_t *)base = val;            break;
            }
        }
        base = hp->coherent + addr;
        switch (size) {
        case 1: *(uint8_t  *)base = (uint8_t)val;  break;
        case 2: *(uint16_t *)base = (uint16_t)val; break;
        case 4: *(uint32_t *)base = (uint32_t)val; break;
        case 8: *(uint64_t *)base = val;            break;
        }
    }
}

static const MemoryRegionOps hpsram_local_ops = {
    .read  = hpsram_local_read,
    .write = hpsram_local_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

/*
 * adsp_hpsram_setup - replace the plain hp-sram RAM with a per-core model.
 *
 * @mem:           the hp-sram adsp_mem_desc entry
 * @coherent_base: address of the cache-coherent alias (e.g. 0x40020000)
 * @num_cores:     number of DSP cores
 *
 * After this call:
 *   mem->ptr   → coherent backing buffer (used by firmware loading memcpy paths)
 *   0xa0020000 → MemoryRegionOps dispatcher (per-core non-coherent)
 *   coherent_base → shared coherent RAM
 */
void adsp_hpsram_setup(struct adsp_dev *adsp, struct adsp_mem_desc *mem,
                       uint32_t coherent_base, int num_cores)
{
    adsp_hpsram_state *hp = g_new0(adsp_hpsram_state, 1);
    int c;

    hp->base         = mem->base;
    hp->coherent_base = coherent_base;
    hp->size         = mem->size;
    hp->num_cores    = num_cores;

    /* Coherent (cache-bypassed) backing RAM at coherent_base. */
    memory_region_init_ram(&hp->coherent_mr, NULL, "hp-sram-coherent",
                           mem->size, &error_fatal);
    memory_region_add_subregion(adsp->system_memory, coherent_base,
                                &hp->coherent_mr);
    hp->coherent = memory_region_get_ram_ptr(&hp->coherent_mr);
    memset(hp->coherent, 0xff, mem->size);

    /* Per-core private shadow buffers. */
    for (c = 0; c < num_cores; c++) {
        hp->per_core[c] = g_malloc(mem->size);
        memset(hp->per_core[c], 0xff, mem->size);
    }

    /* Per-core dispatcher MemoryRegion at the cached HP-SRAM address. */
    memory_region_init_io(&hp->dispatcher_mr, NULL, &hpsram_local_ops, hp,
                          "hp-sram-local", mem->size);
    memory_region_add_subregion(adsp->system_memory, mem->base,
                                &hp->dispatcher_mr);

    /*
     * Point mem->ptr at the coherent buffer so that direct memcpy paths in
     * firmware loading (ace-manifest.c, adsp-common.c ELF loader) write to
     * the coherent store.  After loading we sync coherent → all shadows.
     */
    mem->ptr = hp->coherent;

    adsp->hpsram = hp;

    ace_log("  hp-sram non-coherent: %d core shadows @ 0x%08x, "
             "coherent alias @ 0x%08x (size 0x%x)\n",
             num_cores, mem->base, coherent_base, mem->size);
}

struct adsp_dev *adsp_ace_init(const struct adsp_desc *board,
    MachineState *machine, const MemoryRegionOps *io_ops,
    uint32_t exec_addr, uint32_t imr_addr, uint32_t clk_kHz)
{
    struct adsp_dev *adsp;
    struct adsp_mem_desc *mem;
    void *man_ptr, *desc_ptr;
    int n, skip, size;
    void *rom;

    adsp = g_malloc0(sizeof(*adsp));
    adsp->desc = board;
    adsp->shm_idx = 0;
    adsp->system_memory = get_system_memory();
    adsp->kernel_filename = machine->kernel_filename;
    adsp->rom_filename = machine->initrd_filename;
    adsp->ops = board->ops;
    adsp->clk_kHz = clk_kHz;

    for (n = 0; n < machine->smp.cpus; n++) {
        adsp->xtensa[n] = g_malloc(sizeof(struct adsp_xtensa));
        adsp->xtensa[n]->cpu = XTENSA_CPU(cpu_create(machine->cpu_type));

        if (adsp->xtensa[n]->cpu == NULL) {
            error_report("unable to find CPU definition '%s'", machine->cpu_type);
            exit(EXIT_FAILURE);
        }

        adsp->xtensa[n]->env = &adsp->xtensa[n]->cpu->env;
        adsp->xtensa[n]->env->sregs[PRID] = n;
        cpu_reset(CPU(adsp->xtensa[n]->cpu));

        if (n > 0) {
            xtensa_runstall(adsp->xtensa[n]->env, true);
        }
    }

    adsp_create_memory_regions(adsp);

    for (n = 0; n < board->num_mem; n++) {
        mem = &board->mem_region[n];
        if (mem->per_core_non_coherent) {
            adsp_hpsram_setup(adsp, mem, ADSP_ACE40_DSP_LP_UNCACHE_BASE,
                              machine->smp.cpus);
        }
    }

    /*
     * Wire per-core HP-SRAM shadow pointers into each CPU's env so the cache
     * model can propagate dirty lines to the coherent store.
     */
    if (adsp->hpsram) {
        adsp_hpsram_state *hp = adsp->hpsram;

        for (n = 0; n < machine->smp.cpus; n++) {
            CPUXtensaState *env = adsp->xtensa[n]->env;

            env->hpsram_local    = hp->per_core[n];
            env->hpsram_coherent = hp->coherent;
            env->hpsram_base     = hp->base;
            env->hpsram_size     = hp->size;
        }

        adsp_mtrace_start(adsp);
    }

    {
        MemoryRegion *rom_region = g_new(MemoryRegion, 1);
        uint8_t *rom_ptr;
        uint32_t index;

        memory_region_init_ram(rom_region, NULL, "sys-rom", 0x100000, &error_fatal);
        memory_region_add_subregion(adsp->system_memory, 0xfe000000, rom_region);

        rom_ptr = memory_region_get_ram_ptr(rom_region);
        if (rom_ptr) {
            memset(rom_ptr, 0x00, 0x100000);
            for (index = 1; index < 0x100000; index += 3) {
                rom_ptr[index] = 0x30;
            }
        }
    }

    ace_tlb_init();
    sram_banks_global_init(adsp);
    adsp_create_io_devices(adsp, io_ops);

    if (adsp->kernel_filename == NULL) {
        ace_log_mask(CPU_LOG_RESET, "%s initialised, waiting for FW load", board->name);
        return adsp;
    }

    if (adsp->rom_filename != NULL) {
        mem = adsp_get_mem_space(adsp, ADSP_ACE_DSP_ROM_BASE);
        if (!mem) {
            return NULL;
        }

        rom = g_malloc(ADSP_ACE_DSP_ROM_SIZE);
        load_image_size(adsp->rom_filename, rom, ADSP_ACE_DSP_ROM_SIZE);
        memcpy(mem->ptr, rom, ADSP_ACE_DSP_ROM_SIZE);
    }

    {
        uint64_t elf_entry, elf_lowaddr, elf_highaddr;
        int elf_success;

        /* Attempt to natively load the file as a parsed ELF (for Unit Tests) */
        elf_success = load_elf(adsp->kernel_filename, NULL, NULL, NULL,
                               &elf_entry, &elf_lowaddr, &elf_highaddr, NULL,
                               0, EM_XTENSA, 0, 0);

        if (elf_success > 0) {
            CPUXtensaState *env = adsp->xtensa[0]->env;
            
            env->sregs[WINDOW_BASE] = 0;
            env->sregs[WINDOW_START] = 1;
            env->sregs[PS] = 0x00040000;
            env->regs[1] = 0xa10b0000; /* Default SP */
            env->regs[7] = 0xa1040000; /* Default context */
            env->pc = elf_entry;
            
            ace_log("Loaded DSP Firmware as native ELF: '%s' (Entry: 0x%08" PRIx64 ")\n", 
                    adsp->kernel_filename, elf_entry);
            return adsp;
        }
    }

    man_ptr = g_malloc0(MAX_IMAGE_SIZE);
    char *abs_path = realpath(adsp->kernel_filename, NULL);
    ace_log("Loading DSP Firmware: %s\n", abs_path ? abs_path : adsp->kernel_filename);
    free(abs_path);
    size = load_image_size(adsp->kernel_filename, man_ptr, MAX_IMAGE_SIZE);
    if (size <= 0) {
        error_report("error: failed to load firmware file '%s'\n",
                     adsp->kernel_filename);
        exit(EXIT_FAILURE);
    }
    
    adsp->fw_manifest = man_ptr;
    adsp->fw_manifest_size = size;

    if (exec_addr) {
        mem = adsp_get_mem_space(adsp, exec_addr);
        if (!mem) {
            exit(EXIT_FAILURE);
        }

        memcpy(mem->ptr + board->imr_boot_ldr_offset, man_ptr, size);
        return adsp;
    }

    skip = adsp_get_ext_man_size(man_ptr);
    desc_ptr = (uint8_t *)man_ptr + skip;
    while (*((uint32_t *)desc_ptr) != HEADER_MAGIC) {
        desc_ptr = desc_ptr + sizeof(uint32_t);
        skip += sizeof(uint32_t);
        if (skip >= size) {
            error_report("error: failed to find FW manifest header $AM1\n");
            exit(EXIT_FAILURE);
        }
    }

    {
        struct adsp_fw_desc *desc = (struct adsp_fw_desc *)desc_ptr;
        uint32_t brngup_text_file_offset = 0;
        uint32_t brngup_text_vaddr = 0;
        struct adsp_mem_desc *imr_mem;
        uint32_t fw_base_in_file;
        uint32_t brngup_text_absolute_offset;
        uint32_t target_brngup_addr = 0xa1048000;
        uint32_t file_base_offset;
        uint8_t *src;
        uint8_t *dst;
        uint8_t *brngup_in_file;
        int i;

        if (desc->header.num_module_entries > 0) {
            struct module *brngup = &desc->module[0];

            for (i = 0; i < 3; i++) {
                if (brngup->segment[i].flags.r.load &&
                    brngup->segment[i].flags.r.type == 0) {
                    brngup_text_file_offset = brngup->segment[i].file_offset;
                    brngup_text_vaddr = brngup->segment[i].v_base_addr;
                    break;
                }
            }
        }

        imr_mem = adsp_get_mem_space(adsp, imr_addr);
        if (imr_mem && brngup_text_file_offset > 0) {
            fw_base_in_file = skip - 0x2000;
            brngup_text_absolute_offset = fw_base_in_file + brngup_text_file_offset;
            file_base_offset = target_brngup_addr - brngup_text_absolute_offset - imr_addr;

            src = (uint8_t *)man_ptr;
            dst = imr_mem->ptr + file_base_offset;
            brngup_in_file = (uint8_t *)man_ptr + brngup_text_absolute_offset;
            memcpy(dst, src, size);
            (void)brngup_in_file;
            (void)brngup_text_vaddr;
        }

        if (desc->header.num_module_entries > 0) {
            uint32_t entry = desc->module[0].entry_point;
            CPUXtensaState *env = adsp->xtensa[0]->env;
            struct adsp_mem_desc *boot_mem;
            uint32_t *boot_ptr;

            env->sregs[WINDOW_BASE] = 0;
            env->sregs[WINDOW_START] = 1;
            env->sregs[PS] = 0x00040000;
            env->regs[0] = entry & 0xFFFFF000;
            env->regs[1] = 0xa10b0000;
            env->regs[7] = 0xa1040000;

            boot_mem = adsp_get_mem_space(adsp, 0xbe040000);
            if (boot_mem) {
                boot_ptr = (uint32_t *)(boot_mem->ptr);
                boot_ptr[0] = env->regs[1];
                boot_ptr[1] = 0;
            }

            env->pc = entry;
        }
        /* removed tb_flush */
    }

    return adsp;
}

struct adsp_mem_desc *adsp_get_mem_space(struct adsp_dev *adsp, uint32_t addr)
{
    const struct adsp_desc *board = adsp->desc;
    int i;

    /* find memory region for this address */
    for (i = 0; i < board->num_mem; i++) {
        if (addr >= board->mem_region[i].base &&
            addr < board->mem_region[i].base + board->mem_region[i].size) {
            return &board->mem_region[i];
        }
    }

    return NULL;
}

void adsp_set_lvl1_irq(struct adsp_dev *adsp, int irq, int active)
{
    /* TODO: allow interrupts other cores than core 0 */
    CPUXtensaState *env = adsp->xtensa[0]->env;
    uint32_t irq_bit = 1 << irq;

    if (active) {
        env->irq_counts[irq]++;
        env->sregs[INTSET] |= irq_bit;
    } else if (env->config->interrupt[irq].inttype == INTTYPE_LEVEL) {
        env->sregs[INTSET] &= ~irq_bit;
    }

    ace_log("ADSP: set lvl1 irq %d to %d\n", irq, active);
    check_interrupts(env);
}

#define SST_FW_SIG_SIZE		4
#define SST_HSW_FW_SIGN		"$SST"
#define SST_HSW_IRAM	1
#define SST_HSW_DRAM	2
#define SST_HSW_REGS	3

/*
 * Firmware module is made up of 1 . N blocks of different types. The
 * Block header is used to determine where and how block is to be copied in the
 * DSP/host memory space.
 */

struct snd_sst_dma_block_info {
    uint32_t type;		/* IRAM/DRAM */
    uint32_t size;		/* Bytes */
    uint32_t offset;	/* Offset in I/DRAM */
    uint32_t rsvd;		/* Reserved field */
} __attribute__((packed));

struct snd_sst_fw_module_info {
    uint32_t persistent_size;
    uint32_t scratch_size;
} __attribute__((packed));

struct snd_sst_fw_header {
    char sig[SST_FW_SIG_SIZE]; /* FW signature */
    uint32_t file_size;		/* size of fw minus this header */
    uint32_t num_modules;		/*  # of modules */
    uint32_t file_format;	/* version of header format */
    uint32_t reserved[4];
} __attribute__((packed));

struct snd_sst_fw_module_header {
    char sig[SST_FW_SIG_SIZE]; /* module signature */
    uint32_t size;	/* size of module */
    uint32_t num_blocks;	/* # of blocks */
    uint16_t padding;
    uint16_t type;	/* codec type, pp lib */
    uint32_t entry_point;
    struct snd_sst_fw_module_info info;
} __attribute__((packed));


/* generic module parser for mmaped DSPs */
static int sof_module_memcpy(struct adsp_dev *adsp,
                struct snd_sof_mod_hdr *module)
{
    const struct adsp_desc *board = adsp->desc;
    struct snd_sof_blk_hdr *block;
    struct adsp_mem_desc *mem;
    int count;

    fprintf(stdout, "new module size 0x%x blocks 0x%x type 0x%x\n",
        module->size, module->num_blocks, module->type);

    block = (void *)module + sizeof(*module);

    for (count = 0; count < module->num_blocks; count++) {
        if (block->size == 0) {
            fprintf(stderr,
                 "warning: block %d size zero\n", count);
            fprintf(stderr, " type 0x%x offset 0x%x\n",
                 block->type, block->offset);
            continue;
        }

        switch (block->type) {
        case SOF_FW_BLK_TYPE_RSRVD0:
        case SOF_FW_BLK_TYPE_ROM:
        case SOF_FW_BLK_TYPE_IMR:
        case SOF_FW_BLK_TYPE_RSRVD6:
            continue;	/* not handled atm */
        case SOF_FW_BLK_TYPE_IRAM:
        case SOF_FW_BLK_TYPE_DRAM:
        case SOF_FW_BLK_TYPE_SRAM:
            fprintf(stdout, "data: 0x%x size 0x%x\n",
                board->mem_zones[block->type].base + block->offset - board->mem_zones[block->type].host_offset,
                block->size);

            mem = adsp_get_mem_space(adsp, board->mem_zones[block->type].base + block->offset - board->mem_zones[block->type].host_offset);
            if (!mem)
                goto next;
            memcpy(mem->ptr + block->offset - board->mem_zones[block->type].host_offset,
                (void *)block + sizeof(*block), block->size);
            break;
        default:
            fprintf(stderr, "error: bad type 0x%x for block 0x%x\n",
                block->type, count);
            return -EINVAL;
        }

        fprintf(stdout,
            "block %d type 0x%x size 0x%x ==>  offset 0x%x\n",
            count, block->type, block->size, block->offset);

next:
        /* next block */
        block = (void *)block + sizeof(*block) + block->size;
    }

    return 0;
}

static int sst_module_memcpy(struct adsp_dev *adsp,
                struct snd_sst_fw_module_header *module)
{
    const struct adsp_desc *board = adsp->desc;
    struct snd_sst_dma_block_info *block;
    uint32_t offset;
    int count;

    fprintf(stdout, "new module size 0x%x blocks 0x%x type 0x%x\n",
        module->size, module->num_blocks, module->type);

    block = (void *)module + sizeof(*module);

    for (count = 0; count < module->num_blocks; count++) {
        if (block->size == 0) {
            fprintf(stderr,
                 "warning: block %d size zero\n", count);
            fprintf(stderr, " type 0x%x offset 0x%x\n",
                 block->type, block->offset);
            continue;
        }

        switch (block->type) {
        case SST_HSW_REGS:
            continue;	/* not handled atm */
        case SST_HSW_IRAM:
            fprintf(stdout, "text: 0x%x size 0x%x\n",
                block->offset,
                block->size);
            offset = board->mem_zones[block->type].host_offset + block->offset;
            cpu_physical_memory_write(board->mem_zones[block->type].base + block->offset,
                (void *)block + sizeof(*block), block->size);
            break;
        case SST_HSW_DRAM:
            fprintf(stdout, "data: 0x%x size 0x%x\n",
                board->mem_zones[block->type].base + block->offset - board->mem_zones[block->type].host_offset,
                block->size);
            offset = board->mem_zones[block->type].host_offset + block->offset;
            cpu_physical_memory_write(board->mem_zones[block->type].base + block->offset,
                (void *)block + sizeof(*block), block->size);
            break;
        default:
            fprintf(stderr, "error: bad type 0x%x for block 0x%x\n",
                block->type, count);
            return -EINVAL;
        }

        fprintf(stdout,
            "block %d type 0x%x size 0x%x ==>  offset 0x%x\n",
            count, block->type, block->size, offset);


        /* next block */
        block = (void *)block + sizeof(*block) + block->size;
    }

    return 0;
}

static int sof_check_header(struct adsp_dev *adsp,
    struct snd_sof_fw_header *header, size_t size)
{

    /* verify FW sig */
    if (strncmp(header->sig, SND_SOF_FW_SIG, SND_SOF_FW_SIG_SIZE) != 0)
        return -EINVAL;

    /* check size is valid */
    if (size != header->file_size + sizeof(*header)) {
        fprintf(stderr, "error: invalid filesize mismatch got 0x%lx expected 0x%lx\n",
            size, header->file_size + sizeof(*header));
        return -EINVAL;
    }

    ace_log("header size=0x%x modules=0x%x abi=0x%x size=%zu\n",
        header->file_size, header->num_modules,
        header->abi, sizeof(*header));

    return 0;
}

static int sst_check_header(struct adsp_dev *adsp,
    struct snd_sst_fw_header *header, size_t size)
{

    /* verify FW sig */
    if (strncmp(header->sig, SST_HSW_FW_SIGN, SST_FW_SIG_SIZE) != 0)
        return -EINVAL;

    /* check size is valid */
    if (size != header->file_size + sizeof(*header)) {
        fprintf(stderr, "error: invalid filesize mismatch got 0x%lx expected 0x%lx\n",
            size, header->file_size + sizeof(*header));
        return -EINVAL;
    }

    ace_log("header size=0x%x modules=0x%x size=%zu\n",
        header->file_size, header->num_modules,
        sizeof(*header));

    return 0;
}

static int sof_load_modules(struct adsp_dev *adsp, void *fw,
    struct snd_sof_fw_header *header, size_t size)
{
    struct snd_sof_mod_hdr *module;
    int ret, count;

    /* parse each module */
    module = fw + sizeof(*header);
    for (count = 0; count < header->num_modules; count++) {
        /* module */
        ret = sof_module_memcpy(adsp, module);
        if (ret < 0) {
            fprintf(stderr, "error: invalid module %d\n", count);
            return ret;
        }
        module = (void *)module + sizeof(*module) + module->size;
    }

    return 0;
}

static int sst_load_modules(struct adsp_dev *adsp, void *fw,
    struct snd_sst_fw_header *header, size_t size)
{
    struct snd_sst_fw_module_header *module;
    int ret, count;

    /* parse each module */
    module = fw + sizeof(*header);
    for (count = 0; count < header->num_modules; count++) {
        /* module */
        ret = sst_module_memcpy(adsp, module);
        if (ret < 0) {
            fprintf(stderr, "error: invalid module %d\n", count);
            return ret;
        }
        module = (void *)module + sizeof(*module) + module->size;
    }

    return 0;
}

uint32_t adsp_get_ext_man_size(const uint32_t *fw)
{
    /*
     * When fw points to extended manifest,
     * then first u32 must be equal SND_SOF_EXT_MAN_MAGIC_NUMBER.
     */
    if (fw[0] == SND_SOF_EXT_MAN_MAGIC_NUMBER)
        return fw[1];
    
    /* Check for $AE1 extended manifest format */
    if (fw[0] == EXT_MANIFEST_HEADER_MAGIC_AE1)
        return fw[1];

    /* otherwise given fw don't have an extended manifest */
    return 0;
}

void adsp_create_memory_regions(struct adsp_dev *adsp)
{
    const struct adsp_desc *board = adsp->desc;
    int i;

    ace_log("\n=== Virtual Address Space ===\n");
    ace_log("  %-12s: 0x00000000 - 0xffffffff  (size: 0x100000000 / 4194304 KB)\n", "Global 32-bit");
    
    ace_log("\n=== Memory Regions ===\n");
    
    /* create memory regions for each defined region */
    for (i = 0; i < board->num_mem; i++) {
        /* Per-core non-coherent regions are set up separately in adsp_hpsram_setup(). */
        if (board->mem_region[i].per_core_non_coherent) {
            ace_log("  %-12s: deferred (per-core non-coherent)\n",
                     board->mem_region[i].name);
            continue;
        }

        memory_region_init_ram(&board->mem_region[i].mr, NULL,
                board->mem_region[i].name,
                board->mem_region[i].size, &error_fatal);
        memory_region_add_subregion(adsp->system_memory,
                board->mem_region[i].base,
                &board->mem_region[i].mr);
        board->mem_region[i].ptr = memory_region_get_ram_ptr(&board->mem_region[i].mr);
        
        /* Initialize IMR and LP-SRAM to 0xFF to simulate unused flash / memory consistently. */
        if (strcmp(board->mem_region[i].name, "imr") == 0 ||
            strcmp(board->mem_region[i].name, "lp-sram") == 0) {
            memset(board->mem_region[i].ptr, 0xff, board->mem_region[i].size);
        }
        
        /* Print memory region information */
        ace_log("  %-12s: 0x%08lx - 0x%08lx  (size: 0x%lx / %lu KB)",
                board->mem_region[i].name,
                (unsigned long)board->mem_region[i].base,
                (unsigned long)(board->mem_region[i].base + board->mem_region[i].size - 1),
                (unsigned long)board->mem_region[i].size,
                (unsigned long)board->mem_region[i].size / 1024);
        
        /* Print alias/virtual address if present */
        if (board->mem_region[i].alias != 0) {
            ace_log("\n                    Alias: 0x%08lx - 0x%08lx (uncached)",
                    (unsigned long)board->mem_region[i].alias,
                    (unsigned long)(board->mem_region[i].alias + board->mem_region[i].size - 1));
        }
        ace_log("\n");
    }

    {
        const struct adsp_mem_desc *hp = NULL, *lp = NULL;
        for (i = 0; i < board->num_mem; i++) {
            if (!strcmp(board->mem_region[i].name, "hp-sram")) {
                hp = &board->mem_region[i];
            } else if (!strcmp(board->mem_region[i].name, "lp-sram")) {
                lp = &board->mem_region[i];
            }
        }
        if (hp || lp) {
            ace_log("  --- SRAM Topologies ---\n");
            if (hp) {
                ace_log("  %-12s: %d banks of 0x%x bytes (Total: 0x%x / %u KB)\n",
                    "hp-sram", MAX_HPSRAM_BANKS, HPSRAM_BANK_SIZE, 
                    MAX_HPSRAM_BANKS * HPSRAM_BANK_SIZE, (MAX_HPSRAM_BANKS * HPSRAM_BANK_SIZE) / 1024);
            }
            if (lp) {
                ace_log("  %-12s: %d banks of 0x%x bytes (Total: 0x%x / %u KB)\n",
                    "lp-sram", MAX_LPSRAM_BANKS, LPSRAM_BANK_SIZE, 
                    MAX_LPSRAM_BANKS * LPSRAM_BANK_SIZE, (MAX_LPSRAM_BANKS * LPSRAM_BANK_SIZE) / 1024);
            }
        }
    }

    ace_log("======================\n\n");
}

void adsp_create_io_devices(struct adsp_dev *adsp, const MemoryRegionOps *ops)
{
    const struct adsp_desc *board = adsp->desc;
    int i;

    /* create IO devices for each defined IO region */
    for (i = 0; i < board->num_io; i++) {
        if (board->io_dev[i].init) {
            struct adsp_io_info *info = g_malloc(sizeof(*info));
            const MemoryRegionOps *dev_ops;
            
            info->name = board->io_dev[i].name;
            info->next = NULL;
            info->num = i;
            info->adsp = adsp;
            info->space = &board->io_dev[i];
            info->region = g_malloc0(board->io_dev[i].desc.size);
            info->private = NULL;

            /* Use device-specific ops if available, otherwise use default ops */
            dev_ops = board->io_dev[i].ops ? board->io_dev[i].ops : ops;
            info->ops = dev_ops;

            memory_region_init_io(&board->io_dev[i].mr, NULL, dev_ops,
                    info, board->io_dev[i].name, board->io_dev[i].desc.size);
            memory_region_add_subregion(adsp->system_memory,
                    board->io_dev[i].desc.base,
                    &board->io_dev[i].mr);
            
            if (board->io_dev[i].init) {
                board->io_dev[i].init(adsp, adsp->system_memory, info);
            }
        }
    }
}

int adsp_load_modules(struct adsp_dev *adsp, void *fw, size_t size)
{
    struct snd_sof_fw_header *sof_header;
    struct snd_sst_fw_header *sst_header;
    int ext_man_size;
    int ret;

    ext_man_size = adsp_get_ext_man_size(fw);
    fw = (uint8_t *)fw + ext_man_size;
    size -= ext_man_size;

    sof_header = (struct snd_sof_fw_header *)fw;
    ret = sof_check_header(adsp, sof_header, size);
    if (ret == 0)
        return sof_load_modules(adsp, fw, sof_header, size);

    sst_header = (struct snd_sst_fw_header *)fw;
    ret = sst_check_header(adsp, sst_header, size);
    if (ret == 0)
        return sst_load_modules(adsp, fw, sst_header, size);

    fprintf(stderr, "error: invalid firmware signature\n");
    return -EINVAL;
}

