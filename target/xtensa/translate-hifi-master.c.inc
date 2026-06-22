/*
 * Xtensa HiFi Audio Engine (AE) instruction translation - Master Include
 *
 * Copyright (c) 2024, QEMU contributors.
 *
 * Implements simulation support for HiFi3/HiFi4 AE opcodes that operate on
 * the 64-bit AE data register file (AE_DR) and related state.
 *
 * This file is meant to be #included from translate.c (after DisasContext
 * is defined) and orchestrates includes of 6 modular instruction class files:
 *
 *   1. translate-hifi-load-store.c.inc  - Memory access operations
 *   2. translate-hifi-arithmetic.c.inc  - Basic arithmetic & helpers
 *   3. translate-hifi-saturation.c.inc  - Saturation helpers & narrow ops
 *   4. translate-hifi-multiply.c.inc    - Multiply & multiply-accumulate
 *   5. translate-hifi-advanced.c.inc    - Vector/carrier operations
 *   6. translate-hifi-opcodes.c.inc     - Opcode dispatch table
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

/*
 * Argument conventions for AE load/store instructions:
 *
 * Immediate-offset loads (_I / _IP variants):
 *   arg[0]  AE_DR  destination register (64-bit, out)
 *   arg[1]  AR     base address register (32-bit, in)
 *   arg[2]  imm    byte offset (for _IP, used as post-increment amount)
 *
 * Register-offset loads (_X / _XP variants):
 *   arg[0]  AE_DR  destination register (64-bit, out)
 *   arg[1]  AR     base address register (32-bit, in/out for _XP)
 *   arg[2]  AR     index/offset register (32-bit, in)
 *
 * Circular loads (_XC / _XC1 variants):
 *   arg[0]  AE_DR  destination register (64-bit, out)
 *   arg[1]  AR     base address register (32-bit, in)
 *   arg[2]  AR     index register (32-bit, in)
 *
 * Store variants mirror loads, with arg[0] as AE_DR source (in).
 *
 * Memory width:
 *   ae_l16*      - 16-bit memory access
 *   ae_l32*      - 32-bit memory access
 *   ae_l64*      - 64-bit memory access
 *   ae_l32x2*    - 64-bit (two 32-bit words)
 *   ae_l16x4*    - 64-bit (four 16-bit words)
 *   ae_s16.0*    - 16-bit (lower 16 bits of AE_DR)
 *   ae_s32.l*    - 32-bit (lower 32 bits of AE_DR)
 *   ae_s64*      - 64-bit
 *   ae_s32x2*    - 64-bit (two 32-bit words)
 *   ae_s16x4*    - 64-bit (four 16-bit words)
 */

/* ============================================================================
 * Section 1: Load/Store Memory Access Instructions
 * Implements AE_L (load) and AE_S (store) variants for all widths and modes.
 * ============================================================================ */
#include "translate-hifi-load-store.c.inc"

/* ============================================================================
 * Section 2: Basic Arithmetic, Logical, and Helper Functions
 * Generic arithmetic, comparison, min/max, and common helper utilities.
 * ============================================================================ */
#include "translate-hifi-arithmetic.c.inc"

/* ============================================================================
 * Section 3: Saturation and Narrow-Width Arithmetic
 * Saturation helpers, per-width operations (16/24/32/64-bit), clamping.
 * Depends on: Section 2 (arithmetic helpers)
 * ============================================================================ */
#include "translate-hifi-saturation.c.inc"

/* ============================================================================
 * Section 4: Multiply and Multiply-Accumulate Operations
 * Multiply families (mul32, mula32, muls32, mulf32, mulfp32, etc.)
 * and multi-lane multiply-accumulate (mulaaaaq16, mulaaaaq32x16, etc.).
 * Depends on: Sections 2,3 (arithmetic, saturation)
 * ============================================================================ */
#include "translate-hifi-multiply.c.inc"

/* ============================================================================
 * Section 5: Advanced Operations (Vector, Carrier, Bitfield)
 * Carrier field manipulations, vector load shifts, field decode helpers.
 * Depends on: Sections 2,3,4 (all previous)
 * ============================================================================ */
#include "translate-hifi-advanced.c.inc"

/* ============================================================================
 * Section 6: Opcode and Dispatch Table
 * Maps instruction names to translator functions for all ~200+ HiFi opcodes.
 * References all translators from sections 1-5.
 * ============================================================================ */
#include "translate-hifi-opcodes.c.inc"
