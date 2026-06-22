/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
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
#include "exec/helper-proto.h"
#include "exec/page-protection.h"
#include "qemu/host-utils.h"
#include "system/memory.h"
#include "qemu/atomic.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "exec/log.h"
#ifndef CONFIG_USER_ONLY
#include "system/runstate.h"
#endif

#ifndef CONFIG_USER_ONLY

void HELPER(update_ccount)(CPUXtensaState *env)
{
    XtensaCPU *cpu = env_archcpu(env);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    env->ccount_time = now;
    env->sregs[CCOUNT] = env->ccount_base +
        (uint32_t)clock_ns_to_ticks(cpu->clock, now - env->time_base);
}

void HELPER(wsr_ccount)(CPUXtensaState *env, uint32_t v)
{
    int i;

    HELPER(update_ccount)(env);
    env->ccount_base += v - env->sregs[CCOUNT];
    for (i = 0; i < env->config->nccompare; ++i) {
        HELPER(update_ccompare)(env, i);
    }
}

void HELPER(update_ccompare)(CPUXtensaState *env, uint32_t i)
{
    XtensaCPU *cpu = env_archcpu(env);
    uint64_t dcc;

    qatomic_and(&env->sregs[INTSET],
               ~(1u << env->config->timerint[i]));
    HELPER(update_ccount)(env);
    dcc = (uint64_t)(env->sregs[CCOMPARE + i] - env->sregs[CCOUNT] - 1) + 1;
    timer_mod(env->ccompare[i].timer,
              env->ccount_time + clock_ticks_to_ns(cpu->clock, dcc));
    env->yield_needed = 1;
}

/*!
 * Check vaddr accessibility/cache attributes and raise an exception if
 * specified by the ATOMCTL SR.
 *
 * Note: local memory exclusion is not implemented
 */
void HELPER(check_atomctl)(CPUXtensaState *env, uint32_t pc, uint32_t vaddr)
{
    uint32_t paddr, page_size, access;
    uint32_t atomctl = env->sregs[ATOMCTL];
    int rc = xtensa_get_physical_addr(env, true, vaddr, 1,
            xtensa_get_cring(env), &paddr, &page_size, &access);

    /*
     * s32c1i never causes LOAD_PROHIBITED_CAUSE exceptions,
     * see opcode description in the ISA
     */
    if (rc == 0 &&
            (access & (PAGE_READ | PAGE_WRITE)) != (PAGE_READ | PAGE_WRITE)) {
        rc = STORE_PROHIBITED_CAUSE;
    }

    if (rc) {
        HELPER(exception_cause_vaddr)(env, pc, rc, vaddr);
    }

    /*
     * When data cache is not configured use ATOMCTL bypass field.
     * See ISA, 4.3.12.4 The Atomic Operation Control Register (ATOMCTL)
     * under the Conditional Store Option.
     */
    if (!xtensa_option_enabled(env->config, XTENSA_OPTION_DCACHE)) {
        access = PAGE_CACHE_BYPASS;
    }

    switch (access & PAGE_CACHE_MASK) {
    case PAGE_CACHE_WB:
        atomctl >>= 2;
        /* fall through */
    case PAGE_CACHE_WT:
        atomctl >>= 2;
        /* fall through */
    case PAGE_CACHE_BYPASS:
        if ((atomctl & 0x3) == 0) {
            HELPER(exception_cause_vaddr)(env, pc,
                    LOAD_STORE_ERROR_CAUSE, vaddr);
        }
        break;

    case PAGE_CACHE_ISOLATE:
        HELPER(exception_cause_vaddr)(env, pc,
                LOAD_STORE_ERROR_CAUSE, vaddr);
        break;

    default:
        break;
    }
}

void HELPER(check_exclusive)(CPUXtensaState *env, uint32_t pc, uint32_t vaddr,
                             uint32_t is_write)
{
    uint32_t paddr, page_size, access;
    uint32_t atomctl = env->sregs[ATOMCTL];
    int rc = xtensa_get_physical_addr(env, true, vaddr, is_write,
                                      xtensa_get_cring(env), &paddr,
                                      &page_size, &access);

    if (rc) {
        HELPER(exception_cause_vaddr)(env, pc, rc, vaddr);
    }

    /* When data cache is not configured use ATOMCTL bypass field. */
    if (!xtensa_option_enabled(env->config, XTENSA_OPTION_DCACHE)) {
        access = PAGE_CACHE_BYPASS;
    }

    switch (access & PAGE_CACHE_MASK) {
    case PAGE_CACHE_WB:
        atomctl >>= 2;
        /* fall through */
    case PAGE_CACHE_WT:
        atomctl >>= 2;
        /* fall through */
    case PAGE_CACHE_BYPASS:
        if ((atomctl & 0x3) == 0) {
            HELPER(exception_cause_vaddr)(env, pc,
                                          EXCLUSIVE_ERROR_CAUSE, vaddr);
        }
        break;

    case PAGE_CACHE_ISOLATE:
        HELPER(exception_cause_vaddr)(env, pc,
                LOAD_STORE_ERROR_CAUSE, vaddr);
        break;

    default:
        break;
    }
}

void HELPER(wsr_memctl)(CPUXtensaState *env, uint32_t v)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_ICACHE)) {
        if (extract32(v, MEMCTL_IUSEWAYS_SHIFT, MEMCTL_IUSEWAYS_LEN) >
            env->config->icache_ways) {
            deposit32(v, MEMCTL_IUSEWAYS_SHIFT, MEMCTL_IUSEWAYS_LEN,
                      env->config->icache_ways);
        }
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_DCACHE)) {
        if (extract32(v, MEMCTL_DUSEWAYS_SHIFT, MEMCTL_DUSEWAYS_LEN) >
            env->config->dcache_ways) {
            deposit32(v, MEMCTL_DUSEWAYS_SHIFT, MEMCTL_DUSEWAYS_LEN,
                      env->config->dcache_ways);
        }
        if (extract32(v, MEMCTL_DALLOCWAYS_SHIFT, MEMCTL_DALLOCWAYS_LEN) >
            env->config->dcache_ways) {
            deposit32(v, MEMCTL_DALLOCWAYS_SHIFT, MEMCTL_DALLOCWAYS_LEN,
                      env->config->dcache_ways);
        }
    }
    env->sregs[MEMCTL] = v & env->config->memctl_mask;
}

void HELPER(cache_iaccess)(CPUXtensaState *env, uint32_t vaddr)
{
    xtensa_cache_access(env, vaddr, false, false);
}

void HELPER(cache_iprefetch)(CPUXtensaState *env, uint32_t vaddr)
{
    xtensa_cache_prefetch(env, vaddr, false);
}

void HELPER(cache_daccess)(CPUXtensaState *env, uint32_t vaddr,
                           uint32_t is_write)
{
    xtensa_cache_access(env, vaddr, true, is_write != 0);
}

void HELPER(cache_dprefetch)(CPUXtensaState *env, uint32_t vaddr)
{
    xtensa_cache_prefetch(env, vaddr, true);
}

void HELPER(cache_iinvalidate)(CPUXtensaState *env, uint32_t vaddr)
{
    xtensa_cache_invalidate(env, vaddr, false);
}

void HELPER(cache_din_lock_pref)(CPUXtensaState *env, uint32_t vaddr)
{
    /* PREF_LOCK.D: Prefetch with dcache lock. Access first, then lock. */
    xtensa_cache_access(env, vaddr, true, false);
    xtensa_cache_lock_line(env, vaddr, true);
}

void HELPER(cache_in_lock_pref)(CPUXtensaState *env, uint32_t vaddr)
{
    /* PREF_LOCK.I: Prefetch with icache lock. Access first, then lock. */
    xtensa_cache_access(env, vaddr, false, false);
    xtensa_cache_lock_line(env, vaddr, false);
}

void HELPER(cache_dinvalidate)(CPUXtensaState *env, uint32_t vaddr)
{
    xtensa_cache_invalidate(env, vaddr, true);
}

void HELPER(cache_iinvalidate_all)(CPUXtensaState *env)
{
    xtensa_cache_invalidate_all(env, false);
}

void HELPER(cache_dinvalidate_all)(CPUXtensaState *env)
{
    xtensa_cache_invalidate_all(env, true);
}

#endif

uint32_t HELPER(rer)(CPUXtensaState *env, uint32_t addr)
{
#ifndef CONFIG_USER_ONLY
    return address_space_ldl(env->address_space_er, addr,
                             MEMTXATTRS_UNSPECIFIED, NULL);
#else
    return 0;
#endif
}

void HELPER(wer)(CPUXtensaState *env, uint32_t data, uint32_t addr)
{
#ifndef CONFIG_USER_ONLY
    address_space_stl(env->address_space_er, addr, data,
                      MEMTXATTRS_UNSPECIFIED, NULL);
#endif
}

extern void __attribute__((weak)) ace_log_prefix(void);

static uint32_t last_trace_sp[16] = {0};

void HELPER(log_entry)(CPUXtensaState *env, uint32_t pc)
{
    if (unlikely(qemu_loglevel_mask(CPU_LOG_FUNC))) {
        uint32_t sp = env->regs[1];
        CPUState *cs = env_cpu(env);
        uint32_t cid = cs->cpu_index & 15;
        
        if (last_trace_sp[cid] != 0) {
            uint32_t delta = (sp > last_trace_sp[cid]) ? (sp - last_trace_sp[cid]) : (last_trace_sp[cid] - sp);
            if (delta > 8192) {
                if (ace_log_prefix) { ace_log_prefix(); }
                qemu_log("--- STACK SWITCH DETECTED: sp abruptly jumped from 0x%08x to 0x%08x ---\n", last_trace_sp[cid], sp);
            }
        }
        last_trace_sp[cid] = sp;

        HELPER(update_ccount)(env);
        if (ace_log_prefix) { ace_log_prefix(); }
        qemu_log("FUNC ENTRY: pc=0x%08x sp=0x%08x ps=0x%08x a2=0x%08x a3=0x%08x a4=0x%08x a5=0x%08x a6=0x%08x a7=0x%08x ccount=%u Imiss=%" PRIu64 " Dmiss=%" PRIu64 "\n",
                 pc, env->regs[1], env->sregs[PS], 
                 env->regs[2], env->regs[3], env->regs[4], env->regs[5], env->regs[6], env->regs[7],
                 env->sregs[CCOUNT], env->icache.misses, env->dcache.misses);
    }
}

void HELPER(log_ret)(CPUXtensaState *env, uint32_t pc)
{
    if (unlikely(qemu_loglevel_mask(CPU_LOG_FUNC))) {
        uint32_t sp = env->regs[1];
        CPUState *cs = env_cpu(env);
        uint32_t cid = cs->cpu_index & 15;
        
        if (last_trace_sp[cid] != 0) {
            uint32_t delta = (sp > last_trace_sp[cid]) ? (sp - last_trace_sp[cid]) : (last_trace_sp[cid] - sp);
            if (delta > 8192) {
                if (ace_log_prefix) { ace_log_prefix(); }
                qemu_log("--- STACK SWITCH DETECTED: sp abruptly jumped from 0x%08x to 0x%08x ---\n", last_trace_sp[cid], sp);
            }
        }
        last_trace_sp[cid] = sp;

        HELPER(update_ccount)(env);
        if (ace_log_prefix) { ace_log_prefix(); }
        qemu_log("FUNC RET:   pc=0x%08x sp=0x%08x ps=0x%08x ret=0x%08x a2=0x%08x a3=0x%08x a4=0x%08x a5=0x%08x a6=0x%08x a7=0x%08x ccount=%u Imiss=%" PRIu64 " Dmiss=%" PRIu64 "\n",
                 pc, env->regs[1], env->sregs[PS], env->regs[2],
                 env->regs[2], env->regs[3], env->regs[4], env->regs[5], env->regs[6], env->regs[7],
                 env->sregs[CCOUNT], env->icache.misses, env->dcache.misses);
    }
}

void HELPER(check_wsr_excause)(CPUXtensaState *env, uint32_t val)
{
    if (val == 63) {
        qemu_log("ZEPHYR FATAL ERROR (wsr.excause 63) DETECTED!\n");
        fprintf(stderr, "ZEPHYR FATAL ERROR (wsr.excause 63) DETECTED!\n");
        log_cpu_state(env_cpu(env), 0);
        cpu_dump_state(env_cpu(env), stderr, 0);
#ifndef CONFIG_USER_ONLY
        if (!XTENSA_CPU(env_cpu(env))->continue_on_exception) {
            qemu_system_guest_panicked(NULL);
        }
#endif
    }
}
