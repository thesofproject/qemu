/*
 * QEMU Xtensa CPU
 *
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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
#include "qapi/error.h"
#include "cpu.h"
#include "fpu/softfloat.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-clock.h"
#include "accel/tcg/cpu-ops.h"
#include "qemu/log.h"
#ifndef CONFIG_USER_ONLY
#include "system/memory.h"
#endif
#include "hw/core/qdev-properties.h"

static void xtensa_cache_init_one(XtensaCache *cache, uint32_t size,
                                  uint32_t line_size, uint32_t ways)
{
    uint32_t num_sets;

    memset(cache, 0, sizeof(*cache));

    if (!size || !line_size || !ways) {
        return;
    }

    num_sets = size / (line_size * ways);
    if (!num_sets) {
        return;
    }

    cache->line_size = line_size;
    cache->num_sets = num_sets;
    cache->ways = ways;
    cache->lines = g_new0(XtensaCacheLine, num_sets * ways);
}

static void xtensa_cache_reset_one(XtensaCache *cache)
{
    if (!cache->lines) {
        return;
    }

    memset(cache->lines, 0,
           sizeof(*cache->lines) * cache->num_sets * cache->ways);
    cache->accesses = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->evictions = 0;
    cache->writebacks = 0;
    cache->prefetches = 0;
    cache->generation = 0;
}

static void xtensa_cache_fini_one(XtensaCache *cache)
{
    g_free(cache->lines);
    memset(cache, 0, sizeof(*cache));
}

static bool xtensa_cache_in_range(const CPUXtensaState *env, uint32_t vaddr)
{
    uint64_t base = env->config->cache_region_base;
    uint64_t size = env->config->cache_region_size;

    return size && vaddr >= base && (uint64_t)vaddr < base + size;
}

static unsigned xtensa_cache_active_ways(const CPUXtensaState *env,
                                         bool is_data)
{
    unsigned configured = is_data ? env->config->dcache_ways
                                  : env->config->icache_ways;
    unsigned active;

    if (!configured) {
        return 0;
    }

    if (is_data) {
        active = extract32(env->sregs[MEMCTL], MEMCTL_DUSEWAYS_SHIFT,
                           MEMCTL_DUSEWAYS_LEN);
    } else {
        active = extract32(env->sregs[MEMCTL], MEMCTL_IUSEWAYS_SHIFT,
                           MEMCTL_IUSEWAYS_LEN);
    }

    if (!active || active > configured) {
        active = configured;
    }

    return active;
}

static unsigned xtensa_cache_alloc_ways(const CPUXtensaState *env)
{
    unsigned alloc = extract32(env->sregs[MEMCTL], MEMCTL_DALLOCWAYS_SHIFT,
                               MEMCTL_DALLOCWAYS_LEN);
    unsigned active = xtensa_cache_active_ways(env, true);

    if (!alloc || alloc > active) {
        alloc = active;
    }

    return alloc;
}

void xtensa_cache_init(CPUXtensaState *env)
{
    xtensa_cache_init_one(&env->icache, env->config->icache_size,
                          env->config->icache_line_bytes,
                          env->config->icache_ways);
    xtensa_cache_init_one(&env->dcache, env->config->dcache_size,
                          env->config->dcache_line_bytes,
                          env->config->dcache_ways);
}

void xtensa_cache_reset(CPUXtensaState *env)
{
    xtensa_cache_reset_one(&env->icache);
    xtensa_cache_reset_one(&env->dcache);
}

void xtensa_cache_fini(CPUXtensaState *env)
{
    xtensa_cache_fini_one(&env->icache);
    xtensa_cache_fini_one(&env->dcache);
}

void xtensa_cache_access(CPUXtensaState *env, uint32_t vaddr,
                         bool is_data, bool is_write)
{
    XtensaCache *cache = is_data ? &env->dcache : &env->icache;
    unsigned active_ways;
    unsigned alloc_ways;
    uint32_t line_addr;
    uint32_t set;
    uint32_t tag;
    XtensaCacheLine *set_lines;
    XtensaCacheLine *victim;
    uint64_t oldest_use;
    unsigned i;

    if (!cache->lines || !xtensa_cache_in_range(env, vaddr)) {
        return;
    }

    active_ways = xtensa_cache_active_ways(env, is_data);
    if (!active_ways) {
        return;
    }

    cache->accesses++;
    cache->generation++;

    line_addr = vaddr / cache->line_size;
    set = line_addr % cache->num_sets;
    tag = line_addr / cache->num_sets;
    set_lines = &cache->lines[set * cache->ways];

    for (i = 0; i < active_ways; i++) {
        XtensaCacheLine *line = &set_lines[i];

        if (line->valid && line->tag == tag) {
            cache->hits++;
            line->last_use = cache->generation;
            if (is_data && is_write && env->config->dcache_is_writeback) {
                line->dirty = true;
            }
            qemu_log_mask(CPU_LOG_CACHE,
                          "%scache hit:  vaddr=0x%08x set=%u way=%u tag=0x%x%s\n",
                          is_data ? "D" : "I", vaddr, set, i, tag,
                          (is_data && is_write) ? " [dirty]" : "");
            return;
        }
    }

    cache->misses++;
    qemu_log_mask(CPU_LOG_CACHE,
                  "%scache miss: vaddr=0x%08x set=%u tag=0x%x\n",
                  is_data ? "D" : "I", vaddr, set, tag);

    /*
     * HP-SRAM non-coherent model: on a cache miss, fill this core's local
     * shadow from the coherent store so we see the authoritative contents
     * (written by other cores via the coherent alias or after their own
     * writebacks).
     */
    if (is_data && env->hpsram_local && env->hpsram_coherent) {
        uint32_t line_vaddr = (vaddr / cache->line_size) * cache->line_size;
        if (line_vaddr >= env->hpsram_base &&
            (uint64_t)line_vaddr + cache->line_size <=
                (uint64_t)env->hpsram_base + env->hpsram_size) {
            uint32_t offset = line_vaddr - env->hpsram_base;
            memcpy(env->hpsram_local  + offset,
                   env->hpsram_coherent + offset,
                   cache->line_size);
            qemu_log_mask(CPU_LOG_CACHE,
                          "Dcache fill from coherent: offset=0x%x line_size=%u\n",
                          offset, cache->line_size);
        }
    }

    alloc_ways = is_data ? xtensa_cache_alloc_ways(env) : active_ways;
    if (!alloc_ways) {
        return;
    }

    victim = &set_lines[0];
    oldest_use = UINT64_MAX;
    for (i = 0; i < alloc_ways; i++) {
        XtensaCacheLine *line = &set_lines[i];

        if (!line->valid) {
            victim = line;
            oldest_use = 0;
            break;
        }
        /* Skip locked lines in eviction search; find oldest unlocked victim. */
        if (!line->locked && line->last_use < oldest_use) {
            oldest_use = line->last_use;
            victim = line;
        }
    }

    /* If all candidates are locked, cannot allocate a line. */
    if (victim->valid && victim->locked) {
        qemu_log_mask(CPU_LOG_CACHE,
                      "%scache miss cannot allocate: all ways locked for set=%u\n",
                      is_data ? "D" : "I", set);
        return;
    }

    if (victim->valid) {
        cache->evictions++;
        if (is_data && victim->dirty && env->config->dcache_is_writeback) {
            cache->writebacks++;
            /*
             * HP-SRAM non-coherent model: flush the evicted dirty line from
             * this core's shadow to the shared coherent store so other cores
             * can observe it via 0x40020000.
             */
            if (env->hpsram_local && env->hpsram_coherent) {
                uint32_t evict_vaddr =
                    (victim->tag * cache->num_sets + set) * cache->line_size;
                if (evict_vaddr >= env->hpsram_base &&
                    (uint64_t)evict_vaddr + cache->line_size <=
                        (uint64_t)env->hpsram_base + env->hpsram_size) {
                    uint32_t offset = evict_vaddr - env->hpsram_base;
                    memcpy(env->hpsram_coherent + offset,
                           env->hpsram_local    + offset,
                           cache->line_size);
                    qemu_log_mask(CPU_LOG_CACHE,
                                  "Dcache evict+wb coherent: offset=0x%x\n",
                                  offset);
                }
            }
            qemu_log_mask(CPU_LOG_CACHE,
                          "Dcache evict+wb: set=%u evicted_tag=0x%x for vaddr=0x%08x\n",
                          set, victim->tag, vaddr);
        } else {
            qemu_log_mask(CPU_LOG_CACHE,
                          "%scache evict: set=%u evicted_tag=0x%x for vaddr=0x%08x\n",
                          is_data ? "D" : "I", set, victim->tag, vaddr);
        }
    }

    victim->valid = true;
    victim->tag = tag;
    victim->last_use = cache->generation;
    victim->dirty = is_data && is_write && env->config->dcache_is_writeback;
    /* Inherit lock state if victim was locked (should not happen, but preserve it). */
    /* victim->locked is already set. */
}

void xtensa_cache_prefetch(CPUXtensaState *env, uint32_t vaddr,
                           bool is_data)
{
    XtensaCache *cache = is_data ? &env->dcache : &env->icache;

    if (!cache->lines || !xtensa_cache_in_range(env, vaddr)) {
        return;
    }

    cache->prefetches++;
    xtensa_cache_access(env, vaddr, is_data, false);
}

void xtensa_cache_lock_line(CPUXtensaState *env, uint32_t vaddr,
                            bool is_data)
{
    XtensaCache *cache = is_data ? &env->dcache : &env->icache;
    unsigned active_ways;
    uint32_t line_addr;
    uint32_t set;
    uint32_t tag;
    XtensaCacheLine *set_lines;
    unsigned i;

    if (!cache->lines || !xtensa_cache_in_range(env, vaddr)) {
        return;
    }

    active_ways = xtensa_cache_active_ways(env, is_data);
    if (!active_ways) {
        return;
    }

    line_addr = vaddr / cache->line_size;
    set = line_addr % cache->num_sets;
    tag = line_addr / cache->num_sets;
    set_lines = &cache->lines[set * cache->ways];

    /* Look for existing line and lock it. */
    for (i = 0; i < active_ways; i++) {
        XtensaCacheLine *line = &set_lines[i];

        if (line->valid && line->tag == tag) {
            line->locked = true;
            qemu_log_mask(CPU_LOG_CACHE,
                          "%scache lock:  vaddr=0x%08x set=%u way=%u tag=0x%x\n",
                          is_data ? "D" : "I", vaddr, set, i, tag);
            return;
        }
    }

    qemu_log_mask(CPU_LOG_CACHE,
                  "%scache lock fail: vaddr=0x%08x not in cache (set=%u tag=0x%x)\n",
                  is_data ? "D" : "I", vaddr, set, tag);
}

void xtensa_cache_invalidate(CPUXtensaState *env, uint32_t vaddr,
                             bool is_data)
{
    XtensaCache *cache = is_data ? &env->dcache : &env->icache;
    unsigned active_ways;
    uint32_t line_addr;
    uint32_t set;
    uint32_t tag;
    XtensaCacheLine *set_lines;
    unsigned i;

    if (!cache->lines || !xtensa_cache_in_range(env, vaddr)) {
        return;
    }

    active_ways = xtensa_cache_active_ways(env, is_data);
    if (!active_ways) {
        return;
    }

    line_addr = vaddr / cache->line_size;
    set = line_addr % cache->num_sets;
    tag = line_addr / cache->num_sets;
    set_lines = &cache->lines[set * cache->ways];

    for (i = 0; i < active_ways; i++) {
        XtensaCacheLine *line = &set_lines[i];

        if (line->valid && line->tag == tag) {
            if (is_data && line->dirty && env->config->dcache_is_writeback) {
                cache->writebacks++;
                /*
                 * HP-SRAM non-coherent model: flush dirty line to coherent
                 * store on explicit cache invalidation/writeback.
                 */
                if (env->hpsram_local && env->hpsram_coherent) {
                    uint32_t line_vaddr =
                        (tag * cache->num_sets + set) * cache->line_size;
                    if (line_vaddr >= env->hpsram_base &&
                        (uint64_t)line_vaddr + cache->line_size <=
                            (uint64_t)env->hpsram_base + env->hpsram_size) {
                        uint32_t offset = line_vaddr - env->hpsram_base;
                        memcpy(env->hpsram_coherent + offset,
                               env->hpsram_local    + offset,
                               cache->line_size);
                        qemu_log_mask(CPU_LOG_CACHE,
                                      "Dcache inv+wb coherent: offset=0x%x\n",
                                      offset);
                    }
                }
                qemu_log_mask(CPU_LOG_CACHE,
                              "Dcache inv+wb: vaddr=0x%08x set=%u tag=0x%x\n",
                              vaddr, set, tag);
            } else {
                qemu_log_mask(CPU_LOG_CACHE,
                              "%scache inv: vaddr=0x%08x set=%u tag=0x%x\n",
                              is_data ? "D" : "I", vaddr, set, tag);
            }
            memset(line, 0, sizeof(*line));
            return;
        }
    }
}

void xtensa_cache_invalidate_all(CPUXtensaState *env, bool is_data)
{
    XtensaCache *cache = is_data ? &env->dcache : &env->icache;
    uint32_t total_lines;
    uint32_t i;

    if (!cache->lines) {
        return;
    }

    total_lines = cache->num_sets * cache->ways;
    if (is_data && env->config->dcache_is_writeback) {
        for (i = 0; i < total_lines; i++) {
            if (cache->lines[i].valid && cache->lines[i].dirty) {
                cache->writebacks++;
            }
        }
    }

    qemu_log_mask(CPU_LOG_CACHE,
                  "%scache inv_all: %u lines flushed\n",
                  is_data ? "D" : "I", total_lines);
    memset(cache->lines, 0, sizeof(*cache->lines) * total_lines);
}

static void xtensa_cpu_finalize(Object *obj)
{
    XtensaCPU *cpu = XTENSA_CPU(obj);

    xtensa_cache_fini(&cpu->env);
}


static void xtensa_cpu_set_pc(CPUState *cs, vaddr value)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    cpu->env.pc = value;
}

static vaddr xtensa_cpu_get_pc(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    return cpu->env.pc;
}

static TCGTBCPUState xtensa_get_tb_cpu_state(CPUState *cs)
{
    CPUXtensaState *env = cpu_env(cs);
    uint32_t flags = 0;
    uint64_t cs_base = 0;

    flags |= xtensa_get_ring(env);
    if (env->sregs[PS] & PS_EXCM) {
        flags |= XTENSA_TBFLAG_EXCM;
    } else if (xtensa_option_enabled(env->config, XTENSA_OPTION_LOOP)) {
        uint64_t lend_dist =
            env->sregs[LEND] - (env->pc & -(1u << TARGET_PAGE_BITS));

        /*
         * 0 in the csbase_lend field means that there may not be a loopback
         * for any instruction that starts inside this page. Any other value
         * means that an instruction that ends at this offset from the page
         * start may loop back and will need loopback code to be generated.
         *
         * lend_dist is 0 when LEND points to the start of the page, but
         * no instruction that starts inside this page may end at offset 0,
         * so it's still correct.
         *
         * When an instruction ends at a page boundary it may only start in
         * the previous page. lend_dist will be encoded as TARGET_PAGE_SIZE
         * for the TB that contains this instruction.
         */
        if (lend_dist < (1u << TARGET_PAGE_BITS) + env->config->max_insn_size) {
            uint64_t lbeg_off = env->sregs[LEND] - env->sregs[LBEG];

            cs_base = lend_dist;
            if (lbeg_off < 256) {
                cs_base |= lbeg_off << XTENSA_CSBASE_LBEG_OFF_SHIFT;
            }
        }
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_EXTENDED_L32R) &&
            (env->sregs[LITBASE] & 1)) {
        flags |= XTENSA_TBFLAG_LITBASE;
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_DEBUG)) {
        if (xtensa_get_cintlevel(env) < env->config->debug_level) {
            flags |= XTENSA_TBFLAG_DEBUG;
        }
        if (xtensa_get_cintlevel(env) < env->sregs[ICOUNTLEVEL]) {
            flags |= XTENSA_TBFLAG_ICOUNT;
        }
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_COPROCESSOR)) {
        flags |= env->sregs[CPENABLE] << XTENSA_TBFLAG_CPENABLE_SHIFT;
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_WINDOWED_REGISTER) &&
        (env->sregs[PS] & (PS_WOE | PS_EXCM)) == PS_WOE) {
        uint32_t windowstart = xtensa_replicate_windowstart(env) >>
            (env->sregs[WINDOW_BASE] + 1);
        uint32_t w = ctz32(windowstart | 0x8);

        flags |= (w << XTENSA_TBFLAG_WINDOW_SHIFT) | XTENSA_TBFLAG_CWOE;
        flags |= extract32(env->sregs[PS], PS_CALLINC_SHIFT,
                            PS_CALLINC_LEN) << XTENSA_TBFLAG_CALLINC_SHIFT;
    } else {
        flags |= 3 << XTENSA_TBFLAG_WINDOW_SHIFT;
    }
    if (env->yield_needed) {
        flags |= XTENSA_TBFLAG_YIELD;
    }

    return (TCGTBCPUState){
        .pc = env->pc,
        .flags = flags,
        .cs_base = cs_base,
    };
}

static void xtensa_restore_state_to_opc(CPUState *cs,
                                        const TranslationBlock *tb,
                                        const uint64_t *data)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    cpu->env.pc = data[0];
}

#ifndef CONFIG_USER_ONLY
static bool xtensa_cpu_has_work(CPUState *cs)
{
    CPUXtensaState *env = cpu_env(cs);

    return !env->runstall && env->pending_irq_level;
}
#endif /* !CONFIG_USER_ONLY */

static int xtensa_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return xtensa_get_cring(cpu_env(cs));
}

#ifdef CONFIG_USER_ONLY
static bool abi_call0;

void xtensa_set_abi_call0(void)
{
    abi_call0 = true;
}

bool xtensa_abi_call0(void)
{
    return abi_call0;
}
#endif

static void xtensa_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(obj);
    CPUXtensaState *env = cpu_env(cs);
    bool dfpu = xtensa_option_enabled(env->config,
                                      XTENSA_OPTION_DFP_COPROCESSOR);

    if (xcc->parent_phases.hold) {
        xcc->parent_phases.hold(obj, type);
    }

    env->pc = env->config->exception_vector[EXC_RESET0 + env->static_vectors];
    env->sregs[LITBASE] &= ~1;
#ifndef CONFIG_USER_ONLY
    env->sregs[PS] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_INTERRUPT) ? 0x1f : 0x10;
    env->pending_irq_level = 0;
#else
    env->sregs[PS] = PS_UM | (3 << PS_RING_SHIFT);
    if (xtensa_option_enabled(env->config,
                              XTENSA_OPTION_WINDOWED_REGISTER) &&
        !xtensa_abi_call0()) {
        env->sregs[PS] |= PS_WOE;
    }
    env->sregs[CPENABLE] = 0xff;
#endif
    env->sregs[VECBASE] = env->config->vecbase;
    qemu_log("CPU reset: Set VECBASE to 0x%x from config\n", env->config->vecbase);
    env->sregs[IBREAKENABLE] = 0;
    env->sregs[MEMCTL] = MEMCTL_IL0EN & env->config->memctl_mask;
    env->sregs[ATOMCTL] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_ATOMCTL) ? 0x28 : 0x15;
    env->sregs[CONFIGID0] = env->config->configid[0];
    env->sregs[CONFIGID1] = env->config->configid[1];
    env->exclusive_addr = -1;
    xtensa_cache_reset(env);

#ifndef CONFIG_USER_ONLY
    reset_mmu(env);
    cs->halted = env->runstall;
#endif
    /* For inf * 0 + NaN, return the input NaN */
    set_float_infzeronan_rule(float_infzeronan_dnan_never, &env->fp_status);
    set_no_signaling_nans(!dfpu, &env->fp_status);
    /* Default NaN value: sign bit clear, set frac msb */
    set_float_default_nan_pattern(0b01000000, &env->fp_status);
    xtensa_use_first_nan(env, !dfpu);
}

static ObjectClass *xtensa_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = g_strdup_printf(XTENSA_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

    return oc;
}

static void xtensa_cpu_disas_set_info(const CPUState *cs,
                                      disassemble_info *info)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    info->private_data = cpu->env.config->isa;
    info->print_insn = print_insn_xtensa;
    info->endian = TARGET_BIG_ENDIAN ? BFD_ENDIAN_BIG
                                     : BFD_ENDIAN_LITTLE;
}

static void xtensa_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

#ifndef CONFIG_USER_ONLY
    CPUXtensaState *env = &XTENSA_CPU(dev)->env;

    env->address_space_er = g_malloc(sizeof(*env->address_space_er));
    env->system_er = g_malloc(sizeof(*env->system_er));
    memory_region_init_io(env->system_er, OBJECT(dev), NULL, env, "er",
                          UINT64_C(0x100000000));
    address_space_init(env->address_space_er, env->system_er, "ER");

    xtensa_irq_init(&XTENSA_CPU(dev)->env);
#endif

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    cs->gdb_num_regs = xcc->config->gdb_regmap.num_regs;

    qemu_init_vcpu(cs);

    xcc->parent_realize(dev, errp);
}

static void xtensa_cpu_initfn(Object *obj)
{
    XtensaCPU *cpu = XTENSA_CPU(obj);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(obj);
    CPUXtensaState *env = &cpu->env;

    env->config = xcc->config;

#ifndef CONFIG_USER_ONLY
    cpu->clock = qdev_init_clock_in(DEVICE(obj), "clk-in", NULL, cpu, 0);
    clock_set_hz(cpu->clock, env->config->clock_freq_khz * 1000);
#endif

    xtensa_cache_init(env);
}

XtensaCPU *xtensa_cpu_create_with_clock(const char *cpu_type, Clock *cpu_refclk)
{
    DeviceState *cpu;

    cpu = qdev_new(cpu_type);
    qdev_connect_clock_in(cpu, "clk-in", cpu_refclk);
    qdev_realize(cpu, NULL, &error_abort);

    return XTENSA_CPU(cpu);
}

#ifndef CONFIG_USER_ONLY
static const VMStateDescription vmstate_xtensa_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps xtensa_sysemu_ops = {
    .has_work = xtensa_cpu_has_work,
    .get_phys_page_debug = xtensa_cpu_get_phys_page_debug,
};
#endif

static const TCGCPUOps xtensa_tcg_ops = {
    /* Xtensa processors have a weak memory model */
    .guest_default_memory_order = 0,
    .mttcg_supported = true,

    .initialize = xtensa_translate_init,
    .translate_code = xtensa_translate_code,
    .debug_excp_handler = xtensa_breakpoint_handler,
    .get_tb_cpu_state = xtensa_get_tb_cpu_state,
    .restore_state_to_opc = xtensa_restore_state_to_opc,
    .mmu_index = xtensa_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = xtensa_cpu_tlb_fill,
    .pointer_wrap = cpu_pointer_wrap_uint32,
    .cpu_exec_interrupt = xtensa_cpu_exec_interrupt,
    .cpu_exec_halt = xtensa_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = xtensa_cpu_do_interrupt,
    .do_transaction_failed = xtensa_cpu_do_transaction_failed,
    .do_unaligned_access = xtensa_cpu_do_unaligned_access,
    .debug_check_breakpoint = xtensa_debug_check_breakpoint,
#endif /* !CONFIG_USER_ONLY */
};

static const Property xtensa_cpu_properties[] = {
    DEFINE_PROP_BOOL("continue-on-exception", XtensaCPU, continue_on_exception, false),
};

static void xtensa_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    XtensaCPUClass *xcc = XTENSA_CPU_CLASS(cc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, xtensa_cpu_realizefn,
                                    &xcc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, xtensa_cpu_reset_hold, NULL,
                                       &xcc->parent_phases);

    device_class_set_props(dc, xtensa_cpu_properties);

    cc->class_by_name = xtensa_cpu_class_by_name;
    cc->dump_state = xtensa_cpu_dump_state;
    cc->set_pc = xtensa_cpu_set_pc;
    cc->get_pc = xtensa_cpu_get_pc;
    cc->gdb_read_register = xtensa_cpu_gdb_read_register;
    cc->gdb_write_register = xtensa_cpu_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &xtensa_sysemu_ops;
    dc->vmsd = &vmstate_xtensa_cpu;
#endif
    cc->disas_set_info = xtensa_cpu_disas_set_info;
    cc->tcg_ops = &xtensa_tcg_ops;
}

static const TypeInfo xtensa_cpu_type_info = {
    .name = TYPE_XTENSA_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(XtensaCPU),
    .instance_align = __alignof(XtensaCPU),
    .instance_init = xtensa_cpu_initfn,
    .instance_finalize = xtensa_cpu_finalize,
    .abstract = true,
    .class_size = sizeof(XtensaCPUClass),
    .class_init = xtensa_cpu_class_init,
};

static void xtensa_cpu_register_types(void)
{
    type_register_static(&xtensa_cpu_type_info);
}

type_init(xtensa_cpu_register_types)
