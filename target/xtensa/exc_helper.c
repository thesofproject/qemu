/*
 * Copyright (c) 2011 - 2019, Max Filippov, Open Source and Linux Lab.
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
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "qemu/atomic.h"
#include "qemu/plugin.h"
#include "exec/log.h"
#ifndef CONFIG_USER_ONLY
#include "system/runstate.h"
#endif

void HELPER(exception)(CPUXtensaState *env, uint32_t excp)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    if (excp == EXCP_YIELD) {
        env->yield_needed = 0;
    }
    cpu_loop_exit(cs);
}

void HELPER(exception_cause)(CPUXtensaState *env, uint32_t pc, uint32_t cause)
{
    uint32_t vector;

    env->pc = pc;
    if (env->sregs[PS] & PS_EXCM) {
        if (env->config->ndepc) {
            env->sregs[DEPC] = pc;
        } else {
            env->sregs[EPC1] = pc;
        }
        vector = EXC_DOUBLE;
    } else {
        env->sregs[EPC1] = pc;
        vector = (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
    }

    env->sregs[EXCCAUSE] = cause;
    env->sregs[PS] |= PS_EXCM;

    switch (cause) {
    case ILLEGAL_INSTRUCTION_CAUSE:
    case INSTRUCTION_FETCH_ERROR_CAUSE:
    case LOAD_STORE_ERROR_CAUSE:
    case INTEGER_DIVIDE_BY_ZERO_CAUSE:
    case PRIVILEGED_CAUSE:
    case LOAD_STORE_ALIGNMENT_CAUSE:
    case INSTR_PIF_DATA_ERROR_CAUSE:
    case LOAD_STORE_PIF_DATA_ERROR_CAUSE:
    case INSTR_PIF_ADDR_ERROR_CAUSE:
    case LOAD_STORE_PIF_ADDR_ERROR_CAUSE:
        qemu_log("ZEPHYR FATAL ERROR / EXCEPTION DETECTED (EXCCAUSE %d)!\n", cause);
        fprintf(stderr, "ZEPHYR FATAL ERROR / EXCEPTION DETECTED (EXCCAUSE %d)!\n", cause);
        log_cpu_state(env_cpu(env), 0);
        cpu_dump_state(env_cpu(env), stderr, 0);
#ifndef CONFIG_USER_ONLY
        if (!XTENSA_CPU(env_cpu(env))->continue_on_exception) {
            qemu_system_guest_panicked(NULL);
        }
#endif
        break;
    default:
        break;
    }

    HELPER(exception)(env, vector);
}

void HELPER(exception_cause_vaddr)(CPUXtensaState *env,
                                   uint32_t pc, uint32_t cause, uint32_t vaddr)
{
    env->sregs[EXCVADDR] = vaddr;
    HELPER(exception_cause)(env, pc, cause);
}

void debug_exception_env(CPUXtensaState *env, uint32_t cause)
{
    if (xtensa_get_cintlevel(env) < env->config->debug_level) {
        HELPER(debug_exception)(env, env->pc, cause);
    }
}

void HELPER(debug_exception)(CPUXtensaState *env, uint32_t pc, uint32_t cause)
{
    unsigned level = env->config->debug_level;

    env->pc = pc;
    env->sregs[DEBUGCAUSE] = cause;
    env->sregs[EPC1 + level - 1] = pc;
    env->sregs[EPS2 + level - 2] = env->sregs[PS];
    env->sregs[PS] = (env->sregs[PS] & ~PS_INTLEVEL) | PS_EXCM |
        (level << PS_INTLEVEL_SHIFT);
    HELPER(exception)(env, EXC_DEBUG);
}

#ifndef CONFIG_USER_ONLY

void HELPER(waiti)(CPUXtensaState *env, uint32_t pc, uint32_t intlevel)
{
    CPUState *cpu = env_cpu(env);

    env->pc = pc;
    env->sregs[PS] = (env->sregs[PS] & ~PS_INTLEVEL) |
        (intlevel << PS_INTLEVEL_SHIFT);

    bql_lock();

    /* 
     * Native ADSP D3 ROM sequence simulation. 
     * If waiti is executed with all interrupts masked cleanly, the CPU cannot 
     * natively wake via conventional methods. We treat this as an explicit payload
     * indicating a simulated ROM reset cycle.
     */
    if (env->sregs[INTENABLE] == 0) {
        qemu_log("waiti: all IRQs masked, executing ROM D3 wakeup\n");
        uint32_t val = 0;
        uint32_t imr_vec = 0;

        /* Power up the first 4 HP-SRAM banks locally */
        cpu_memory_rw_debug(cpu, 0x71d00 + 0, (uint8_t *)&val, 4, 1);
        cpu_memory_rw_debug(cpu, 0x71d00 + 8, (uint8_t *)&val, 4, 1);
        cpu_memory_rw_debug(cpu, 0x71d00 + 16, (uint8_t *)&val, 4, 1);
        cpu_memory_rw_debug(cpu, 0x71d00 + 24, (uint8_t *)&val, 4, 1);

        /* Fetch the dynamic imr_restore_vector from IMR layout and jump to it */
        cpu_memory_rw_debug(cpu, 0xA1000008, (uint8_t *)&imr_vec, 4, 0);
        env->pc = imr_vec;

        bql_unlock();
        cpu_loop_exit(cpu);
        return;
    }

    check_interrupts(env);
    bql_unlock();

    if (env->pending_irq_level) {
        cpu_loop_exit(cpu);
        return;
    }

    cpu->halted = 1;
    HELPER(exception)(env, EXCP_HLT);
}

static uint32_t last_ring_by_cpu[8] = {0, 0, 0, 0, 0, 0, 0, 0};

extern void __attribute__((weak)) ace_log_prefix(void);

static void print_ring_change_if_any(CPUXtensaState *env) {
    CPUState *cpu = env_cpu(env);
    int idx = cpu->cpu_index;
    if (idx >= 0 && idx < 8) {
        uint32_t current_ring = (env->sregs[PS] & PS_EXCM) ? 0 : ((env->sregs[PS] & PS_RING) >> PS_RING_SHIFT);
        uint32_t current_um = (env->sregs[PS] & PS_EXCM) ? 0 : ((env->sregs[PS] & PS_UM) ? 1 : 0);
        uint32_t current_state = (current_ring << 1) | current_um;

        if (current_state != last_ring_by_cpu[idx]) {
            if (qemu_loglevel_mask(CPU_LOG_INT)) {
                if (ace_log_prefix) { ace_log_prefix(); }
                HELPER(update_ccount)(env);
                
                uint32_t last_ring = last_ring_by_cpu[idx] >> 1;
                uint32_t last_um = last_ring_by_cpu[idx] & 1;
                
                qemu_log("CPU%d: CONTEXT SWITCH: Ring %d (UM %d) -> Ring %d (UM %d) (PC = 0x%08x, PS = 0x%08x, CCOUNT = 0x%08x)\n",
                         idx, last_ring, last_um, current_ring, current_um, env->pc, env->sregs[PS], env->sregs[CCOUNT]);
            }
            last_ring_by_cpu[idx] = current_state;
        }
    }
}

void HELPER(check_ring_switch)(CPUXtensaState *env) {
    if (unlikely(qemu_loglevel_mask(CPU_LOG_INT))) {
        print_ring_change_if_any(env);
    }
}

void HELPER(check_wsr_eps)(CPUXtensaState *env, uint32_t eps_val) {
    if (unlikely(qemu_loglevel_mask(CPU_LOG_INT))) {
        /* Check if the new EPS value sets the User Mode (UM) bit while we are currently NOT in User Mode */
        if ((eps_val & PS_UM) && !(env->sregs[PS] & PS_UM)) {
            if (ace_log_prefix) { ace_log_prefix(); }
            HELPER(update_ccount)(env);
            qemu_log("CPU%d: wsr.ZSR_EPS trap: Setting up Userspace transition (Target PS = 0x%08x, CCOUNT = 0x%08x)\n",
                     env_cpu(env)->cpu_index, eps_val, env->sregs[CCOUNT]);
        }
    }
}

void HELPER(check_interrupts)(CPUXtensaState *env)
{
    bql_lock();
    check_interrupts(env);
    print_ring_change_if_any(env);
    bql_unlock();
}

void HELPER(intset)(CPUXtensaState *env, uint32_t v)
{
    qatomic_or(&env->sregs[INTSET],
              v & env->config->inttype_mask[INTTYPE_SOFTWARE]);
}

static void intclear(CPUXtensaState *env, uint32_t v)
{
    qatomic_and(&env->sregs[INTSET], ~v);
}

void HELPER(intclear)(CPUXtensaState *env, uint32_t v)
{
    intclear(env, v & (env->config->inttype_mask[INTTYPE_SOFTWARE] |
                       env->config->inttype_mask[INTTYPE_EDGE]));
}

static uint32_t relocated_vector(CPUXtensaState *env, uint32_t vector)
{
    if (xtensa_option_enabled(env->config,
                              XTENSA_OPTION_RELOCATABLE_VECTOR)) {
        /* VECBASE lowest 4 bits are ignored when calculating exception vectors.
         * This ensures proper alignment of the vector base address.
         */
        return vector - env->config->vecbase + (env->sregs[VECBASE] & ~0xF);
    } else {
        return vector;
    }
}

/*!
 * Handle penging IRQ.
 * For the high priority interrupt jump to the corresponding interrupt vector.
 * For the level-1 interrupt convert it to either user, kernel or double
 * exception with the 'level-1 interrupt' exception cause.
 */
static void handle_interrupt(CPUXtensaState *env)
{
    int level = env->pending_irq_level;

    if ((level > xtensa_get_cintlevel(env) &&
         level <= env->config->nlevel &&
         (env->config->level_mask[level] &
          env->sregs[INTSET] & env->sregs[INTENABLE])) ||
        level == env->config->nmi_level) {
        CPUState *cs = env_cpu(env);

        if (level > 1) {
            /* env->config->nlevel check should have ensured this */
            assert(level < ARRAY_SIZE(env->config->interrupt_vector));

            env->sregs[EPC1 + level - 1] = env->pc;
            env->sregs[EPS2 + level - 2] = env->sregs[PS];
            env->sregs[PS] =
                (env->sregs[PS] & ~PS_INTLEVEL) | level | PS_EXCM;
            env->pc = relocated_vector(env,
                                       env->config->interrupt_vector[level]);
            if (level == env->config->nmi_level) {
                intclear(env, env->config->inttype_mask[INTTYPE_NMI]);
            }
        } else {
            env->sregs[EXCCAUSE] = LEVEL1_INTERRUPT_CAUSE;

            if (env->sregs[PS] & PS_EXCM) {
                if (env->config->ndepc) {
                    env->sregs[DEPC] = env->pc;
                } else {
                    env->sregs[EPC1] = env->pc;
                }
                cs->exception_index = EXC_DOUBLE;
            } else {
                env->sregs[EPC1] = env->pc;
                cs->exception_index =
                    (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
            }
            env->sregs[PS] |= PS_EXCM;
        }
    }
}

/* Called from cpu_handle_interrupt with BQL held */
void xtensa_cpu_do_interrupt(CPUState *cs)
{
    CPUXtensaState *env = cpu_env(cs);

    if (cs->exception_index == EXC_IRQ) {
        uint64_t last_pc = env->pc;

        if (qemu_loglevel_mask(CPU_LOG_INT)) {
            if (ace_log_prefix) { ace_log_prefix(); }
            qemu_log("%s(EXC_IRQ) level = %d, cintlevel = %d, "
                     "pc = %08x, a0 = %08x, ps = %08x, "
                     "intset = %08x, intenable = %08x, "
                     "ccount = %08x\n",
                     __func__, env->pending_irq_level,
                     xtensa_get_cintlevel(env),
                     env->pc, env->regs[0], env->sregs[PS],
                     env->sregs[INTSET], env->sregs[INTENABLE],
                     env->sregs[CCOUNT]);
        }
        handle_interrupt(env);
        qemu_plugin_vcpu_interrupt_cb(cs, last_pc);
    }

    switch (cs->exception_index) {
    case EXC_WINDOW_OVERFLOW4:
    case EXC_WINDOW_UNDERFLOW4:
    case EXC_WINDOW_OVERFLOW8:
    case EXC_WINDOW_UNDERFLOW8:
    case EXC_WINDOW_OVERFLOW12:
    case EXC_WINDOW_UNDERFLOW12:
    case EXC_KERNEL:
    case EXC_USER:
    case EXC_DOUBLE:
    case EXC_DEBUG:
        if (qemu_loglevel_mask(CPU_LOG_INT)) {
            if (ace_log_prefix) { ace_log_prefix(); }
            qemu_log("%s(%d) "
                      "pc = %08x, a0 = %08x, ps = %08x, ccount = %08x, "
                      "EXCCAUSE = %d, EXCVADDR = %08x\n",
                      __func__, cs->exception_index,
                      env->pc, env->regs[0], env->sregs[PS],
                      env->sregs[CCOUNT], env->sregs[EXCCAUSE],
                      env->sregs[EXCVADDR]);
        }
        if (env->config->exception_vector[cs->exception_index]) {
            uint32_t vector;
            uint64_t last_pc = env->pc;

            vector = env->config->exception_vector[cs->exception_index];
            env->pc = relocated_vector(env, vector);
            qemu_plugin_vcpu_exception_cb(cs, last_pc);
        } else {
            if (qemu_loglevel_mask(CPU_LOG_INT)) {
                if (ace_log_prefix) { ace_log_prefix(); }
                qemu_log("%s(pc = %08x) bad exception_index: %d\n",
                         __func__, env->pc, cs->exception_index);
            }
        }
        break;

    case EXC_IRQ:
        break;

    default:
        if (qemu_loglevel_mask(CPU_LOG_INT)) {
            if (ace_log_prefix) { ace_log_prefix(); }
            qemu_log("%s(pc = %08x) unknown exception_index: %d\n",
                     __func__, env->pc, cs->exception_index);
        }
        break;
    }
    check_interrupts(env);
    print_ring_change_if_any(env);
    
    /* Emit a synthetic 'FUNC ENTRY' event if CPU_LOG_FUNC is active to balance 
     * the 'FUNC RET' emitted by RFI/RFE. This captures the hardware-imposed 
     * leap to the exception vector address.
     * We explicitly ignore Window Overflow and Underflow exceptions, as they 
     * rotate the registers dynamically causing SP to temporarily evaluate to 0, 
     * and they return via RFWO/RFWU which do not emit FUNC RET.
     */
    if (unlikely(qemu_loglevel_mask(CPU_LOG_FUNC))) {
        if (cs->exception_index != EXC_WINDOW_OVERFLOW4 &&
            cs->exception_index != EXC_WINDOW_UNDERFLOW4 &&
            cs->exception_index != EXC_WINDOW_OVERFLOW8 &&
            cs->exception_index != EXC_WINDOW_UNDERFLOW8 &&
            cs->exception_index != EXC_WINDOW_OVERFLOW12 &&
            cs->exception_index != EXC_WINDOW_UNDERFLOW12) {
            helper_log_entry(env, env->pc);
        }
    }
}

bool xtensa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        cs->exception_index = EXC_IRQ;
        xtensa_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

#endif /* !CONFIG_USER_ONLY */
