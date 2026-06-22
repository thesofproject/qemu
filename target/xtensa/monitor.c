/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"

static unsigned xtensa_monitor_cache_active_ways(const CPUXtensaState *env,
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

static void xtensa_monitor_dump_locked_lines(Monitor *mon,
                                             const CPUXtensaState *env,
                                             const XtensaCache *cache,
                                             bool is_data)
{
    unsigned active_ways = xtensa_monitor_cache_active_ways(env, is_data);
    uint64_t locked_count = 0;
    uint32_t set;

    if (!cache->lines || !active_ways) {
        monitor_printf(mon, "%sCACHE locked lines: 0\n",
                       is_data ? "D" : "I");
        return;
    }

    for (set = 0; set < cache->num_sets; set++) {
        unsigned way;

        for (way = 0; way < active_ways; way++) {
            const XtensaCacheLine *line = &cache->lines[set * cache->ways + way];

            if (line->valid && line->locked) {
                locked_count++;
            }
        }
    }

    monitor_printf(mon, "%sCACHE locked lines: %" PRIu64 "\n",
                   is_data ? "D" : "I", locked_count);

    if (!locked_count) {
        return;
    }

    for (set = 0; set < cache->num_sets; set++) {
        unsigned way;

        for (way = 0; way < active_ways; way++) {
            const XtensaCacheLine *line = &cache->lines[set * cache->ways + way];

            if (line->valid && line->locked) {
                uint32_t line_addr = (line->tag * cache->num_sets + set) *
                                     cache->line_size;

                monitor_printf(mon,
                               "  %c set=%u way=%u addr=0x%08x tag=0x%x%s\n",
                               is_data ? 'D' : 'I', set, way, line_addr,
                               line->tag,
                               is_data && line->dirty ? " dirty" : "");
            }
        }
    }
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env(mon);

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    dump_mmu(env1);
}

void hmp_info_xtensa_cache(Monitor *mon, const QDict *qdict)
{
    CPUState *cs = mon_get_cpu(mon);
    XtensaCPU *cpu;
    CPUXtensaState *env;

    if (!cs) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    cpu = XTENSA_CPU(cs);
    env = &cpu->env;

    monitor_printf(mon, "Xtensa CPU: %s\n",
                   env->config->name ? env->config->name : "unknown");

    monitor_printf(mon,
                   "Cache region: base=0x%08x size=0x%08x\n",
                   env->config->cache_region_base,
                   env->config->cache_region_size);

    monitor_printf(mon,
                   "ICACHE config: size=%u line=%u ways=%u active_ways=%u\n",
                   env->config->icache_size,
                   env->config->icache_line_bytes,
                   env->config->icache_ways,
                   extract32(env->sregs[MEMCTL], MEMCTL_IUSEWAYS_SHIFT,
                             MEMCTL_IUSEWAYS_LEN));

    monitor_printf(mon,
                   "DCACHE config: size=%u line=%u ways=%u active_ways=%u alloc_ways=%u writeback=%s\n",
                   env->config->dcache_size,
                   env->config->dcache_line_bytes,
                   env->config->dcache_ways,
                   extract32(env->sregs[MEMCTL], MEMCTL_DUSEWAYS_SHIFT,
                             MEMCTL_DUSEWAYS_LEN),
                   extract32(env->sregs[MEMCTL], MEMCTL_DALLOCWAYS_SHIFT,
                             MEMCTL_DALLOCWAYS_LEN),
                   env->config->dcache_is_writeback ? "on" : "off");

    monitor_printf(mon,
                   "ICACHE stats: access=%" PRIu64 " hit=%" PRIu64
                   " miss=%" PRIu64 " evict=%" PRIu64 " prefetches=%" PRIu64 "\n",
                   env->icache.accesses,
                   env->icache.hits,
                   env->icache.misses,
                   env->icache.evictions,
                   env->icache.prefetches);

    xtensa_monitor_dump_locked_lines(mon, env, &env->icache, false);

    monitor_printf(mon,
                   "DCACHE stats: access=%" PRIu64 " hit=%" PRIu64
                   " miss=%" PRIu64 " evict=%" PRIu64
                   " writeback=%" PRIu64 " prefetches=%" PRIu64 "\n",
                   env->dcache.accesses,
                   env->dcache.hits,
                   env->dcache.misses,
                   env->dcache.evictions,
                   env->dcache.writebacks,
                   env->dcache.prefetches);

    xtensa_monitor_dump_locked_lines(mon, env, &env->dcache, true);
}

void __attribute__((weak)) adsp_monitor_ace_irq(Monitor *mon);
void __attribute__((weak)) adsp_monitor_ace_core(Monitor *mon, const QDict *qdict);
void __attribute__((weak)) adsp_monitor_ace_manifest(Monitor *mon, const QDict *qdict);

void hmp_info_ace_irq(Monitor *mon, const QDict *qdict)
{
    CPUState *cs = mon_get_cpu(mon);
    XtensaCPU *cpu;
    CPUXtensaState *env;
    int i;

    if (!cs) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    cpu = XTENSA_CPU(cs);
    env = &cpu->env;

    monitor_printf(mon, "Xtensa CPU: %s\n",
                   env->config->name ? env->config->name : "unknown");

    monitor_printf(mon, "IRQ counts:\n");
    for (i = 0; i < env->config->ninterrupt; i++) {
        if (env->irq_counts[i] > 0) {
            const char *name = "unknown";
            const char *type = "unknown";
            int level = env->config->interrupt[i].level;

            if (env->config->irq_info) {
                for (int j = 0; j < env->config->num_irq_info; j++) {
                    if (env->config->irq_info[j].irq == i) {
                        name = env->config->irq_info[j].name;
                        type = env->config->irq_info[j].type;
                        if (env->config->irq_info[j].level) {
                            level = env->config->irq_info[j].level;
                        }
                        break;
                    }
                }
            }

            monitor_printf(mon, "  IRQ %2d (%15s, %10s, level %d): %" PRIu64 "\n",
                           i, name, type, level, env->irq_counts[i]);
        }
    }

    if (adsp_monitor_ace_irq) {
        adsp_monitor_ace_irq(mon);
    }
}

void hmp_info_ace_core(Monitor *mon, const QDict *qdict)
{
    if (adsp_monitor_ace_core) {
        adsp_monitor_ace_core(mon, qdict);
    } else {
        monitor_printf(mon, "ACE core monitoring not available.\n");
    }
}

void hmp_info_ace_manifest(Monitor *mon, const QDict *qdict)
{
    if (adsp_monitor_ace_manifest) {
        adsp_monitor_ace_manifest(mon, qdict);
    } else {
        monitor_printf(mon, "ACE manifest monitoring not available.\n");
    }
}

void __attribute__((weak)) adsp_monitor_ace_ipc(Monitor *mon, const QDict *qdict);
void hmp_info_ace_ipc(Monitor *mon, const QDict *qdict)
{
    if (adsp_monitor_ace_ipc) {
        adsp_monitor_ace_ipc(mon, qdict);
    } else {
        monitor_printf(mon, "ACE IPC monitoring not available.\n");
    }
}

void __attribute__((weak)) adsp_monitor_ace_ipc_tx(Monitor *mon, const QDict *qdict);
void hmp_ace_ipc_tx(Monitor *mon, const QDict *qdict)
{
    if (adsp_monitor_ace_ipc_tx) {
        adsp_monitor_ace_ipc_tx(mon, qdict);
    } else {
        monitor_printf(mon, "ACE IPC injection not available.\n");
    }
}

void __attribute__((weak)) adsp_monitor_ace_ipc_rx(Monitor *mon, const QDict *qdict);
void hmp_ace_ipc_rx(Monitor *mon, const QDict *qdict)
{
    if (adsp_monitor_ace_ipc_rx) {
        adsp_monitor_ace_ipc_rx(mon, qdict);
    } else {
        monitor_printf(mon, "ACE IPC reply not available.\n");
    }
}
