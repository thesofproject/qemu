/*
 * Xtensa HiFi Audio Engine (AE) load/store instruction translation.
 *
 * Copyright (c) 2024, QEMU contributors.
 *
 * Implements simulation support for HiFi3/HiFi4 AE_L and AE_S opcodes
 * that operate on the 64-bit AE data register file (AE_DR) and the
 * 64-bit alignment register file (AE_VALIGN).
 *
 * This file is meant to be #included from translate.c (after DisasContext
 * is defined) so that it can access the same types and helpers.
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

/*
 * ----- 64-bit loads (AE_L32X2, AE_L64, AE_L16X4) -----
 */

/*
 * ae_l32x2.i  aed, as, imm
 * ae_l64.i    aed, as, imm
 * ae_l16x4.i  aed, as, imm
 * Load 64-bit from [AR_base + imm*8] into AE_DR (big-endian word order).
 */
static void translate_ae_l64_i(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
}

/*
 * ae_l32x2.ip  aed, as, imm
 * ae_l64.ip    aed, as, imm
 * ae_l16x4.ip  aed, as, imm
 * Load 64-bit from [AR_base + imm] into AE_DR, then AR_base += imm.
 */
static void translate_ae_l64_ip(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_mov_i32(addr, arg[1].in);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
    tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
}

/*
 * ae_l32x2.x  aed, as, ax
 * ae_l64.x    aed, as, ax
 * ae_l16x4.x  aed, as, ax
 * Load 64-bit from [AR_base + AR_index].
 */
static void translate_ae_l64_x(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
}

/*
 * ae_l32x2.xp  aed, as, ax
 * ae_l64.xp    aed, as, ax
 * ae_l16x4.xp  aed, as, ax
 * Load 64-bit from [AR_base + AR_index], then AR_base += AR_index.
 */
static void translate_ae_l64_xp(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

/*
 * ae_l32x2.xc  aed, as, ax   (circular: addr = as + ax, as = circ(as, ax))
 * ae_l64.xc    aed, as, ax
 * Circular addressing is complex; for simulation purposes we approximate
 * by treating it identically to _x (no circular wrap simulation).
 */
static void translate_ae_l64_xc(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
    /* No circular wrap: treat as plain _x for simulation */
}

/*
 * ae_l32x2.ric  aed, as, ax   (reverse immediate circular)
 * ae_l32x2.ric1 aed, as, ax
 * Load from [as], then as -= 8 (for ae_l32x2) with circular constraint.
 * Approximated as load from [as], as -= step.
 */
static void translate_ae_l64_ric(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    /* par[0] = step size (e.g. 8 for 64-bit) */
    tcg_gen_qemu_ld_i64(arg[0].out, arg[1].in, dc->cring, MO_TEUQ);
    tcg_gen_subi_i32(arg[1].out, arg[1].in, par[0]);
}

/*
 * ----- 32-bit loads (AE_L32, AE_L32F24) -----
 * Loads a 32-bit value sign-extended to 64 bits (or zero-extended).
 */

/*
 * ae_l32.i   aed, as, imm
 * Load 32-bit from [as + imm], sign-extend to 64-bit AE_DR.
 */
static void translate_ae_l32_i(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESL);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
}

/*
 * ae_l32.ip  aed, as, imm
 * Load 32-bit from [as + imm], sign-extend to 64 bits, as += imm.
 */
static void translate_ae_l32_ip(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_mov_i32(addr, arg[1].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESL);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
    tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
}

/*
 * ae_l32.x   aed, as, ax
 * Load 32-bit from [as + ax], sign-extend to 64 bits.
 */
static void translate_ae_l32_x(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESL);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
}

/*
 * ae_l32.xp  aed, as, ax
 * Load 32-bit from [as + ax], sign-extend, as += ax.
 */
static void translate_ae_l32_xp(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESL);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

/*
 * ae_l32.xc  aed, as, ax
 * Load 32-bit from [as + ax], sign-extend (circular approx).
 */
static void translate_ae_l32_xc(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESL);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
}

/*
 * ----- 16-bit loads (AE_L16) -----
 * AE_L16 loads sign-extend to 64 bits, placed in lower 16 bits.
 */

/*
 * ae_l16.i   aed, as, imm
 * Load 16-bit from [as + imm], sign-extend to 64 bits.
 */
static void translate_ae_l16_i(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESW);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
}

/*
 * ae_l16.ip  aed, as, imm
 * Load 16-bit from [as], sign-extend, as += imm.
 */
static void translate_ae_l16_ip(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_mov_i32(addr, arg[1].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESW);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
    tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
}

/*
 * ae_l16.x   aed, as, ax
 * Load 16-bit from [as + ax], sign-extend to 64 bits.
 */
static void translate_ae_l16_x(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESW);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
}

/*
 * ae_l16.xp  aed, as, ax
 * Load 16-bit from [as + ax], sign-extend, as += ax.
 */
static void translate_ae_l16_xp(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESW);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

/*
 * ae_l16.xc  aed, as, ax
 * Load 16-bit from [as + ax], sign-extend (circular approx).
 */
static void translate_ae_l16_xc(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESW);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
}

/*
 * ----- Narrow (N) 16-bit loads (ae_l16si.n / ae_l16ui.n) -----
 * These are 2-byte Xtensa density format instructions.
 * arg[0] = AE_DR dest, arg[1] = AR base, arg[2] = imm offset
 */
static void translate_ae_l16si_n(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TESW);
    tcg_gen_ext_i32_i64(arg[0].out, tmp32);
}

static void translate_ae_l16ui_n(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TEUW);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
}

/*
 * ----- 64-bit stores (AE_S32X2, AE_S64, AE_S16X4) -----
 */

/*
 * ae_s32x2.i   aed, as, imm
 * ae_s64.i     aed, as, imm
 * ae_s16x4.i   aed, as, imm
 * Store 64-bit AE_DR to [as + imm].
 */
static void translate_ae_s64_i(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
}

/*
 * ae_s32x2.ip  aed, as, imm
 * ae_s64.ip    aed, as, imm
 * ae_s16x4.ip  aed, as, imm
 * Store 64-bit AE_DR to [as], then as += imm.
 */
static void translate_ae_s64_ip(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    tcg_gen_qemu_st_i64(arg[0].in, arg[1].in, dc->cring, MO_TEUQ);
    tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
}

/*
 * ae_s32x2.x   aed, as, ax
 * ae_s64.x     aed, as, ax
 * ae_s16x4.x   aed, as, ax
 * Store 64-bit AE_DR to [as + ax].
 */
static void translate_ae_s64_x(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
}

/*
 * ae_s32x2.xp  aed, as, ax
 * ae_s64.xp    aed, as, ax
 * ae_s16x4.xp  aed, as, ax
 * Store 64-bit to [as + ax], then as += ax.
 */
static void translate_ae_s64_xp(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

/*
 * ae_s32x2.xc  aed, as, ax (circular approx)
 * ae_s64.xc    aed, as, ax
 */
static void translate_ae_s64_xc(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
}

/*
 * ae_s32x2.ric  aed, as, ax
 * Store 64-bit at [as], then as -= step (par[0]).
 */
static void translate_ae_s64_ric(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    tcg_gen_qemu_st_i64(arg[0].in, arg[1].in, dc->cring, MO_TEUQ);
    tcg_gen_subi_i32(arg[1].out, arg[1].in, par[0]);
}

/*
 * ----- 32-bit stores (AE_S32.L, AE_S32F24.L) -----
 * Stores the lower 32 bits of AE_DR.
 */

/*
 * ae_s32.l.i   aed, as, imm
 * ae_s32f24.l.i
 * Store lower 32 bits of AE_DR to [as + imm].
 */
static void translate_ae_s32_l_i(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
}

/*
 * ae_s32.l.ip  aed, as, imm
 * Store lower 32 bits of AE_DR to [as], then as += imm.
 */
static void translate_ae_s32_l_ip(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, arg[1].in, dc->cring, MO_TEUL);
    tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
}

/*
 * ae_s32.l.x   aed, as, ax
 * Store lower 32 bits of AE_DR to [as + ax].
 */
static void translate_ae_s32_l_x(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
}

/*
 * ae_s32.l.xp  aed, as, ax
 * Store lower 32 bits of AE_DR to [as + ax], then as += ax.
 */
static void translate_ae_s32_l_xp(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

/*
 * ae_s32.l.xc  aed, as, ax  (circular approx)
 */
static void translate_ae_s32_l_xc(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
}

/*
 * ----- 16-bit stores (AE_S16.0) -----
 * Stores the lower 16 bits of AE_DR.
 */

/*
 * ae_s16.0.i   aed, as, imm
 * Store lower 16 bits of AE_DR to [as + imm].
 */
static void translate_ae_s16_0_i(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUW);
}

/*
 * ae_s16.0.ip  aed, as, imm
 * Store lower 16 bits to [as], then as += imm.
 */
static void translate_ae_s16_0_ip(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, arg[1].in, dc->cring, MO_TEUW);
    tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
}

/*
 * ae_s16.0.x   aed, as, ax
 * Store lower 16 bits to [as + ax].
 */
static void translate_ae_s16_0_x(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUW);
}

/*
 * ae_s16.0.xp  aed, as, ax
 * Store lower 16 bits to [as + ax], then as += ax.
 */
static void translate_ae_s16_0_xp(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUW);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

/*
 * ae_s16.0.xc  aed, as, ax  (circular approx)
 */
static void translate_ae_s16_0_xc(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUW);
}

/*
 * ae_s16i.n  aed, as, imm  (narrow format)
 * Store lower 16 bits to [as + imm].
 */
static void translate_ae_s16i_n(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUW);
}

/*
 * ----- AE_L32X2F24 variants (load 32-bit, store upper half in MSB) -----
 * These are essentially 32-bit loads but may shift the value.
 * For simulation we treat them the same as ae_l32 (zero-extend).
 */
static void translate_ae_l32x2f24_i(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TEUL);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
}

static void translate_ae_l32x2f24_ip(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_mov_i32(addr, arg[1].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TEUL);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
    tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
}

static void translate_ae_l32x2f24_x(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TEUL);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
}

static void translate_ae_l32x2f24_xp(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TEUL);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

static void translate_ae_l32x2f24_xc(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_qemu_ld_i32(tmp32, addr, dc->cring, MO_TEUL);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
}

/*
 * ae_l32x2f24.ri  aed, as, imm  (reverse immediate)
 * Load from [as], as -= imm.
 */
static void translate_ae_l32x2f24_ri(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp32, arg[1].in, dc->cring, MO_TEUL);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
    tcg_gen_subi_i32(arg[1].out, arg[1].in, arg[2].imm);
}

/*
 * ae_l32x2f24.rip  aed0, aed1, as, imm
 * Two consecutive 32-bit loads (pipeline variant) - treated as two loads
 * at [as] and [as + 4], with as -= imm.
 * arg[0] = aed0 (first load dest, 64-bit)
 * arg[1] = aed1 (second load dest, 64-bit)  OR  arg[1] = as (AR)
 * For simplicity, implement as single load to arg[0], as += imm.
 */
static void translate_ae_l32x2f24_rip(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp32, arg[1].in, dc->cring, MO_TEUL);
    tcg_gen_extu_i32_i64(arg[0].out, tmp32);
    tcg_gen_subi_i32(arg[1].out, arg[1].in, arg[2].imm);
}

/*
 * ----- AE_S32X2F24 store variants -----
 */
static void translate_ae_s32x2f24_i(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
}

static void translate_ae_s32x2f24_ip(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, arg[1].in, dc->cring, MO_TEUL);
    tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
}

static void translate_ae_s32x2f24_x(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
}

static void translate_ae_s32x2f24_xp(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

static void translate_ae_s32x2f24_xc(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, addr, dc->cring, MO_TEUL);
}

static void translate_ae_s32x2f24_ric(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, arg[1].in, dc->cring, MO_TEUL);
    tcg_gen_subi_i32(arg[1].out, arg[1].in, par[0]);
}

static void translate_ae_s32x2f24_rip(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    TCGv_i32 val32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(val32, arg[0].in);
    tcg_gen_qemu_st_i32(val32, arg[1].in, dc->cring, MO_TEUL);
    tcg_gen_subi_i32(arg[1].out, arg[1].in, par[0]);
}

/* ric1 variant (same as ric for simulation purposes) */
static void translate_ae_s32x2f24_ric1(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    translate_ae_s32x2f24_ric(dc, arg, par);
}

/* xc1 circular (approx same as xc) */
static void translate_ae_l64_xc1(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    translate_ae_l64_xc(dc, arg, par);
}

static void translate_ae_l32_xc1(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    translate_ae_l32_xc(dc, arg, par);
}

static void translate_ae_l16_xc1(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    translate_ae_l16_xc(dc, arg, par);
}

static void translate_ae_l32x2f24_xc1(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    translate_ae_l32x2f24_xc(dc, arg, par);
}

static void translate_ae_l32x2_ric1(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    translate_ae_l64_ric(dc, arg, par);
}

static void translate_ae_s64_xc1(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    translate_ae_s64_xc(dc, arg, par);
}

static void translate_ae_s32_l_xc1(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    translate_ae_s32_l_xc(dc, arg, par);
}

static void translate_ae_s16_0_xc1(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    translate_ae_s16_0_xc(dc, arg, par);
}

static void translate_ae_s32x2f24_xc1(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    translate_ae_s32x2f24_xc(dc, arg, par);
}

/*
 * ae_l32x2.ric  / ae_l32x2.ric1 - same as ae_l64_ric
 */
static void translate_ae_l32x2_ric(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    translate_ae_l64_ric(dc, arg, par);
}

/*
 * ae_s32x2.ric / ae_s32x2.ric1
 */
static void translate_ae_s32x2_ric(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    translate_ae_s64_ric(dc, arg, par);
}

static void translate_ae_s32x2_ric1(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    translate_ae_s64_ric(dc, arg, par);
}

/* ae_s32x2.xc1 */
static void translate_ae_s32x2_xc1(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    translate_ae_s64_xc(dc, arg, par);
}

/*
 * ----- Streaming alignment loads and stores -----
 *
 * ae_la*.{ic,ic1,ip,ric,ric1,rip}  --  load 64-bit from memory into AE_DR,
 *     update AE_VALIGN, advance AR.
 * ae_sa*.{ic,ic1,ip,ric,ric1,rip}  --  store 64-bit from AE_DR to memory,
 *     update AE_VALIGN, advance AR.
 * ae_la*.{pc,pc1}                  --  prime AE_VALIGN from aligned address,
 *     advance AR circularly (circular bounds ignored in simulation).
 *
 * All streaming alignment loads:
 *   arg[0]  AE_DR    output  (64-bit, out)
 *   arg[1]  AE_VALIGN modify (64-bit, in+out) -- alignment buffer
 *   arg[2]  AR       modify  (32-bit, in+out) -- address pointer
 *   par[0]  step     byte increment; cast to int32_t for direction
 *
 * All streaming alignment stores:
 *   arg[0]  AE_DR    input   (64-bit, in)
 *   arg[1]  AE_VALIGN modify (64-bit, in+out) -- alignment buffer
 *   arg[2]  AR       modify  (32-bit, in+out) -- address pointer
 *   par[0]  step     byte increment
 *
 * Alignment is approximated: simple aligned 64-bit load/store at [AR].
 * AE_VALIGN is refreshed with the data just loaded/stored.
 * Circular addressing bounds (AE_CBEGIN/AE_CEND) are not enforced.
 */

/*
 * translate_ae_la64_align_load  --  streaming alignment load (all variants)
 */
static void translate_ae_la64_align_load(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    if (!arg[0].out || !arg[2].in || !arg[2].out) {
        return;
    }
    tcg_gen_qemu_ld_i64(arg[0].out, arg[2].in, dc->cring, MO_TEUQ);
    if (arg[1].out) {
        tcg_gen_mov_i64(arg[1].out, arg[0].out);
    }
    tcg_gen_addi_i32(arg[2].out, arg[2].in, (int32_t)par[0]);
}

/*
 * translate_ae_sa64_align_store  --  streaming alignment store (all variants)
 */
static void translate_ae_sa64_align_store(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
    if (!arg[0].in || !arg[2].in || !arg[2].out) {
        return;
    }
    tcg_gen_qemu_st_i64(arg[0].in, arg[2].in, dc->cring, MO_TEUQ);
    if (arg[1].out) {
        tcg_gen_mov_i64(arg[1].out, arg[0].in);
    }
    tcg_gen_addi_i32(arg[2].out, arg[2].in, (int32_t)par[0]);
}

/*
 * translate_ae_la_align_prime_pc  --  prime AE_VALIGN, advance AR (circular)
 *
 * Used by: ae_la{24,24x2,32x2,16x4}{pos,neg}.pc and .pc1 variants.
 *
 *   arg[0]  AE_VALIGN  out    -- primed alignment register
 *   arg[1]  AR         modify -- address pointer (circular, bounds ignored)
 *   par[0]  step       8 (pos) or (uint32_t)-8 (neg)
 */
static void translate_ae_la_align_prime_pc(DisasContext *dc, const OpcodeArg arg[],
                                            const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in || !arg[1].out) {
        return;
    }
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_andi_i32(addr, arg[1].in, ~7U);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
    tcg_gen_addi_i32(arg[1].out, arg[1].in, (int32_t)par[0]);
}

/*
 * translate_ae_la64_pp  --  prime alignment buffer, no AR advance
 *
 * ae_la64.pp(aeu_out, as):
 *   AEU = mem[AS & ~7]   (loads the 8-byte aligned block containing AS)
 *
 *   arg[0]  AE_VALIGN  out -- primed alignment register
 *   arg[1]  AR         in  -- source address (NOT modified)
 */
static void translate_ae_la64_pp(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in) {
        return;
    }
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_andi_i32(addr, arg[1].in, ~7U);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
}

/*
 * translate_ae_lalign64_i  --  load aligned 64-bit into AEU, immediate offset
 *
 * ae_lalign64.i(aeu_out, as, imm):
 *   AEU = mem[AS + imm]   (imm already byte-scaled by decoder; multiple of 8)
 *
 *   arg[0]  AE_VALIGN  out -- destination alignment register
 *   arg[1]  AR         in  -- base address
 *   arg[2]  imm            -- byte offset (decoded; multiple of 8)
 */
static void translate_ae_lalign64_i(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in) {
        return;
    }
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
}

/*
 * translate_ae_salign64_i  --  store AEU to memory, immediate offset
 *
 * ae_salign64.i(aeu_in, as, imm):
 *   mem[AS + imm] = AEU
 *
 *   arg[0]  AE_VALIGN  in  -- source alignment register
 *   arg[1]  AR         in  -- base address
 *   arg[2]  imm            -- byte offset (decoded; multiple of 8)
 */
static void translate_ae_salign64_i(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (!arg[0].in || !arg[1].in) {
        return;
    }
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
}

/*
 * translate_ae_sa64_fp  --  flush alignment store buffer to memory
 *
 * ae_sa64pos.fp(aeu_inout, as) / ae_sa64neg.fp(aeu_inout, as):
 *   Writes any pending partial tail block to the aligned store address.
 *   For simulation (aligned access only): all data was already written by
 *   preceding ae_sa*.ip operations; just clear AEU to mark buffer empty.
 *
 *   arg[0]  AE_VALIGN  modify -- alignment buffer (cleared after flush)
 *   arg[1]  AR         in     -- flush address (not modified by this insn)
 */
static void translate_ae_sa64_fp(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out) {
        tcg_gen_movi_i64(arg[0].out, 0);
    }
}

/*
 * translate_ae_movalign  --  copy AE_VALIGN to AE_VALIGN
 *
 * ae_movalign(aeu_out, aeu_in):
 *   AEU_out = AEU_in
 *
 *   arg[0]  AE_VALIGN  out -- destination
 *   arg[1]  AE_VALIGN  in  -- source
 */
static void translate_ae_movalign(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in) {
        return;
    }
    tcg_gen_mov_i64(arg[0].out, arg[1].in);
}

/*
 * translate_ae_movvfcrfsr  --  FCR/FSR to AE_DR
 *
 * ae_movvfcrfsr(aed_out):
 *   Pack FP Control Register and FP Status Register into AE_DR:
 *     bits 31:0  = FCR  (rounding mode + exception enable flags)
 *     bits 63:32 = FSR  (exception status flags, synced from softfloat)
 *
 *   arg[0]  AE_DR  out  -- destination register
 */
static void translate_ae_movvfcrfsr(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    TCGv_i64 fsr64;
    TCGv_i32 fsr;

    if (!arg[0].out) {
        return;
    }
    /* bits 31:0 = FCR (rounding mode + enable flags) */
    tcg_gen_extu_i32_i64(arg[0].out, cpu_UR[FCR]);
    /* bits 63:32 = FSR (sync exception flags from softfloat first) */
    fsr = tcg_temp_new_i32();
    gen_helper_rur_fpu_fsr(fsr, tcg_env);
    fsr64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(fsr64, fsr);
    tcg_gen_shli_i64(fsr64, fsr64, 32);
    tcg_gen_or_i64(arg[0].out, arg[0].out, fsr64);
}

/*
 * translate_ae_movfcrfsrv  --  AE_DR to FCR/FSR
 *
 * ae_movfcrfsrv(aed_in):
 *   Unpack a 64-bit AE_DR and write both FP registers:
 *     FCR  = bits 31:0  of AE_DR  (also updates softfloat rounding mode)
 *     FSR  = bits 63:32 of AE_DR  (also updates softfloat exception flags)
 *
 *   arg[0]  AE_DR  in  -- source register
 */
static void translate_ae_movfcrfsrv(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    TCGv_i32 fcr, fsr;

    if (!arg[0].in) {
        return;
    }
    fcr = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(fcr, arg[0].in);   /* lower 32 = FCR */
    gen_helper_wur_fpu_fcr(tcg_env, fcr);

    fsr = tcg_temp_new_i32();
    tcg_gen_extrh_i64_i32(fsr, arg[0].in);   /* upper 32 = FSR */
    gen_helper_wur_fpu_fsr(tcg_env, fsr);
}

  enum {
    AE_USR_OVERFLOW,
    AE_USR_SAR,
    AE_USR_CBEGIN0,
    AE_USR_CEND0,
    AE_USR_CBEGIN1,
    AE_USR_CEND1,
    AE_USR_CWRAP,
    AE_USR_BITHEAD,
    AE_USR_BITPTR,
    AE_USR_BITSUSED,
    AE_USR_TABLESIZE,
    AE_USR_FIRST_TS,
    AE_USR_NEXTOFFSET,
    AE_USR_SEARCHDONE,
  };

  static inline TCGv_i32 ae_usr_state_reg(unsigned id)
  {
    switch (id) {
    case AE_USR_OVERFLOW:
      return cpu_AE_OVERFLOW;
    case AE_USR_SAR:
      return cpu_AE_SAR;
    case AE_USR_CBEGIN0:
      return cpu_AE_CBEGIN0;
    case AE_USR_CEND0:
      return cpu_AE_CEND0;
    case AE_USR_CBEGIN1:
      return cpu_AE_CBEGIN1;
    case AE_USR_CEND1:
      return cpu_AE_CEND1;
    case AE_USR_CWRAP:
      return cpu_AE_CWRAP;
    case AE_USR_BITHEAD:
      return cpu_AE_BITHEAD;
    case AE_USR_BITPTR:
      return cpu_AE_BITPTR;
    case AE_USR_BITSUSED:
      return cpu_AE_BITSUSED;
    case AE_USR_TABLESIZE:
      return cpu_AE_TABLESIZE;
    case AE_USR_FIRST_TS:
      return cpu_AE_FIRST_TS;
    case AE_USR_NEXTOFFSET:
      return cpu_AE_NEXTOFFSET;
    case AE_USR_SEARCHDONE:
      return cpu_AE_SEARCHDONE;
    default:
      g_assert_not_reached();
    }
  }

  static inline uint32_t ae_usr_state_mask(unsigned id)
  {
    switch (id) {
    case AE_USR_SAR:
      return 0x3f;
    case AE_USR_SEARCHDONE:
      return 0x1;
    default:
      return 0xffffffffu;
    }
  }

  static void translate_ae_rur_state(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    if (!arg[0].out) {
      return;
    }
    tcg_gen_mov_i32(arg[0].out, ae_usr_state_reg(par[0]));
  }

  static void translate_ae_wur_state(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    uint32_t mask;

    if (!arg[0].in) {
      return;
    }
    mask = ae_usr_state_mask(par[0]);
    if (mask == 0xffffffffu) {
      tcg_gen_mov_i32(ae_usr_state_reg(par[0]), arg[0].in);
    } else {
      tcg_gen_andi_i32(ae_usr_state_reg(par[0]), arg[0].in, mask);
    }
  }

  static void translate_ae_xsr_state(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    TCGv_i32 tmp;
    TCGv_i32 reg;
    uint32_t mask;

    if (!arg[0].in || !arg[0].out) {
      return;
    }
    reg = ae_usr_state_reg(par[0]);
    mask = ae_usr_state_mask(par[0]);
    tmp = tcg_temp_new_i32();
    tcg_gen_mov_i32(tmp, arg[0].in);
    tcg_gen_mov_i32(arg[0].out, reg);
    if (mask == 0xffffffffu) {
      tcg_gen_mov_i32(reg, tmp);
    } else {
      tcg_gen_andi_i32(reg, tmp, mask);
    }
  }

  /* rur/wur/xsr.ae_ovf_sar : packed {overflow[15:8], sar[5:0]} */
  static void translate_ae_rur_ovf_sar(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    TCGv_i32 ovf = tcg_temp_new_i32();

    if (!arg[0].out) {
      return;
    }
    tcg_gen_andi_i32(arg[0].out, cpu_AE_SAR, 0x3f);
    tcg_gen_andi_i32(ovf, cpu_AE_OVERFLOW, 0xff);
    tcg_gen_shli_i32(ovf, ovf, 8);
    tcg_gen_or_i32(arg[0].out, arg[0].out, ovf);
  }

  static void translate_ae_wur_ovf_sar(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    if (!arg[0].in) {
      return;
    }
    tcg_gen_andi_i32(cpu_AE_SAR, arg[0].in, 0x3f);
    tcg_gen_shri_i32(cpu_AE_OVERFLOW, arg[0].in, 8);
    tcg_gen_andi_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, 0xff);
  }

  static void translate_ae_xsr_ovf_sar(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    TCGv_i32 old = tcg_temp_new_i32();

    if (!arg[0].in || !arg[0].out) {
      return;
    }
    tcg_gen_andi_i32(old, cpu_AE_SAR, 0x3f);
    tcg_gen_deposit_i32(old, old, cpu_AE_OVERFLOW, 8, 8);
    tcg_gen_mov_i32(arg[0].out, old);
    tcg_gen_andi_i32(cpu_AE_SAR, arg[0].in, 0x3f);
    tcg_gen_shri_i32(cpu_AE_OVERFLOW, arg[0].in, 8);
    tcg_gen_andi_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, 0xff);
  }

  /* rur/wur/xsr.ae_ts_fts_bu_bp : packed bytes {bp, bu, ts, fts} */
  static void translate_ae_rur_ts_fts_bu_bp(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
  {
    TCGv_i32 t = tcg_temp_new_i32();

    if (!arg[0].out) {
      return;
    }
    tcg_gen_andi_i32(arg[0].out, cpu_AE_BITPTR, 0xff);
    tcg_gen_andi_i32(t, cpu_AE_BITSUSED, 0xff);
    tcg_gen_shli_i32(t, t, 8);
    tcg_gen_or_i32(arg[0].out, arg[0].out, t);
    tcg_gen_andi_i32(t, cpu_AE_TABLESIZE, 0xff);
    tcg_gen_shli_i32(t, t, 16);
    tcg_gen_or_i32(arg[0].out, arg[0].out, t);
    tcg_gen_andi_i32(t, cpu_AE_FIRST_TS, 0xff);
    tcg_gen_shli_i32(t, t, 24);
    tcg_gen_or_i32(arg[0].out, arg[0].out, t);
  }

  static void translate_ae_wur_ts_fts_bu_bp(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
  {
    if (!arg[0].in) {
      return;
    }
    tcg_gen_andi_i32(cpu_AE_BITPTR, arg[0].in, 0xff);
    tcg_gen_shri_i32(cpu_AE_BITSUSED, arg[0].in, 8);
    tcg_gen_andi_i32(cpu_AE_BITSUSED, cpu_AE_BITSUSED, 0xff);
    tcg_gen_shri_i32(cpu_AE_TABLESIZE, arg[0].in, 16);
    tcg_gen_andi_i32(cpu_AE_TABLESIZE, cpu_AE_TABLESIZE, 0xff);
    tcg_gen_shri_i32(cpu_AE_FIRST_TS, arg[0].in, 24);
  }

  static void translate_ae_xsr_ts_fts_bu_bp(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
  {
    translate_ae_rur_ts_fts_bu_bp(dc, arg, par);
    translate_ae_wur_ts_fts_bu_bp(dc, arg, par);
  }

  /* rur/wur/xsr.ae_cw_sd_no : packed {cwrap[7:0], nextoffset[23:8], searchdone[24]} */
  static void translate_ae_rur_cw_sd_no(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
  {
    TCGv_i32 t = tcg_temp_new_i32();

    if (!arg[0].out) {
      return;
    }
    tcg_gen_andi_i32(arg[0].out, cpu_AE_CWRAP, 0xff);
    tcg_gen_andi_i32(t, cpu_AE_NEXTOFFSET, 0xffff);
    tcg_gen_shli_i32(t, t, 8);
    tcg_gen_or_i32(arg[0].out, arg[0].out, t);
    tcg_gen_andi_i32(t, cpu_AE_SEARCHDONE, 0x1);
    tcg_gen_shli_i32(t, t, 24);
    tcg_gen_or_i32(arg[0].out, arg[0].out, t);
  }

  static void translate_ae_wur_cw_sd_no(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
  {
    if (!arg[0].in) {
      return;
    }
    tcg_gen_andi_i32(cpu_AE_CWRAP, arg[0].in, 0xff);
    tcg_gen_shri_i32(cpu_AE_NEXTOFFSET, arg[0].in, 8);
    tcg_gen_andi_i32(cpu_AE_NEXTOFFSET, cpu_AE_NEXTOFFSET, 0xffff);
    tcg_gen_shri_i32(cpu_AE_SEARCHDONE, arg[0].in, 24);
    tcg_gen_andi_i32(cpu_AE_SEARCHDONE, cpu_AE_SEARCHDONE, 0x1);
  }

  static void translate_ae_xsr_cw_sd_no(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
  {
    translate_ae_rur_cw_sd_no(dc, arg, par);
    translate_ae_wur_cw_sd_no(dc, arg, par);
  }

/*
 * ----- Move instructions -----
 */
static void translate_ae_mov_generic(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (!arg[0].out || !arg[1].in) {
    return;
  }

  if (arg[0].num_bits <= 32 && arg[1].num_bits <= 32) {
    tcg_gen_mov_i32(arg[0].out, arg[1].in);
  } else if (arg[0].num_bits <= 64 && arg[1].num_bits <= 64) {
    if (arg[0].num_bits > 32 && arg[1].num_bits > 32) {
      tcg_gen_mov_i64(arg[0].out, arg[1].in);
    } else if (arg[0].num_bits > 32 && arg[1].num_bits <= 32) {
      tcg_gen_extu_i32_i64(arg[0].out, arg[1].in);
    } else if (arg[0].num_bits <= 32 && arg[1].num_bits > 32) {
      tcg_gen_extrl_i64_i32(arg[0].out, arg[1].in);
    } else {
      tcg_gen_mov_i32(arg[0].out, arg[1].in);
    }
  }
}

static void translate_ae_movi_generic(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  if (!arg[0].out) {
    return;
  }

  if (arg[0].num_bits <= 32) {
    tcg_gen_movi_i32(arg[0].out, arg[1].imm);
  } else {
    tcg_gen_movi_i64(arg[0].out, arg[1].imm);
  }
}

static void translate_ae_movasar(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (!arg[0].out) {
    return;
  }

  if (arg[0].num_bits <= 32) {
    tcg_gen_mov_i32(arg[0].out, cpu_AE_SAR);
  } else {
    tcg_gen_extu_i32_i64(arg[0].out, cpu_AE_SAR);
  }
}

static void translate_ae_movsara(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  TCGv_i32 v = tcg_temp_new_i32();
  void *src = arg[1].in ? arg[1].in : arg[0].in;
  uint32_t src_bits = arg[1].in ? arg[1].num_bits : arg[0].num_bits;

  if (!src) {
    return;
  }

  if (src_bits > 32) {
    tcg_gen_extrl_i64_i32(v, src);
  } else {
    tcg_gen_mov_i32(v, src);
  }
  tcg_gen_andi_i32(cpu_AE_SAR, v, 0x3f);
}

static void ae_pack_lanes32(TCGv_i64 dst, TCGv_i32 lo, TCGv_i32 hi);

/*
 * =========================================================
 * HiFi4 additional translator functions
 * =========================================================
 */

/* ae_and / ae_or / ae_xor / ae_nand -- 64-bit bitwise logical */
static void translate_ae_and(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_and_i64(arg[0].out, arg[1].in, arg[2].in);
    }
}

static void translate_ae_or(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_or_i64(arg[0].out, arg[1].in, arg[2].in);
    }
}

static void translate_ae_xor(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_xor_i64(arg[0].out, arg[1].in, arg[2].in);
    }
}

static void translate_ae_nand(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_and_i64(arg[0].out, arg[1].in, arg[2].in);
        tcg_gen_not_i64(arg[0].out, arg[0].out);
    }
}

/* ae_add64 / ae_sub64 / ae_neg64 / ae_abs64 */
static void translate_ae_add64(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_add_i64(arg[0].out, arg[1].in, arg[2].in);
    }
}

static void translate_ae_sub64(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_sub_i64(arg[0].out, arg[1].in, arg[2].in);
    }
}

static void translate_ae_neg64(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_neg_i64(arg[0].out, arg[1].in);
    }
}

static void translate_ae_abs64(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i64 mask = tcg_temp_new_i64();
        tcg_gen_sari_i64(mask, arg[1].in, 63);
        tcg_gen_xor_i64(arg[0].out, arg[1].in, mask);
        tcg_gen_sub_i64(arg[0].out, arg[0].out, mask);
    }
}

    static TCGv_i32 ae_extract_lane16s(TCGv_i64 src, unsigned lane)
    {
      TCGv_i32 word = tcg_temp_new_i32();
      TCGv_i32 value = tcg_temp_new_i32();

      if (lane < 2) {
        tcg_gen_extrl_i64_i32(word, src);
      } else {
        tcg_gen_extrh_i64_i32(word, src);
      }
      tcg_gen_sextract_i32(value, word, (lane & 1) * 16, 16);
      return value;
    }

    static TCGv_i32 ae_extract_lane32(TCGv_i64 src, unsigned lane)
    {
      TCGv_i32 value = tcg_temp_new_i32();

      if (lane == 0) {
        tcg_gen_extrl_i64_i32(value, src);
      } else {
        tcg_gen_extrh_i64_i32(value, src);
      }
      return value;
    }

    static void ae_pack_lanes32(TCGv_i64 dst, TCGv_i32 lo, TCGv_i32 hi)
    {
      tcg_gen_concat_i32_i64(dst, lo, hi);
    }

    static TCGv_i32 ae_abs32s_mag(TCGv_i32 src, TCGv_i32 overflow)
    {
      TCGv_i32 mask = tcg_temp_new_i32();
      TCGv_i32 abs_val = tcg_temp_new_i32();
      TCGv_i32 is_int_min = tcg_temp_new_i32();

      tcg_gen_sari_i32(mask, src, 31);
      tcg_gen_xor_i32(abs_val, src, mask);
      tcg_gen_sub_i32(abs_val, abs_val, mask);
      tcg_gen_setcondi_i32(TCG_COND_EQ, is_int_min, src, INT32_MIN);
      tcg_gen_or_i32(overflow, overflow, is_int_min);
      tcg_gen_movcond_i32(TCG_COND_NE, abs_val,
                is_int_min, tcg_constant_i32(0),
                tcg_constant_i32(INT32_MAX), abs_val);
      return abs_val;
    }

    static TCGv_i64 ae_abs64s_mag(TCGv_i64 src, TCGv_i32 overflow)
    {
      TCGv_i64 mask = tcg_temp_new_i64();
      TCGv_i64 abs_val = tcg_temp_new_i64();
      TCGv_i64 is_int_min64 = tcg_temp_new_i64();
      TCGv_i32 is_int_min32 = tcg_temp_new_i32();

      tcg_gen_sari_i64(mask, src, 63);
      tcg_gen_xor_i64(abs_val, src, mask);
      tcg_gen_sub_i64(abs_val, abs_val, mask);
      tcg_gen_setcond_i64(TCG_COND_EQ, is_int_min64,
                src, tcg_constant_i64(INT64_MIN));
      tcg_gen_extrl_i64_i32(is_int_min32, is_int_min64);
      tcg_gen_or_i32(overflow, overflow, is_int_min32);
      tcg_gen_movcond_i64(TCG_COND_NE, abs_val,
                is_int_min64, tcg_constant_i64(0),
                tcg_constant_i64(INT64_MAX), abs_val);
      return abs_val;
    }

    static void translate_ae_cmp16(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
    {
      TCGCond cond = par[0];

      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs64 = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs64 = (TCGv_i64)arg[2].in;
        TCGv_i32 mask = tcg_temp_new_i32();
        unsigned lane;

        tcg_gen_movi_i32(mask, 0);
        for (lane = 0; lane < 4; ++lane) {
          TCGv_i32 lhs = ae_extract_lane16s(lhs64, lane);
          TCGv_i32 rhs = ae_extract_lane16s(rhs64, lane);
          TCGv_i32 bit = tcg_temp_new_i32();

          tcg_gen_setcond_i32(cond, bit, lhs, rhs);
          tcg_gen_deposit_i32(mask, mask, bit, lane, 1);
        }
        tcg_gen_mov_i32(arg[0].out, mask);
      }
    }

    static void translate_ae_cmp32(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
    {
      TCGCond cond = par[0];

      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs64 = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs64 = (TCGv_i64)arg[2].in;
        TCGv_i32 mask = tcg_temp_new_i32();
        unsigned lane;

        tcg_gen_movi_i32(mask, 0);
        for (lane = 0; lane < 2; ++lane) {
          TCGv_i32 lhs = ae_extract_lane32(lhs64, lane);
          TCGv_i32 rhs = ae_extract_lane32(rhs64, lane);
          TCGv_i32 bit = tcg_temp_new_i32();

          tcg_gen_setcond_i32(cond, bit, lhs, rhs);
          tcg_gen_deposit_i32(mask, mask, bit, lane, 1);
        }
        tcg_gen_mov_i32(arg[0].out, mask);
      }
    }

    static void translate_ae_cmp64(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
    {
      TCGCond cond = par[0];

      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs64 = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs64 = (TCGv_i64)arg[2].in;
        TCGv_i64 bit64 = tcg_temp_new_i64();

        tcg_gen_setcond_i64(cond, bit64, lhs64, rhs64);
        tcg_gen_extrl_i64_i32(arg[0].out, bit64);
      }
    }

    static void translate_ae_minmax32(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      TCGCond cond = par[0] ? TCG_COND_GE : TCG_COND_LE;

      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 lhs_lo = ae_extract_lane32(arg[1].in, 0);
        TCGv_i32 lhs_hi = ae_extract_lane32(arg[1].in, 1);
        TCGv_i32 rhs_lo = ae_extract_lane32(arg[2].in, 0);
        TCGv_i32 rhs_hi = ae_extract_lane32(arg[2].in, 1);
        TCGv_i32 dst_lo = tcg_temp_new_i32();
        TCGv_i32 dst_hi = tcg_temp_new_i32();

        tcg_gen_movcond_i32(cond, dst_lo, lhs_lo, rhs_lo, lhs_lo, rhs_lo);
        tcg_gen_movcond_i32(cond, dst_hi, lhs_hi, rhs_hi, lhs_hi, rhs_hi);
        ae_pack_lanes32(arg[0].out, dst_lo, dst_hi);
      }
    }

    static void translate_ae_minmax64(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      TCGCond cond = par[0] ? TCG_COND_GE : TCG_COND_LE;

      if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_movcond_i64(cond, arg[0].out,
                  arg[1].in, arg[2].in,
                  arg[1].in, arg[2].in);
      }
    }

    static void translate_ae_minmaxabs32s(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
    {
      TCGCond cond = par[0] ? TCG_COND_GE : TCG_COND_LE;

      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lhs_lo = ae_extract_lane32(arg[1].in, 0);
        TCGv_i32 lhs_hi = ae_extract_lane32(arg[1].in, 1);
        TCGv_i32 rhs_lo = ae_extract_lane32(arg[2].in, 0);
        TCGv_i32 rhs_hi = ae_extract_lane32(arg[2].in, 1);
        TCGv_i32 lhs_abs_lo;
        TCGv_i32 lhs_abs_hi;
        TCGv_i32 rhs_abs_lo;
        TCGv_i32 rhs_abs_hi;
        TCGv_i32 dst_lo = tcg_temp_new_i32();
        TCGv_i32 dst_hi = tcg_temp_new_i32();

        tcg_gen_movi_i32(overflow, 0);
        lhs_abs_lo = ae_abs32s_mag(lhs_lo, overflow);
        lhs_abs_hi = ae_abs32s_mag(lhs_hi, overflow);
        rhs_abs_lo = ae_abs32s_mag(rhs_lo, overflow);
        rhs_abs_hi = ae_abs32s_mag(rhs_hi, overflow);

        tcg_gen_movcond_i32(cond, dst_lo,
                  lhs_abs_lo, rhs_abs_lo,
                  lhs_lo, rhs_lo);
        tcg_gen_movcond_i32(cond, dst_hi,
                  lhs_abs_hi, rhs_abs_hi,
                  lhs_hi, rhs_hi);
        ae_pack_lanes32(arg[0].out, dst_lo, dst_hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static void translate_ae_minmaxabs64s(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
    {
      TCGCond cond = par[0] ? TCG_COND_GE : TCG_COND_LE;

      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 lhs_abs;
        TCGv_i64 rhs_abs;

        tcg_gen_movi_i32(overflow, 0);
        lhs_abs = ae_abs64s_mag(arg[1].in, overflow);
        rhs_abs = ae_abs64s_mag(arg[2].in, overflow);
        tcg_gen_movcond_i64(cond, arg[0].out,
                  lhs_abs, rhs_abs,
                  arg[1].in, arg[2].in);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static TCGv_i32 ae_sat_add32(TCGv_i32 lhs, TCGv_i32 rhs, TCGv_i32 overflow)
    {
      TCGv_i64 lhs64 = tcg_temp_new_i64();
      TCGv_i64 rhs64 = tcg_temp_new_i64();
      TCGv_i64 sum64 = tcg_temp_new_i64();
      TCGv_i64 clamped64 = tcg_temp_new_i64();
      TCGv_i64 gtmax64 = tcg_temp_new_i64();
      TCGv_i64 ltmin64 = tcg_temp_new_i64();
      TCGv_i32 gtmax32 = tcg_temp_new_i32();
      TCGv_i32 ltmin32 = tcg_temp_new_i32();
      TCGv_i32 res32 = tcg_temp_new_i32();

      tcg_gen_ext_i32_i64(lhs64, lhs);
      tcg_gen_ext_i32_i64(rhs64, rhs);
      tcg_gen_add_i64(sum64, lhs64, rhs64);
      tcg_gen_setcond_i64(TCG_COND_GT, gtmax64, sum64,
                tcg_constant_i64(INT32_MAX));
      tcg_gen_setcond_i64(TCG_COND_LT, ltmin64, sum64,
                tcg_constant_i64(INT32_MIN));
      tcg_gen_extrl_i64_i32(gtmax32, gtmax64);
      tcg_gen_extrl_i64_i32(ltmin32, ltmin64);
      tcg_gen_or_i32(overflow, overflow, gtmax32);
      tcg_gen_or_i32(overflow, overflow, ltmin32);

      tcg_gen_movcond_i64(TCG_COND_NE, clamped64,
                gtmax64, tcg_constant_i64(0),
                tcg_constant_i64(INT32_MAX), sum64);
      tcg_gen_movcond_i64(TCG_COND_NE, clamped64,
                ltmin64, tcg_constant_i64(0),
                tcg_constant_i64(INT32_MIN), clamped64);
      tcg_gen_extrl_i64_i32(res32, clamped64);
      return res32;
    }

    static TCGv_i32 ae_sat_sub32(TCGv_i32 lhs, TCGv_i32 rhs, TCGv_i32 overflow)
    {
      TCGv_i64 lhs64 = tcg_temp_new_i64();
      TCGv_i64 rhs64 = tcg_temp_new_i64();
      TCGv_i64 diff64 = tcg_temp_new_i64();
      TCGv_i64 clamped64 = tcg_temp_new_i64();
      TCGv_i64 gtmax64 = tcg_temp_new_i64();
      TCGv_i64 ltmin64 = tcg_temp_new_i64();
      TCGv_i32 gtmax32 = tcg_temp_new_i32();
      TCGv_i32 ltmin32 = tcg_temp_new_i32();
      TCGv_i32 res32 = tcg_temp_new_i32();

      tcg_gen_ext_i32_i64(lhs64, lhs);
      tcg_gen_ext_i32_i64(rhs64, rhs);
      tcg_gen_sub_i64(diff64, lhs64, rhs64);
      tcg_gen_setcond_i64(TCG_COND_GT, gtmax64, diff64,
                tcg_constant_i64(INT32_MAX));
      tcg_gen_setcond_i64(TCG_COND_LT, ltmin64, diff64,
                tcg_constant_i64(INT32_MIN));
      tcg_gen_extrl_i64_i32(gtmax32, gtmax64);
      tcg_gen_extrl_i64_i32(ltmin32, ltmin64);
      tcg_gen_or_i32(overflow, overflow, gtmax32);
      tcg_gen_or_i32(overflow, overflow, ltmin32);

      tcg_gen_movcond_i64(TCG_COND_NE, clamped64,
                gtmax64, tcg_constant_i64(0),
                tcg_constant_i64(INT32_MAX), diff64);
      tcg_gen_movcond_i64(TCG_COND_NE, clamped64,
                ltmin64, tcg_constant_i64(0),
                tcg_constant_i64(INT32_MIN), clamped64);
      tcg_gen_extrl_i64_i32(res32, clamped64);
      return res32;
    }

    static TCGv_i32 ae_shift_rng32(TCGv_i32 src)
    {
      TCGv_i32 sh = tcg_temp_new_i32();
      TCGv_i32 dst = tcg_temp_new_i32();

      tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
      tcg_gen_sar_i32(dst, src, sh);
      return dst;
    }

    static void translate_ae_addsub32_common(DisasContext *dc,
                         const OpcodeArg arg[],
                         bool saturated,
                         bool reverse,
                         bool with_rng)
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs64 = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs64 = (TCGv_i64)arg[2].in;
        TCGv_i32 lhs_lo = ae_extract_lane32(lhs64, 0);
        TCGv_i32 lhs_hi = ae_extract_lane32(lhs64, 1);
        TCGv_i32 rhs_lo = ae_extract_lane32(rhs64, 0);
        TCGv_i32 rhs_hi = ae_extract_lane32(rhs64, 1);
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 out_lo;
        TCGv_i32 out_hi;

        tcg_gen_movi_i32(overflow, 0);

        if (!reverse) {
          if (saturated) {
            out_lo = ae_sat_add32(lhs_lo, rhs_lo, overflow);
            out_hi = ae_sat_sub32(lhs_hi, rhs_hi, overflow);
          } else {
            out_lo = tcg_temp_new_i32();
            out_hi = tcg_temp_new_i32();
            tcg_gen_add_i32(out_lo, lhs_lo, rhs_lo);
            tcg_gen_sub_i32(out_hi, lhs_hi, rhs_hi);
          }
        } else {
          if (saturated) {
            out_lo = ae_sat_sub32(lhs_lo, rhs_lo, overflow);
            out_hi = ae_sat_add32(lhs_hi, rhs_hi, overflow);
          } else {
            out_lo = tcg_temp_new_i32();
            out_hi = tcg_temp_new_i32();
            tcg_gen_sub_i32(out_lo, lhs_lo, rhs_lo);
            tcg_gen_add_i32(out_hi, lhs_hi, rhs_hi);
          }
        }

        if (with_rng) {
          out_lo = ae_shift_rng32(out_lo);
          out_hi = ae_shift_rng32(out_hi);
        }

        ae_pack_lanes32(arg[0].out, out_lo, out_hi);
        if (saturated) {
          tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
        }
      }
    }

    static void translate_ae_addsub32(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      translate_ae_addsub32_common(dc, arg, false, false, false);
    }

    static void translate_ae_subadd32(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      translate_ae_addsub32_common(dc, arg, false, true, false);
    }

    static void translate_ae_addsub32s(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      translate_ae_addsub32_common(dc, arg, true, false, false);
    }

    static void translate_ae_subadd32s(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      translate_ae_addsub32_common(dc, arg, true, true, false);
    }

    static void translate_ae_addrng32(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs64 = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs64 = (TCGv_i64)arg[2].in;
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();

        tcg_gen_add_i32(lo, ae_extract_lane32(lhs64, 0), ae_extract_lane32(rhs64, 0));
        tcg_gen_add_i32(hi, ae_extract_lane32(lhs64, 1), ae_extract_lane32(rhs64, 1));
        lo = ae_shift_rng32(lo);
        hi = ae_shift_rng32(hi);
        ae_pack_lanes32(arg[0].out, lo, hi);
      }
    }

    static void translate_ae_subrng32(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs64 = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs64 = (TCGv_i64)arg[2].in;
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();

        tcg_gen_sub_i32(lo, ae_extract_lane32(lhs64, 0), ae_extract_lane32(rhs64, 0));
        tcg_gen_sub_i32(hi, ae_extract_lane32(lhs64, 1), ae_extract_lane32(rhs64, 1));
        lo = ae_shift_rng32(lo);
        hi = ae_shift_rng32(hi);
        ae_pack_lanes32(arg[0].out, lo, hi);
      }
    }

    static void translate_ae_addandsub32s(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
    {
      if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
        OpcodeArg addsub_arg[3] = { 0 };
        OpcodeArg subadd_arg[3] = { 0 };

        addsub_arg[0].out = arg[0].out;
        addsub_arg[1].in = arg[2].in;
        addsub_arg[2].in = arg[3].in;
        subadd_arg[0].out = arg[1].out;
        subadd_arg[1].in = arg[2].in;
        subadd_arg[2].in = arg[3].in;

        translate_ae_addsub32s(dc, addsub_arg, par);
        translate_ae_subadd32s(dc, subadd_arg, par);
      }
    }

    static void translate_ae_addandsubrng32(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
    {
      if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
        OpcodeArg addrng_arg[3] = { 0 };
        OpcodeArg subrng_arg[3] = { 0 };

        addrng_arg[0].out = arg[0].out;
        addrng_arg[1].in = arg[2].in;
        addrng_arg[2].in = arg[3].in;
        subrng_arg[0].out = arg[1].out;
        subrng_arg[1].in = arg[2].in;
        subrng_arg[2].in = arg[3].in;

        translate_ae_addrng32(dc, addrng_arg, par);
        translate_ae_subrng32(dc, subrng_arg, par);
      }
    }

    static TCGv_i32 ae_nsa32(TCGv_i32 src)
    {
      TCGv_i32 sign = tcg_temp_new_i32();
      TCGv_i32 norm = tcg_temp_new_i32();
      TCGv_i32 clz = tcg_temp_new_i32();
      TCGv_i32 nsa = tcg_temp_new_i32();

      tcg_gen_sari_i32(sign, src, 31);
      tcg_gen_xor_i32(norm, src, sign);
      tcg_gen_clzi_i32(clz, norm, 32);
      tcg_gen_subi_i32(nsa, clz, 1);
      return nsa;
    }

    static void translate_ae_rng32x2(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
    {
      if (arg[0].in) {
        TCGv_i64 src64 = (TCGv_i64)arg[0].in;
        TCGv_i32 lo = ae_extract_lane32(src64, 0);
        TCGv_i32 hi = ae_extract_lane32(src64, 1);
        TCGv_i32 nsa_lo = ae_nsa32(lo);
        TCGv_i32 nsa_hi = ae_nsa32(hi);

        tcg_gen_movcond_i32(TCG_COND_LE, cpu_AE_SAR,
                  nsa_lo, nsa_hi,
                  nsa_lo, nsa_hi);
        tcg_gen_andi_i32(cpu_AE_SAR, cpu_AE_SAR, 0x3f);
      }
    }

    static void translate_ae_calcrng(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
    {
      tcg_gen_movi_i32(cpu_AE_SAR, par[0] & 0x3f);
    }

    static TCGv_i64 ae_sat_add64(TCGv_i64 lhs, TCGv_i64 rhs, TCGv_i32 overflow)
    {
      TCGv_i64 sum = tcg_temp_new_i64();
      TCGv_i64 x1 = tcg_temp_new_i64();
      TCGv_i64 x2 = tcg_temp_new_i64();
      TCGv_i64 ov64 = tcg_temp_new_i64();
      TCGv_i32 ov32 = tcg_temp_new_i32();
      TCGv_i64 sat = tcg_temp_new_i64();
      TCGv_i64 sign = tcg_temp_new_i64();
      TCGv_i64 res = tcg_temp_new_i64();

      tcg_gen_add_i64(sum, lhs, rhs);
      tcg_gen_xor_i64(x1, lhs, sum);
      tcg_gen_xor_i64(x2, rhs, sum);
      tcg_gen_and_i64(x1, x1, x2);
      tcg_gen_setcondi_i64(TCG_COND_LT, ov64, x1, 0);
      tcg_gen_extrl_i64_i32(ov32, ov64);
      tcg_gen_or_i32(overflow, overflow, ov32);

      tcg_gen_sari_i64(sign, lhs, 63);
      tcg_gen_movcond_i64(TCG_COND_NE, sat,
                sign, tcg_constant_i64(0),
                tcg_constant_i64(INT64_MIN),
                tcg_constant_i64(INT64_MAX));
      tcg_gen_movcond_i64(TCG_COND_NE, res,
                ov64, tcg_constant_i64(0),
                sat, sum);
      return res;
    }

    static TCGv_i64 ae_sat_sub64(TCGv_i64 lhs, TCGv_i64 rhs, TCGv_i32 overflow)
    {
      TCGv_i64 diff = tcg_temp_new_i64();
      TCGv_i64 x1 = tcg_temp_new_i64();
      TCGv_i64 x2 = tcg_temp_new_i64();
      TCGv_i64 ov64 = tcg_temp_new_i64();
      TCGv_i32 ov32 = tcg_temp_new_i32();
      TCGv_i64 sat = tcg_temp_new_i64();
      TCGv_i64 sign = tcg_temp_new_i64();
      TCGv_i64 res = tcg_temp_new_i64();

      tcg_gen_sub_i64(diff, lhs, rhs);
      tcg_gen_xor_i64(x1, lhs, rhs);
      tcg_gen_xor_i64(x2, lhs, diff);
      tcg_gen_and_i64(x1, x1, x2);
      tcg_gen_setcondi_i64(TCG_COND_LT, ov64, x1, 0);
      tcg_gen_extrl_i64_i32(ov32, ov64);
      tcg_gen_or_i32(overflow, overflow, ov32);

      tcg_gen_sari_i64(sign, lhs, 63);
      tcg_gen_movcond_i64(TCG_COND_NE, sat,
                sign, tcg_constant_i64(0),
                tcg_constant_i64(INT64_MIN),
                tcg_constant_i64(INT64_MAX));
      tcg_gen_movcond_i64(TCG_COND_NE, res,
                ov64, tcg_constant_i64(0),
                sat, diff);
      return res;
    }

    static void translate_ae_add64s(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 res;

        tcg_gen_movi_i32(overflow, 0);
        res = ae_sat_add64((TCGv_i64)arg[1].in, (TCGv_i64)arg[2].in, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, res);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static void translate_ae_sub64s(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 res;

        tcg_gen_movi_i32(overflow, 0);
        res = ae_sat_sub64((TCGv_i64)arg[1].in, (TCGv_i64)arg[2].in, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, res);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static void translate_ae_add72(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].in && arg[0].out && arg[1].in && arg[1].out &&
          arg[2].in && arg[3].in) {
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[3].in;
        TCGv_i64 sum = tcg_temp_new_i64();
        TCGv_i64 carry64 = tcg_temp_new_i64();
        TCGv_i32 carry32 = tcg_temp_new_i32();
        TCGv_i32 ep0 = tcg_temp_new_i32();
        TCGv_i32 ep1 = tcg_temp_new_i32();
        TCGv_i32 ep = tcg_temp_new_i32();

        tcg_gen_add_i64(sum, lhs, rhs);
        tcg_gen_setcond_i64(TCG_COND_LTU, carry64, sum, lhs);
        tcg_gen_extrl_i64_i32(carry32, carry64);
        tcg_gen_andi_i32(ep0, (TCGv_i32)arg[0].in, 0xff);
        tcg_gen_andi_i32(ep1, (TCGv_i32)arg[2].in, 0xff);
        tcg_gen_add_i32(ep, ep0, ep1);
        tcg_gen_add_i32(ep, ep, carry32);
        tcg_gen_andi_i32((TCGv_i32)arg[0].out, ep, 0xff);
        tcg_gen_mov_i64((TCGv_i64)arg[1].out, sum);
      }
    }

    static void translate_ae_sub72(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].in && arg[0].out && arg[1].in && arg[1].out &&
          arg[2].in && arg[3].in) {
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[3].in;
        TCGv_i64 diff = tcg_temp_new_i64();
        TCGv_i64 borrow64 = tcg_temp_new_i64();
        TCGv_i32 borrow32 = tcg_temp_new_i32();
        TCGv_i32 ep0 = tcg_temp_new_i32();
        TCGv_i32 ep1 = tcg_temp_new_i32();
        TCGv_i32 ep = tcg_temp_new_i32();

        tcg_gen_sub_i64(diff, lhs, rhs);
        tcg_gen_setcond_i64(TCG_COND_LTU, borrow64, lhs, rhs);
        tcg_gen_extrl_i64_i32(borrow32, borrow64);
        tcg_gen_andi_i32(ep0, (TCGv_i32)arg[0].in, 0xff);
        tcg_gen_andi_i32(ep1, (TCGv_i32)arg[2].in, 0xff);
        tcg_gen_sub_i32(ep, ep0, ep1);
        tcg_gen_sub_i32(ep, ep, borrow32);
        tcg_gen_andi_i32((TCGv_i32)arg[0].out, ep, 0xff);
        tcg_gen_mov_i64((TCGv_i64)arg[1].out, diff);
      }
    }

    static void translate_ae_add72x64(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].in && arg[0].out && arg[1].in && arg[1].out && arg[2].in) {
        OpcodeArg add72_arg[4] = { 0 };
        TCGv_i32 ep1 = tcg_temp_new_i32();

        tcg_gen_extrh_i64_i32(ep1, (TCGv_i64)arg[2].in);
        tcg_gen_sari_i32(ep1, ep1, 31);
        add72_arg[0].in = arg[0].in;
        add72_arg[0].out = arg[0].out;
        add72_arg[1].in = arg[1].in;
        add72_arg[1].out = arg[1].out;
        add72_arg[2].in = ep1;
        add72_arg[3].in = arg[2].in;
        translate_ae_add72(dc, add72_arg, par);
      }
    }

    static void translate_ae_sub72x64(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].in && arg[0].out && arg[1].in && arg[1].out && arg[2].in) {
        OpcodeArg sub72_arg[4] = { 0 };
        TCGv_i32 ep1 = tcg_temp_new_i32();

        tcg_gen_extrh_i64_i32(ep1, (TCGv_i64)arg[2].in);
        tcg_gen_sari_i32(ep1, ep1, 31);
        sub72_arg[0].in = arg[0].in;
        sub72_arg[0].out = arg[0].out;
        sub72_arg[1].in = arg[1].in;
        sub72_arg[1].out = arg[1].out;
        sub72_arg[2].in = ep1;
        sub72_arg[3].in = arg[2].in;
        translate_ae_sub72(dc, sub72_arg, par);
      }
    }

    static TCGv_i64 ae_sat56_from_i64(TCGv_i64 value, TCGv_i32 overflow)
    {
      TCGv_i64 gtmax64 = tcg_temp_new_i64();
      TCGv_i64 ltmin64 = tcg_temp_new_i64();
      TCGv_i32 gtmax32 = tcg_temp_new_i32();
      TCGv_i32 ltmin32 = tcg_temp_new_i32();
      TCGv_i64 res = tcg_temp_new_i64();

      tcg_gen_setcond_i64(TCG_COND_GT, gtmax64, value,
                tcg_constant_i64(INT64_C(0x007fffffffffffff)));
      tcg_gen_setcond_i64(TCG_COND_LT, ltmin64, value,
                tcg_constant_i64(INT64_C(0xff80000000000000)));
      tcg_gen_extrl_i64_i32(gtmax32, gtmax64);
      tcg_gen_extrl_i64_i32(ltmin32, ltmin64);
      tcg_gen_or_i32(overflow, overflow, gtmax32);
      tcg_gen_or_i32(overflow, overflow, ltmin32);

      tcg_gen_movcond_i64(TCG_COND_NE, res,
                gtmax64, tcg_constant_i64(0),
                tcg_constant_i64(INT64_C(0x007fffffffffffff)),
                value);
      tcg_gen_movcond_i64(TCG_COND_NE, res,
                ltmin64, tcg_constant_i64(0),
                tcg_constant_i64(INT64_C(0xff80000000000000)),
                res);
      return res;
    }

    static void translate_ae_addsq56s(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 sum = tcg_temp_new_i64();

        tcg_gen_movi_i32(overflow, 0);
        tcg_gen_add_i64(sum, (TCGv_i64)arg[1].in, (TCGv_i64)arg[2].in);
        sum = ae_sat56_from_i64(sum, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, sum);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static void translate_ae_subsq56s(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 diff = tcg_temp_new_i64();

        tcg_gen_movi_i32(overflow, 0);
        tcg_gen_sub_i64(diff, (TCGv_i64)arg[1].in, (TCGv_i64)arg[2].in);
        diff = ae_sat56_from_i64(diff, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, diff);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static void translate_ae_negsq56s(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 neg = tcg_temp_new_i64();

        tcg_gen_movi_i32(overflow, 0);
        tcg_gen_neg_i64(neg, (TCGv_i64)arg[1].in);
        neg = ae_sat56_from_i64(neg, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, neg);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static void translate_ae_abssq56s(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 mask = tcg_temp_new_i64();
        TCGv_i64 abs64 = tcg_temp_new_i64();

        tcg_gen_movi_i32(overflow, 0);
        tcg_gen_sari_i64(mask, (TCGv_i64)arg[1].in, 63);
        tcg_gen_xor_i64(abs64, (TCGv_i64)arg[1].in, mask);
        tcg_gen_sub_i64(abs64, abs64, mask);
        abs64 = ae_sat56_from_i64(abs64, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, abs64);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

/* ae_slai64 / ae_srli64 / ae_srai64 / ae_sra64_32 / ae_srla64 / ae_slaa64 */
/* ---- Saturating / narrow-width arithmetic (16/24/32-bit, sat helpers) ---- */

    /* Clamp i32 to 16-bit signed range; set overflow flag if clamped */
    static TCGv_i32 ae_sat16_from_i32(TCGv_i32 val, TCGv_i32 overflow)
    {
      TCGv_i32 res = tcg_temp_new_i32();
      TCGv_i32 gt = tcg_temp_new_i32();
      TCGv_i32 lt = tcg_temp_new_i32();

      tcg_gen_setcondi_i32(TCG_COND_GT, gt, val,  32767);
      tcg_gen_setcondi_i32(TCG_COND_LT, lt, val, -32768);
      tcg_gen_or_i32(overflow, overflow, gt);
      tcg_gen_or_i32(overflow, overflow, lt);
      tcg_gen_movcond_i32(TCG_COND_NE, res, gt, tcg_constant_i32(0),
                          tcg_constant_i32(32767), val);
      tcg_gen_movcond_i32(TCG_COND_NE, res, lt, tcg_constant_i32(0),
                          tcg_constant_i32(-32768), res);
      return res;
    }

    static TCGv_i32 ae_abs16s_mag(TCGv_i32 src, TCGv_i32 overflow)
    {
      TCGv_i32 mask = tcg_temp_new_i32();
      TCGv_i32 abs_val = tcg_temp_new_i32();
      TCGv_i32 is_min = tcg_temp_new_i32();

      tcg_gen_sari_i32(mask, src, 31);
      tcg_gen_xor_i32(abs_val, src, mask);
      tcg_gen_sub_i32(abs_val, abs_val, mask);
      tcg_gen_setcondi_i32(TCG_COND_EQ, is_min, src, -32768);
      tcg_gen_or_i32(overflow, overflow, is_min);
      tcg_gen_movcond_i32(TCG_COND_NE, abs_val,
                          is_min, tcg_constant_i32(0),
                          tcg_constant_i32(32767), abs_val);
      return abs_val;
    }

    static TCGv_i32 ae_sat_add16(TCGv_i32 lhs, TCGv_i32 rhs, TCGv_i32 overflow)
    {
      TCGv_i32 sum = tcg_temp_new_i32();
      tcg_gen_add_i32(sum, lhs, rhs);
      return ae_sat16_from_i32(sum, overflow);
    }

    static TCGv_i32 ae_sat_sub16(TCGv_i32 lhs, TCGv_i32 rhs, TCGv_i32 overflow)
    {
      TCGv_i32 diff = tcg_temp_new_i32();
      tcg_gen_sub_i32(diff, lhs, rhs);
      return ae_sat16_from_i32(diff, overflow);
    }

    /* Extract the 24-bit signed value from lane 0 (bits[23:0]) or lane 1
     * (bits[55:32]) of a 64-bit AE_DR register, sign-extended to i32. */
    static TCGv_i32 ae_extract_lane24(TCGv_i64 src, unsigned lane)
    {
      TCGv_i32 word = tcg_temp_new_i32();
      TCGv_i32 value = tcg_temp_new_i32();

      if (lane == 0) {
        tcg_gen_extrl_i64_i32(word, src);
      } else {
        tcg_gen_extrh_i64_i32(word, src);
      }
      tcg_gen_sextract_i32(value, word, 0, 24);
      return value;
    }

    /* Pack two 24-bit values (in i32, bits[23:0] valid) into one 64-bit reg.
     * Each slot is sign-extended from bit 23 to fill bits[31:24]. */
    static void ae_pack_lanes24(TCGv_i64 dst, TCGv_i32 lo, TCGv_i32 hi)
    {
      TCGv_i32 lo_sx = tcg_temp_new_i32();
      TCGv_i32 hi_sx = tcg_temp_new_i32();
      tcg_gen_sextract_i32(lo_sx, lo, 0, 24);
      tcg_gen_sextract_i32(hi_sx, hi, 0, 24);
      tcg_gen_concat_i32_i64(dst, lo_sx, hi_sx);
    }

    /* Clamp i32 to 24-bit signed range [-2^23, 2^23-1]; set overflow if clamped */
    static TCGv_i32 ae_sat24_from_i32(TCGv_i32 val, TCGv_i32 overflow)
    {
      TCGv_i32 res = tcg_temp_new_i32();
      TCGv_i32 gt = tcg_temp_new_i32();
      TCGv_i32 lt = tcg_temp_new_i32();

      tcg_gen_setcondi_i32(TCG_COND_GT, gt, val,  0x7fffff);
      tcg_gen_setcondi_i32(TCG_COND_LT, lt, val, -0x800000);
      tcg_gen_or_i32(overflow, overflow, gt);
      tcg_gen_or_i32(overflow, overflow, lt);
      tcg_gen_movcond_i32(TCG_COND_NE, res, gt, tcg_constant_i32(0),
                          tcg_constant_i32(0x7fffff), val);
      tcg_gen_movcond_i32(TCG_COND_NE, res, lt, tcg_constant_i32(0),
                          tcg_constant_i32(-0x800000), res);
      return res;
    }

    static TCGv_i32 ae_sat_add24(TCGv_i32 lhs, TCGv_i32 rhs, TCGv_i32 overflow)
    {
      TCGv_i32 sum = tcg_temp_new_i32();
      tcg_gen_add_i32(sum, lhs, rhs);
      return ae_sat24_from_i32(sum, overflow);
    }

    static TCGv_i32 ae_sat_sub24(TCGv_i32 lhs, TCGv_i32 rhs, TCGv_i32 overflow)
    {
      TCGv_i32 diff = tcg_temp_new_i32();
      tcg_gen_sub_i32(diff, lhs, rhs);
      return ae_sat24_from_i32(diff, overflow);
    }

    static TCGv_i32 ae_abs24s_mag(TCGv_i32 src, TCGv_i32 overflow)
    {
      TCGv_i32 mask = tcg_temp_new_i32();
      TCGv_i32 abs_val = tcg_temp_new_i32();
      TCGv_i32 is_min = tcg_temp_new_i32();

      tcg_gen_sari_i32(mask, src, 31);
      tcg_gen_xor_i32(abs_val, src, mask);
      tcg_gen_sub_i32(abs_val, abs_val, mask);
      tcg_gen_setcondi_i32(TCG_COND_EQ, is_min, src, -0x800000);
      tcg_gen_or_i32(overflow, overflow, is_min);
      tcg_gen_movcond_i32(TCG_COND_NE, abs_val,
                          is_min, tcg_constant_i32(0),
                          tcg_constant_i32(0x7fffff), abs_val);
      return abs_val;
    }

    /* Clamp i64 to 48-bit signed range [-2^47, 2^47-1]; set overflow if clamped */
    static TCGv_i64 ae_sat48_from_i64(TCGv_i64 val, TCGv_i32 overflow)
    {
      TCGv_i64 res = tcg_temp_new_i64();
      TCGv_i64 gt = tcg_temp_new_i64();
      TCGv_i64 lt = tcg_temp_new_i64();
      TCGv_i32 gt32 = tcg_temp_new_i32();
      TCGv_i32 lt32 = tcg_temp_new_i32();
      const int64_t INT48_MAX =  (int64_t)((1LL << 47) - 1);
      const int64_t INT48_MIN = -(int64_t)(1LL << 47);

      tcg_gen_setcond_i64(TCG_COND_GT, gt, val, tcg_constant_i64(INT48_MAX));
      tcg_gen_setcond_i64(TCG_COND_LT, lt, val, tcg_constant_i64(INT48_MIN));
      tcg_gen_extrl_i64_i32(gt32, gt);
      tcg_gen_extrl_i64_i32(lt32, lt);
      tcg_gen_or_i32(overflow, overflow, gt32);
      tcg_gen_or_i32(overflow, overflow, lt32);
      tcg_gen_movcond_i64(TCG_COND_NE, res, gt, tcg_constant_i64(0),
                          tcg_constant_i64(INT48_MAX), val);
      tcg_gen_movcond_i64(TCG_COND_NE, res, lt, tcg_constant_i64(0),
                          tcg_constant_i64(INT48_MIN), res);
      return res;
    }

    /* Saturate 72-bit two's complement {ep[7:0], dr[63:0]} to i64.
     * ep is stored as a 32-bit TCGv holding an 8-bit sign byte.
     * The value fits in i64 iff: sext8(ep) == (dr < 0 ? -1 : 0). */
    static TCGv_i64 ae_sat64_from_72(TCGv_i32 ep, TCGv_i64 dr, TCGv_i32 overflow)
    {
      TCGv_i64 res = tcg_temp_new_i64();
      TCGv_i32 ep_sx = tcg_temp_new_i32();
      TCGv_i32 dr_sign = tcg_temp_new_i32();
      TCGv_i32 expected_ep = tcg_temp_new_i32();
      TCGv_i32 fits32 = tcg_temp_new_i32();
      TCGv_i32 ovf32 = tcg_temp_new_i32();
      TCGv_i32 pos_ovf = tcg_temp_new_i32();
      TCGv_i64 dr_msb = tcg_temp_new_i64();

      /* Sign-extend ep byte */
      tcg_gen_sextract_i32(ep_sx, ep, 0, 8);
      /* Get sign of dr */
      tcg_gen_shri_i64(dr_msb, dr, 63);
      tcg_gen_extrl_i64_i32(dr_sign, dr_msb);
      /* expected_ep = dr < 0 ? -1 : 0 */
      tcg_gen_neg_i32(expected_ep, dr_sign);
      /* fits = (ep_sx == expected_ep) */
      tcg_gen_setcond_i32(TCG_COND_EQ, fits32, ep_sx, expected_ep);
      tcg_gen_xori_i32(ovf32, fits32, 1);
      tcg_gen_or_i32(overflow, overflow, ovf32);
      /* positive overflow: ep_sx > 0, or (ep_sx == 0 and dr < 0) */
      tcg_gen_setcondi_i32(TCG_COND_GT, pos_ovf, ep_sx, 0);
      tcg_gen_or_i32(pos_ovf, pos_ovf,
                     tcg_temp_new_i32());  /* placeholder, build below */
      /* Recalculate: pos_ovf = (ep_sx > 0) | (ep_sx == 0 & dr_sign == 1) */
      {
        TCGv_i32 corner_pos = tcg_temp_new_i32();
        TCGv_i32 ep_zero = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, ep_zero, ep_sx, 0);
        tcg_gen_and_i32(corner_pos, ep_zero, dr_sign);
        tcg_gen_setcondi_i32(TCG_COND_GT, pos_ovf, ep_sx, 0);
        tcg_gen_or_i32(pos_ovf, pos_ovf, corner_pos);
      }
      tcg_gen_movcond_i64(TCG_COND_NE, res,
                          tcg_temp_new_i64(), tcg_constant_i64(0),
                          tcg_constant_i64(INT64_MAX), dr);
      /* Use pos_ovf to select INT64_MAX vs negative result */
      {
        TCGv_i64 pos_ovf64 = tcg_temp_new_i64();
        TCGv_i64 neg_ovf64 = tcg_temp_new_i64();
        TCGv_i32 neg_ovf = tcg_temp_new_i32();
        tcg_gen_extu_i32_i64(pos_ovf64, pos_ovf);
        tcg_gen_xori_i32(neg_ovf, fits32, 1);
        tcg_gen_andc_i32(neg_ovf, neg_ovf, pos_ovf);
        tcg_gen_extu_i32_i64(neg_ovf64, neg_ovf);
        tcg_gen_movcond_i64(TCG_COND_NE, res, pos_ovf64, tcg_constant_i64(0),
                            tcg_constant_i64(INT64_MAX), dr);
        tcg_gen_movcond_i64(TCG_COND_NE, res, neg_ovf64, tcg_constant_i64(0),
                            tcg_constant_i64(INT64_MIN), res);
      }
      return res;
    }

    /* --- abs16s --- */
    static void translate_ae_abs16s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        unsigned lane;
        TCGv_i32 lanes[4];

        tcg_gen_movi_i32(overflow, 0);
        for (lane = 0; lane < 4; ++lane) {
          lanes[lane] = ae_abs16s_mag(ae_extract_lane16s(src, lane), overflow);
        }
        /* pack 4×16 back into 64 */
        {
          TCGv_i64 lo = tcg_temp_new_i64();
          TCGv_i64 hi = tcg_temp_new_i64();
          TCGv_i32 lo16 = tcg_temp_new_i32();
          TCGv_i32 hi16 = tcg_temp_new_i32();
          tcg_gen_deposit_i32(lo16, lanes[0], lanes[1], 16, 16);
          tcg_gen_deposit_i32(hi16, lanes[2], lanes[3], 16, 16);
          tcg_gen_extu_i32_i64(lo, lo16);
          tcg_gen_extu_i32_i64(hi, hi16);
          tcg_gen_shli_i64(hi, hi, 32);
          tcg_gen_or_i64((TCGv_i64)arg[0].out, lo, hi);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- abs24s --- */
    static void translate_ae_abs24s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i32 lo = ae_extract_lane24(src, 0);
        TCGv_i32 hi = ae_extract_lane24(src, 1);

        tcg_gen_movi_i32(overflow, 0);
        lo = ae_abs24s_mag(lo, overflow);
        hi = ae_abs24s_mag(hi, overflow);
        ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- abs32 (wrapping, no overflow) --- */
    static void translate_ae_abs32(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i32 lo = ae_extract_lane32(src, 0);
        TCGv_i32 hi = ae_extract_lane32(src, 1);
        TCGv_i32 mask_lo = tcg_temp_new_i32();
        TCGv_i32 mask_hi = tcg_temp_new_i32();
        TCGv_i32 res_lo = tcg_temp_new_i32();
        TCGv_i32 res_hi = tcg_temp_new_i32();

        tcg_gen_sari_i32(mask_lo, lo, 31);
        tcg_gen_xor_i32(res_lo, lo, mask_lo);
        tcg_gen_sub_i32(res_lo, res_lo, mask_lo);

        tcg_gen_sari_i32(mask_hi, hi, 31);
        tcg_gen_xor_i32(res_hi, hi, mask_hi);
        tcg_gen_sub_i32(res_hi, res_hi, mask_hi);

        ae_pack_lanes32((TCGv_i64)arg[0].out, res_lo, res_hi);
      }
    }

    /* --- abs32s (saturating) --- */
    static void translate_ae_abs32s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i32 lo = ae_extract_lane32(src, 0);
        TCGv_i32 hi = ae_extract_lane32(src, 1);

        tcg_gen_movi_i32(overflow, 0);
        lo = ae_abs32s_mag(lo, overflow);
        hi = ae_abs32s_mag(hi, overflow);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- abs64s (saturating) --- */
    static void translate_ae_abs64s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 result;

        tcg_gen_movi_i32(overflow, 0);
        result = ae_abs64s_mag((TCGv_i64)arg[1].in, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, result);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- neg16s (saturating) --- */
    static void translate_ae_neg16s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        unsigned lane;
        TCGv_i32 lanes[4];

        tcg_gen_movi_i32(overflow, 0);
        for (lane = 0; lane < 4; ++lane) {
          TCGv_i32 v = ae_extract_lane16s(src, lane);
          TCGv_i32 neg = tcg_temp_new_i32();
          tcg_gen_neg_i32(neg, v);
          lanes[lane] = ae_sat16_from_i32(neg, overflow);
        }
        {
          TCGv_i64 lo = tcg_temp_new_i64();
          TCGv_i64 hi = tcg_temp_new_i64();
          TCGv_i32 lo16 = tcg_temp_new_i32();
          TCGv_i32 hi16 = tcg_temp_new_i32();
          tcg_gen_deposit_i32(lo16, lanes[0], lanes[1], 16, 16);
          tcg_gen_deposit_i32(hi16, lanes[2], lanes[3], 16, 16);
          tcg_gen_extu_i32_i64(lo, lo16);
          tcg_gen_extu_i32_i64(hi, hi16);
          tcg_gen_shli_i64(hi, hi, 32);
          tcg_gen_or_i64((TCGv_i64)arg[0].out, lo, hi);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- neg24s (saturating) --- */
    static void translate_ae_neg24s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i32 lo = ae_extract_lane24(src, 0);
        TCGv_i32 hi = ae_extract_lane24(src, 1);
        TCGv_i32 neg_lo = tcg_temp_new_i32();
        TCGv_i32 neg_hi = tcg_temp_new_i32();

        tcg_gen_movi_i32(overflow, 0);
        tcg_gen_neg_i32(neg_lo, lo);
        tcg_gen_neg_i32(neg_hi, hi);
        neg_lo = ae_sat24_from_i32(neg_lo, overflow);
        neg_hi = ae_sat24_from_i32(neg_hi, overflow);
        ae_pack_lanes24((TCGv_i64)arg[0].out, neg_lo, neg_hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- neg32 (wrapping, no overflow) --- */
    static void translate_ae_neg32(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i32 lo = ae_extract_lane32(src, 0);
        TCGv_i32 hi = ae_extract_lane32(src, 1);
        TCGv_i32 neg_lo = tcg_temp_new_i32();
        TCGv_i32 neg_hi = tcg_temp_new_i32();

        tcg_gen_neg_i32(neg_lo, lo);
        tcg_gen_neg_i32(neg_hi, hi);
        ae_pack_lanes32((TCGv_i64)arg[0].out, neg_lo, neg_hi);
      }
    }

    /* --- neg32s (saturating) --- */
    static void translate_ae_neg32s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i32 lo = ae_extract_lane32(src, 0);
        TCGv_i32 hi = ae_extract_lane32(src, 1);
        TCGv_i32 neg_lo = tcg_temp_new_i32();
        TCGv_i32 neg_hi = tcg_temp_new_i32();

        tcg_gen_movi_i32(overflow, 0);
        tcg_gen_neg_i32(neg_lo, lo);
        tcg_gen_neg_i32(neg_hi, hi);
        neg_lo = ae_sat_add32(tcg_constant_i32(0), lo,
                              overflow); /* 0 - lo = sat_neg */
        neg_hi = ae_sat_add32(tcg_constant_i32(0), hi, overflow);
        /* Use sat_sub32(0, x) for correct saturation of INT32_MIN -> INT32_MAX */
        tcg_gen_movi_i32(overflow, 0);
        neg_lo = ae_sat_sub32(tcg_constant_i32(0), lo, overflow);
        neg_hi = ae_sat_sub32(tcg_constant_i32(0), hi, overflow);
        ae_pack_lanes32((TCGv_i64)arg[0].out, neg_lo, neg_hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- add16 (wrapping) --- */
    static void translate_ae_add16(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[2].in;
        /* Wrapping 16-bit per lane: just add the full 64-bit word with
         * carries masked after each 16-bit boundary. XOR approach:
         * result = (lhs + rhs) with per-16b wrap.
         * Simplest TCG: add with mask to prevent carry propagation. */
        TCGv_i64 mask = tcg_constant_i64(0x0000ffff0000ffffULL);
        TCGv_i64 lhs_lo = tcg_temp_new_i64();
        TCGv_i64 rhs_lo = tcg_temp_new_i64();
        TCGv_i64 lhs_hi = tcg_temp_new_i64();
        TCGv_i64 rhs_hi = tcg_temp_new_i64();
        TCGv_i64 sum_lo = tcg_temp_new_i64();
        TCGv_i64 sum_hi = tcg_temp_new_i64();
        TCGv_i64 result = tcg_temp_new_i64();

        tcg_gen_and_i64(lhs_lo, lhs, mask);
        tcg_gen_and_i64(rhs_lo, rhs, mask);
        tcg_gen_add_i64(sum_lo, lhs_lo, rhs_lo);
        tcg_gen_and_i64(sum_lo, sum_lo, mask);

        tcg_gen_shri_i64(lhs_hi, lhs, 16);
        tcg_gen_shri_i64(rhs_hi, rhs, 16);
        tcg_gen_and_i64(lhs_hi, lhs_hi, mask);
        tcg_gen_and_i64(rhs_hi, rhs_hi, mask);
        tcg_gen_add_i64(sum_hi, lhs_hi, rhs_hi);
        tcg_gen_and_i64(sum_hi, sum_hi, mask);
        tcg_gen_shli_i64(sum_hi, sum_hi, 16);
        tcg_gen_or_i64(result, sum_lo, sum_hi);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, result);
      }
    }

    /* --- add16s (saturating) --- */
    static void translate_ae_add16s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[2].in;
        unsigned lane;
        TCGv_i32 lanes[4];

        tcg_gen_movi_i32(overflow, 0);
        for (lane = 0; lane < 4; ++lane) {
          lanes[lane] = ae_sat_add16(ae_extract_lane16s(lhs, lane),
                                     ae_extract_lane16s(rhs, lane), overflow);
        }
        {
          TCGv_i64 lo = tcg_temp_new_i64();
          TCGv_i64 hi = tcg_temp_new_i64();
          TCGv_i32 lo16 = tcg_temp_new_i32();
          TCGv_i32 hi16 = tcg_temp_new_i32();
          tcg_gen_deposit_i32(lo16, lanes[0], lanes[1], 16, 16);
          tcg_gen_deposit_i32(hi16, lanes[2], lanes[3], 16, 16);
          tcg_gen_extu_i32_i64(lo, lo16);
          tcg_gen_extu_i32_i64(hi, hi16);
          tcg_gen_shli_i64(hi, hi, 32);
          tcg_gen_or_i64((TCGv_i64)arg[0].out, lo, hi);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- add24s (saturating) --- */
    static void translate_ae_add24s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[2].in;

        tcg_gen_movi_i32(overflow, 0);
        TCGv_i32 lo = ae_sat_add24(ae_extract_lane24(lhs, 0),
                                   ae_extract_lane24(rhs, 0), overflow);
        TCGv_i32 hi = ae_sat_add24(ae_extract_lane24(lhs, 1),
                                   ae_extract_lane24(rhs, 1), overflow);
        ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- add32 (wrapping) --- */
    static void translate_ae_add32(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 rlo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        TCGv_i32 rhi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
        TCGv_i32 res_lo = tcg_temp_new_i32();
        TCGv_i32 res_hi = tcg_temp_new_i32();

        tcg_gen_add_i32(res_lo, lo, rlo);
        tcg_gen_add_i32(res_hi, hi, rhi);
        ae_pack_lanes32((TCGv_i64)arg[0].out, res_lo, res_hi);
      }
    }

    /* --- add32s (saturating) --- */
    static void translate_ae_add32s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 rlo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        TCGv_i32 rhi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);

        tcg_gen_movi_i32(overflow, 0);
        lo = ae_sat_add32(lo, rlo, overflow);
        hi = ae_sat_add32(hi, rhi, overflow);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- sub16 (wrapping) --- */
    static void translate_ae_sub16(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[2].in;
        TCGv_i64 mask = tcg_constant_i64(0x0000ffff0000ffffULL);
        TCGv_i64 lhs_lo = tcg_temp_new_i64();
        TCGv_i64 rhs_lo = tcg_temp_new_i64();
        TCGv_i64 lhs_hi = tcg_temp_new_i64();
        TCGv_i64 rhs_hi = tcg_temp_new_i64();
        TCGv_i64 diff_lo = tcg_temp_new_i64();
        TCGv_i64 diff_hi = tcg_temp_new_i64();
        TCGv_i64 result = tcg_temp_new_i64();

        tcg_gen_and_i64(lhs_lo, lhs, mask);
        tcg_gen_and_i64(rhs_lo, rhs, mask);
        tcg_gen_sub_i64(diff_lo, lhs_lo, rhs_lo);
        tcg_gen_and_i64(diff_lo, diff_lo, mask);

        tcg_gen_shri_i64(lhs_hi, lhs, 16);
        tcg_gen_shri_i64(rhs_hi, rhs, 16);
        tcg_gen_and_i64(lhs_hi, lhs_hi, mask);
        tcg_gen_and_i64(rhs_hi, rhs_hi, mask);
        tcg_gen_sub_i64(diff_hi, lhs_hi, rhs_hi);
        tcg_gen_and_i64(diff_hi, diff_hi, mask);
        tcg_gen_shli_i64(diff_hi, diff_hi, 16);
        tcg_gen_or_i64(result, diff_lo, diff_hi);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, result);
      }
    }

    /* --- sub16s (saturating) --- */
    static void translate_ae_sub16s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[2].in;
        unsigned lane;
        TCGv_i32 lanes[4];

        tcg_gen_movi_i32(overflow, 0);
        for (lane = 0; lane < 4; ++lane) {
          lanes[lane] = ae_sat_sub16(ae_extract_lane16s(lhs, lane),
                                     ae_extract_lane16s(rhs, lane), overflow);
        }
        {
          TCGv_i64 lo = tcg_temp_new_i64();
          TCGv_i64 hi = tcg_temp_new_i64();
          TCGv_i32 lo16 = tcg_temp_new_i32();
          TCGv_i32 hi16 = tcg_temp_new_i32();
          tcg_gen_deposit_i32(lo16, lanes[0], lanes[1], 16, 16);
          tcg_gen_deposit_i32(hi16, lanes[2], lanes[3], 16, 16);
          tcg_gen_extu_i32_i64(lo, lo16);
          tcg_gen_extu_i32_i64(hi, hi16);
          tcg_gen_shli_i64(hi, hi, 32);
          tcg_gen_or_i64((TCGv_i64)arg[0].out, lo, hi);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- sub24s (saturating) --- */
    static void translate_ae_sub24s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 lhs = (TCGv_i64)arg[1].in;
        TCGv_i64 rhs = (TCGv_i64)arg[2].in;

        tcg_gen_movi_i32(overflow, 0);
        TCGv_i32 lo = ae_sat_sub24(ae_extract_lane24(lhs, 0),
                                   ae_extract_lane24(rhs, 0), overflow);
        TCGv_i32 hi = ae_sat_sub24(ae_extract_lane24(lhs, 1),
                                   ae_extract_lane24(rhs, 1), overflow);
        ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- sub32 (wrapping) --- */
    static void translate_ae_sub32(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 rlo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        TCGv_i32 rhi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
        TCGv_i32 res_lo = tcg_temp_new_i32();
        TCGv_i32 res_hi = tcg_temp_new_i32();

        tcg_gen_sub_i32(res_lo, lo, rlo);
        tcg_gen_sub_i32(res_hi, hi, rhi);
        ae_pack_lanes32((TCGv_i64)arg[0].out, res_lo, res_hi);
      }
    }

    /* --- sub32s (saturating) --- */
    static void translate_ae_sub32s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 rlo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        TCGv_i32 rhi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);

        tcg_gen_movi_i32(overflow, 0);
        lo = ae_sat_sub32(lo, rlo, overflow);
        hi = ae_sat_sub32(hi, rhi, overflow);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- sat16x4: pack 2×ae_int32x2 → ae_int16x4 with saturation --- */
    static void translate_ae_sat16x4(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
    {
      /* arg[0]=dst_16x4, arg[1]=hi_32x2 (lanes 3,2), arg[2]=lo_32x2 (lanes 1,0) */
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lanes[4];

        tcg_gen_movi_i32(overflow, 0);
        /* lo register supplies lanes 0 and 1 */
        lanes[0] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[2].in, 0), overflow);
        lanes[1] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[2].in, 1), overflow);
        /* hi register supplies lanes 2 and 3 */
        lanes[2] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[1].in, 0), overflow);
        lanes[3] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[1].in, 1), overflow);
        {
          TCGv_i64 lo = tcg_temp_new_i64();
          TCGv_i64 hi = tcg_temp_new_i64();
          TCGv_i32 lo16 = tcg_temp_new_i32();
          TCGv_i32 hi16 = tcg_temp_new_i32();
          tcg_gen_deposit_i32(lo16, lanes[0], lanes[1], 16, 16);
          tcg_gen_deposit_i32(hi16, lanes[2], lanes[3], 16, 16);
          tcg_gen_extu_i32_i64(lo, lo16);
          tcg_gen_extu_i32_i64(hi, hi16);
          tcg_gen_shli_i64(hi, hi, 32);
          tcg_gen_or_i64((TCGv_i64)arg[0].out, lo, hi);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- sat24s: clamp each 32-bit lane to 24-bit range --- */
    static void translate_ae_sat24s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i32 lo = ae_extract_lane32(src, 0);
        TCGv_i32 hi = ae_extract_lane32(src, 1);

        tcg_gen_movi_i32(overflow, 0);
        lo = ae_sat24_from_i32(lo, overflow);
        hi = ae_sat24_from_i32(hi, overflow);
        ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- sat48s: clamp 64-bit to 48-bit signed range --- */
    static void translate_ae_sat48s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 result;

        tcg_gen_movi_i32(overflow, 0);
        result = ae_sat48_from_i64((TCGv_i64)arg[1].in, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, result);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- sat64s: saturate 72-bit {arg[1]=ep, arg[2]=dr} to 64-bit --- */
    static void translate_ae_sat64s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
    {
      /* arg[0]=dst_dr, arg[1]=src_ep (i32), arg[2]=src_dr (i64) */
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 result;

        tcg_gen_movi_i32(overflow, 0);
        result = ae_sat64_from_72((TCGv_i32)arg[1].in,
                                  (TCGv_i64)arg[2].in, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, result);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    /* --- satq56s: clamp 64-bit to q56 range (reuse ae_sat56_from_i64) --- */
    static void translate_ae_satq56s(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 result;

        tcg_gen_movi_i32(overflow, 0);
        result = ae_sat56_from_i64((TCGv_i64)arg[1].in, overflow);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, result);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

/* ================================================================
 * ae_div64d32.h / ae_div64d32.l
 * One step of Xtensa HiFi non-restoring binary division.
 *   arg[0]: ae_int64 d  (inout) - partial remainder / accumulated quotient
 *   arg[1]: ae_int32x2  (in)   - divisor source register
 *   par[0]: 0 = use D1[63:32] (high lane .H), 1 = use D1[31:0] (low lane .L)
 *
 * Algorithm derived from the official HiFi-2 cstub C model:
 *   shifted   = d << 1
 *   new_hi    = (uint32_t)(shifted >> 32) - divisor32
 *   sub_result= (uint64_t)(uint32_t)new_hi << 32 | (uint32_t)shifted
 *   sub_sign  = new_hi >> 31          # 1 if subtraction overflowed
 *   if !sub_sign: tn4 = sub_result >> 1; qbit = 1
 *   else:         tn4 = d & ~(1<<63); qbit = 0
 *   d_out = (tn4 << 1) | qbit
 * ================================================================ */
static void translate_ae_div64d32(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (!arg[0].in || !arg[0].out || !arg[1].in) {
        return;
    }
    /* par[0]: 0 = high lane of d1, 1 = low lane of d1 */
    TCGv_i64 d1_in = (TCGv_i64)arg[1].in;

    /* Copy d_in to a temp to handle possible in==out aliasing */
    TCGv_i64 d_val = tcg_temp_new_i64();
    tcg_gen_mov_i64(d_val, (TCGv_i64)arg[0].in);

    /* Extract divisor from the appropriate 32-bit lane of d1 */
    TCGv_i32 divisor32 = tcg_temp_new_i32();
    if (par[0] == 0) {
        tcg_gen_extrh_i64_i32(divisor32, d1_in); /* .H: high 32 bits */
    } else {
        tcg_gen_extrl_i64_i32(divisor32, d1_in); /* .L: low 32 bits */
    }

    /* Step 1: shifted = d_val << 1 (64-bit) */
    TCGv_i64 shifted = tcg_temp_new_i64();
    tcg_gen_shli_i64(shifted, d_val, 1);

    /* Step 2: shifted_hi = (uint32_t)(shifted >> 32) */
    TCGv_i32 shifted_hi = tcg_temp_new_i32();
    tcg_gen_extrh_i64_i32(shifted_hi, shifted);

    /* Step 3: new_hi = shifted_hi - divisor32 (32-bit unsigned wrap) */
    TCGv_i32 new_hi = tcg_temp_new_i32();
    tcg_gen_sub_i32(new_hi, shifted_hi, divisor32);

    /* Step 4: sub_result = ((uint64_t)(uint32_t)new_hi << 32) | (uint32_t)shifted */
    TCGv_i32 shifted_lo = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(shifted_lo, shifted);
    TCGv_i64 sub_result = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(sub_result, shifted_lo, new_hi);

    /* sub_sign = new_hi >> 31: 1 if upper subtraction underflowed (negative) */
    TCGv_i32 sub_sign32 = tcg_temp_new_i32();
    tcg_gen_shri_i32(sub_sign32, new_hi, 31);
    TCGv_i64 sub_sign = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sub_sign, sub_sign32);

    /* Step 5: tn4_pos = sub_result >> 1 (unsigned) — used when sub_sign == 0 */
    TCGv_i64 tn4_pos = tcg_temp_new_i64();
    tcg_gen_shri_i64(tn4_pos, sub_result, 1);

    /* tn4_restore = d_val & ~(1ULL<<63): clear bit 63 — used when sub_sign == 1 */
    TCGv_i64 tn4_restore = tcg_temp_new_i64();
    tcg_gen_andi_i64(tn4_restore, d_val, (uint64_t)0x7FFFFFFFFFFFFFFFULL);

    /* Select tn4: if sub_sign == 0 -> tn4_pos, else -> tn4_restore */
    TCGv_i64 tn4 = tcg_temp_new_i64();
    tcg_gen_movcond_i64(TCG_COND_EQ, tn4,
                        sub_sign, tcg_constant_i64(0),
                        tn4_pos, tn4_restore);

    /* qbit = !sub_sign = 1 if successful (sub_sign XOR 1) */
    TCGv_i64 qbit = tcg_temp_new_i64();
    tcg_gen_xori_i64(qbit, sub_sign, 1);

    /* Step 7: d_out = (tn4 << 1) | qbit */
    TCGv_i64 d_out = (TCGv_i64)arg[0].out;
    tcg_gen_shli_i64(d_out, tn4, 1);
    tcg_gen_or_i64(d_out, d_out, qbit);
}

/* ================================================================
 * ae_sha32 / ae_nsa64 / ae_nsaz* / ae_clamps16
 * ================================================================ */

/* ae_sha32(arr_out, ars_in): normalized shift amount for 32-bit value */
static void translate_ae_sha32(DisasContext *dc, const OpcodeArg arg[],
                const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 nsa = ae_nsa32(arg[1].in);
    tcg_gen_mov_i32(arg[0].out, nsa);
  }
}

/* ae_nsa64(arr_out, aed_in): normalized shift amount for 64-bit signed value */
static TCGv_i32 ae_nsa64_compute(TCGv_i64 src)
{
  TCGv_i64 sign = tcg_temp_new_i64();
  TCGv_i64 norm = tcg_temp_new_i64();
  TCGv_i64 clz  = tcg_temp_new_i64();
  TCGv_i32 nsa  = tcg_temp_new_i32();

  tcg_gen_sari_i64(sign, src, 63);
  tcg_gen_xor_i64(norm, src, sign);
  tcg_gen_clzi_i64(clz, norm, 64);
  tcg_gen_subi_i64(clz, clz, 1);
  tcg_gen_extrl_i64_i32(nsa, clz);
  return nsa;
}

static void translate_ae_nsa64(DisasContext *dc, const OpcodeArg arg[],
                const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 nsa = ae_nsa64_compute((TCGv_i64)arg[1].in);
    tcg_gen_mov_i32(arg[0].out, nsa);
  }
}

/* ae_nsaz32.l(arr_out, aed_in): CLZ of lower 32-bit lane (unsigned) */
static void translate_ae_nsaz32l(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 val = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(val, (TCGv_i64)arg[1].in);
    tcg_gen_clzi_i32(arg[0].out, val, 32);
  }
}

/* ae_nsaz16.0(arr_out, aed_in): CLZ of lower 16-bit lane (0..16) */
static void translate_ae_nsaz16_0(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 val = tcg_temp_new_i32();
    TCGv_i32 clz = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(val, (TCGv_i64)arg[1].in);
    tcg_gen_andi_i32(val, val, 0xffff);
    tcg_gen_clzi_i32(clz, val, 32);
    tcg_gen_subi_i32(arg[0].out, clz, 16);
  }
}

/* ae_clamps16(art_out, ars_in): clamp AR to int16 range; no overflow flag */
static void translate_ae_clamps16(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 t = tcg_temp_new_i32();
    tcg_gen_movcond_i32(TCG_COND_GT, t, arg[1].in, tcg_constant_i32(32767),
              tcg_constant_i32(32767), arg[1].in);
    tcg_gen_movcond_i32(TCG_COND_LT, arg[0].out, t, tcg_constant_i32(-32768),
              tcg_constant_i32(-32768), t);
  }
}

/* ================================================================
 * 32-bit two-lane shifts (non-saturating)
 * ================================================================ */

static void translate_ae_slai32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_shli_i32(lo, lo, sh);
    tcg_gen_shli_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_srai32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_sari_i32(lo, lo, sh);
    tcg_gen_sari_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_srli32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_shri_i32(lo, lo, sh);
    tcg_gen_shri_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_slaa32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, arg[2].in, 31);
    tcg_gen_shl_i32(lo, lo, sh);
    tcg_gen_shl_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_sraa32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, arg[2].in, 31);
    tcg_gen_sar_i32(lo, lo, sh);
    tcg_gen_sar_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_srla32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, arg[2].in, 31);
    tcg_gen_shr_i32(lo, lo, sh);
    tcg_gen_shr_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

/* ================================================================
 * 24-bit two-lane shifts (non-saturating)
 * ================================================================ */

static TCGv_i32 ae_sat24_lshv(TCGv_i32 val, TCGv_i32 sh32, TCGv_i32 overflow);

static void translate_ae_slai24(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);
    tcg_gen_shli_i32(lo, lo, sh);
    tcg_gen_shli_i32(hi, hi, sh);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_slai24s(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned imm = arg[2].imm & 31;
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);

    tcg_gen_movi_i32(overflow, 0);
    tcg_gen_movi_i32(sh, imm);
    lo = ae_sat24_lshv(lo, sh, overflow);
    hi = ae_sat24_lshv(hi, sh, overflow);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_srai24(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);
    tcg_gen_sari_i32(lo, lo, sh);
    tcg_gen_sari_i32(hi, hi, sh);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_srli24(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);
    /* logical shift: treat as unsigned 24-bit */
    tcg_gen_andi_i32(lo, lo, 0x00ffffff);
    tcg_gen_andi_i32(hi, hi, 0x00ffffff);
    tcg_gen_shri_i32(lo, lo, sh);
    tcg_gen_shri_i32(hi, hi, sh);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
  }
}

/* ================================================================
 * 16-bit four-lane arithmetic right shift by immediate (non-saturating)
 * ae_srai16: 4x16-bit lanes, arithmetic right shift, ae_osa16 (0..15)
 * ================================================================ */
static void translate_ae_srai16(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 15;
    TCGv_i32 lo32 = tcg_temp_new_i32();
    TCGv_i32 hi32 = tcg_temp_new_i32();
    TCGv_i32 l0   = tcg_temp_new_i32();
    TCGv_i32 l1   = tcg_temp_new_i32();
    TCGv_i32 l2   = tcg_temp_new_i32();
    TCGv_i32 l3   = tcg_temp_new_i32();
    TCGv_i64 lo64 = tcg_temp_new_i64();
    TCGv_i64 hi64 = tcg_temp_new_i64();

    tcg_gen_extrl_i64_i32(lo32, (TCGv_i64)arg[1].in);
    tcg_gen_extrh_i64_i32(hi32, (TCGv_i64)arg[1].in);

    tcg_gen_ext16s_i32(l0, lo32);
    tcg_gen_sari_i32(l0, l0, sh);
    tcg_gen_sari_i32(l1, lo32, 16);
    tcg_gen_sari_i32(l1, l1, sh);
    tcg_gen_ext16s_i32(l2, hi32);
    tcg_gen_sari_i32(l2, l2, sh);
    tcg_gen_sari_i32(l3, hi32, 16);
    tcg_gen_sari_i32(l3, l3, sh);

    tcg_gen_deposit_i32(lo32, l0, l1, 16, 16);
    tcg_gen_deposit_i32(hi32, l2, l3, 16, 16);
    tcg_gen_extu_i32_i64(lo64, lo32);
    tcg_gen_extu_i32_i64(hi64, hi32);
    tcg_gen_shli_i64(hi64, hi64, 32);
    tcg_gen_or_i64((TCGv_i64)arg[0].out, lo64, hi64);
  }
}

static void translate_ae_srai16r(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 15;
    TCGv_i32 lo32 = tcg_temp_new_i32();
    TCGv_i32 hi32 = tcg_temp_new_i32();
    TCGv_i64 lo64 = tcg_temp_new_i64();
    TCGv_i64 hi64 = tcg_temp_new_i64();
    TCGv_i32 lanes[4];
    int lane;

    for (lane = 0; lane < 4; lane++) {
      TCGv_i32 orig = ae_extract_lane16s((TCGv_i64)arg[1].in, lane);

      lanes[lane] = tcg_temp_new_i32();
      tcg_gen_sari_i32(lanes[lane], orig, sh);
      if (sh != 0) {
        TCGv_i32 rnd = tcg_temp_new_i32();

        tcg_gen_sari_i32(rnd, orig, sh - 1);
        tcg_gen_andi_i32(rnd, rnd, 1);
        tcg_gen_add_i32(lanes[lane], lanes[lane], rnd);
        tcg_gen_movcond_i32(TCG_COND_GT, lanes[lane], lanes[lane],
                            tcg_constant_i32(32767),
                            tcg_constant_i32(32767), lanes[lane]);
      }
    }

    tcg_gen_deposit_i32(lo32, lanes[0], lanes[1], 16, 16);
    tcg_gen_deposit_i32(hi32, lanes[2], lanes[3], 16, 16);
    tcg_gen_extu_i32_i64(lo64, lo32);
    tcg_gen_extu_i32_i64(hi64, hi32);
    tcg_gen_shli_i64(hi64, hi64, 32);
    tcg_gen_or_i64((TCGv_i64)arg[0].out, lo64, hi64);
  }
}

static void translate_ae_srai32r(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);

    tcg_gen_sari_i32(lo, lo, sh);
    tcg_gen_sari_i32(hi, hi, sh);
    if (sh != 0) {
      TCGv_i32 rnd_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
      TCGv_i32 rnd_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);

      tcg_gen_sari_i32(rnd_lo, rnd_lo, sh - 1);
      tcg_gen_andi_i32(rnd_lo, rnd_lo, 1);
      tcg_gen_sari_i32(rnd_hi, rnd_hi, sh - 1);
      tcg_gen_andi_i32(rnd_hi, rnd_hi, 1);
      tcg_gen_add_i32(lo, lo, rnd_lo);
      tcg_gen_add_i32(hi, hi, rnd_hi);
    }
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

/* ================================================================
 * AE_SAR-based 24/32/64-bit shifts (non-saturating)
 * ================================================================ */

static void translate_ae_slas24(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    tcg_gen_shl_i32(lo, lo, sh);
    tcg_gen_shl_i32(hi, hi, sh);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_sras24(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    tcg_gen_sar_i32(lo, lo, sh);
    tcg_gen_sar_i32(hi, hi, sh);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_srls24(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    tcg_gen_andi_i32(lo, lo, 0x00ffffff);
    tcg_gen_andi_i32(hi, hi, 0x00ffffff);
    tcg_gen_shr_i32(lo, lo, sh);
    tcg_gen_shr_i32(hi, hi, sh);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_slas32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    tcg_gen_shl_i32(lo, lo, sh);
    tcg_gen_shl_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_sras32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    tcg_gen_sar_i32(lo, lo, sh);
    tcg_gen_sar_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_srls32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    tcg_gen_shr_i32(lo, lo, sh);
    tcg_gen_shr_i32(hi, hi, sh);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
  }
}

static void translate_ae_slas64(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i64 sh = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh, cpu_AE_SAR);
    tcg_gen_andi_i64(sh, sh, 63);
    tcg_gen_shl_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, sh);
  }
}

static void translate_ae_sras64(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i64 sh = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh, cpu_AE_SAR);
    tcg_gen_andi_i64(sh, sh, 63);
    tcg_gen_sar_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, sh);
  }
}

static void translate_ae_srls64(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i64 sh = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh, cpu_AE_SAR);
    tcg_gen_andi_i64(sh, sh, 63);
    tcg_gen_shr_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, sh);
  }
}

/* ae_sraa64: 64-bit arithmetic right shift by register amount */
static void translate_ae_sraa64(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i64 sh = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh, arg[2].in);
    tcg_gen_andi_i64(sh, sh, 63);
    tcg_gen_sar_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, sh);
  }
}

/* ================================================================
 * Saturating shift helpers
 * ================================================================ */

/*
 * ae_sat32_lshi: sat left shift of 32-bit signed value by immediate.
 * Uses 64-bit arithmetic to detect 32-bit overflow.
 */
static TCGv_i32 ae_sat32_lshi(TCGv_i32 val, unsigned sh, TCGv_i32 overflow)
{
  TCGv_i64 v64  = tcg_temp_new_i64();
  TCGv_i64 s64  = tcg_temp_new_i64();
  TCGv_i64 gt64 = tcg_temp_new_i64();
  TCGv_i64 lt64 = tcg_temp_new_i64();
  TCGv_i32 gt32 = tcg_temp_new_i32();
  TCGv_i32 lt32 = tcg_temp_new_i32();
  TCGv_i32 res  = tcg_temp_new_i32();

  tcg_gen_ext_i32_i64(v64, val);
  tcg_gen_shli_i64(s64, v64, sh);
  tcg_gen_setcondi_i64(TCG_COND_GT, gt64, s64, (int64_t)INT32_MAX);
  tcg_gen_setcondi_i64(TCG_COND_LT, lt64, s64, (int64_t)INT32_MIN);
  tcg_gen_extrl_i64_i32(gt32, gt64);
  tcg_gen_extrl_i64_i32(lt32, lt64);
  tcg_gen_or_i32(overflow, overflow, gt32);
  tcg_gen_or_i32(overflow, overflow, lt32);
  tcg_gen_movcond_i64(TCG_COND_NE, s64, gt64, tcg_constant_i64(0),
            tcg_constant_i64(INT32_MAX), s64);
  tcg_gen_movcond_i64(TCG_COND_NE, s64, lt64, tcg_constant_i64(0),
            tcg_constant_i64(INT32_MIN), s64);
  tcg_gen_extrl_i64_i32(res, s64);
  return res;
}

/*
 * ae_sat32_lshv: sat left shift of 32-bit signed value by variable amount.
 * sh32 must already be masked to the valid range by the caller.
 */
static TCGv_i32 ae_sat32_lshv(TCGv_i32 val, TCGv_i32 sh32, TCGv_i32 overflow)
{
  TCGv_i64 v64  = tcg_temp_new_i64();
  TCGv_i64 sh64 = tcg_temp_new_i64();
  TCGv_i64 s64  = tcg_temp_new_i64();
  TCGv_i64 gt64 = tcg_temp_new_i64();
  TCGv_i64 lt64 = tcg_temp_new_i64();
  TCGv_i32 gt32 = tcg_temp_new_i32();
  TCGv_i32 lt32 = tcg_temp_new_i32();
  TCGv_i32 res  = tcg_temp_new_i32();

  tcg_gen_ext_i32_i64(v64, val);
  tcg_gen_extu_i32_i64(sh64, sh32);
  tcg_gen_shl_i64(s64, v64, sh64);
  tcg_gen_setcondi_i64(TCG_COND_GT, gt64, s64, (int64_t)INT32_MAX);
  tcg_gen_setcondi_i64(TCG_COND_LT, lt64, s64, (int64_t)INT32_MIN);
  tcg_gen_extrl_i64_i32(gt32, gt64);
  tcg_gen_extrl_i64_i32(lt32, lt64);
  tcg_gen_or_i32(overflow, overflow, gt32);
  tcg_gen_or_i32(overflow, overflow, lt32);
  tcg_gen_movcond_i64(TCG_COND_NE, s64, gt64, tcg_constant_i64(0),
            tcg_constant_i64(INT32_MAX), s64);
  tcg_gen_movcond_i64(TCG_COND_NE, s64, lt64, tcg_constant_i64(0),
            tcg_constant_i64(INT32_MIN), s64);
  tcg_gen_extrl_i64_i32(res, s64);
  return res;
}

/*
 * ae_sat24_lshv: sat left shift of 24-bit signed value by variable amount.
 * val is sign-extended from 24 bits into an i32.
 * Returns value clamped to [-2^23, 2^23-1].
 */
static TCGv_i32 ae_sat24_lshv(TCGv_i32 val, TCGv_i32 sh32, TCGv_i32 overflow)
{
  TCGv_i64 v64  = tcg_temp_new_i64();
  TCGv_i64 sh64 = tcg_temp_new_i64();
  TCGv_i64 s64  = tcg_temp_new_i64();
  TCGv_i64 gt64 = tcg_temp_new_i64();
  TCGv_i64 lt64 = tcg_temp_new_i64();
  TCGv_i32 gt32 = tcg_temp_new_i32();
  TCGv_i32 lt32 = tcg_temp_new_i32();
  TCGv_i32 res  = tcg_temp_new_i32();

  tcg_gen_ext_i32_i64(v64, val);
  tcg_gen_extu_i32_i64(sh64, sh32);
  tcg_gen_shl_i64(s64, v64, sh64);
  tcg_gen_setcondi_i64(TCG_COND_GT, gt64, s64, (int64_t)0x7fffffLL);
  tcg_gen_setcondi_i64(TCG_COND_LT, lt64, s64, (int64_t)-0x800000LL);
  tcg_gen_extrl_i64_i32(gt32, gt64);
  tcg_gen_extrl_i64_i32(lt32, lt64);
  tcg_gen_or_i32(overflow, overflow, gt32);
  tcg_gen_or_i32(overflow, overflow, lt32);
  tcg_gen_movcond_i64(TCG_COND_NE, s64, gt64, tcg_constant_i64(0),
            tcg_constant_i64(0x7fffffLL), s64);
  tcg_gen_movcond_i64(TCG_COND_NE, s64, lt64, tcg_constant_i64(0),
            tcg_constant_i64(-0x800000LL), s64);
  tcg_gen_extrl_i64_i32(res, s64);
  return res;
}

/*
 * ae_sat64_lshi: saturating left shift of 64-bit signed value by immediate.
 * Uses round-trip check: if sar(shl(val,sh),sh) != val, overflow occurred.
 */
static TCGv_i64 ae_sat64_lshi(TCGv_i64 val, unsigned sh, TCGv_i32 overflow)
{
  TCGv_i64 s    = tcg_temp_new_i64();
  TCGv_i64 back = tcg_temp_new_i64();
  TCGv_i64 ne64 = tcg_temp_new_i64();
  TCGv_i32 ne32 = tcg_temp_new_i32();
  TCGv_i64 sign = tcg_temp_new_i64();
  TCGv_i64 satv = tcg_temp_new_i64();
  TCGv_i64 res  = tcg_temp_new_i64();

  if (sh == 0) {
    return val;
  }
  tcg_gen_shli_i64(s, val, sh);
  tcg_gen_sari_i64(back, s, sh);
  tcg_gen_setcond_i64(TCG_COND_NE, ne64, back, val);
  tcg_gen_extrl_i64_i32(ne32, ne64);
  tcg_gen_or_i32(overflow, overflow, ne32);
  /* sat value: INT64_MAX if val >= 0, INT64_MIN if val < 0 */
  tcg_gen_sari_i64(sign, val, 63);
  tcg_gen_xori_i64(satv, sign, INT64_MAX);
  tcg_gen_movcond_i64(TCG_COND_NE, res, ne64, tcg_constant_i64(0), satv, s);
  return res;
}

/*
 * ae_sat64_lshv: saturating left shift of 64-bit signed value by variable amount.
 * sh32 must already be masked to valid range by caller.
 */
static TCGv_i64 ae_sat64_lshv(TCGv_i64 val, TCGv_i32 sh32, TCGv_i32 overflow)
{
  TCGv_i64 sh64 = tcg_temp_new_i64();
  TCGv_i64 s    = tcg_temp_new_i64();
  TCGv_i64 back = tcg_temp_new_i64();
  TCGv_i64 ne64 = tcg_temp_new_i64();
  TCGv_i32 ne32 = tcg_temp_new_i32();
  TCGv_i64 sign = tcg_temp_new_i64();
  TCGv_i64 satv = tcg_temp_new_i64();
  TCGv_i64 res  = tcg_temp_new_i64();

  tcg_gen_extu_i32_i64(sh64, sh32);
  tcg_gen_shl_i64(s, val, sh64);
  tcg_gen_sar_i64(back, s, sh64);
  tcg_gen_setcond_i64(TCG_COND_NE, ne64, back, val);
  tcg_gen_extrl_i64_i32(ne32, ne64);
  tcg_gen_or_i32(overflow, overflow, ne32);
  tcg_gen_sari_i64(sign, val, 63);
  tcg_gen_xori_i64(satv, sign, INT64_MAX);
  tcg_gen_movcond_i64(TCG_COND_NE, res, ne64, tcg_constant_i64(0), satv, s);
  return res;
}

/* ================================================================
 * Immediate saturating left shifts
 * ================================================================ */

static void translate_ae_slai16s(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 15;
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 lo32 = tcg_temp_new_i32();
    TCGv_i32 hi32 = tcg_temp_new_i32();
    TCGv_i64 lo64 = tcg_temp_new_i64();
    TCGv_i64 hi64 = tcg_temp_new_i64();
    TCGv_i32 results[4];
    int lane;

    tcg_gen_movi_i32(overflow, 0);
    for (lane = 0; lane < 4; lane++) {
      TCGv_i32 v = ae_extract_lane16s((TCGv_i64)arg[1].in, lane);
      TCGv_i32 shifted = tcg_temp_new_i32();
      TCGv_i32 sat = tcg_temp_new_i32();
      TCGv_i32 ovf_bit = tcg_temp_new_i32();

      tcg_gen_shli_i32(shifted, v, sh);
      tcg_gen_movcond_i32(TCG_COND_GT, sat, shifted, tcg_constant_i32(32767),
                          tcg_constant_i32(32767), shifted);
      tcg_gen_movcond_i32(TCG_COND_LT, sat, sat, tcg_constant_i32(-32768),
                          tcg_constant_i32(-32768), sat);
      tcg_gen_setcond_i32(TCG_COND_NE, ovf_bit, sat, shifted);
      tcg_gen_or_i32(overflow, overflow, ovf_bit);
      results[lane] = sat;
    }

    tcg_gen_deposit_i32(lo32, results[0], results[1], 16, 16);
    tcg_gen_deposit_i32(hi32, results[2], results[3], 16, 16);
    tcg_gen_extu_i32_i64(lo64, lo32);
    tcg_gen_extu_i32_i64(hi64, hi32);
    tcg_gen_shli_i64(hi64, hi64, 32);
    tcg_gen_or_i64((TCGv_i64)arg[0].out, lo64, hi64);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_slai32s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 31;
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);

    tcg_gen_movi_i32(overflow, 0);
    lo = ae_sat32_lshi(lo, sh, overflow);
    hi = ae_sat32_lshi(hi, sh, overflow);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_slai64s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 63;
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i64 res;

    tcg_gen_movi_i32(overflow, 0);
    res = ae_sat64_lshi((TCGv_i64)arg[1].in, sh, overflow);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, res);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ================================================================
 * Bidirectional variable-amount saturating shifts (16-bit four-lane)
 * - ae_slaa16s: ars >= 0 → sat left; ars < 0 → arith right (no sat)
 * - ae_sraa16s: ars >= 0 → arith right; ars < 0 → sat left
 * - ae_sraa16rs: like sraa16s but rounds on right shifts
 * ================================================================ */

static void ae_bidi16_shift4(TCGv_i64 dst, TCGv_i64 src, TCGv_i32 ars_raw,
                int dir, bool round, TCGv_i32 overflow)
{
  TCGv_i32 is_neg   = tcg_temp_new_i32();
  TCGv_i32 abs_sh   = tcg_temp_new_i32();
  TCGv_i32 sh       = tcg_temp_new_i32();
  TCGv_i32 temp_ovf = tcg_temp_new_i32();
  TCGv_i32 cond_ovf = tcg_temp_new_i32();
  TCGv_i32 lo32 = tcg_temp_new_i32();
  TCGv_i32 hi32 = tcg_temp_new_i32();
  TCGv_i64 lo64 = tcg_temp_new_i64();
  TCGv_i64 hi64 = tcg_temp_new_i64();
  TCGv_i32 results[4];
  int lane;

  /* Compute abs(ars) and direction flag */
  tcg_gen_setcondi_i32(TCG_COND_LT, is_neg, ars_raw, 0);
  tcg_gen_neg_i32(abs_sh, ars_raw);
  tcg_gen_movcond_i32(TCG_COND_NE, abs_sh, is_neg, tcg_constant_i32(0),
            abs_sh, ars_raw);
  /* Clamp shift to [0, 15] for 16-bit lanes */
  tcg_gen_movcond_i32(TCG_COND_GEU, sh, abs_sh, tcg_constant_i32(16),
            tcg_constant_i32(15), abs_sh);
  tcg_gen_movi_i32(temp_ovf, 0);

  for (lane = 0; lane < 4; lane++) {
    TCGv_i32 v       = ae_extract_lane16s(src, lane);
    TCGv_i32 shifted = tcg_temp_new_i32();
    TCGv_i32 sat     = tcg_temp_new_i32();
    TCGv_i32 ovf_bit = tcg_temp_new_i32();
    TCGv_i32 rsh     = tcg_temp_new_i32();

    /* Saturating left shift path */
    tcg_gen_shl_i32(shifted, v, sh);
    tcg_gen_movcond_i32(TCG_COND_GT, sat, shifted,
              tcg_constant_i32(32767),
              tcg_constant_i32(32767), shifted);
    tcg_gen_movcond_i32(TCG_COND_LT, sat, sat,
              tcg_constant_i32(-32768),
              tcg_constant_i32(-32768), sat);
    tcg_gen_setcond_i32(TCG_COND_NE, ovf_bit, sat, shifted);
    tcg_gen_or_i32(temp_ovf, temp_ovf, ovf_bit);

    /* Arithmetic right shift path */
    tcg_gen_sar_i32(rsh, v, sh);
    if (round) {
      TCGv_i32 rnd_pos = tcg_temp_new_i32();
      TCGv_i32 rnd_bit = tcg_temp_new_i32();
      /* rnd_pos = (sh > 0) ? sh - 1 : 0 */
      tcg_gen_subi_i32(rnd_pos, sh, 1);
      tcg_gen_movcond_i32(TCG_COND_EQ, rnd_pos, sh, tcg_constant_i32(0),
                tcg_constant_i32(0), rnd_pos);
      tcg_gen_sar_i32(rnd_bit, v, rnd_pos);
      tcg_gen_andi_i32(rnd_bit, rnd_bit, 1);
      tcg_gen_movcond_i32(TCG_COND_EQ, rnd_bit, sh, tcg_constant_i32(0),
                tcg_constant_i32(0), rnd_bit);
      tcg_gen_add_i32(rsh, rsh, rnd_bit);
      /* Clamp rounding overflow at INT16_MAX */
      tcg_gen_movcond_i32(TCG_COND_GT, rsh, rsh, tcg_constant_i32(32767),
                tcg_constant_i32(32767), rsh);
    }

    results[lane] = tcg_temp_new_i32();
    /* dir > 0: sat when is_neg==0, rsh when is_neg==1 */
    /* dir < 0: rsh when is_neg==0, sat when is_neg==1 */
    if (dir > 0) {
      tcg_gen_movcond_i32(TCG_COND_EQ, results[lane], is_neg,
                tcg_constant_i32(0), sat, rsh);
    } else {
      tcg_gen_movcond_i32(TCG_COND_EQ, results[lane], is_neg,
                tcg_constant_i32(0), rsh, sat);
    }
  }

  /* Apply overflow only when the sat path is active */
  if (dir > 0) {
    /* sat active when is_neg == 0 */
    tcg_gen_movcond_i32(TCG_COND_EQ, cond_ovf, is_neg,
              tcg_constant_i32(0), temp_ovf, tcg_constant_i32(0));
  } else {
    /* sat active when is_neg != 0 */
    tcg_gen_movcond_i32(TCG_COND_NE, cond_ovf, is_neg,
              tcg_constant_i32(0), temp_ovf, tcg_constant_i32(0));
  }
  tcg_gen_or_i32(overflow, overflow, cond_ovf);

  tcg_gen_deposit_i32(lo32, results[0], results[1], 16, 16);
  tcg_gen_deposit_i32(hi32, results[2], results[3], 16, 16);
  tcg_gen_extu_i32_i64(lo64, lo32);
  tcg_gen_extu_i32_i64(hi64, hi32);
  tcg_gen_shli_i64(hi64, hi64, 32);
  tcg_gen_or_i64(dst, lo64, hi64);
}

static void translate_ae_slaa16s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);
    ae_bidi16_shift4((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
             arg[2].in, 1, false, overflow);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_sraa16s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);
    ae_bidi16_shift4((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
             arg[2].in, -1, false, overflow);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_sraa16rs(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);
    ae_bidi16_shift4((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                     arg[2].in, -1, true, overflow);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ================================================================
 * Bidirectional variable-amount saturating shifts (32-bit two-lane)
 * - ae_slaa32s: ars >= 0 -> sat left; ars < 0 -> arith right
 * - ae_sraa32s: ars >= 0 -> arith right; ars < 0 -> sat left
 * - ae_sraa32rs: like sraa32s but rounds on right shifts
 * ================================================================ */
static void ae_bidi32_shift2(TCGv_i64 dst, TCGv_i64 src, TCGv_i32 ars_raw,
                             int dir, bool round, TCGv_i32 overflow)
{
  TCGv_i32 is_neg   = tcg_temp_new_i32();
  TCGv_i32 abs_sh   = tcg_temp_new_i32();
  TCGv_i32 sh       = tcg_temp_new_i32();
  TCGv_i32 temp_ovf = tcg_temp_new_i32();
  TCGv_i32 cond_ovf = tcg_temp_new_i32();
  TCGv_i32 lo = ae_extract_lane32(src, 0);
  TCGv_i32 hi = ae_extract_lane32(src, 1);
  TCGv_i32 lo_sat;
  TCGv_i32 hi_sat;
  TCGv_i32 lo_rsh = tcg_temp_new_i32();
  TCGv_i32 hi_rsh = tcg_temp_new_i32();
  TCGv_i32 lo_res = tcg_temp_new_i32();
  TCGv_i32 hi_res = tcg_temp_new_i32();

  tcg_gen_setcondi_i32(TCG_COND_LT, is_neg, ars_raw, 0);
  tcg_gen_neg_i32(abs_sh, ars_raw);
  tcg_gen_movcond_i32(TCG_COND_NE, abs_sh, is_neg, tcg_constant_i32(0),
                      abs_sh, ars_raw);
  tcg_gen_movcond_i32(TCG_COND_GEU, sh, abs_sh, tcg_constant_i32(32),
                      tcg_constant_i32(31), abs_sh);

  tcg_gen_movi_i32(temp_ovf, 0);
  lo_sat = ae_sat32_lshv(lo, sh, temp_ovf);
  hi_sat = ae_sat32_lshv(hi, sh, temp_ovf);

  tcg_gen_sar_i32(lo_rsh, lo, sh);
  tcg_gen_sar_i32(hi_rsh, hi, sh);
  if (round) {
    TCGv_i32 rnd_pos = tcg_temp_new_i32();
    TCGv_i32 rnd_lo = tcg_temp_new_i32();
    TCGv_i32 rnd_hi = tcg_temp_new_i32();

    tcg_gen_subi_i32(rnd_pos, sh, 1);
    tcg_gen_movcond_i32(TCG_COND_EQ, rnd_pos, sh, tcg_constant_i32(0),
                        tcg_constant_i32(0), rnd_pos);
    tcg_gen_sar_i32(rnd_lo, lo, rnd_pos);
    tcg_gen_andi_i32(rnd_lo, rnd_lo, 1);
    tcg_gen_sar_i32(rnd_hi, hi, rnd_pos);
    tcg_gen_andi_i32(rnd_hi, rnd_hi, 1);
    tcg_gen_movcond_i32(TCG_COND_EQ, rnd_lo, sh, tcg_constant_i32(0),
                        tcg_constant_i32(0), rnd_lo);
    tcg_gen_movcond_i32(TCG_COND_EQ, rnd_hi, sh, tcg_constant_i32(0),
                        tcg_constant_i32(0), rnd_hi);
    tcg_gen_add_i32(lo_rsh, lo_rsh, rnd_lo);
    tcg_gen_add_i32(hi_rsh, hi_rsh, rnd_hi);
  }

  if (dir > 0) {
    tcg_gen_movcond_i32(TCG_COND_EQ, cond_ovf, is_neg, tcg_constant_i32(0),
                        temp_ovf, tcg_constant_i32(0));
  } else {
    tcg_gen_movcond_i32(TCG_COND_NE, cond_ovf, is_neg, tcg_constant_i32(0),
                        temp_ovf, tcg_constant_i32(0));
  }
  tcg_gen_or_i32(overflow, overflow, cond_ovf);

  if (dir > 0) {
    tcg_gen_movcond_i32(TCG_COND_EQ, lo_res, is_neg, tcg_constant_i32(0),
                        lo_sat, lo_rsh);
    tcg_gen_movcond_i32(TCG_COND_EQ, hi_res, is_neg, tcg_constant_i32(0),
                        hi_sat, hi_rsh);
  } else {
    tcg_gen_movcond_i32(TCG_COND_EQ, lo_res, is_neg, tcg_constant_i32(0),
                        lo_rsh, lo_sat);
    tcg_gen_movcond_i32(TCG_COND_EQ, hi_res, is_neg, tcg_constant_i32(0),
                        hi_rsh, hi_sat);
  }

  ae_pack_lanes32(dst, lo_res, hi_res);
}

static void translate_ae_slaa32s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);
    ae_bidi32_shift2((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
             arg[2].in, 1, false, overflow);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_sraa32s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);
    ae_bidi32_shift2((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
             arg[2].in, -1, false, overflow);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_sraa32rs(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);
    ae_bidi32_shift2((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
             arg[2].in, -1, true, overflow);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ================================================================
 * ae_slaa64s: bidirectional 64-bit saturating shift
 * ars >= 0: sat left; ars < 0: arith right (no sat)
 * ================================================================ */
static void translate_ae_slaa64s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow  = tcg_temp_new_i32();
    TCGv_i32 is_neg    = tcg_temp_new_i32();
    TCGv_i32 abs_sh    = tcg_temp_new_i32();
    TCGv_i32 sh        = tcg_temp_new_i32();
    TCGv_i32 temp_ovf  = tcg_temp_new_i32();
    TCGv_i32 cond_ovf  = tcg_temp_new_i32();
    TCGv_i64 sh64      = tcg_temp_new_i64();
    TCGv_i64 sat_res, rsh_res;
    TCGv_i64 is_neg64  = tcg_temp_new_i64();
    TCGv_i64 result    = tcg_temp_new_i64();

    tcg_gen_movi_i32(overflow, 0);
    tcg_gen_setcondi_i32(TCG_COND_LT, is_neg, arg[2].in, 0);
    tcg_gen_neg_i32(abs_sh, arg[2].in);
    tcg_gen_movcond_i32(TCG_COND_NE, abs_sh, is_neg, tcg_constant_i32(0),
              abs_sh, arg[2].in);
    tcg_gen_movcond_i32(TCG_COND_GEU, sh, abs_sh, tcg_constant_i32(64),
              tcg_constant_i32(63), abs_sh);

    tcg_gen_movi_i32(temp_ovf, 0);
    sat_res = ae_sat64_lshv((TCGv_i64)arg[1].in, sh, temp_ovf);

    tcg_gen_extu_i32_i64(sh64, sh);
    rsh_res = tcg_temp_new_i64();
    tcg_gen_sar_i64(rsh_res, (TCGv_i64)arg[1].in, sh64);

    /* Apply overflow only for left sat path (is_neg == 0) */
    tcg_gen_movcond_i32(TCG_COND_EQ, cond_ovf, is_neg,
              tcg_constant_i32(0), temp_ovf, tcg_constant_i32(0));
    tcg_gen_or_i32(overflow, overflow, cond_ovf);

    tcg_gen_extu_i32_i64(is_neg64, is_neg);
    /* is_neg==0 → left sat; is_neg!=0 → right arith */
    tcg_gen_movcond_i64(TCG_COND_EQ, result, is_neg64, tcg_constant_i64(0),
              sat_res, rsh_res);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, result);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ================================================================
 * AE_SAR-based saturating left shifts
 * ================================================================ */

static void translate_ae_slas24s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo, hi;

    tcg_gen_movi_i32(overflow, 0);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    lo = ae_extract_lane24((TCGv_i64)arg[1].in, 0);
    hi = ae_extract_lane24((TCGv_i64)arg[1].in, 1);
    lo = ae_sat24_lshv(lo, sh, overflow);
    hi = ae_sat24_lshv(hi, sh, overflow);
    ae_pack_lanes24((TCGv_i64)arg[0].out, lo, hi);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_slas32s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 sh = tcg_temp_new_i32();
    TCGv_i32 lo, hi;

    tcg_gen_movi_i32(overflow, 0);
    tcg_gen_andi_i32(sh, cpu_AE_SAR, 31);
    lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    lo = ae_sat32_lshv(lo, sh, overflow);
    hi = ae_sat32_lshv(hi, sh, overflow);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

static void translate_ae_slas64s(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 sh32 = tcg_temp_new_i32();
    TCGv_i64 res;

    tcg_gen_movi_i32(overflow, 0);
    tcg_gen_andi_i32(sh32, cpu_AE_SAR, 63);
    res = ae_sat64_lshv((TCGv_i64)arg[1].in, sh32, overflow);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, res);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ================================================================
 * Q56 shifts (56-bit signed integer shifts with AE_SAR or register shift amount)
 * Q56 values are 56-bit signed, stored in 64-bit registers
 * ================================================================ */

/* ae_slasq56: shift left arithmetic with AE_SAR (non-saturating) */
static void translate_ae_slasq56(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh32 = tcg_temp_new_i32();
    TCGv_i64 sh64 = tcg_temp_new_i64();
    tcg_gen_andi_i32(sh32, cpu_AE_SAR, 0x3f);
    tcg_gen_umin_i32(sh32, sh32, tcg_constant_i32(55));
    tcg_gen_extu_i32_i64(sh64, sh32);
    tcg_gen_shl_i64(arg[0].out, arg[1].in, sh64);
  }
}

/* ae_srlsq56: shift right logical with AE_SAR (non-saturating) */
static void translate_ae_srlsq56(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh32 = tcg_temp_new_i32();
    TCGv_i64 sh64 = tcg_temp_new_i64();
    tcg_gen_andi_i32(sh32, cpu_AE_SAR, 0x3f);
    tcg_gen_umin_i32(sh32, sh32, tcg_constant_i32(55));
    tcg_gen_extu_i32_i64(sh64, sh32);
    tcg_gen_shr_i64(arg[0].out, arg[1].in, sh64);
  }
}

/* ae_srasq56: shift right arithmetic with AE_SAR (non-saturating) */
static void translate_ae_srasq56(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 sh32 = tcg_temp_new_i32();
    TCGv_i64 sh64 = tcg_temp_new_i64();
    tcg_gen_andi_i32(sh32, cpu_AE_SAR, 0x3f);
    tcg_gen_umin_i32(sh32, sh32, tcg_constant_i32(55));
    tcg_gen_extu_i32_i64(sh64, sh32);
    tcg_gen_sar_i64(arg[0].out, arg[1].in, sh64);
  }
}

/* ae_slaaq56: shift left arithmetic with register shift amount (non-saturating) */
static void translate_ae_slaaq56(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i64 sh = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh, arg[2].in);
    tcg_gen_umin_i64(sh, sh, tcg_constant_i64(55));
    tcg_gen_shl_i64(arg[0].out, arg[1].in, sh);
  }
}

/* ae_srlaq56: shift right logical with register shift amount (non-saturating) */
static void translate_ae_srlaq56(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i64 sh = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh, arg[2].in);
    tcg_gen_umin_i64(sh, sh, tcg_constant_i64(55));
    tcg_gen_shr_i64(arg[0].out, arg[1].in, sh);
  }
}

/* ae_sraaq56: shift right arithmetic with register shift amount (non-saturating) */
static void translate_ae_sraaq56(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i64 sh = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh, arg[2].in);
    tcg_gen_umin_i64(sh, sh, tcg_constant_i64(55));
    tcg_gen_sar_i64(arg[0].out, arg[1].in, sh);
  }
}

/* ae_slaisq56s: shift left with immediate, saturating to Q56 range */
static void translate_ae_slaisq56s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    unsigned sh = arg[2].imm & 55;
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i64 res;
    
    tcg_gen_movi_i32(overflow, 0);
    res = ae_sat56_from_i64((TCGv_i64)arg[1].in, overflow);
    if (sh > 0) {
      res = ae_sat64_lshi(res, sh, overflow);
      res = ae_sat56_from_i64(res, overflow);
    }
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, res);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ae_slassq56s: shift left with AE_SAR, saturating to Q56 range */
static void translate_ae_slassq56s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
  if (arg[0].out && arg[1].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 sh32 = tcg_temp_new_i32();
    TCGv_i64 res;

    tcg_gen_movi_i32(overflow, 0);
    res = ae_sat56_from_i64((TCGv_i64)arg[1].in, overflow);
    tcg_gen_andi_i32(sh32, cpu_AE_SAR, 0x3f);
    tcg_gen_umin_i32(sh32, sh32, tcg_constant_i32(55));
    res = ae_sat64_lshv(res, sh32, overflow);
    res = ae_sat56_from_i64(res, overflow);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, res);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ae_slaasq56s: shift left with register, saturating to Q56 range */
static void translate_ae_slaasq56s(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 sh32 = tcg_temp_new_i32();
    TCGv_i64 sh64 = tcg_temp_new_i64();
    TCGv_i64 res;

    tcg_gen_movi_i32(overflow, 0);
    res = ae_sat56_from_i64((TCGv_i64)arg[1].in, overflow);
    tcg_gen_andi_i32(sh32, arg[2].in, 0x3f);
    tcg_gen_umin_i32(sh32, sh32, tcg_constant_i32(55));
    tcg_gen_extu_i32_i64(sh64, sh32);
    res = ae_sat64_lshv(res, sh32, overflow);
    res = ae_sat56_from_i64(res, overflow);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, res);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/* ae_slai64 / ae_srli64 / ae_srai64 / ae_sra64_32 / ae_srla64 / ae_slaa64 */
static void translate_ae_slai64(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_shli_i64(arg[0].out, arg[1].in, arg[2].imm & 63);
    }
}

static void translate_ae_srli64(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_shri_i64(arg[0].out, arg[1].in, arg[2].imm & 63);
    }
}

static void translate_ae_srai64(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_sari_i64(arg[0].out, arg[1].in, arg[2].imm & 63);
    }
}

static void translate_ae_sra64_32(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 sh = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(sh, arg[2].in);
        tcg_gen_andi_i64(sh, sh, 63);
        tcg_gen_sar_i64(arg[0].out, arg[1].in, sh);
    }
}

static void translate_ae_srla64(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 sh = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(sh, arg[2].in);
        tcg_gen_andi_i64(sh, sh, 63);
        tcg_gen_shr_i64(arg[0].out, arg[1].in, sh);
    }
}

static void translate_ae_slaa64(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 sh = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(sh, arg[2].in);
        tcg_gen_andi_i64(sh, sh, 63);
        tcg_gen_shl_i64(arg[0].out, arg[1].in, sh);
    }
}

/*
 * ae_movda32   aed, ar       => AE_DR = zero_extend_32(AR)
 * ae_movda32x2 aed, arh, arl => AE_DR = (ARh<<32)|ARl
 * ae_movad32.l ar, aes       => AR = AE_DR[31:0]
 * ae_movad32.h ar, aes       => AR = AE_DR[63:32]
 */
static void translate_ae_movda32(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_extu_i32_i64(arg[0].out, arg[1].in);
    }
}

static void translate_ae_movda32x2(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 hi = tcg_temp_new_i64();
        TCGv_i64 lo = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(hi, arg[1].in);
        tcg_gen_extu_i32_i64(lo, arg[2].in);
        tcg_gen_shli_i64(hi, hi, 32);
        tcg_gen_or_i64(arg[0].out, hi, lo);
    }
}

static void translate_ae_movad32l(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_extrl_i64_i32(arg[0].out, arg[1].in);
    }
}

static void translate_ae_movad32h(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_extrh_i64_i32(arg[0].out, arg[1].in);
    }
}

/*
 * ae_movda16   aed, ar   => AE_DR = AR[15:0] (zero-extended)
 * ae_movda16x2 aed, arh, arl
 * ae_movad16.N ar, aes   => AR = sign_extend16(AE_DR[lane*16+15:lane*16])
 *   par[0] = lane number (0=bits[15:0], 1=bits[31:16], ...)
 */
static void translate_ae_movda16(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(tmp, arg[1].in);
        tcg_gen_andi_i64(arg[0].out, tmp, 0xffffULL);
    }
}

static void translate_ae_movda16x2(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 hi = tcg_temp_new_i64();
        TCGv_i64 lo = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(hi, arg[1].in);
        tcg_gen_extu_i32_i64(lo, arg[2].in);
        tcg_gen_andi_i64(hi, hi, 0xffffULL);
        tcg_gen_andi_i64(lo, lo, 0xffffULL);
        tcg_gen_shli_i64(hi, hi, 16);
        tcg_gen_or_i64(arg[0].out, hi, lo);
    }
}

static void translate_ae_movad16(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    unsigned lane = par[0];
    if (arg[0].out && arg[1].in) {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_shri_i64(tmp, arg[1].in, lane * 16);
        tcg_gen_extrl_i64_i32(arg[0].out, tmp);
        tcg_gen_ext16s_i32(arg[0].out, arg[0].out);
    }
}

/* ae_moveep -- copy AE_VALIGN register */
static void translate_ae_moveep(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_mov_i64(arg[0].out, arg[1].in);
    }
}

/*
 * ae_movab / ae_movab2 / ae_movab4 -- BR -> AR
 * Baseline model: preserve boolean payload width in low bits.
 * par[0]: boolean width in bits (1, 2, 4); defaults to 4.
 */
static void translate_ae_movab(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
  uint32_t width = par ? par[0] : 4;
  uint32_t mask = (width >= 32) ? 0xffffffffu : ((1u << width) - 1u);

  (void)dc;

  if (!arg[0].out) {
    return;
  }

  if (arg[1].in) {
    tcg_gen_andi_i32(arg[0].out, arg[1].in, mask);
  } else {
    tcg_gen_movi_i32(arg[0].out, 0);
  }
}

/* ae_l16m.i/iu/x/xu/xc/xc1 -- load 16-bit signed to AE_DR */
static void translate_ae_l16m_i(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESW);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_l16m_iu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESW);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_l16m_x(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESW);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_l16m_xu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESW);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

/* ae_l16x2m -- load 32-bit (two 16-bit values) to AE_DR */
static void translate_ae_l16x2m_i(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_l16x2m_iu(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_l16x2m_x(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_l16x2m_xu(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

/* ae_l32m -- load 32-bit sign-extended to AE_DR */
static void translate_ae_l32m_i(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_l32m_iu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_l32m_x(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_l32m_xu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TESL);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

/* ae_l8x4f -- load 4 bytes (approximate: load 32-bit zero-extended) */
static void translate_ae_l8x4f_i(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TEUL);
        tcg_gen_extu_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_l8x4f_ip(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_qemu_ld_i32(tmp, addr, dc->cring, MO_TEUL);
        tcg_gen_extu_i32_i64(arg[0].out, tmp);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

    /* ----------------------------------------------------------------
     * Bitstream byte load/store baseline helpers
     * ---------------------------------------------------------------- */

    static void translate_ae_lb(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, cpu_AE_BITPTR, arg[1].in);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UB);
      }
    }

    static void translate_ae_lbi(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].out) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, cpu_AE_BITPTR, arg[1].imm);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UB);
      }
    }

    static void translate_ae_lbk(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_add_i32(addr, addr, cpu_AE_BITPTR);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UB);
      }
    }

    static void translate_ae_lbki(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, cpu_AE_BITPTR, arg[1].in);
        tcg_gen_addi_i32(addr, addr, arg[2].imm);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UB);
      }
    }

    static void translate_ae_lbs(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, cpu_AE_BITPTR, arg[1].in);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_SB);
      }
    }

    static void translate_ae_lbsi(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
    {
      if (arg[0].out) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, cpu_AE_BITPTR, arg[1].imm);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_SB);
      }
    }

    static void translate_ae_db(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
    {
      if (arg[0].out && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();

        tcg_gen_add_i32(addr, cpu_AE_BITPTR, arg[1].in);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UW);

        if (par && par[0]) {
          tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 2);
        }
      }
    }

    static void translate_ae_dbi(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].out) {
        TCGv_i32 addr = tcg_temp_new_i32();

        tcg_gen_addi_i32(addr, cpu_AE_BITPTR, arg[1].imm);
        tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UW);

        if (par && par[0]) {
          tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 2);
        }
      }
    }

    static void translate_ae_vldl16c(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      if (arg[0].out) {
        tcg_gen_qemu_ld_i32(arg[0].out, cpu_AE_BITPTR, dc->cring, MO_UW);
        if (par && par[0]) {
          tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 2);
        }
      }
    }

    static void translate_ae_vldl16t(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      if (arg[0].out) {
        if (arg[1].in && arg[2].in) {
          TCGv_i32 addr = tcg_temp_new_i32();
          tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
          tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UW);
        } else {
          tcg_gen_qemu_ld_i32(arg[0].out, cpu_AE_BITPTR, dc->cring, MO_UW);
        }
      }
    }

    static void translate_ae_vldl32t(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      if (arg[0].out) {
        if (arg[1].in && arg[2].in) {
          TCGv_i32 addr = tcg_temp_new_i32();
          tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
          tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_TEUL);
        } else {
          tcg_gen_qemu_ld_i32(arg[0].out, cpu_AE_BITPTR, dc->cring, MO_TEUL);
        }
      }
    }

    static void translate_ae_vlel16t(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      if (arg[0].out) {
        if (arg[1].in && arg[2].in) {
          TCGv_i32 addr = tcg_temp_new_i32();
          tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
          tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_UW);
        } else {
          tcg_gen_qemu_ld_i32(arg[0].out, cpu_AE_BITPTR, dc->cring, MO_UW);
        }
      }
    }

    static void translate_ae_vlel32t(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      if (arg[0].out) {
        if (arg[1].in && arg[2].in) {
          TCGv_i32 addr = tcg_temp_new_i32();
          tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
          tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, MO_TEUL);
        } else {
          tcg_gen_qemu_ld_i32(arg[0].out, cpu_AE_BITPTR, dc->cring, MO_TEUL);
        }
      }
    }

    static void translate_ae_vles16c(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      if (arg[0].in) {
        TCGv_i32 tmp = tcg_temp_new_i32();

        tcg_gen_andi_i32(tmp, arg[0].in, 0xffff);
        tcg_gen_qemu_st_i32(tmp, cpu_AE_BITPTR, dc->cring, MO_UW);

        if (par && par[0]) {
          tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 2);
        }
        if (arg[0].out) {
          tcg_gen_mov_i32(arg[0].out, arg[0].in);
        }
      }
    }

    /* translate_ae_sb disabled: infrastructure issue (ae3_slot0 operand extraction)
       See translate.c around line 1000 for operand extraction during decode.
       Even a NOP implementation hangs, confirming the issue is in instruction decoding,
       not the translator logic. Root cause: multi-operand HiFi instruction (with FIELD_t)
       in ae3_slot0 slot causes hang during xtensa_operand_get_field(). */
    /*
    static void translate_ae_sb(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
    {
      if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 v = tcg_temp_new_i32();

        tcg_gen_add_i32(addr, cpu_AE_BITPTR, arg[1].in);
        tcg_gen_andi_i32(v, arg[0].in, 0xff);
        tcg_gen_qemu_st_i32(v, addr, dc->cring, MO_UB);

        if (par && par[0]) {
          tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 1);
        }
        if (arg[0].out) {
          tcg_gen_mov_i32(arg[0].out, arg[0].in);
        }
      }
    }
    */

    static void translate_ae_sbi(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 v = tcg_temp_new_i32();

      tcg_gen_add_i32(addr, cpu_AE_BITPTR, arg[1].in);
        tcg_gen_addi_i32(addr, addr, arg[2].imm);
      tcg_gen_andi_i32(v, arg[0].in, 0xff);
        tcg_gen_qemu_st_i32(v, addr, dc->cring, MO_UB);

        if (par && par[0]) {
          tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 1);
        }
        if (arg[0].out) {
          tcg_gen_mov_i32(arg[0].out, arg[0].in);
        }
      }
    }

    static void translate_ae_sbf(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
    {
      if (arg[0].in) {
        TCGv_i32 v = tcg_temp_new_i32();
        tcg_gen_andi_i32(v, arg[0].in, 0xff);
        tcg_gen_qemu_st_i32(v, cpu_AE_BITPTR, dc->cring, MO_UB);

        if (par && par[0]) {
          tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 1);
        }
        if (arg[0].out) {
          tcg_gen_mov_i32(arg[0].out, arg[0].in);
        }
      }
    }

/* ae_s16m.l -- store lower 16 bits of AE_DR */
static void translate_ae_s16m_i(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUW);
    }
}

static void translate_ae_s16m_iu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUW);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_s16m_x(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUW);
    }
}

static void translate_ae_s16m_xu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUW);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

/* ae_s16x2m -- store lower 32 bits as two packed 16-bit values */
static void translate_ae_s16x2m_i(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
}

static void translate_ae_s16x2m_iu(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_s16x2m_x(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
}

static void translate_ae_s16x2m_xu(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

/* ae_s32m -- store lower 32 bits of AE_DR */
static void translate_ae_s32m_i(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
}

static void translate_ae_s32m_iu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_s32m_x(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
}

static void translate_ae_s32m_xu(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrl_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

/* ae_s32ra64s -- store high 32 bits of AE_DR (approximate: rounded/saturated) */
static void translate_ae_s32ra64s_i(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_extrh_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
}

static void translate_ae_s32ra64s_ip(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_extrh_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_s32ra64s_x(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrh_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
}

static void translate_ae_s32ra64s_xp(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        TCGv_i32 tmp  = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_extrh_i64_i32(tmp, arg[0].in);
        tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

  /* ae_s24ra64s -- store upper lane with baseline 24-bit packing */
  static void translate_ae_s24ra64s_i(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    if (arg[0].in && arg[1].in) {
      TCGv_i32 addr = tcg_temp_new_i32();
      TCGv_i32 tmp  = tcg_temp_new_i32();
      tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
      tcg_gen_extrh_i64_i32(tmp, arg[0].in);
      tcg_gen_andi_i32(tmp, tmp, 0x00ffffff);
      tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
  }

  static void translate_ae_s24ra64s_ip(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
  {
    if (arg[0].in && arg[1].in) {
      TCGv_i32 addr = tcg_temp_new_i32();
      TCGv_i32 tmp  = tcg_temp_new_i32();
      tcg_gen_mov_i32(addr, arg[1].in);
      tcg_gen_extrh_i64_i32(tmp, arg[0].in);
      tcg_gen_andi_i32(tmp, tmp, 0x00ffffff);
      tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
      if (arg[1].out) {
        tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
      }
    }
  }

  static void translate_ae_s24ra64s_x(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    if (arg[0].in && arg[1].in && arg[2].in) {
      TCGv_i32 addr = tcg_temp_new_i32();
      TCGv_i32 tmp  = tcg_temp_new_i32();
      tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
      tcg_gen_extrh_i64_i32(tmp, arg[0].in);
      tcg_gen_andi_i32(tmp, tmp, 0x00ffffff);
      tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
    }
  }

  static void translate_ae_s24ra64s_xp(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
  {
    if (arg[0].in && arg[1].in && arg[2].in) {
      TCGv_i32 addr = tcg_temp_new_i32();
      TCGv_i32 tmp  = tcg_temp_new_i32();
      tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
      tcg_gen_extrh_i64_i32(tmp, arg[0].in);
      tcg_gen_andi_i32(tmp, tmp, 0x00ffffff);
      tcg_gen_qemu_st_i32(tmp, addr, dc->cring, MO_TEUL);
      if (arg[1].out) {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
      }
    }
  }

  static void translate_ae_s24x2ra64s_ip(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
  {
    translate_ae_s24ra64s_ip(dc, arg, par);
  }

  static void translate_ae_s32x2ra64s_ip(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
  {
    translate_ae_s32ra64s_ip(dc, arg, par);
  }

/* ae_s32x2rng -- store 64-bit AE_DR with range scaling (approximate: store as-is) */
static void translate_ae_s32x2rng_i(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
        tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
    }
}

static void translate_ae_s32x2rng_ip(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    if (arg[0].in && arg[1].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_mov_i32(addr, arg[1].in);
        tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
        if (arg[1].out) {
            tcg_gen_addi_i32(arg[1].out, addr, arg[2].imm);
        }
    }
}

static void translate_ae_s32x2rng_x(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
    }
}

static void translate_ae_s32x2rng_xp(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    if (arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
        tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
        if (arg[1].out) {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

/* ae_zext16 / ae_sext16 / ae_sext32 */
static void translate_ae_zext16(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_andi_i64(arg[0].out, arg[1].in, 0xffffULL);
    }
}

static void translate_ae_sext16(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(tmp, arg[1].in);
        tcg_gen_ext16s_i32(tmp, tmp);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

static void translate_ae_sext32(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(tmp, arg[1].in);
        tcg_gen_ext_i32_i64(arg[0].out, tmp);
    }
}

/*
 * ae_cvt32x2f16.10 / ae_sext32x2d16.10 -- sign-extend 16-bit lanes 1,0 to 32-bit each.
 * ae_cvt32x2f16.32 / ae_sext32x2d16.32 -- sign-extend 16-bit lanes 3,2 to 32-bit each.
 * par[0] = base lane index (0 for .10, 2 for .32).
 * arg[0].out: AE 64-bit output; arg[1].in: AE 64-bit input.
 */
static void translate_ae_cvt32x2f16(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        unsigned base = par[0];
        TCGv_i32 lo = ae_extract_lane16s((TCGv_i64)arg[1].in, base);
        TCGv_i32 hi = ae_extract_lane16s((TCGv_i64)arg[1].in, base + 1);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    }
}

static void translate_ae_cvt32x2f16_10(DisasContext *dc, const OpcodeArg arg[],
                                         const uint32_t par[])
{
    uint32_t p[] = {0};
    translate_ae_cvt32x2f16(dc, arg, p);
}

static void translate_ae_cvt32x2f16_32(DisasContext *dc, const OpcodeArg arg[],
                                         const uint32_t par[])
{
    uint32_t p[] = {2};
    translate_ae_cvt32x2f16(dc, arg, p);
}

/*
 * ae_cvt48a32 / ae_cvt64a32 / ae_cvtq56a32s:
 * Sign-extend a 32-bit AR value into an AE 64-bit register.
 * arg[0].out: AE 64-bit; arg[1].in: AR 32-bit.
 */
static void translate_ae_cvt48a32(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_ext_i32_i64((TCGv_i64)arg[0].out, (TCGv_i32)arg[1].in);
    }
}

/*
 * ae_cvtq56p32s.l / ae_cvt48f32.l:
 * Sign-extend lower 32-bit lane of AE input to full 64-bit AE output.
 * ae_cvtq56p32s.h / ae_cvt48f32.h / ae_cvt64f32.h:
 * Sign-extend upper 32-bit lane of AE input to full 64-bit AE output.
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit.
 */
static void translate_ae_cvtp32s_l(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        tcg_gen_ext_i32_i64((TCGv_i64)arg[0].out, lo);
    }
}

static void translate_ae_cvtp32s_h(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        tcg_gen_ext_i32_i64((TCGv_i64)arg[0].out, hi);
    }
}

/*
 * ae_cvta32f24s.h: AR = sign-extend(AE[63:32]) — upper 32-bit lane to AR.
 * ae_cvta32f24s.l: AR = sign-extend(AE[31:0])  — lower 32-bit lane to AR.
 * arg[0].out: AR 32-bit; arg[1].in: AE 64-bit.
 * (Baseline approximation: treat p24 slots as 32-bit lanes.)
 */
static void translate_ae_cvta32f24s_h(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_extrh_i64_i32((TCGv_i32)arg[0].out, (TCGv_i64)arg[1].in);
    }
}

static void translate_ae_cvta32f24s_l(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_extrl_i64_i32((TCGv_i32)arg[0].out, (TCGv_i64)arg[1].in);
    }
}

/*
 * ae_cvtp24a16x2.hh / .hl / .lh / .ll:
 * Pack two sign-extended 16-bit halves from two AR registers into one AE register.
 * arg[0].out: AE 64-bit; arg[1].in: ars (AR 32-bit); arg[2].in: art (AR 32-bit).
 * par[0]: lane selector for ars (0=bits[15:0], 1=bits[31:16]).
 * par[1]: lane selector for art (0=bits[15:0], 1=bits[31:16]).
 * Baseline: sign-extend the selected 16-bit half to 32-bit, pack as two 32-bit lanes.
 */
static void translate_ae_cvtp24a16x2(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 hi32 = tcg_temp_new_i32();
        TCGv_i32 lo32 = tcg_temp_new_i32();
        if (par[0]) {
            tcg_gen_sari_i32(hi32, (TCGv_i32)arg[1].in, 16);
        } else {
            tcg_gen_ext16s_i32(hi32, (TCGv_i32)arg[1].in);
        }
        if (par[1]) {
            tcg_gen_sari_i32(lo32, (TCGv_i32)arg[2].in, 16);
        } else {
            tcg_gen_ext16s_i32(lo32, (TCGv_i32)arg[2].in);
        }
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo32, hi32);
    }
}

static void translate_ae_cvtp24a16x2_ll(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    uint32_t p[] = {0, 0};
    translate_ae_cvtp24a16x2(dc, arg, p);
}

static void translate_ae_cvtp24a16x2_lh(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    uint32_t p[] = {0, 1};
    translate_ae_cvtp24a16x2(dc, arg, p);
}

static void translate_ae_cvtp24a16x2_hl(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    uint32_t p[] = {1, 0};
    translate_ae_cvtp24a16x2(dc, arg, p);
}

static void translate_ae_cvtp24a16x2_hh(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    uint32_t p[] = {1, 1};
    translate_ae_cvtp24a16x2(dc, arg, p);
}

/*
 * ae_add32_hl_lh: cross-butterfly add.
 *   OUT[63:32] = IN0[63:32] + IN1[31:0]
 *   OUT[31:0]  = IN0[31:0]  + IN1[63:32]
 * ae_add32s_hl_lh: saturating version (sets AE_OVERFLOW).
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit; arg[2].in: AE 64-bit.
 */
static void translate_ae_add32_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 a_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 a_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 b_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
        TCGv_i32 b_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        TCGv_i32 res_hi = tcg_temp_new_i32();
        TCGv_i32 res_lo = tcg_temp_new_i32();
        tcg_gen_add_i32(res_hi, a_hi, b_lo);
        tcg_gen_add_i32(res_lo, a_lo, b_hi);
        ae_pack_lanes32((TCGv_i64)arg[0].out, res_lo, res_hi);
    }
}

static void translate_ae_add32s_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 a_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 a_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 b_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
        TCGv_i32 b_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        tcg_gen_movi_i32(overflow, 0);
        TCGv_i32 res_hi = ae_sat_add32(a_hi, b_lo, overflow);
        TCGv_i32 res_lo = ae_sat_add32(a_lo, b_hi, overflow);
        ae_pack_lanes32((TCGv_i64)arg[0].out, res_lo, res_hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
    }
}

/*
 * ae_sext72: sign-extend 64-bit AE value to 72-bit EP (extended precision).
 * In QEMU's model the EP register holds the overflow/sign bits above bit 63.
 * arg[0].out: EP (TCGv_i32); arg[1].in: AE 64-bit.
 */
static void translate_ae_sext72(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        /* EP = sign-byte extracted from top of the 64-bit value */
        TCGv_i64 sign64 = tcg_temp_new_i64();
        tcg_gen_sari_i64(sign64, (TCGv_i64)arg[1].in, 63);
        tcg_gen_extrl_i64_i32((TCGv_i32)arg[0].out, sign64);
    }
}

/*
 * ae_neg64s: saturating negate of 64-bit AE value.
 * INT64_MIN → INT64_MAX (with AE_OVERFLOW), all others → -x.
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit.
 */
static void translate_ae_neg64s(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i64 src = (TCGv_i64)arg[1].in;
        TCGv_i64 neg = tcg_temp_new_i64();
        TCGv_i64 is_min = tcg_temp_new_i64();

        tcg_gen_movi_i32(overflow, 0);
        tcg_gen_neg_i64(neg, src);
        tcg_gen_setcond_i64(TCG_COND_EQ, is_min,
                    src, tcg_constant_i64(INT64_MIN));
        /* is_min[0] feeds overflow flag */
        {
            TCGv_i32 is_min32 = tcg_temp_new_i32();
            tcg_gen_extrl_i64_i32(is_min32, is_min);
            tcg_gen_or_i32(overflow, overflow, is_min32);
        }
        /* If src == INT64_MIN, saturate to INT64_MAX */
        tcg_gen_movcond_i64(TCG_COND_NE, neg,
                   is_min, tcg_constant_i64(0),
                   tcg_constant_i64(INT64_MAX), neg);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, neg);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
    }
}

/*
 * ae_shortswap: swap the two 16-bit halves within each 32-bit lane.
 *   OUT[63:32] = { IN[47:32], IN[63:48] }
 *   OUT[31:0]  = { IN[15:0],  IN[31:16] }
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit.
 */
static void translate_ae_shortswap(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 lo_swapped = tcg_temp_new_i32();
        TCGv_i32 hi_swapped = tcg_temp_new_i32();
        tcg_gen_rotri_i32(lo_swapped, lo, 16);
        tcg_gen_rotri_i32(hi_swapped, hi, 16);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo_swapped, hi_swapped);
    }
}

/*
 * ae_sel16i: select 4 16-bit lanes from two AE registers using an 8-bit immediate.
 * Each pair of bits [2k+1:2k] selects output lane k from one of 8 input lanes:
 *   0-3 → arg[1] lanes 0-3;  4-7 would extend into arg[2] (2-bit can only reach 0-3).
 * With a 2-bit selector (4 lanes total), one register's 4 lanes per field:
 *   Bit field for lane k = imm[2k+1:2k]. If bit[2] = 0 → from arg[1], else from arg[2].
 *   Low 2 bits of field = lane within that register.
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit; arg[2].in: AE 64-bit; arg[3].imm: selector.
 */
static void translate_ae_sel16i(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        unsigned sel = (unsigned)arg[3].imm;
        TCGv_i32 out[4];
        unsigned k;

        for (k = 0; k < 4; ++k) {
            unsigned field = (sel >> (2 * k)) & 0x3;
            /* field: which of the 4 16-bit lanes in arg[1] or arg[2] */
            /* For ae_sel16i with 2-bit field: arg[1] lanes 0-3 only (use arg[1])
             * For ae_sel16i.n with 4-bit field: also needs arg[2].
             * Both use the same helper; just dispatch field→register. */
            TCGv_i64 src = (field < 2) ?
                (TCGv_i64)arg[1].in : (TCGv_i64)arg[2].in;
            unsigned lane = field & 1;
            /* Our 64-bit AE register holds 4 16-bit lanes: lane0=[15:0], ..., lane3=[63:48] */
            /* Extract the 16-bit lane from the lower or upper 32-bit word */
            TCGv_i32 word = tcg_temp_new_i32();
            TCGv_i32 val = tcg_temp_new_i32();
            if (lane < 1) {
                tcg_gen_extrl_i64_i32(word, src);
                tcg_gen_sextract_i32(val, word, 0, 16);
            } else {
                tcg_gen_extrl_i64_i32(word, src);
                tcg_gen_sextract_i32(val, word, 16, 16);
            }
            out[k] = val;
        }
        /* Pack 4 lanes into output: lane0=[15:0], lane1=[31:16], lane2=[47:32], lane3=[63:48] */
        {
            TCGv_i32 lo16 = tcg_temp_new_i32();
            TCGv_i32 hi16 = tcg_temp_new_i32();
            TCGv_i64 lo64 = tcg_temp_new_i64();
            TCGv_i64 hi64 = tcg_temp_new_i64();
            tcg_gen_deposit_i32(lo16, out[0], out[1], 16, 16);
            tcg_gen_deposit_i32(hi16, out[2], out[3], 16, 16);
            tcg_gen_extu_i32_i64(lo64, lo16);
            tcg_gen_extu_i32_i64(hi64, hi16);
            tcg_gen_shli_i64(hi64, hi64, 32);
            tcg_gen_or_i64((TCGv_i64)arg[0].out, lo64, hi64);
        }
    }
}

/*
 * ae_sel16i.n uses a 4-bit selector field mapped to 8 total lanes across two registers.
 * For baseline, use same implementation — the upper 2 bits of each 2-bit sel_k field
 * route to arg[2] (bit2=1) or arg[1] (bit2=0), lane within = bits[1:0] >> 1.
 * The .n variant has a different immediate encoding; treat both identically.
 */
static void translate_ae_sel16i_n(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    /* For ae_sel16i.n with 4-bit per-lane selector in imm: route to arg[1] or arg[2] */
    if (arg[0].out && arg[1].in && arg[2].in) {
        unsigned sel = (unsigned)arg[3].imm;
        /* Each lane uses 2 bits; bit[1]=register (0→arg[1], 1→arg[2]),
         * bit[0]=half (0→low 16, 1→high 16) within that 32-bit lower word.
         * For a simple baseline, select 16-bit lane from one of arg[1]/arg[2] lanes. */
        TCGv_i32 out[4];
        unsigned k;

        for (k = 0; k < 4; ++k) {
            unsigned field = (sel >> (2 * k)) & 0x3;
            TCGv_i64 src = (field & 2) ?
                (TCGv_i64)arg[2].in : (TCGv_i64)arg[1].in;
            unsigned lane_idx = field & 1;  /* 0 or 1 only within lower 32-bit word */
            TCGv_i32 word = tcg_temp_new_i32();
            TCGv_i32 val = tcg_temp_new_i32();
            tcg_gen_extrl_i64_i32(word, src);
            tcg_gen_sextract_i32(val, word, lane_idx * 16, 16);
            out[k] = val;
        }
        {
            TCGv_i32 lo16 = tcg_temp_new_i32();
            TCGv_i32 hi16 = tcg_temp_new_i32();
            TCGv_i64 lo64 = tcg_temp_new_i64();
            TCGv_i64 hi64 = tcg_temp_new_i64();
            tcg_gen_deposit_i32(lo16, out[0], out[1], 16, 16);
            tcg_gen_deposit_i32(hi16, out[2], out[3], 16, 16);
            tcg_gen_extu_i32_i64(lo64, lo16);
            tcg_gen_extu_i32_i64(hi64, hi16);
            tcg_gen_shli_i64(hi64, hi64, 32);
            tcg_gen_or_i64((TCGv_i64)arg[0].out, lo64, hi64);
        }
    }
}

/*
 * ae_mul32js: multiply the two 32-bit halves of a single AE register.
 * OUT[63:0] = sext64(IN[63:32]) * sext64(IN[31:0])
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit.
 */
static void translate_ae_mul32js(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i64 lo64 = tcg_temp_new_i64();
        TCGv_i64 hi64 = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(lo64, lo);
        tcg_gen_ext_i32_i64(hi64, hi);
        tcg_gen_mul_i64((TCGv_i64)arg[0].out, lo64, hi64);
    }
}

/*
 * ae_truncp24a32x2: pack two AR 32-bit values into one AE 64-bit register.
 * Baseline: same as ae_movda32x2 (OPERAND_ae_ar_to_dr_v, ars, art).
 * arg[0].out: AE 64-bit; arg[1].in: AR 32-bit (ars); arg[2].in: AR 32-bit (art).
 * Note: ars → hi lane, art → lo lane (same convention as ae_movda32x2).
 */
static void translate_ae_truncp24a32x2(DisasContext *dc, const OpcodeArg arg[],
                                         const uint32_t par[])
{
    /* Delegate to ae_movda32x2 logic: hi=arg[1], lo=arg[2] */
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i64 hi = tcg_temp_new_i64();
        TCGv_i64 lo = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(hi, (TCGv_i32)arg[1].in);
        tcg_gen_extu_i32_i64(lo, (TCGv_i32)arg[2].in);
        tcg_gen_shli_i64(hi, hi, 32);
        tcg_gen_or_i64((TCGv_i64)arg[0].out, hi, lo);
    }
}

/*
 * ae_truncp24q48x2: take lower 32-bit lane of two AE inputs, pack into one AE output.
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit (v0); arg[2].in: AE 64-bit (v1).
 * Baseline: OUT = { lower32(v0), lower32(v1) }
 */
static void translate_ae_truncp24q48x2(DisasContext *dc, const OpcodeArg arg[],
                                         const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 lo0 = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 lo1 = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo1, lo0);
    }
}

/*
 * ae_truncp16: truncate P24x2 to P16x4 — baseline: identity copy.
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit.
 */
static void translate_ae_truncp16(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
    }
}

/* Forward declaration for ae_sat32_from_i64 (defined later in file) */
static TCGv_i32 ae_sat32_from_i64(TCGv_i64 val, TCGv_i32 overflow);

/*
 * ae_round32x2f48ssym / ae_round32x2f48sasym:
 * Round two signed F48 inputs to 32x2 output with saturation.
 * arg[1] -> high lane, arg[2] -> low lane.
 */
static void translate_ae_round32x2f48(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i64 hi_f48 = tcg_temp_new_i64();
    TCGv_i64 lo_f48 = tcg_temp_new_i64();
    TCGv_i32 hi_sat, lo_sat;

    tcg_gen_movi_i32(overflow, 0);

    /* Sign-extend low 48-bit payload from each source. */
    tcg_gen_shli_i64(hi_f48, (TCGv_i64)arg[1].in, 16);
    tcg_gen_sari_i64(hi_f48, hi_f48, 16);
    tcg_gen_shli_i64(lo_f48, (TCGv_i64)arg[2].in, 16);
    tcg_gen_sari_i64(lo_f48, lo_f48, 16);

    hi_sat = ae_sat32_from_i64(hi_f48, overflow);
    lo_sat = ae_sat32_from_i64(lo_f48, overflow);

    ae_pack_lanes32((TCGv_i64)arg[0].out, lo_sat, hi_sat);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/*
 * ae_round32x2f64ssym / ae_round32x2f64sasym:
 * Round two signed F64 inputs to 32x2 output with saturation.
 * arg[1] -> high lane, arg[2] -> low lane.
 */
static void translate_ae_round32x2f64(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i32 hi_sat, lo_sat;

    tcg_gen_movi_i32(overflow, 0);

    hi_sat = ae_sat32_from_i64((TCGv_i64)arg[1].in, overflow);
    lo_sat = ae_sat32_from_i64((TCGv_i64)arg[2].in, overflow);

    ae_pack_lanes32((TCGv_i64)arg[0].out, lo_sat, hi_sat);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }
}

/*
 * ae_round32x2f (baseline wrapper for pksr32)
 * Used when AE_PKSR32 has two input sources; dispatches to simpler baseline
 * that just extracts lower lanes (original behavior for backward compatibility).
 */
static void translate_ae_round32x2f(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    }
}

/*
 * ae_round16x4f32ssym / ae_round16x4f32sasym:
 * Narrow 32x4 → 16x4 with saturation.  arg ordering: {dst, v1, v0}
 * v0 = arg[2] supplies lanes [1:0]; v1 = arg[1] supplies lanes [3:2].
 * Baseline: ae_sat16x4 semantics with swapped input roles.
 */
static void translate_ae_round16x4f32(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lanes[4];

        tcg_gen_movi_i32(overflow, 0);
        /* arg[2]=v0 supplies lanes 0,1; arg[1]=v1 supplies lanes 2,3 */
        lanes[0] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[2].in, 0), overflow);
        lanes[1] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[2].in, 1), overflow);
        lanes[2] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[1].in, 0), overflow);
        lanes[3] = ae_sat16_from_i32(ae_extract_lane32((TCGv_i64)arg[1].in, 1), overflow);
        {
            TCGv_i32 lo16 = tcg_temp_new_i32();
            TCGv_i32 hi16 = tcg_temp_new_i32();
            TCGv_i64 lo64 = tcg_temp_new_i64();
            TCGv_i64 hi64 = tcg_temp_new_i64();
            tcg_gen_deposit_i32(lo16, lanes[0], lanes[1], 16, 16);
            tcg_gen_deposit_i32(hi16, lanes[2], lanes[3], 16, 16);
            tcg_gen_extu_i32_i64(lo64, lo16);
            tcg_gen_extu_i32_i64(hi64, hi16);
            tcg_gen_shli_i64(hi64, hi64, 32);
            tcg_gen_or_i64((TCGv_i64)arg[0].out, lo64, hi64);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
    }
}

/*
 * ae_round24x2f48ssym / ae_round24x2f48sasym:
 * Narrow 32x2 to 24-bit range (saturating). Same 3-arg shape as round32x2.
 * Baseline: saturate each 32-bit lane in arg[2] to 24-bit range.
 */
static void translate_ae_round24x2f48(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);

        tcg_gen_movi_i32(overflow, 0);
        lo = ae_sat24_from_i32(lo, overflow);
        hi = ae_sat24_from_i32(hi, overflow);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
    }
}

/*
 * ae_roundsp16f24sym / ae_roundsp16f24asym:
 * Round+saturate a 24x2 AE value to 16x2 (two signed P24 lanes → two S16 lanes).
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit.
 * Sign-extend each 24-bit payload lane before S16 saturation.
 */
static void translate_ae_roundsp16f24(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);

        tcg_gen_movi_i32(overflow, 0);

        /* P24 lanes live in bits [23:0] of each 32-bit lane. */
        tcg_gen_shli_i32(lo, lo, 8);
        tcg_gen_sari_i32(lo, lo, 8);
        tcg_gen_shli_i32(hi, hi, 8);
        tcg_gen_sari_i32(hi, hi, 8);

        lo = ae_sat16_from_i32(lo, overflow);
        hi = ae_sat16_from_i32(hi, overflow);
        {
            TCGv_i32 packed = tcg_temp_new_i32();
            TCGv_i64 lo64 = tcg_temp_new_i64();
            tcg_gen_deposit_i32(packed, lo, hi, 16, 16);
            tcg_gen_extu_i32_i64(lo64, packed);
            tcg_gen_mov_i64((TCGv_i64)arg[0].out, lo64);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
    }
}

/*
 * ae_roundsp16q48x2sym / ae_roundsp16q48x2asym:
 * Round two Q48 values to S16. Same 3-arg shape as round32x2.
 * arg[1] supplies the high 16-bit output lane, arg[2] supplies the low lane.
 * Each source uses the low 48-bit Q48 payload of its AE input.
 */
static TCGv_i32 ae_sat16_from_i64(TCGv_i64 val, TCGv_i32 overflow)
{
  TCGv_i32 res = tcg_temp_new_i32();
  TCGv_i64 gt = tcg_temp_new_i64();
  TCGv_i64 lt = tcg_temp_new_i64();
  TCGv_i32 gt32 = tcg_temp_new_i32();
  TCGv_i32 lt32 = tcg_temp_new_i32();

  tcg_gen_setcondi_i64(TCG_COND_GT, gt, val, (int64_t)INT16_MAX);
  tcg_gen_setcondi_i64(TCG_COND_LT, lt, val, (int64_t)INT16_MIN);
  tcg_gen_extrl_i64_i32(gt32, gt);
  tcg_gen_extrl_i64_i32(lt32, lt);
  tcg_gen_or_i32(overflow, overflow, gt32);
  tcg_gen_or_i32(overflow, overflow, lt32);

  tcg_gen_extrl_i64_i32(res, val);
  tcg_gen_movcond_i32(TCG_COND_NE, res, gt32, tcg_constant_i32(0),
            tcg_constant_i32(INT16_MAX), res);
  tcg_gen_movcond_i32(TCG_COND_NE, res, lt32, tcg_constant_i32(0),
            tcg_constant_i32(INT16_MIN), res);
  return res;
}

static void translate_ae_roundsp16q48x2(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i64 q48_hi = tcg_temp_new_i64();
    TCGv_i64 q48_lo = tcg_temp_new_i64();
    TCGv_i32 hi;
    TCGv_i32 lo;

        tcg_gen_movi_i32(overflow, 0);

    /* Sign-extend each low-48-bit Q48 payload before saturation. */
    tcg_gen_shli_i64(q48_hi, (TCGv_i64)arg[1].in, 16);
    tcg_gen_sari_i64(q48_hi, q48_hi, 16);
    tcg_gen_shli_i64(q48_lo, (TCGv_i64)arg[2].in, 16);
    tcg_gen_sari_i64(q48_lo, q48_lo, 16);

    hi = ae_sat16_from_i64(q48_hi, overflow);
    lo = ae_sat16_from_i64(q48_lo, overflow);
        {
            TCGv_i32 packed = tcg_temp_new_i32();
            TCGv_i64 lo64 = tcg_temp_new_i64();
            tcg_gen_deposit_i32(packed, lo, hi, 16, 16);
            tcg_gen_extu_i32_i64(lo64, packed);
            tcg_gen_mov_i64((TCGv_i64)arg[0].out, lo64);
        }
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
    }
}

/*
 * ae_roundsq32f48sym / ae_roundsq32f48asym:
 * Round Q48 AE to S32 (saturate 48-bit to 32-bit).
 * arg[0].out: AE 64-bit; arg[1].in: AE 64-bit.
 * The Q48 payload lives in the low 48 bits of the AE register. Sign-extend the
 * 48-bit value, saturate to int32, and write the result sign-extended to AE_DR.
 */
static void translate_ae_roundsq32f48(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i64 q48 = tcg_temp_new_i64();
    TCGv_i64 out64 = tcg_temp_new_i64();
    TCGv_i32 sat;

        tcg_gen_movi_i32(overflow, 0);

    /* Sign-extend the low 48-bit fixed-point payload to i64. */
    tcg_gen_shli_i64(q48, (TCGv_i64)arg[1].in, 16);
    tcg_gen_sari_i64(q48, q48, 16);

    sat = ae_sat32_from_i64(q48, overflow);
    tcg_gen_ext_i32_i64(out64, sat);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, out64);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
    }
}

    /*
     * ae_pksr24 / ae_pksr32 / ae_pksrf32:
     * Baseline pack-round helpers. When decode provides the same 3-arg layout as
     * the round* family, reuse those semantics directly. For single-source forms,
     * apply the same saturation to the source lanes and repack into AE_DR.
     */
    static void translate_ae_pksr24(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (!arg[0].out) {
        return;
      }

      if (arg[1].in && arg[2].in) {
        translate_ae_round24x2f48(dc, arg, par);
        return;
      }

      if (arg[1].in) {
        TCGv_i32 overflow = tcg_temp_new_i32();
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);

        tcg_gen_movi_i32(overflow, 0);
        lo = ae_sat24_from_i32(lo, overflow);
        hi = ae_sat24_from_i32(hi, overflow);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
        tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
      }
    }

    static void translate_ae_pksr32(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
    {
      if (!arg[0].out) {
        return;
      }

      if (arg[1].in && arg[2].in) {
        translate_ae_round32x2f(dc, arg, par);
        return;
      }

      if (arg[1].in) {
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
      }
    }

    static void translate_ae_pksrf32(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
    {
      translate_ae_pksr32(dc, arg, par);
    }

/* ================================================================
 * Boolean-move / conditional-move / bitswap / trunca16p24s /
 * trunc32f64s / slai72 / srai72 baseline batch
 * ================================================================ */

/*
 * ae_movba / ae_movba1x2 / ae_movba2 / ae_movba4 — AR -> BR
 * ae_movb2 / ae_movb4                             — BR -> BR copy
 * Baseline model: preserve dataflow in a packed i32 boolean container.
 */
static void translate_ae_movba(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  (void)dc;
  (void)par;

  if (!arg[0].out) {
    return;
  }

  /* ae_movba1x2 has two scalar inputs; pack two 1-bit lanes. */
  if (arg[2].in) {
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();

    tcg_gen_andi_i32(lo, arg[1].in, 1);
    tcg_gen_andi_i32(hi, arg[2].in, 1);
    tcg_gen_shli_i32(hi, hi, 1);
    tcg_gen_or_i32(arg[0].out, lo, hi);
    return;
  }

  if (arg[1].in) {
    tcg_gen_mov_i32(arg[0].out, arg[1].in);
  }
}

/*
 * ae_movf* / ae_movt* conditional moves.
 * par[0] == 0: movf (copy when cond == 0)
 * par[0] == 1: movt (copy when cond != 0)
 * If cond input is unavailable, keep legacy baseline and always copy.
 */
static void translate_ae_movft(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  bool copy_on_true = par[0];

  (void)dc;

  if (!arg[0].out || !arg[1].in) {
    return;
  }

  if (!arg[2].in) {
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
    return;
    }

  TCGv_i32 cond = tcg_temp_new_i32();
  TCGLabel *copy_label = gen_new_label();
  TCGLabel *done_label = gen_new_label();

  tcg_gen_andi_i32(cond, arg[2].in, 1);
  tcg_gen_brcondi_i32(copy_on_true ? TCG_COND_NE : TCG_COND_EQ,
            cond, 0, copy_label);
  tcg_gen_br(done_label);
  gen_set_label(copy_label);
  tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
  gen_set_label(done_label);
}

/*
 * ae_bitswap — bit-reversal of AR (32-bit) register.
 * Uses 5-stage butterfly: swap 16s, 8s, 4s, 2s, 1s.
 * arg[0]: arr 'o', arg[1]: art 'i'.
 */
static void translate_ae_bitswap(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in) {
        return;
    }
    TCGv_i32 t  = tcg_temp_new_i32();
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();

    tcg_gen_mov_i32(t, arg[1].in);

    /* 1. Swap 16-bit halves */
    tcg_gen_rotli_i32(t, t, 16);

    /* 2. Swap bytes within each 16-bit half */
    tcg_gen_andi_i32(lo, t, 0x00FF00FFu);
    tcg_gen_shli_i32(lo, lo, 8);
    tcg_gen_shri_i32(hi, t, 8);
    tcg_gen_andi_i32(hi, hi, 0x00FF00FFu);
    tcg_gen_or_i32(t, lo, hi);

    /* 3. Swap nibbles within each byte */
    tcg_gen_andi_i32(lo, t, 0x0F0F0F0Fu);
    tcg_gen_shli_i32(lo, lo, 4);
    tcg_gen_shri_i32(hi, t, 4);
    tcg_gen_andi_i32(hi, hi, 0x0F0F0F0Fu);
    tcg_gen_or_i32(t, lo, hi);

    /* 4. Swap bit-pairs within each nibble */
    tcg_gen_andi_i32(lo, t, 0x33333333u);
    tcg_gen_shli_i32(lo, lo, 2);
    tcg_gen_shri_i32(hi, t, 2);
    tcg_gen_andi_i32(hi, hi, 0x33333333u);
    tcg_gen_or_i32(t, lo, hi);

    /* 5. Swap individual bits */
    tcg_gen_andi_i32(lo, t, 0x55555555u);
    tcg_gen_shli_i32(lo, lo, 1);
    tcg_gen_shri_i32(hi, t, 1);
    tcg_gen_andi_i32(hi, hi, 0x55555555u);
    tcg_gen_or_i32(t, lo, hi);

    tcg_gen_mov_i32(arg[0].out, t);
}

/*
 * ae_trunca16p24s.l / ae_trunca16p24s.h
 * Extract one 24-bit lane from AE register, sign-extend to 32, saturate to
 * int16, write to AR output.
 * par[0] = 0 → lower lane (bits 23:0), 1 → upper lane (bits 55:32).
 * arg[0]: arr 'o', arg[1]: ae_dr_to_ar_v0 'i' (AE 64-bit).
 */
static void translate_ae_trunca16p24s(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in) {
        return;
    }
    TCGv_i32 lane     = tcg_temp_new_i32();
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);

    if (par[0] == 0) {
        /* Lower 24-bit lane: bits 23:0, sign-extend from 24 */
        tcg_gen_extrl_i64_i32(lane, (TCGv_i64)arg[1].in);
        tcg_gen_shli_i32(lane, lane, 8);
        tcg_gen_sari_i32(lane, lane, 8);
    } else {
        /* Upper 24-bit lane: bits 55:32, sign-extend from 24 */
        TCGv_i64 shifted = tcg_temp_new_i64();
        tcg_gen_shri_i64(shifted, (TCGv_i64)arg[1].in, 32);
        tcg_gen_extrl_i64_i32(lane, shifted);
        tcg_gen_shli_i32(lane, lane, 8);
        tcg_gen_sari_i32(lane, lane, 8);
    }
    TCGv_i32 sat = ae_sat16_from_i32(lane, overflow);
    tcg_gen_mov_i32(arg[0].out, sat);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
}

/*
 * ae_sat32_from_i64 -- saturate signed 64-bit → signed 32-bit.
 * Updates overflow if clamping occurs.
 */
static TCGv_i32 ae_sat32_from_i64(TCGv_i64 val, TCGv_i32 overflow)
{
    TCGv_i32 res = tcg_temp_new_i32();
    TCGv_i64 gt  = tcg_temp_new_i64();
    TCGv_i64 lt  = tcg_temp_new_i64();
    TCGv_i32 gt32 = tcg_temp_new_i32();
    TCGv_i32 lt32 = tcg_temp_new_i32();

    tcg_gen_setcondi_i64(TCG_COND_GT, gt, val, (int64_t)INT32_MAX);
    tcg_gen_setcondi_i64(TCG_COND_LT, lt, val, (int64_t)INT32_MIN);
    tcg_gen_extrl_i64_i32(gt32, gt);
    tcg_gen_extrl_i64_i32(lt32, lt);
    tcg_gen_or_i32(overflow, overflow, gt32);
    tcg_gen_or_i32(overflow, overflow, lt32);

    tcg_gen_extrl_i64_i32(res, val);
    tcg_gen_movcond_i32(TCG_COND_NE, res, gt32, tcg_constant_i32(0),
                        tcg_constant_i32(INT32_MAX), res);
    tcg_gen_movcond_i32(TCG_COND_NE, res, lt32, tcg_constant_i32(0),
                        tcg_constant_i32(INT32_MIN), res);
    return res;
}

  /*
   * ae_trunca32q48:
   * Convert signed Q48 payload from AE input (low 48 bits) to AR S32 with
   * saturation.
   */
  static void translate_ae_trunca32q48(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
  {
    if (!arg[0].out || !arg[1].in) {
      return;
    }

    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i64 q48 = tcg_temp_new_i64();
    TCGv_i32 sat;

    tcg_gen_movi_i32(overflow, 0);

    /* Sign-extend low 48-bit Q48 payload: (x << 16) >> 16. */
    tcg_gen_shli_i64(q48, (TCGv_i64)arg[1].in, 16);
    tcg_gen_sari_i64(q48, q48, 16);

    sat = ae_sat32_from_i64(q48, overflow);
    tcg_gen_mov_i32(arg[0].out, sat);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }

  /*
   * ae_truncq32:
   * Convert signed Q48 payload from AE input (low 48 bits) to AE S32 result
   * sign-extended to 64-bit destination.
   */
  static void translate_ae_truncq32(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    if (!arg[0].out || !arg[1].in) {
      return;
    }

    TCGv_i32 overflow = tcg_temp_new_i32();
    TCGv_i64 q48 = tcg_temp_new_i64();
    TCGv_i64 out64 = tcg_temp_new_i64();
    TCGv_i32 sat;

    tcg_gen_movi_i32(overflow, 0);

    /* Sign-extend low 48-bit Q48 payload: (x << 16) >> 16. */
    tcg_gen_shli_i64(q48, (TCGv_i64)arg[1].in, 16);
    tcg_gen_sari_i64(q48, q48, 16);

    sat = ae_sat32_from_i64(q48, overflow);
    tcg_gen_ext_i32_i64(out64, sat);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, out64);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
  }

/*
 * ae_trunci32x2f64s — shift right (immediate) two 64-bit inputs, saturate to
 * 2×32 in output AE register.
 * arg[0]: ae_shift_d 'o', arg[1]: ae_shift_d0 'i', arg[2]: ae_shift_sd 'i',
 * arg[3]: ae_osa16 immediate.
 */
static void translate_ae_trunci32x2f64s(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in) {
        return;
    }
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);

    unsigned sh = arg[3].imm & 63;
    TCGv_i64 shifted0 = tcg_temp_new_i64();
    TCGv_i64 shifted1 = tcg_temp_new_i64();

    tcg_gen_sari_i64(shifted0, (TCGv_i64)arg[1].in, sh);
    if (arg[1].in && arg[2].in) {
        tcg_gen_sari_i64(shifted1, (TCGv_i64)arg[2].in, sh);
    } else {
        tcg_gen_movi_i64(shifted1, 0);
    }

    TCGv_i32 lo = ae_sat32_from_i64(shifted0, overflow);
    TCGv_i32 hi = ae_sat32_from_i64(shifted1, overflow);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
}

/*
 * ae_trunca32x2f64s — shift right (AR amount) two 64-bit inputs, saturate to
 * 2×32 in output AE register.
 * arg[0]: ae_shift_d 'o', arg[1]: ae_shift_d0 'i', arg[2]: ae_shift_sd 'i',
 * arg[3]: ars 'i' (AR with shift count).
 */
static void translate_ae_trunca32x2f64s(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in || !arg[3].in) {
        return;
    }
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);

    TCGv_i64 sh64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh64, arg[3].in);
    tcg_gen_andi_i64(sh64, sh64, 63);

    TCGv_i64 shifted0 = tcg_temp_new_i64();
    TCGv_i64 shifted1 = tcg_temp_new_i64();

    tcg_gen_sar_i64(shifted0, (TCGv_i64)arg[1].in, sh64);
    if (arg[2].in) {
        tcg_gen_sar_i64(shifted1, (TCGv_i64)arg[2].in, sh64);
    } else {
        tcg_gen_movi_i64(shifted1, 0);
    }

    TCGv_i32 lo = ae_sat32_from_i64(shifted0, overflow);
    TCGv_i32 hi = ae_sat32_from_i64(shifted1, overflow);
    ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
}

/*
 * ae_trunci32f64s.l — shift right (immediate) one 64-bit input, saturate to
 * 32, write to lower lane of output AE register.
 * arg[0]: ae_shift_d 'o', arg[1]: ae_shift_d0 'i', arg[2]: ae_shift_sd 'i',
 * arg[3]: ae_osa16 immediate.
 */
static void translate_ae_trunci32f64s_l(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in) {
        return;
    }
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);

    unsigned sh = arg[3].imm & 63;
    TCGv_i64 shifted = tcg_temp_new_i64();
    tcg_gen_sari_i64(shifted, (TCGv_i64)arg[1].in, sh);

    TCGv_i32 lo = ae_sat32_from_i64(shifted, overflow);
    /* Write lower 32 bits of output; zero upper 32 */
    TCGv_i64 lo64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(lo64, lo);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, lo64);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
}

/*
 * ae_trunca32f64s.l — shift right (AR amount) one 64-bit input, saturate to
 * 32, write to lower lane of output AE register.
 * arg[0]: ae_shift_d 'o', arg[1]: ae_shift_d0 'i', arg[2]: ae_shift_sd 'i',
 * arg[3]: ars 'i' (AR with shift count).
 */
static void translate_ae_trunca32f64s_l(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in || !arg[3].in) {
        return;
    }
    TCGv_i32 overflow = tcg_temp_new_i32();
    tcg_gen_movi_i32(overflow, 0);

    TCGv_i64 sh64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sh64, arg[3].in);
    tcg_gen_andi_i64(sh64, sh64, 63);

    TCGv_i64 shifted = tcg_temp_new_i64();
    tcg_gen_sar_i64(shifted, (TCGv_i64)arg[1].in, sh64);

    TCGv_i32 lo = ae_sat32_from_i64(shifted, overflow);
    /* Write lower 32 bits of output; zero upper 32 */
    TCGv_i64 lo64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(lo64, lo);
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, lo64);
    tcg_gen_or_i32(cpu_AE_OVERFLOW, cpu_AE_OVERFLOW, overflow);
}

/*
 * ae_slai72 — shift-left 72-bit (EP:DR).
 * Input: 64-bit DR (arg[2].in), immediate shift amount (arg[3].imm).
 * Output: EP (arg[0].out, i32 cast, bits [71:64]) and DR (arg[1].out, i64).
 */
static void translate_ae_slai72(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (!arg[0].out || !arg[1].out || !arg[2].in) {
        return;
    }
    /* opnd_ae_sem_shift_i8 is encoded on 3 bits where 0 represents 8. */
    unsigned sh = arg[3].imm & 0x7;
    if (sh == 0) {
        sh = 8;
    }

    if (sh == 0) {
        tcg_gen_mov_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[2].in);
        tcg_gen_movi_i32((TCGv_i32)arg[0].out, 0);
        return;
    }
    if (sh >= 64) {
        /* bits [sh-64 .. 0] of DR_in go to EP_out[sh-64:0]; DR_out = 0 */
        TCGv_i64 ep64 = tcg_temp_new_i64();
        tcg_gen_shli_i64(ep64, (TCGv_i64)arg[2].in, sh - 64);
        tcg_gen_extrl_i64_i32((TCGv_i32)arg[0].out, ep64);
        tcg_gen_andi_i32((TCGv_i32)arg[0].out, (TCGv_i32)arg[0].out, 0xFF);
        tcg_gen_movi_i64((TCGv_i64)arg[1].out, 0);
        return;
    }
    /* sh ∈ [1, 63]: lower 64 bits → DR_out; bits [64+sh-1:64] → EP_out */
    tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[2].in, sh);
    TCGv_i64 ep64 = tcg_temp_new_i64();
    tcg_gen_shri_i64(ep64, (TCGv_i64)arg[2].in, 64 - sh);
    tcg_gen_extrl_i64_i32((TCGv_i32)arg[0].out, ep64);
    tcg_gen_andi_i32((TCGv_i32)arg[0].out, (TCGv_i32)arg[0].out, 0xFF);
}

/*
 * ae_srai72 — arithmetic shift-right 72-bit (EP:DR) by immediate.
 * Input: EP (arg[1].in, i32 cast, 8-bit), DR (arg[2].in, i64), osa64 imm.
 * Output: DR (arg[0].out, i64) = lower 64 bits of (sign_ext72(EP:DR) >> sh).
 */
static void translate_ae_srai72(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (!arg[0].out || !arg[1].in || !arg[2].in) {
        return;
    }
  /* ae_osa64 is encoded on 6 bits, so the guest-visible range is 0..63. */
  unsigned sh = arg[3].imm & 0x3f;

    if (sh == 0) {
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[2].in);
        return;
    }
    /* Sign-extend EP (8-bit) to i64 */
    TCGv_i32 ep8 = tcg_temp_new_i32();
    tcg_gen_ext8s_i32(ep8, (TCGv_i32)arg[1].in);
    TCGv_i64 ep64 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(ep64, ep8);

    if (sh >= 64) {
        /* Result = EP >> (sh - 64) */
        if (sh > 64) {
            tcg_gen_sari_i64(ep64, ep64, sh - 64);
        }
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, ep64);
        return;
    }
    /* sh ∈ [1, 63]: DR_out = (EP << (64-sh)) | (DR >> sh) */
    TCGv_i64 ep_part = tcg_temp_new_i64();
    TCGv_i64 dr_part = tcg_temp_new_i64();
    tcg_gen_shli_i64(ep_part, ep64, 64 - sh);
    tcg_gen_shri_i64(dr_part, (TCGv_i64)arg[2].in, sh);
    tcg_gen_or_i64((TCGv_i64)arg[0].out, ep_part, dr_part);
}

/* ae_addcirc / ae_addicirc -- circular address (approximated as plain add) */
static void translate_ae_addcirc(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    if (arg[0].out && arg[0].in && arg[1].in) {
        tcg_gen_add_i32(arg[0].out, arg[0].in, arg[1].in);
    }
}

static void translate_ae_addicirc(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (arg[0].out && arg[0].in) {
        tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);
    }
}

/* --- mul32: multiply two 32-bit lanes from 64-bit inputs → 64-bit output --- */
/* par[0]: 0=low, 1=high lane for input1; par[1]: 0=low, 1=high lane for input2 */
static void translate_ae_mul32(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        /* Extract 32-bit lanes as signed integers */
        TCGv_i32 lane1 = ae_extract_lane32((TCGv_i64)arg[1].in, par[0] & 1);
        TCGv_i32 lane2 = ae_extract_lane32((TCGv_i64)arg[2].in, par[1] & 1);
        
        /* Multiply: 32 * 32 = 64-bit result */
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        
        /* Perform 32-bit signed multiplication, result goes to lo:hi */
        tcg_gen_muls2_i32(lo, hi, lane1, lane2);
        
        /* Pack lo and hi into 64-bit output */
        TCGv_i64 result = tcg_temp_new_i64();
        tcg_gen_concat_i32_i64(result, lo, hi);
        tcg_gen_mov_i64(arg[0].out, result);
    }
}

/* mul32.ll - multiply low lane × low lane */
static void translate_ae_mul32_ll(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    uint32_t par_ll[] = {0, 0};  /* both low lanes */
    translate_ae_mul32(dc, arg, par_ll);
}

/* mul32.lh - multiply low lane (arg[1]) × high lane (arg[2]) */
static void translate_ae_mul32_lh(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    uint32_t par_lh[] = {0, 1};  /* low × high */
    translate_ae_mul32(dc, arg, par_lh);
}

/* mul32.hh - multiply high lane × high lane */
static void translate_ae_mul32_hh(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    uint32_t par_hh[] = {1, 1};  /* both high lanes */
    translate_ae_mul32(dc, arg, par_hh);
}

/* --- mula32: multiply-accumulate: acc += lane(d0) * lane(d1) --- */
/* acc is arg[0].in/out (AE_DR, modify); d0=arg[1].in; d1=arg[2].in        */
/* par[0]: 0=low, 1=high for d0; par[1]: 0=low, 1=high for d1              */
static void translate_ae_mula32(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
        TCGv_i32 lane1 = ae_extract_lane32((TCGv_i64)arg[1].in, par[0] & 1);
        TCGv_i32 lane2 = ae_extract_lane32((TCGv_i64)arg[2].in, par[1] & 1);

        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        tcg_gen_muls2_i32(lo, hi, lane1, lane2);

        TCGv_i64 product = tcg_temp_new_i64();
        tcg_gen_concat_i32_i64(product, lo, hi);

        tcg_gen_add_i64(arg[0].out, arg[0].in, product);
    }
}

static void translate_ae_mula32_ll(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    uint32_t p[] = {0, 0};
    translate_ae_mula32(dc, arg, p);
}

static void translate_ae_mula32_lh(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    uint32_t p[] = {0, 1};
    translate_ae_mula32(dc, arg, p);
}

static void translate_ae_mula32_hh(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    uint32_t p[] = {1, 1};
    translate_ae_mula32(dc, arg, p);
}

/* --- mul32u.ll / mula32u.ll: unsigned low-lane 32x32 --- */
static void translate_ae_mul32u_ll(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_mulu2_i32(lo, hi, lhs, rhs);
    tcg_gen_concat_i32_i64(product, lo, hi);
    tcg_gen_mov_i64(arg[0].out, product);
  }
}

static void translate_ae_mula32u_ll(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_mulu2_i32(lo, hi, lhs, rhs);
    tcg_gen_concat_i32_i64(product, lo, hi);
    tcg_gen_add_i64(arg[0].out, arg[0].in, product);
  }
}

static void ae_set_ep_from_i64(TCGv_i32 ep, TCGv_i64 value)
{
  TCGv_i64 sign64 = tcg_temp_new_i64();
  tcg_gen_sari_i64(sign64, value, 63);
  tcg_gen_extrl_i64_i32(ep, sign64);
}

/* --- mul32usep.{ll,lh} / mula32usep.{ll,lh} --- */
static void translate_ae_mul32usep(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
    TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[2].in, par[0] & 1);
    TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[3].in, par[1] & 1);
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_muls2_i32(lo, hi, lhs, rhs);
    tcg_gen_concat_i32_i64(product, lo, hi);
    tcg_gen_mov_i64(arg[1].out, product);
    ae_set_ep_from_i64(arg[0].out, product);
  }
}

static void translate_ae_mul32usep_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
{
  uint32_t p[] = {0, 0};
  translate_ae_mul32usep(dc, arg, p);
}

static void translate_ae_mul32usep_lh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
{
  uint32_t p[] = {0, 1};
  translate_ae_mul32usep(dc, arg, p);
}

static void translate_ae_mula32usep(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].out && arg[1].in && arg[2].in && arg[3].in) {
    TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[2].in, par[0] & 1);
    TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[3].in, par[1] & 1);
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 product = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_muls2_i32(lo, hi, lhs, rhs);
    tcg_gen_concat_i32_i64(product, lo, hi);
    tcg_gen_add_i64(sum, arg[1].in, product);
    tcg_gen_mov_i64(arg[1].out, sum);
    ae_set_ep_from_i64(arg[0].out, sum);
  }
}

static void translate_ae_mula32usep_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  uint32_t p[] = {0, 0};
  translate_ae_mula32usep(dc, arg, p);
}

static void translate_ae_mula32usep_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  uint32_t p[] = {0, 1};
  translate_ae_mula32usep(dc, arg, p);
}

/* --- mul32ep.hh / mula32ep.hh share the same 4-operand form --- */
static void translate_ae_mul32ep_hh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  uint32_t p[] = {1, 1};
  translate_ae_mul32usep(dc, arg, p);
}

static void translate_ae_mula32ep_hh(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  uint32_t p[] = {1, 1};
  translate_ae_mula32usep(dc, arg, p);
}

/* --- mul32x16 / mula32x16 (signed): 32-bit lane * selected 16-bit lane --- */
static void translate_ae_mul32x16(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 lhs32 = ae_extract_lane32((TCGv_i64)arg[1].in, par[0] & 1);
    TCGv_i32 rhs16 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[1] & 3);
    TCGv_i64 lhs64 = tcg_temp_new_i64();
    TCGv_i64 rhs64 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(lhs64, lhs32);
    tcg_gen_ext_i32_i64(rhs64, rhs16);
    tcg_gen_mul_i64(arg[0].out, lhs64, rhs64);
  }
}

static void translate_ae_mula32x16(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 lhs32 = ae_extract_lane32((TCGv_i64)arg[1].in, par[0] & 1);
    TCGv_i32 rhs16 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[1] & 3);
    TCGv_i64 lhs64 = tcg_temp_new_i64();
    TCGv_i64 rhs64 = tcg_temp_new_i64();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(lhs64, lhs32);
    tcg_gen_ext_i32_i64(rhs64, rhs16);
    tcg_gen_mul_i64(product, lhs64, rhs64);
    tcg_gen_add_i64(arg[0].out, arg[0].in, product);
  }
}

#define DEF_MUL32X16_WRAPPERS(_suf, _lane32, _lane16) \
static void translate_ae_mul32x16_##_suf(DisasContext *dc, const OpcodeArg arg[], \
                     const uint32_t par[]) \
{ \
  uint32_t p[] = {_lane32, _lane16}; \
  translate_ae_mul32x16(dc, arg, p); \
} \
static void translate_ae_mula32x16_##_suf(DisasContext *dc, const OpcodeArg arg[], \
                      const uint32_t par[]) \
{ \
  uint32_t p[] = {_lane32, _lane16}; \
  translate_ae_mula32x16(dc, arg, p); \
}

DEF_MUL32X16_WRAPPERS(h0, 1, 0)
DEF_MUL32X16_WRAPPERS(h1, 1, 1)
DEF_MUL32X16_WRAPPERS(h2, 1, 2)
DEF_MUL32X16_WRAPPERS(h3, 1, 3)
DEF_MUL32X16_WRAPPERS(l0, 0, 0)
DEF_MUL32X16_WRAPPERS(l1, 0, 1)
DEF_MUL32X16_WRAPPERS(l2, 0, 2)
DEF_MUL32X16_WRAPPERS(l3, 0, 3)

#undef DEF_MUL32X16_WRAPPERS

/* --- muls32 family: modeled as non-accumulating signed/unsigned multiply --- */
static void translate_ae_muls32(DisasContext *dc, const OpcodeArg arg[],
                const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[1].in, par[0] & 1);
    TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[2].in, par[1] & 1);
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_muls2_i32(lo, hi, lhs, rhs);
    tcg_gen_concat_i32_i64(product, lo, hi);
    tcg_gen_mov_i64(arg[0].out, product);
  }
}

static void translate_ae_muls32_ll(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  uint32_t p[] = {0, 0};
  translate_ae_muls32(dc, arg, p);
}

static void translate_ae_muls32_lh(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  uint32_t p[] = {0, 1};
  translate_ae_muls32(dc, arg, p);
}

static void translate_ae_muls32_hh(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  uint32_t p[] = {1, 1};
  translate_ae_muls32(dc, arg, p);
}

static void translate_ae_muls32u_ll(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_mulu2_i32(lo, hi, lhs, rhs);
    tcg_gen_concat_i32_i64(product, lo, hi);
    tcg_gen_mov_i64(arg[0].out, product);
  }
}

/* muls32ep.hh modeled with current ep.hh baseline path */
static void translate_ae_muls32ep_hh(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  uint32_t p[] = {1, 1};
  translate_ae_mul32usep(dc, arg, p);
}

/* muls32x16 modeled with current 32x16 baseline path */
static void translate_ae_muls32x16_h0(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h0(dc, arg, par); }
static void translate_ae_muls32x16_h1(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h1(dc, arg, par); }
static void translate_ae_muls32x16_h2(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h2(dc, arg, par); }
static void translate_ae_muls32x16_h3(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h3(dc, arg, par); }
static void translate_ae_muls32x16_l0(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l0(dc, arg, par); }
static void translate_ae_muls32x16_l1(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l1(dc, arg, par); }
static void translate_ae_muls32x16_l2(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l2(dc, arg, par); }
static void translate_ae_muls32x16_l3(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l3(dc, arg, par); }

/* mulsf32* baseline mapping onto muls32 lane variants */
static void translate_ae_mulsf32_ll(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_muls32_ll(dc, arg, par); }
static void translate_ae_mulsf32_lh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_muls32_lh(dc, arg, par); }
static void translate_ae_mulsf32_hh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_muls32_hh(dc, arg, par); }

/* mulsf32x16* baseline mapping onto mul32x16 lane variants */
static void translate_ae_mulsf32x16_h0(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_h0(dc, arg, par); }
static void translate_ae_mulsf32x16_h1(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_h1(dc, arg, par); }
static void translate_ae_mulsf32x16_h2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_h2(dc, arg, par); }
static void translate_ae_mulsf32x16_h3(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_h3(dc, arg, par); }
static void translate_ae_mulsf32x16_l0(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_l0(dc, arg, par); }
static void translate_ae_mulsf32x16_l1(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_l1(dc, arg, par); }
static void translate_ae_mulsf32x16_l2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_l2(dc, arg, par); }
static void translate_ae_mulsf32x16_l3(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mul32x16_l3(dc, arg, par); }

/* muls32f48p16s baseline mapping onto muls32 lane variants */
static void translate_ae_muls32f48p16s_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_muls32_ll(dc, arg, par); }
static void translate_ae_muls32f48p16s_lh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_muls32_lh(dc, arg, par); }
static void translate_ae_muls32f48p16s_hh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_muls32_hh(dc, arg, par); }

/* mulaf32* baseline mapping onto mula32 lane variants */
static void translate_ae_mulaf32_ll(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mula32_ll(dc, arg, par); }
static void translate_ae_mulaf32_lh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mula32_lh(dc, arg, par); }
static void translate_ae_mulaf32_hh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mula32_hh(dc, arg, par); }

/* mulaf32x16* baseline mapping onto mula32x16 lane variants */
static void translate_ae_mulaf32x16_h0(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_h0(dc, arg, par); }
static void translate_ae_mulaf32x16_h1(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_h1(dc, arg, par); }
static void translate_ae_mulaf32x16_h2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_h2(dc, arg, par); }
static void translate_ae_mulaf32x16_h3(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_h3(dc, arg, par); }
static void translate_ae_mulaf32x16_l0(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_l0(dc, arg, par); }
static void translate_ae_mulaf32x16_l1(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_l1(dc, arg, par); }
static void translate_ae_mulaf32x16_l2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_l2(dc, arg, par); }
static void translate_ae_mulaf32x16_l3(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32x16_l3(dc, arg, par); }

/* mulas32f48p16s baseline mapping onto mula32 lane variants */
static void translate_ae_mulas32f48p16s_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32_ll(dc, arg, par); }
static void translate_ae_mulas32f48p16s_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32_lh(dc, arg, par); }
static void translate_ae_mulas32f48p16s_hh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mula32_hh(dc, arg, par); }

/* mulf32* baseline mapping onto muls32 lane variants */
static void translate_ae_mulf32_ll(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_muls32_ll(dc, arg, par); }
static void translate_ae_mulf32_lh(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_muls32_lh(dc, arg, par); }
static void translate_ae_mulf32_hh(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_muls32_hh(dc, arg, par); }

/* mulf32x16* baseline mapping onto mul32x16 lane variants */
static void translate_ae_mulf32x16_h0(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h0(dc, arg, par); }
static void translate_ae_mulf32x16_h1(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h1(dc, arg, par); }
static void translate_ae_mulf32x16_h2(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h2(dc, arg, par); }
static void translate_ae_mulf32x16_h3(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_h3(dc, arg, par); }
static void translate_ae_mulf32x16_l0(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l0(dc, arg, par); }
static void translate_ae_mulf32x16_l1(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l1(dc, arg, par); }
static void translate_ae_mulf32x16_l2(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l2(dc, arg, par); }
static void translate_ae_mulf32x16_l3(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mul32x16_l3(dc, arg, par); }

/* ================================================================
 * _s2 (scale-by-2) wrappers: product is scaled << 1 after compute.
 *
 * Pure-multiply _s2:  out = product << 1
 *   → call base, then shift arg[0].out left by 1.
 *
 * MAC _s2:  out = acc + (product << 1)
 *   → call base: arg[0].out = acc + product
 *   → then: arg[0].out = 2*(acc+product) - acc_old = acc + 2*product
 *   Safe because arg[0].in (read-temp for old acc) is distinct from
 *   arg[0].out (write-temp) in the Xtensa decode infrastructure.
 *
 * EP variants: same tricks applied to arg[1] (the 64-bit data reg);
 *   arg[0] (the EP overflow reg) is recomputed from the shifted result.
 * ================================================================ */

#define DEF_MUL_S2_PURE(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
    translate_ae_##_fn(dc, arg, par); \
    if (arg[0].out) { \
        tcg_gen_shli_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, 1); \
    } \
}

#define DEF_MUL_S2_MAC(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
    translate_ae_##_fn(dc, arg, par); \
    if (arg[0].out && arg[0].in) { \
        tcg_gen_shli_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, 1); \
        tcg_gen_sub_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, \
                        (TCGv_i64)arg[0].in); \
    } \
}

/* --- mul32 pure-multiply _s2 --- */
DEF_MUL_S2_PURE(mul32_hh)
DEF_MUL_S2_PURE(mul32_lh)
DEF_MUL_S2_PURE(mul32_ll)
DEF_MUL_S2_PURE(mul32x16_h0)
DEF_MUL_S2_PURE(mul32x16_h1)
DEF_MUL_S2_PURE(mul32x16_h2)
DEF_MUL_S2_PURE(mul32x16_h3)
DEF_MUL_S2_PURE(mul32x16_l0)
DEF_MUL_S2_PURE(mul32x16_l1)
DEF_MUL_S2_PURE(mul32x16_l2)
DEF_MUL_S2_PURE(mul32x16_l3)

/* mul32ep.hh_s2: pure-multiply with EP output (arg[0]=EP, arg[1]=64-bit data) */
static void translate_ae_mul32ep_hh_s2(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    translate_ae_mul32ep_hh(dc, arg, par);
    if (arg[0].out && arg[1].out) {
        tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
        ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
    }
}

/* --- muls32 pure-multiply _s2 --- */
DEF_MUL_S2_PURE(muls32_hh)
DEF_MUL_S2_PURE(muls32_lh)
DEF_MUL_S2_PURE(muls32_ll)
DEF_MUL_S2_PURE(muls32x16_h0)
DEF_MUL_S2_PURE(muls32x16_h1)
DEF_MUL_S2_PURE(muls32x16_h2)
DEF_MUL_S2_PURE(muls32x16_h3)
DEF_MUL_S2_PURE(muls32x16_l0)
DEF_MUL_S2_PURE(muls32x16_l1)
DEF_MUL_S2_PURE(muls32x16_l2)
DEF_MUL_S2_PURE(muls32x16_l3)

/* muls32ep.hh_s2: pure-multiply with EP output */
static void translate_ae_muls32ep_hh_s2(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    translate_ae_muls32ep_hh(dc, arg, par);
    if (arg[0].out && arg[1].out) {
        tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
        ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
    }
}

/* --- mulf32 pure-multiply _s2 (baseline delegates to muls32) --- */
DEF_MUL_S2_PURE(mulf32_hh)
DEF_MUL_S2_PURE(mulf32_lh)
DEF_MUL_S2_PURE(mulf32_ll)
DEF_MUL_S2_PURE(mulf32x16_h0)
DEF_MUL_S2_PURE(mulf32x16_h1)
DEF_MUL_S2_PURE(mulf32x16_h2)
DEF_MUL_S2_PURE(mulf32x16_h3)
DEF_MUL_S2_PURE(mulf32x16_l0)
DEF_MUL_S2_PURE(mulf32x16_l1)
DEF_MUL_S2_PURE(mulf32x16_l2)
DEF_MUL_S2_PURE(mulf32x16_l3)

/* --- mulsf32 pure-multiply _s2 (baseline delegates to muls32) --- */
DEF_MUL_S2_PURE(mulsf32_hh)
DEF_MUL_S2_PURE(mulsf32_lh)
DEF_MUL_S2_PURE(mulsf32_ll)
DEF_MUL_S2_PURE(mulsf32x16_h0)
DEF_MUL_S2_PURE(mulsf32x16_h1)
DEF_MUL_S2_PURE(mulsf32x16_h2)
DEF_MUL_S2_PURE(mulsf32x16_h3)
DEF_MUL_S2_PURE(mulsf32x16_l0)
DEF_MUL_S2_PURE(mulsf32x16_l1)
DEF_MUL_S2_PURE(mulsf32x16_l2)
DEF_MUL_S2_PURE(mulsf32x16_l3)

/* --- muls32f48p16s pure-multiply _s2 --- */
DEF_MUL_S2_PURE(muls32f48p16s_hh)
DEF_MUL_S2_PURE(muls32f48p16s_lh)
DEF_MUL_S2_PURE(muls32f48p16s_ll)

/*
 * mula32.*_s2: slot-2 variants with identical semantics to the base form.
 * Using DEF_MUL_S2_MAC is incorrect when arg[0] aliases in/out ('m' operand)
 * because shift-then-subtract cancels to zero.
 */
#define DEF_MULA32_S2(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ translate_ae_##_fn(dc, arg, par); }

DEF_MULA32_S2(mula32_hh)
DEF_MULA32_S2(mula32_lh)
DEF_MULA32_S2(mula32_ll)
DEF_MULA32_S2(mula32x16_h0)
DEF_MULA32_S2(mula32x16_h1)
DEF_MULA32_S2(mula32x16_h2)
DEF_MULA32_S2(mula32x16_h3)
DEF_MULA32_S2(mula32x16_l0)
DEF_MULA32_S2(mula32x16_l1)
DEF_MULA32_S2(mula32x16_l2)
DEF_MULA32_S2(mula32x16_l3)

#undef DEF_MULA32_S2

/* mula32ep.hh_s2: MAC with EP output */
static void translate_ae_mula32ep_hh_s2(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    translate_ae_mula32ep_hh(dc, arg, par);
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
    TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[3].in, 1);
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_muls2_i32(lo, hi, lhs, rhs);
    tcg_gen_concat_i32_i64(product, lo, hi);
    tcg_gen_add_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, product);
        ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
    }
}

/*
 * mulaf32.*_s2: slot-2 variants with identical semantics to the base form.
 * DEF_MUL_S2_MAC is wrong here because arg[0].in and arg[0].out alias the
 * same TCGv_i64 ('m' operand), causing the shift-then-subtract to cancel
 * to zero.  Simply delegate to the base translator unchanged.
 */
#define DEF_MULAF32_S2(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                     const uint32_t par[]) \
{ translate_ae_##_fn(dc, arg, par); }

DEF_MULAF32_S2(mulaf32_hh)
DEF_MULAF32_S2(mulaf32_lh)
DEF_MULAF32_S2(mulaf32_ll)
DEF_MULAF32_S2(mulaf32x16_h0)
DEF_MULAF32_S2(mulaf32x16_h1)
DEF_MULAF32_S2(mulaf32x16_h2)
DEF_MULAF32_S2(mulaf32x16_h3)
DEF_MULAF32_S2(mulaf32x16_l0)
DEF_MULAF32_S2(mulaf32x16_l1)
DEF_MULAF32_S2(mulaf32x16_l2)
DEF_MULAF32_S2(mulaf32x16_l3)

#undef DEF_MULAF32_S2

/*
 * mulas32f48p16s.*_s2 carry AE_OVERFLOW state and the generic
 * "shift then subtract old accumulator" wrapper does not reliably see the
 * pre-accumulate q0 input for these forms. Recompute the lane product from
 * the original inputs and add it once more after the base MAC result.
 */
static void translate_ae_mulas32f48p16s_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                              const uint32_t par[])
{
    translate_ae_mulas32f48p16s_ll(dc, arg, par);
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        TCGv_i64 product = tcg_temp_new_i64();

        tcg_gen_muls2_i32(lo, hi, lhs, rhs);
        tcg_gen_concat_i32_i64(product, lo, hi);
        tcg_gen_add_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, product);
    }
}

static void translate_ae_mulas32f48p16s_lh_s2(DisasContext *dc, const OpcodeArg arg[],
                                              const uint32_t par[])
{
    translate_ae_mulas32f48p16s_lh(dc, arg, par);
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        TCGv_i64 product = tcg_temp_new_i64();

        tcg_gen_muls2_i32(lo, hi, lhs, rhs);
        tcg_gen_concat_i32_i64(product, lo, hi);
        tcg_gen_add_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, product);
    }
}

static void translate_ae_mulas32f48p16s_hh_s2(DisasContext *dc, const OpcodeArg arg[],
                                              const uint32_t par[])
{
    translate_ae_mulas32f48p16s_hh(dc, arg, par);
    if (arg[0].out && arg[1].in && arg[2].in) {
        TCGv_i32 lhs = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        TCGv_i32 rhs = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        TCGv_i64 product = tcg_temp_new_i64();

        tcg_gen_muls2_i32(lo, hi, lhs, rhs);
        tcg_gen_concat_i32_i64(product, lo, hi);
        tcg_gen_add_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, product);
    }
}

#undef DEF_MUL_S2_PURE
#undef DEF_MUL_S2_MAC

static void translate_ae_mulsad32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_add_i64(arg[0].out, arg[0].in, sum);
  }
}

static void translate_ae_mulsad32x16_common(DisasContext *dc, const OpcodeArg arg[],
                        unsigned hi16_lane, unsigned lo16_lane)
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo16 = ae_extract_lane16s((TCGv_i64)arg[2].in, lo16_lane);
    TCGv_i32 d1_hi16 = ae_extract_lane16s((TCGv_i64)arg[2].in, hi16_lane);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo16);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi16);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_add_i64(arg[0].out, arg[0].in, sum);
  }
}

static void translate_ae_mulsad32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulsad32x16_common(dc, arg, 1, 0);
}

static void translate_ae_mulsad32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulsad32x16_common(dc, arg, 3, 2);
}

/* baseline map for mulsafd* pairwise forms */
static void translate_ae_mulsafd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulsafd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulsafd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulsad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulsafd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulsad32x16_h3_l2(dc, arg, par); }

/* baseline map for mulasd/mulasfd* pairwise forms */
static void translate_ae_mulasd32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulasd32_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulasd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulasd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32x16_h3_l2(dc, arg, par); }

static void translate_ae_mulasfd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulasfd32s_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulasfd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulasfd32ra_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulasfd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulsad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulasfd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulsad32x16_h3_l2(dc, arg, par); }

/* mulaad/mulaafd baseline mapping: accumulate pairwise products into 64-bit acc */
static void translate_ae_mulaad32(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[2].in, par[1] & 1);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[2].in, par[0] & 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_add_i64(arg[0].out, arg[0].in, sum);
  }
}

static void translate_ae_mulaad32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  uint32_t p[] = {1, 0};
  translate_ae_mulaad32(dc, arg, p);
}

static void translate_ae_mulaad32_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  uint32_t p[] = {0, 1};
  translate_ae_mulaad32(dc, arg, p);
}

static void translate_ae_mulaad32usep(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
{
  if (arg[0].out && arg[1].out && arg[1].in && arg[2].in && arg[3].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[3].in, par[1] & 1);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[3].in, par[0] & 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 prod_sum = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(prod_sum, p0, p1);
    tcg_gen_add_i64(sum, arg[1].in, prod_sum);
    tcg_gen_mov_i64(arg[1].out, sum);
    ae_set_ep_from_i64(arg[0].out, sum);
  }
}

static void translate_ae_mulaad32ep_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
{
  uint32_t p[] = {1, 0};
  translate_ae_mulaad32usep(dc, arg, p);
}

static void translate_ae_mulaad32usep_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  uint32_t p[] = {0, 1};
  translate_ae_mulaad32usep(dc, arg, p);
}

static void translate_ae_mulzaad32(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[2].in, par[1] & 1);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[2].in, par[0] & 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_mov_i64(arg[0].out, sum);
  }
}

static void translate_ae_mulzaad32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  uint32_t p[] = {1, 0};
  translate_ae_mulzaad32(dc, arg, p);
}

static void translate_ae_mulzaad32_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  uint32_t p[] = {0, 1};
  translate_ae_mulzaad32(dc, arg, p);
}

static void translate_ae_mulzaad32usep(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[3].in, par[1] & 1);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[3].in, par[0] & 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_mov_i64(arg[1].out, sum);
    ae_set_ep_from_i64(arg[0].out, sum);
  }
}

static void translate_ae_mulzaad32ep_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  uint32_t p[] = {1, 0};
  translate_ae_mulzaad32usep(dc, arg, p);
}

static void translate_ae_mulzaad32usep_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
  uint32_t p[] = {0, 1};
  translate_ae_mulzaad32usep(dc, arg, p);
}

static void translate_ae_mulaad32x16_common(DisasContext *dc, const OpcodeArg arg[],
                        unsigned hi16_lane, unsigned lo16_lane)
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo16 = ae_extract_lane16s((TCGv_i64)arg[2].in, lo16_lane);
    TCGv_i32 d1_hi16 = ae_extract_lane16s((TCGv_i64)arg[2].in, hi16_lane);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo16);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi16);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_add_i64(arg[0].out, arg[0].in, sum);
  }
}

static void translate_ae_mulaad32x16_h0_l1(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulaad32x16_common(dc, arg, 0, 1);
}

static void translate_ae_mulaad32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulaad32x16_common(dc, arg, 1, 0);
}

static void translate_ae_mulaad32x16_h2_l3(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulaad32x16_common(dc, arg, 2, 3);
}

static void translate_ae_mulaad32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulaad32x16_common(dc, arg, 3, 2);
}

static void translate_ae_mulzaad32x16_common(DisasContext *dc, const OpcodeArg arg[],
                         unsigned hi16_lane, unsigned lo16_lane)
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo16 = ae_extract_lane16s((TCGv_i64)arg[2].in, lo16_lane);
    TCGv_i32 d1_hi16 = ae_extract_lane16s((TCGv_i64)arg[2].in, hi16_lane);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo16);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi16);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_mov_i64(arg[0].out, sum);
  }
}

static void translate_ae_mulzaad32x16_h0_l1(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulzaad32x16_common(dc, arg, 0, 1);
}

static void translate_ae_mulzaad32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulzaad32x16_common(dc, arg, 1, 0);
}

static void translate_ae_mulzaad32x16_h2_l3(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulzaad32x16_common(dc, arg, 2, 3);
}

static void translate_ae_mulzaad32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulzaad32x16_common(dc, arg, 3, 2);
}

/* baseline map for mulaafd* pairwise forms */
static void translate_ae_mulaafd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulaafd32s_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulaafd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulaafd32ra_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulaafd32x16_h0_l1(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulaad32x16_h0_l1(dc, arg, par); }
static void translate_ae_mulaafd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulaafd32x16_h2_l3(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulaad32x16_h2_l3(dc, arg, par); }
static void translate_ae_mulaafd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulaad32x16_h3_l2(dc, arg, par); }

static void translate_ae_mulzaafd16ss(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 l0 = ae_extract_lane16s((TCGv_i64)arg[1].in, par[0] & 3);
    TCGv_i32 r0 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[1] & 3);
    TCGv_i32 l1 = ae_extract_lane16s((TCGv_i64)arg[1].in, par[2] & 3);
    TCGv_i32 r1 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[3] & 3);
    TCGv_i64 l64 = tcg_temp_new_i64();
    TCGv_i64 r64 = tcg_temp_new_i64();
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(l64, l0);
    tcg_gen_ext_i32_i64(r64, r0);
    tcg_gen_mul_i64(p0, l64, r64);
    tcg_gen_ext_i32_i64(l64, l1);
    tcg_gen_ext_i32_i64(r64, r1);
    tcg_gen_mul_i64(p1, l64, r64);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_mov_i64(arg[0].out, sum);
  }
}

static void translate_ae_mulzaafd16ss_11_00(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  uint32_t p[] = {1, 1, 0, 0};
  translate_ae_mulzaafd16ss(dc, arg, p);
}

static void translate_ae_mulzaafd16ss_13_02(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  uint32_t p[] = {1, 3, 0, 2};
  translate_ae_mulzaafd16ss(dc, arg, p);
}

static void translate_ae_mulzaafd16ss_33_22(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  uint32_t p[] = {3, 3, 2, 2};
  translate_ae_mulzaafd16ss(dc, arg, p);
}

static void translate_ae_mulzaafd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzaafd32s_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzaafd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzaafd32ra_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzaafd32x16_h0_l1(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h0_l1(dc, arg, par); }
static void translate_ae_mulzaafd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulzaafd32x16_h2_l3(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h2_l3(dc, arg, par); }
static void translate_ae_mulzaafd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h3_l2(dc, arg, par); }
static void translate_ae_mulzasd32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzasd32_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzasd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulzasd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32x16_h3_l2(dc, arg, par); }
static void translate_ae_mulzasfd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzasfd32s_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzasfd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzasfd32ra_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzasfd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulzasfd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h3_l2(dc, arg, par); }
static void translate_ae_mulzsad32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzsad32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulzsad32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32x16_h3_l2(dc, arg, par); }
static void translate_ae_mulzsafd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzsafd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzsafd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulzsafd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h3_l2(dc, arg, par); }
static void translate_ae_mulzssd32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzssd32_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzssd32ep_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32ep_hh_ll(dc, arg, par); }
static void translate_ae_mulzssd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulzssd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32x16_h3_l2(dc, arg, par); }
static void translate_ae_mulzssfd16ss_11_00(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaafd16ss_11_00(dc, arg, par); }
static void translate_ae_mulzssfd16ss_13_02(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaafd16ss_13_02(dc, arg, par); }
static void translate_ae_mulzssfd16ss_33_22(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaafd16ss_33_22(dc, arg, par); }
static void translate_ae_mulzssfd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzssfd32s_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzssfd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32_hh_ll(dc, arg, par); }
static void translate_ae_mulzssfd32ra_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulzaad32_hl_lh(dc, arg, par); }
static void translate_ae_mulzssfd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulzssfd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[]) { translate_ae_mulzaad32x16_h3_l2(dc, arg, par); }

/* mulac baseline mapping onto pairwise accumulate forms */
static void translate_ae_mulac32(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  translate_ae_mulaad32_hh_ll(dc, arg, par);
}

static void translate_ae_mulac32x16_l(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  translate_ae_mulaad32x16_h1_l0(dc, arg, par);
}

static void translate_ae_mulac32x16_h(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  translate_ae_mulaad32x16_h3_l2(dc, arg, par);
}

/* mulc/mulfc baseline: non-accumulating pairwise sum into q0 */
static void translate_ae_mulc32(DisasContext *dc, const OpcodeArg arg[],
                const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64((TCGv_i64)arg[0].out, p0, p1);
  }
}

static void translate_ae_mulc32x16_common(DisasContext *dc, const OpcodeArg arg[],
                      unsigned hi16_lane, unsigned lo16_lane)
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
    TCGv_i32 d1_lo16 = ae_extract_lane16s((TCGv_i64)arg[2].in, lo16_lane);
    TCGv_i32 d1_hi16 = ae_extract_lane16s((TCGv_i64)arg[2].in, hi16_lane);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo16);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi16);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64((TCGv_i64)arg[0].out, p0, p1);
  }
}

static void translate_ae_mulc32x16_l(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  translate_ae_mulc32x16_common(dc, arg, 1, 0);
}

static void translate_ae_mulc32x16_h(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  translate_ae_mulc32x16_common(dc, arg, 3, 2);
}

/* mulfc baseline mapping onto mulc and mulafc onto mulac */
static void translate_ae_mulfc24ra(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }
static void translate_ae_mulfc32ras(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }
static void translate_ae_mulfc32x16ras_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }
static void translate_ae_mulfc32x16ras_h(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulc32x16_h(dc, arg, par); }

static void translate_ae_mulafc24ra(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }
static void translate_ae_mulafc32ras(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }
static void translate_ae_mulafc32x16ras_l(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulafc32x16ras_h(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }

/* mulafp/mulap/mulaq baseline: map onto existing pairwise accumulate helpers */
static void translate_ae_mulafp24x2r(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }
static void translate_ae_mulafp24x2ra(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }

static void translate_ae_mulafp32x16x2ras_h(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulafp32x16x2ras_l(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulafp32x16x2rs_h(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulafp32x16x2rs_l(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulafp32x16x2s_h(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulafp32x16x2s_l(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }

static void translate_ae_mulafp32x2ras(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }
static void translate_ae_mulafp32x2rs(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }

static void translate_ae_mulap32x16x2_h(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulap32x16x2_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulap32x2(DisasContext *dc, const OpcodeArg arg[],
                const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }

static void translate_ae_mulaq32sp16s_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulaq32sp16u_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }

static void translate_ae_mulaf48q32sp16s_l(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulaf48q32sp16u_l(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }

static void translate_ae_mulafd32x_fir_common(DisasContext *dc, const OpcodeArg arg[],
                          unsigned q0_hi16_lane,
                          unsigned q0_lo16_lane,
                          unsigned q1_hi16_lane,
                          unsigned q1_lo16_lane)
{
  if (arg[0].out && arg[0].in && arg[1].out && arg[1].in &&
      arg[2].in && arg[3].in && arg[4].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_lo16 = ae_extract_lane16s((TCGv_i64)arg[3].in, q0_lo16_lane);
    TCGv_i32 d1_hi16 = ae_extract_lane16s((TCGv_i64)arg[3].in, q0_hi16_lane);
    TCGv_i32 d2_lo16 = ae_extract_lane16s((TCGv_i64)arg[4].in, q1_lo16_lane);
    TCGv_i32 d2_hi16 = ae_extract_lane16s((TCGv_i64)arg[4].in, q1_hi16_lane);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 p2 = tcg_temp_new_i64();
    TCGv_i64 p3 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 sum0 = tcg_temp_new_i64();
    TCGv_i64 sum1 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo16);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi16);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(sum0, p0, p1);

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d2_lo16);
    tcg_gen_mul_i64(p2, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d2_hi16);
    tcg_gen_mul_i64(p3, t0, t1);
    tcg_gen_add_i64(sum1, p2, p3);

    tcg_gen_add_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].in, sum0);
    tcg_gen_add_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].in, sum1);
  }
}

static void translate_ae_mulafd32x16x2_fir_hh(DisasContext *dc,
                           const OpcodeArg arg[],
                           const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 3, 2, 3, 2);
}

static void translate_ae_mulafd32x16x2_fir_hl(DisasContext *dc,
                           const OpcodeArg arg[],
                           const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 3, 2, 1, 0);
}

static void translate_ae_mulafd32x16x2_fir_lh(DisasContext *dc,
                           const OpcodeArg arg[],
                           const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 1, 0, 3, 2);
}

static void translate_ae_mulafd32x16x2_fir_ll(DisasContext *dc,
                           const OpcodeArg arg[],
                           const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 1, 0, 1, 0);
}

static void translate_ae_mulafd32x2ra_fir_h(DisasContext *dc,
                         const OpcodeArg arg[],
                         const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 3, 2, 3, 2);
}

static void translate_ae_mulafd32x2ra_fir_l(DisasContext *dc,
                         const OpcodeArg arg[],
                         const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 1, 0, 1, 0);
}

static void translate_ae_mulafd32x2s_fir_h(DisasContext *dc,
                        const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 3, 2, 3, 2);
}

static void translate_ae_mulafd32x2s_fir_l(DisasContext *dc,
                        const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulafd32x_fir_common(dc, arg, 1, 0, 1, 0);
}

static void translate_ae_mulfd32x_fir_common(DisasContext *dc, const OpcodeArg arg[],
                         unsigned q0_hi16_lane,
                         unsigned q0_lo16_lane,
                         unsigned q1_hi16_lane,
                         unsigned q1_lo16_lane)
{
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in && arg[4].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_lo16 = ae_extract_lane16s((TCGv_i64)arg[3].in, q0_lo16_lane);
    TCGv_i32 d1_hi16 = ae_extract_lane16s((TCGv_i64)arg[3].in, q0_hi16_lane);
    TCGv_i32 d2_lo16 = ae_extract_lane16s((TCGv_i64)arg[4].in, q1_lo16_lane);
    TCGv_i32 d2_hi16 = ae_extract_lane16s((TCGv_i64)arg[4].in, q1_hi16_lane);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 p2 = tcg_temp_new_i64();
    TCGv_i64 p3 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo16);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi16);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64((TCGv_i64)arg[0].out, p0, p1);

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d2_lo16);
    tcg_gen_mul_i64(p2, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d2_hi16);
    tcg_gen_mul_i64(p3, t0, t1);
    tcg_gen_add_i64((TCGv_i64)arg[1].out, p2, p3);
  }
}

static void translate_ae_mulfd32x16x2_fir_hh(DisasContext *dc,
                          const OpcodeArg arg[],
                          const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 3, 2, 3, 2);
}

static void translate_ae_mulfd32x16x2_fir_hl(DisasContext *dc,
                          const OpcodeArg arg[],
                          const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 3, 2, 1, 0);
}

static void translate_ae_mulfd32x16x2_fir_lh(DisasContext *dc,
                          const OpcodeArg arg[],
                          const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 1, 0, 3, 2);
}

static void translate_ae_mulfd32x16x2_fir_ll(DisasContext *dc,
                          const OpcodeArg arg[],
                          const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 1, 0, 1, 0);
}

static void translate_ae_mulfd32x2ra_fir_h(DisasContext *dc,
                        const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 3, 2, 3, 2);
}

static void translate_ae_mulfd32x2ra_fir_l(DisasContext *dc,
                        const OpcodeArg arg[],
                        const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 1, 0, 1, 0);
}

static void translate_ae_mulfd32x2s_fir_h(DisasContext *dc,
                       const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 3, 2, 3, 2);
}

static void translate_ae_mulfd32x2s_fir_l(DisasContext *dc,
                       const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulfd32x_fir_common(dc, arg, 1, 0, 1, 0);
}

static void translate_ae_mulfp16x4s(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }
static void translate_ae_mulfp16x4ras(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }

static void translate_ae_mulf48q32sp16s_l(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }
static void translate_ae_mulf48q32sp16u_l(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }

static void translate_ae_mulfp24x2r(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }
static void translate_ae_mulfp24x2ra(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }

static void translate_ae_mulfp32x16x2ras_h(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulc32x16_h(dc, arg, par); }
static void translate_ae_mulfp32x16x2ras_l(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }
static void translate_ae_mulfp32x16x2rs_h(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulc32x16_h(dc, arg, par); }
static void translate_ae_mulfp32x16x2rs_l(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }
static void translate_ae_mulfp32x16x2s_h(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulc32x16_h(dc, arg, par); }
static void translate_ae_mulfp32x16x2s_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }

static void translate_ae_mulfp32x2ras(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }
static void translate_ae_mulfp32x2rs(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }

static void translate_ae_mulp32x16x2_h(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulc32x16_h(dc, arg, par); }
static void translate_ae_mulp32x16x2_l(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }
static void translate_ae_mulp32x2(DisasContext *dc, const OpcodeArg arg[],
               const uint32_t par[]) { translate_ae_mulc32(dc, arg, par); }

static void translate_ae_mulq32sp16s_l(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }
static void translate_ae_mulq32sp16u_l(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulc32x16_l(dc, arg, par); }

/* mulaf16ss baseline: q0 += sext16(d0[l0]) * sext16(d1[l1]) */
static void translate_ae_mulaf16ss(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 lhs16 = ae_extract_lane16s((TCGv_i64)arg[1].in, par[0] & 3);
    TCGv_i32 rhs16 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[1] & 3);
    TCGv_i64 lhs64 = tcg_temp_new_i64();
    TCGv_i64 rhs64 = tcg_temp_new_i64();
    TCGv_i64 product = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(lhs64, lhs16);
    tcg_gen_ext_i32_i64(rhs64, rhs16);
    tcg_gen_mul_i64(product, lhs64, rhs64);
    tcg_gen_add_i64(arg[0].out, arg[0].in, product);
  }
}

#define DEF_MULAF16SS(_suf, _l0, _l1) \
static void translate_ae_mulaf16ss_##_suf(DisasContext *dc, const OpcodeArg arg[], \
                      const uint32_t par[]) \
{ \
  uint32_t p[] = {_l0, _l1}; \
  translate_ae_mulaf16ss(dc, arg, p); \
}

DEF_MULAF16SS(00, 0, 0)
DEF_MULAF16SS(10, 1, 0)
DEF_MULAF16SS(11, 1, 1)
DEF_MULAF16SS(20, 2, 0)
DEF_MULAF16SS(21, 2, 1)
DEF_MULAF16SS(22, 2, 2)
DEF_MULAF16SS(30, 3, 0)
DEF_MULAF16SS(31, 3, 1)
DEF_MULAF16SS(32, 3, 2)
DEF_MULAF16SS(33, 3, 3)

#undef DEF_MULAF16SS

/* mulaafd16ss baseline: q0 += d0[a]*d1[b] + d0[c]*d1[d] */
static void translate_ae_mulaafd16ss(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
    TCGv_i32 l0 = ae_extract_lane16s((TCGv_i64)arg[1].in, par[0] & 3);
    TCGv_i32 r0 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[1] & 3);
    TCGv_i32 l1 = ae_extract_lane16s((TCGv_i64)arg[1].in, par[2] & 3);
    TCGv_i32 r1 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[3] & 3);
    TCGv_i64 l64 = tcg_temp_new_i64();
    TCGv_i64 r64 = tcg_temp_new_i64();
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(l64, l0);
    tcg_gen_ext_i32_i64(r64, r0);
    tcg_gen_mul_i64(p0, l64, r64);
    tcg_gen_ext_i32_i64(l64, l1);
    tcg_gen_ext_i32_i64(r64, r1);
    tcg_gen_mul_i64(p1, l64, r64);
    tcg_gen_add_i64(sum, p0, p1);
    tcg_gen_add_i64(arg[0].out, arg[0].in, sum);
  }
}

static void translate_ae_mulaafd16ss_11_00(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  uint32_t p[] = {1, 1, 0, 0};
  translate_ae_mulaafd16ss(dc, arg, p);
}

static void translate_ae_mulaafd16ss_13_02(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  uint32_t p[] = {1, 3, 0, 2};
  translate_ae_mulaafd16ss(dc, arg, p);
}

static void translate_ae_mulaafd16ss_33_22(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  uint32_t p[] = {3, 3, 2, 2};
  translate_ae_mulaafd16ss(dc, arg, p);
}

/* mulf16ss baseline: q0 = sext16(d0[l0]) * sext16(d1[l1]) */
static void translate_ae_mulf16ss(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in) {
    TCGv_i32 lhs16 = ae_extract_lane16s((TCGv_i64)arg[1].in, par[0] & 3);
    TCGv_i32 rhs16 = ae_extract_lane16s((TCGv_i64)arg[2].in, par[1] & 3);
    TCGv_i64 lhs64 = tcg_temp_new_i64();
    TCGv_i64 rhs64 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(lhs64, lhs16);
    tcg_gen_ext_i32_i64(rhs64, rhs16);
    tcg_gen_mul_i64(arg[0].out, lhs64, rhs64);
  }
}

#define DEF_MULF16SS(_suf, _l0, _l1) \
static void translate_ae_mulf16ss_##_suf(DisasContext *dc, const OpcodeArg arg[], \
                     const uint32_t par[]) \
{ \
  uint32_t p[] = {_l0, _l1}; \
  translate_ae_mulf16ss(dc, arg, p); \
}

DEF_MULF16SS(00, 0, 0)
DEF_MULF16SS(10, 1, 0)
DEF_MULF16SS(11, 1, 1)
DEF_MULF16SS(20, 2, 0)
DEF_MULF16SS(21, 2, 1)
DEF_MULF16SS(22, 2, 2)
DEF_MULF16SS(30, 3, 0)
DEF_MULF16SS(31, 3, 1)
DEF_MULF16SS(32, 3, 2)
DEF_MULF16SS(33, 3, 3)

#undef DEF_MULF16SS

/* x4 16-bit baseline: q0 handles lanes {0,1}, q1 handles lanes {2,3} */
static void translate_ae_mulx16x4ss(DisasContext *dc, const OpcodeArg arg[],
                    bool accumulate)
{
  /* arg[0]=q1, arg[1]=q0, arg[2]=d1, arg[3]=d0 */
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in &&
      (!accumulate || (arg[0].in && arg[1].in))) {
    TCGv_i32 d0_l0 = ae_extract_lane16s((TCGv_i64)arg[3].in, 0);
    TCGv_i32 d0_l1 = ae_extract_lane16s((TCGv_i64)arg[3].in, 1);
    TCGv_i32 d0_l2 = ae_extract_lane16s((TCGv_i64)arg[3].in, 2);
    TCGv_i32 d0_l3 = ae_extract_lane16s((TCGv_i64)arg[3].in, 3);
    TCGv_i32 d1_l0 = ae_extract_lane16s((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d1_l1 = ae_extract_lane16s((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_l2 = ae_extract_lane16s((TCGv_i64)arg[2].in, 2);
    TCGv_i32 d1_l3 = ae_extract_lane16s((TCGv_i64)arg[2].in, 3);
    TCGv_i64 l64 = tcg_temp_new_i64();
    TCGv_i64 r64 = tcg_temp_new_i64();
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 p2 = tcg_temp_new_i64();
    TCGv_i64 p3 = tcg_temp_new_i64();
    TCGv_i64 sum01 = tcg_temp_new_i64();
    TCGv_i64 sum23 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(l64, d0_l0);
    tcg_gen_ext_i32_i64(r64, d1_l0);
    tcg_gen_mul_i64(p0, l64, r64);
    tcg_gen_ext_i32_i64(l64, d0_l1);
    tcg_gen_ext_i32_i64(r64, d1_l1);
    tcg_gen_mul_i64(p1, l64, r64);
    tcg_gen_add_i64(sum01, p0, p1);

    tcg_gen_ext_i32_i64(l64, d0_l2);
    tcg_gen_ext_i32_i64(r64, d1_l2);
    tcg_gen_mul_i64(p2, l64, r64);
    tcg_gen_ext_i32_i64(l64, d0_l3);
    tcg_gen_ext_i32_i64(r64, d1_l3);
    tcg_gen_mul_i64(p3, l64, r64);
    tcg_gen_add_i64(sum23, p2, p3);

    if (accumulate) {
      tcg_gen_add_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].in, sum23);
      tcg_gen_add_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].in, sum01);
    } else {
      tcg_gen_mov_i64((TCGv_i64)arg[0].out, sum23);
      tcg_gen_mov_i64((TCGv_i64)arg[1].out, sum01);
    }
  }
}

static void translate_ae_mulf16x4ss(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  translate_ae_mulx16x4ss(dc, arg, false);
}

static void translate_ae_mulaf16x4ss(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  translate_ae_mulx16x4ss(dc, arg, true);
}

static void translate_ae_mulsf16x4ss(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  translate_ae_mulx16x4ss(dc, arg, true);
}

static void translate_ae_muls16x4(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  translate_ae_mulx16x4ss(dc, arg, true);
}

/* mulsf16ss baseline: map to current 16-bit MAC lane semantics */
static void translate_ae_mulsf16ss_00(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_00(dc, arg, par); }
static void translate_ae_mulsf16ss_10(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_10(dc, arg, par); }
static void translate_ae_mulsf16ss_11(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_11(dc, arg, par); }
static void translate_ae_mulsf16ss_20(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_20(dc, arg, par); }
static void translate_ae_mulsf16ss_21(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_21(dc, arg, par); }
static void translate_ae_mulsf16ss_22(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_22(dc, arg, par); }
static void translate_ae_mulsf16ss_30(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_30(dc, arg, par); }
static void translate_ae_mulsf16ss_31(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_31(dc, arg, par); }
static void translate_ae_mulsf16ss_32(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_32(dc, arg, par); }

static void translate_ae_mul16_00(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[]) { translate_ae_mulf16ss_00(dc, arg, par); }
static void translate_ae_mula16_00(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulaf16ss_00(dc, arg, par); }
static void translate_ae_mulsf16ss_33(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaf16ss_33(dc, arg, par); }

static void translate_ae_mulss32f48p16s_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_muls32_ll(dc, arg, par); }
static void translate_ae_mulss32f48p16s_lh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_muls32_lh(dc, arg, par); }
static void translate_ae_mulss32f48p16s_hh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_muls32_hh(dc, arg, par); }

static void translate_ae_mulssd32_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulssd32_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulssd32ep_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulaad32ep_hh_ll(dc, arg, par); }
static void translate_ae_mulssd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulssd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32x16_h3_l2(dc, arg, par); }

static void translate_ae_mulssfd16ss_11_00(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulaafd16ss_11_00(dc, arg, par); }
static void translate_ae_mulssfd16ss_13_02(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulaafd16ss_13_02(dc, arg, par); }
static void translate_ae_mulssfd16ss_33_22(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulaafd16ss_33_22(dc, arg, par); }

static void translate_ae_mulssfd32ra_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulssfd32ra_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulssfd32s_hh_ll(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulssfd32s_hl_lh(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulsad32_hh_ll(dc, arg, par); }
static void translate_ae_mulssfd32x16_h1_l0(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulsad32x16_h1_l0(dc, arg, par); }
static void translate_ae_mulssfd32x16_h3_l2(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulsad32x16_h3_l2(dc, arg, par); }

static void translate_ae_mulsf48q32sp16s_l(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulsf48q32sp16u_l(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }

static void translate_ae_mulsfp24x2r(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }
static void translate_ae_mulsfp24x2ra(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }

static void translate_ae_mulsfp32x16x2ras_h(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulsfp32x16x2ras_l(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulsfp32x16x2rs_h(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulsfp32x16x2rs_l(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulsfp32x16x2s_h(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulsfp32x16x2s_l(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }

static void translate_ae_mulsfp32x2ras(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }
static void translate_ae_mulsfp32x2rs(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }

static void translate_ae_mulsp32x16x2_h(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_h(dc, arg, par); }
static void translate_ae_mulsp32x16x2_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulsp32x2(DisasContext *dc, const OpcodeArg arg[],
                const uint32_t par[]) { translate_ae_mulac32(dc, arg, par); }

static void translate_ae_mulsq32sp16s_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }
static void translate_ae_mulsq32sp16u_l(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[]) { translate_ae_mulac32x16_l(dc, arg, par); }

/* Additional _s2 wrappers for 16-bit and fp pairwise alias families. */
#define DEF_MUL_S2_PURE_ALIAS(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
  translate_ae_##_fn(dc, arg, par); \
  if (arg[0].out) { \
    tcg_gen_shli_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, 1); \
  } \
}

/*
 * add_mulaf16ss_product: add sext16(d1[l1]) * sext16(d2[l2]) to acc.
 * Used by _s2 wrappers for single 16-bit product MAC instructions.
 */
static void add_mulaf16ss_product(TCGv_i64 acc, TCGv_i64 d1, TCGv_i64 d2,
                                  unsigned l1, unsigned l2)
{
  TCGv_i32 lhs16 = ae_extract_lane16s(d1, l1 & 3);
  TCGv_i32 rhs16 = ae_extract_lane16s(d2, l2 & 3);
  TCGv_i64 lhs64 = tcg_temp_new_i64();
  TCGv_i64 rhs64 = tcg_temp_new_i64();
  TCGv_i64 product = tcg_temp_new_i64();

  tcg_gen_ext_i32_i64(lhs64, lhs16);
  tcg_gen_ext_i32_i64(rhs64, rhs16);
  tcg_gen_mul_i64(product, lhs64, rhs64);
  tcg_gen_add_i64(acc, acc, product);
}

/*
 * add_mulaafd16ss_pair: add pairwise 16-bit sum to acc.
 *   sum = sext16(d1[l0a])*sext16(d2[l1a]) + sext16(d1[l0b])*sext16(d2[l1b])
 */
static void add_mulaafd16ss_pair(TCGv_i64 acc, TCGv_i64 d1, TCGv_i64 d2,
                                 unsigned l0a, unsigned l1a,
                                 unsigned l0b, unsigned l1b)
{
  TCGv_i32 la0 = ae_extract_lane16s(d1, l0a & 3);
  TCGv_i32 ra0 = ae_extract_lane16s(d2, l1a & 3);
  TCGv_i32 lb0 = ae_extract_lane16s(d1, l0b & 3);
  TCGv_i32 rb0 = ae_extract_lane16s(d2, l1b & 3);
  TCGv_i64 t0 = tcg_temp_new_i64();
  TCGv_i64 t1 = tcg_temp_new_i64();
  TCGv_i64 p0 = tcg_temp_new_i64();
  TCGv_i64 p1 = tcg_temp_new_i64();
  TCGv_i64 sum = tcg_temp_new_i64();

  tcg_gen_ext_i32_i64(t0, la0);
  tcg_gen_ext_i32_i64(t1, ra0);
  tcg_gen_mul_i64(p0, t0, t1);
  tcg_gen_ext_i32_i64(t0, lb0);
  tcg_gen_ext_i32_i64(t1, rb0);
  tcg_gen_mul_i64(p1, t0, t1);
  tcg_gen_add_i64(sum, p0, p1);
  tcg_gen_add_i64(acc, acc, sum);
}

/* Group A: single 16-bit product MAC alias _s2 wrappers.
 * Fix: call base (acc += product), then add one more product.
 */
#define DEF_MULAF16SS_S2(_fn, _l0, _l1) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
  translate_ae_##_fn(dc, arg, par); \
  if (arg[0].out && arg[1].in && arg[2].in) { \
    add_mulaf16ss_product((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, \
                          (TCGv_i64)arg[2].in, _l0, _l1); \
  } \
}

DEF_MUL_S2_PURE_ALIAS(mul16_00)
DEF_MULAF16SS_S2(mula16_00, 0, 0)

DEF_MUL_S2_PURE_ALIAS(mulf16ss_00)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_10)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_11)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_20)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_21)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_22)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_30)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_31)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_32)
DEF_MUL_S2_PURE_ALIAS(mulf16ss_33)

DEF_MULAF16SS_S2(mulaf16ss_00, 0, 0)
DEF_MULAF16SS_S2(mulaf16ss_10, 1, 0)
DEF_MULAF16SS_S2(mulaf16ss_11, 1, 1)
DEF_MULAF16SS_S2(mulaf16ss_20, 2, 0)
DEF_MULAF16SS_S2(mulaf16ss_21, 2, 1)
DEF_MULAF16SS_S2(mulaf16ss_22, 2, 2)
DEF_MULAF16SS_S2(mulaf16ss_30, 3, 0)
DEF_MULAF16SS_S2(mulaf16ss_31, 3, 1)
DEF_MULAF16SS_S2(mulaf16ss_32, 3, 2)
DEF_MULAF16SS_S2(mulaf16ss_33, 3, 3)

DEF_MULAF16SS_S2(mulsf16ss_00, 0, 0)
DEF_MULAF16SS_S2(mulsf16ss_10, 1, 0)
DEF_MULAF16SS_S2(mulsf16ss_11, 1, 1)
DEF_MULAF16SS_S2(mulsf16ss_20, 2, 0)
DEF_MULAF16SS_S2(mulsf16ss_21, 2, 1)
DEF_MULAF16SS_S2(mulsf16ss_22, 2, 2)
DEF_MULAF16SS_S2(mulsf16ss_30, 3, 0)
DEF_MULAF16SS_S2(mulsf16ss_31, 3, 1)
DEF_MULAF16SS_S2(mulsf16ss_32, 3, 2)
DEF_MULAF16SS_S2(mulsf16ss_33, 3, 3)

#undef DEF_MULAF16SS_S2

/* Group B: pairwise 16-bit product MAC alias _s2 wrappers. */
#define DEF_MULAAFD16SS_S2(_fn, _l0a, _l1a, _l0b, _l1b) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
  translate_ae_##_fn(dc, arg, par); \
  if (arg[0].out && arg[1].in && arg[2].in) { \
    add_mulaafd16ss_pair((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, \
                         (TCGv_i64)arg[2].in, _l0a, _l1a, _l0b, _l1b); \
  } \
}

DEF_MULAAFD16SS_S2(mulaafd16ss_11_00, 1, 1, 0, 0)
DEF_MULAAFD16SS_S2(mulaafd16ss_13_02, 1, 3, 0, 2)
DEF_MULAAFD16SS_S2(mulaafd16ss_33_22, 3, 3, 2, 2)
DEF_MULAAFD16SS_S2(mulssfd16ss_11_00, 1, 1, 0, 0)
DEF_MULAAFD16SS_S2(mulssfd16ss_13_02, 1, 3, 0, 2)
DEF_MULAAFD16SS_S2(mulssfd16ss_33_22, 3, 3, 2, 2)

#undef DEF_MULAAFD16SS_S2

DEF_MUL_S2_PURE_ALIAS(mulzaafd16ss_11_00)
DEF_MUL_S2_PURE_ALIAS(mulzaafd16ss_13_02)
DEF_MUL_S2_PURE_ALIAS(mulzaafd16ss_33_22)
DEF_MUL_S2_PURE_ALIAS(mulzssfd16ss_11_00)
DEF_MUL_S2_PURE_ALIAS(mulzssfd16ss_13_02)
DEF_MUL_S2_PURE_ALIAS(mulzssfd16ss_33_22)

DEF_MUL_S2_PURE_ALIAS(mulf48q32sp16s_l)
DEF_MUL_S2_PURE_ALIAS(mulf48q32sp16u_l)
DEF_MUL_S2_PURE_ALIAS(mulfp24x2r)
DEF_MUL_S2_PURE_ALIAS(mulfp24x2ra)
DEF_MUL_S2_PURE_ALIAS(mulfp32x16x2ras_h)
DEF_MUL_S2_PURE_ALIAS(mulfp32x16x2ras_l)
DEF_MUL_S2_PURE_ALIAS(mulfp32x16x2rs_h)
DEF_MUL_S2_PURE_ALIAS(mulfp32x16x2rs_l)
DEF_MUL_S2_PURE_ALIAS(mulfp32x16x2s_h)
DEF_MUL_S2_PURE_ALIAS(mulfp32x16x2s_l)
DEF_MUL_S2_PURE_ALIAS(mulfp32x2ras)
DEF_MUL_S2_PURE_ALIAS(mulfp32x2rs)
DEF_MUL_S2_PURE_ALIAS(mulp32x16x2_h)
DEF_MUL_S2_PURE_ALIAS(mulp32x16x2_l)
DEF_MUL_S2_PURE_ALIAS(mulp32x2)
DEF_MUL_S2_PURE_ALIAS(mulq32sp16s_l)
DEF_MUL_S2_PURE_ALIAS(mulq32sp16u_l)

/*
 * Group C: pairwise 32-bit product MAC alias _s2 wrappers.
 * All delegate to mulac32 (→ mulaad32_hh_ll) or mulac32x16_l/h
 * (→ mulaad32x16_h1_l0 / h3_l2). Fix: call base, then add one
 * extra pairwise product sum via existing add_mulaad32_pair32/pairx16.
 *
 * Forward declarations needed: helpers defined later in this file.
 */
static void add_mulaad32_pair32(TCGv_i64 acc, TCGv_i64 d0, TCGv_i64 d1,
                                unsigned d1_hi_lane, unsigned d1_lo_lane);
static void add_mulaad32_pairx16(TCGv_i64 acc, TCGv_i64 d0, TCGv_i64 d1,
                                 unsigned hi16_lane, unsigned lo16_lane);

#define DEF_MULAC32_S2(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
  translate_ae_##_fn(dc, arg, par); \
  if (arg[0].out && arg[1].in && arg[2].in) { \
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, \
                        (TCGv_i64)arg[2].in, 1, 0); \
  } \
}

#define DEF_MULAC32X16L_S2(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
  translate_ae_##_fn(dc, arg, par); \
  if (arg[0].out && arg[1].in && arg[2].in) { \
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, \
                         (TCGv_i64)arg[2].in, 1, 0); \
  } \
}

#define DEF_MULAC32X16H_S2(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
  translate_ae_##_fn(dc, arg, par); \
  if (arg[0].out && arg[1].in && arg[2].in) { \
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, \
                         (TCGv_i64)arg[2].in, 3, 2); \
  } \
}

DEF_MULAC32X16L_S2(mulaf48q32sp16s_l)
DEF_MULAC32X16L_S2(mulaf48q32sp16u_l)
DEF_MULAC32_S2(mulafp24x2r)
DEF_MULAC32_S2(mulafp24x2ra)
DEF_MULAC32X16H_S2(mulafp32x16x2ras_h)
DEF_MULAC32X16L_S2(mulafp32x16x2ras_l)
DEF_MULAC32X16H_S2(mulafp32x16x2rs_h)
DEF_MULAC32X16L_S2(mulafp32x16x2rs_l)
DEF_MULAC32X16H_S2(mulafp32x16x2s_h)
DEF_MULAC32X16L_S2(mulafp32x16x2s_l)
DEF_MULAC32_S2(mulafp32x2ras)
DEF_MULAC32_S2(mulafp32x2rs)
DEF_MULAC32X16H_S2(mulap32x16x2_h)
DEF_MULAC32X16L_S2(mulap32x16x2_l)
DEF_MULAC32_S2(mulap32x2)
DEF_MULAC32X16L_S2(mulaq32sp16s_l)
DEF_MULAC32X16L_S2(mulaq32sp16u_l)
DEF_MULAC32X16L_S2(mulsf48q32sp16s_l)
DEF_MULAC32X16L_S2(mulsf48q32sp16u_l)
DEF_MULAC32_S2(mulsfp24x2r)
DEF_MULAC32_S2(mulsfp24x2ra)
DEF_MULAC32X16H_S2(mulsfp32x16x2ras_h)
DEF_MULAC32X16L_S2(mulsfp32x16x2ras_l)
DEF_MULAC32X16H_S2(mulsfp32x16x2rs_h)
DEF_MULAC32X16L_S2(mulsfp32x16x2rs_l)
DEF_MULAC32X16H_S2(mulsfp32x16x2s_h)
DEF_MULAC32X16L_S2(mulsfp32x16x2s_l)
DEF_MULAC32_S2(mulsfp32x2ras)
DEF_MULAC32_S2(mulsfp32x2rs)
DEF_MULAC32X16H_S2(mulsp32x16x2_h)
DEF_MULAC32X16L_S2(mulsp32x16x2_l)
DEF_MULAC32_S2(mulsp32x2)
DEF_MULAC32X16L_S2(mulsq32sp16s_l)
DEF_MULAC32X16L_S2(mulsq32sp16u_l)

#undef DEF_MULAC32_S2
#undef DEF_MULAC32X16L_S2
#undef DEF_MULAC32X16H_S2

#undef DEF_MUL_S2_PURE_ALIAS

static void add_mulaaaaq16_sum(TCGv_i64 acc, TCGv_i64 d0, TCGv_i64 d1)
{
  TCGv_i64 sum = tcg_temp_new_i64();
  TCGv_i64 product = tcg_temp_new_i64();
  TCGv_i64 lhs64 = tcg_temp_new_i64();
  TCGv_i64 rhs64 = tcg_temp_new_i64();

  tcg_gen_movi_i64(sum, 0);

  for (unsigned lane = 0; lane < 4; lane++) {
    TCGv_i32 lhs = ae_extract_lane16s(d0, lane);
    TCGv_i32 rhs = ae_extract_lane16s(d1, lane);

    tcg_gen_ext_i32_i64(lhs64, lhs);
    tcg_gen_ext_i32_i64(rhs64, rhs);
    tcg_gen_mul_i64(product, lhs64, rhs64);
    tcg_gen_add_i64(sum, sum, product);
  }

  tcg_gen_add_i64(acc, acc, sum);
}

/* ----------------------------------------------------------------
 * ae_mulaaaaq16 / ae_mulzaaaaq16:
 *   Accumulate (or zero-and-load) the sum of all four signed 16x16 lane
 *   products into a single 64-bit AE accumulator.
 *   ae_mulaaaaq16  q0, d0, d1  -> q0 += sum(d0[lane] * d1[lane])
 *   ae_mulzaaaaq16 q0, d0, d1  -> q0  = sum(d0[lane] * d1[lane])
 * ---------------------------------------------------------------- */
static void translate_ae_mulaaaaq16(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (arg[0].out && arg[0].in && arg[1].in && arg[2].in) {
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].in);
        add_mulaaaaq16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                           (TCGv_i64)arg[2].in);
    }
}

static void translate_ae_mulzaaaaq16(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    if (arg[0].out && arg[1].in && arg[2].in) {
        tcg_gen_movi_i64((TCGv_i64)arg[0].out, 0);
        add_mulaaaaq16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                           (TCGv_i64)arg[2].in);
    }
}

/* ----------------------------------------------------------------
 * ae_mulaaaaq32x16 / ae_mulzaaaaq32x16 / ae_mulaaaafq32x16 / ae_mulzaaaafq32x16:
 *   4-operand forms: arg[0]=q0(acc), arg[1]=d0, arg[2]=v1, arg[3]=d2
 *   The "aaaa" variant multiplies all 4 pairings across both d0 and d2
 *   against the 4 16-bit lanes of v1 (following h0.l1 for d0, h2.l3 for d2):
 *     q0 += d0_hi*v1[0] + d0_lo*v1[1] + d2_hi*v1[2] + d2_lo*v1[3]
 *   The "f" (fractional) variants have identical accumulator semantics at the
 *   64-bit level — fractional saturation is deferred.
 * ---------------------------------------------------------------- */
static void add_mulaaaaq32x16_sum(TCGv_i64 acc, TCGv_i64 d0, TCGv_i64 v1,
                   TCGv_i64 d2)
{
  TCGv_i32 d0_lo = ae_extract_lane32(d0, 0);
  TCGv_i32 d0_hi = ae_extract_lane32(d0, 1);
  TCGv_i32 d2_lo = ae_extract_lane32(d2, 0);
  TCGv_i32 d2_hi = ae_extract_lane32(d2, 1);
  TCGv_i32 v1_0 = ae_extract_lane16s(v1, 0);
  TCGv_i32 v1_1 = ae_extract_lane16s(v1, 1);
  TCGv_i32 v1_2 = ae_extract_lane16s(v1, 2);
  TCGv_i32 v1_3 = ae_extract_lane16s(v1, 3);
  TCGv_i64 p0 = tcg_temp_new_i64();
  TCGv_i64 p1 = tcg_temp_new_i64();
  TCGv_i64 p2 = tcg_temp_new_i64();
  TCGv_i64 p3 = tcg_temp_new_i64();
  TCGv_i64 t0 = tcg_temp_new_i64();
  TCGv_i64 t1 = tcg_temp_new_i64();
  TCGv_i64 sum = tcg_temp_new_i64();

  tcg_gen_ext_i32_i64(t0, d0_hi);
  tcg_gen_ext_i32_i64(t1, v1_0);
  tcg_gen_mul_i64(p0, t0, t1);
  tcg_gen_ext_i32_i64(t0, d0_lo);
  tcg_gen_ext_i32_i64(t1, v1_1);
  tcg_gen_mul_i64(p1, t0, t1);
  tcg_gen_ext_i32_i64(t0, d2_hi);
  tcg_gen_ext_i32_i64(t1, v1_2);
  tcg_gen_mul_i64(p2, t0, t1);
  tcg_gen_ext_i32_i64(t0, d2_lo);
  tcg_gen_ext_i32_i64(t1, v1_3);
  tcg_gen_mul_i64(p3, t0, t1);
  tcg_gen_add_i64(sum, p0, p1);
  tcg_gen_add_i64(sum, sum, p2);
  tcg_gen_add_i64(sum, sum, p3);
  tcg_gen_add_i64(acc, acc, sum);
}

static void translate_ae_mulaaaaq32x16(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in && arg[3].in) {
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].in);
    add_mulaaaaq32x16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                (TCGv_i64)arg[2].in, (TCGv_i64)arg[3].in);
  }
}

static void translate_ae_mulzaaaaq32x16(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in && arg[3].in) {
    tcg_gen_movi_i64((TCGv_i64)arg[0].out, 0);
    add_mulaaaaq32x16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                (TCGv_i64)arg[2].in, (TCGv_i64)arg[3].in);
  }
}

static void translate_ae_mulaaaafq32x16(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  if (arg[0].out && arg[0].in && arg[1].in && arg[2].in && arg[3].in) {
    tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].in);
    add_mulaaaaq32x16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                (TCGv_i64)arg[2].in, (TCGv_i64)arg[3].in);
  }
}

static void translate_ae_mulzaaaafq32x16(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
{
  if (arg[0].out && arg[1].in && arg[2].in && arg[3].in) {
    tcg_gen_movi_i64((TCGv_i64)arg[0].out, 0);
    add_mulaaaaq32x16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                (TCGv_i64)arg[2].in, (TCGv_i64)arg[3].in);
  }
}

#define DEF_MUL_S2_PURE_ALIAS2(_fn) \
static void translate_ae_##_fn##_s2(DisasContext *dc, const OpcodeArg arg[], \
                                    const uint32_t par[]) \
{ \
  translate_ae_##_fn(dc, arg, par); \
  if (arg[0].out) { \
    tcg_gen_shli_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, 1); \
  } \
}

static void add_mulaad32_pair32(TCGv_i64 acc, TCGv_i64 d0, TCGv_i64 d1,
                unsigned d1_hi_lane, unsigned d1_lo_lane)
{
  TCGv_i32 d0_lo = ae_extract_lane32(d0, 0);
  TCGv_i32 d0_hi = ae_extract_lane32(d0, 1);
  TCGv_i32 d1_lo = ae_extract_lane32(d1, d1_lo_lane);
  TCGv_i32 d1_hi = ae_extract_lane32(d1, d1_hi_lane);
  TCGv_i64 p0 = tcg_temp_new_i64();
  TCGv_i64 p1 = tcg_temp_new_i64();
  TCGv_i64 t0 = tcg_temp_new_i64();
  TCGv_i64 t1 = tcg_temp_new_i64();
  TCGv_i64 sum = tcg_temp_new_i64();

  tcg_gen_ext_i32_i64(t0, d0_lo);
  tcg_gen_ext_i32_i64(t1, d1_lo);
  tcg_gen_mul_i64(p0, t0, t1);
  tcg_gen_ext_i32_i64(t0, d0_hi);
  tcg_gen_ext_i32_i64(t1, d1_hi);
  tcg_gen_mul_i64(p1, t0, t1);
  tcg_gen_add_i64(sum, p0, p1);
  tcg_gen_add_i64(acc, acc, sum);
}

static void add_mulaad32_pairx16(TCGv_i64 acc, TCGv_i64 d0, TCGv_i64 d1,
                 unsigned hi16_lane, unsigned lo16_lane)
{
  TCGv_i32 d0_lo = ae_extract_lane32(d0, 0);
  TCGv_i32 d0_hi = ae_extract_lane32(d0, 1);
  TCGv_i32 d1_lo16 = ae_extract_lane16s(d1, lo16_lane);
  TCGv_i32 d1_hi16 = ae_extract_lane16s(d1, hi16_lane);
  TCGv_i64 p0 = tcg_temp_new_i64();
  TCGv_i64 p1 = tcg_temp_new_i64();
  TCGv_i64 t0 = tcg_temp_new_i64();
  TCGv_i64 t1 = tcg_temp_new_i64();
  TCGv_i64 sum = tcg_temp_new_i64();

  tcg_gen_ext_i32_i64(t0, d0_lo);
  tcg_gen_ext_i32_i64(t1, d1_lo16);
  tcg_gen_mul_i64(p0, t0, t1);
  tcg_gen_ext_i32_i64(t0, d0_hi);
  tcg_gen_ext_i32_i64(t1, d1_hi16);
  tcg_gen_mul_i64(p1, t0, t1);
  tcg_gen_add_i64(sum, p0, p1);
  tcg_gen_add_i64(acc, acc, sum);
}

/*
 * mulaad32.*_s2 must apply one extra pairwise product sum after base MAC.
 * The generic shift-then-subtract wrapper is incorrect for aliased 'm' args.
 */
static void translate_ae_mulaad32_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulaad32_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
              (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulaad32_hl_lh_s2(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  translate_ae_mulaad32_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
              (TCGv_i64)arg[2].in, 0, 1);
  }
}

static void translate_ae_mulaad32x16_h0_l1_s2(DisasContext *dc,
                         const OpcodeArg arg[],
                         const uint32_t par[])
{
  translate_ae_mulaad32x16_h0_l1(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
               (TCGv_i64)arg[2].in, 0, 1);
  }
}

static void translate_ae_mulaad32x16_h1_l0_s2(DisasContext *dc,
                         const OpcodeArg arg[],
                         const uint32_t par[])
{
  translate_ae_mulaad32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
               (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulaad32x16_h2_l3_s2(DisasContext *dc,
                         const OpcodeArg arg[],
                         const uint32_t par[])
{
  translate_ae_mulaad32x16_h2_l3(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
               (TCGv_i64)arg[2].in, 2, 3);
  }
}

static void translate_ae_mulaad32x16_h3_l2_s2(DisasContext *dc,
                         const OpcodeArg arg[],
                         const uint32_t par[])
{
  translate_ae_mulaad32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
               (TCGv_i64)arg[2].in, 3, 2);
  }
}

/* Remaining single-accumulator _s2 aliases (MAC form). */
static void translate_ae_mulaafd32ra_hh_ll_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulaafd32ra_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulaafd32ra_hl_lh_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulaafd32ra_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 0, 1);
  }
}

static void translate_ae_mulaafd32s_hh_ll_s2(DisasContext *dc,
                                              const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulaafd32s_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulaafd32s_hl_lh_s2(DisasContext *dc,
                                              const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulaafd32s_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 0, 1);
  }
}

static void translate_ae_mulaafd32x16_h0_l1_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulaafd32x16_h0_l1(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 0, 1);
  }
}

static void translate_ae_mulaafd32x16_h1_l0_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulaafd32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulaafd32x16_h2_l3_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulaafd32x16_h2_l3(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 2, 3);
  }
}

static void translate_ae_mulaafd32x16_h3_l2_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulaafd32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 3, 2);
  }
}
static void translate_ae_mulasd32_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
  translate_ae_mulasd32_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasd32_hl_lh_s2(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
  translate_ae_mulasd32_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasd32x16_h1_l0_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulasd32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasd32x16_h3_l2_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulasd32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 3, 2);
  }
}

static void translate_ae_mulasfd32ra_hh_ll_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulasfd32ra_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasfd32ra_hl_lh_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulasfd32ra_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasfd32s_hh_ll_s2(DisasContext *dc,
                                              const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulasfd32s_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasfd32s_hl_lh_s2(DisasContext *dc,
                                              const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulasfd32s_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasfd32x16_h1_l0_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulasfd32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulasfd32x16_h3_l2_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulasfd32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 3, 2);
  }
}
static void translate_ae_mulsad32_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
  translate_ae_mulsad32_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulsad32x16_h1_l0_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulsad32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulsad32x16_h3_l2_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulsad32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 3, 2);
  }
}

static void translate_ae_mulsafd32ra_hh_ll_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulsafd32ra_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulsafd32s_hh_ll_s2(DisasContext *dc,
                                              const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulsafd32s_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulsafd32x16_h1_l0_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulsafd32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulsafd32x16_h3_l2_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulsafd32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 3, 2);
  }
}
static void translate_ae_mulssd32_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
  translate_ae_mulssd32_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssd32_hl_lh_s2(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
  translate_ae_mulssd32_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    /* Keep lane pairing aligned with current base mapping semantics. */
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssd32x16_h1_l0_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulssd32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssd32x16_h3_l2_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulssd32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 3, 2);
  }
}

static void translate_ae_mulssfd32ra_hh_ll_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulssfd32ra_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssfd32ra_hl_lh_s2(DisasContext *dc,
                                               const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulssfd32ra_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssfd32s_hh_ll_s2(DisasContext *dc,
                                              const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulssfd32s_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssfd32s_hl_lh_s2(DisasContext *dc,
                                              const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulssfd32s_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pair32((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                        (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssfd32x16_h1_l0_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulssfd32x16_h1_l0(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 1, 0);
  }
}

static void translate_ae_mulssfd32x16_h3_l2_s2(DisasContext *dc,
                                                const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulssfd32x16_h3_l2(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaad32_pairx16((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                         (TCGv_i64)arg[2].in, 3, 2);
  }
}

/* Remaining single-accumulator _s2 aliases (pure/zero form). */
DEF_MUL_S2_PURE_ALIAS2(mulss32f48p16s_hh)
DEF_MUL_S2_PURE_ALIAS2(mulss32f48p16s_lh)
DEF_MUL_S2_PURE_ALIAS2(mulss32f48p16s_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzaad32_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzaad32_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzaad32x16_h0_l1)
DEF_MUL_S2_PURE_ALIAS2(mulzaad32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzaad32x16_h2_l3)
DEF_MUL_S2_PURE_ALIAS2(mulzaad32x16_h3_l2)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32ra_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32ra_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32s_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32s_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32x16_h0_l1)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32x16_h2_l3)
DEF_MUL_S2_PURE_ALIAS2(mulzaafd32x16_h3_l2)
DEF_MUL_S2_PURE_ALIAS2(mulzasd32_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzasd32_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzasd32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzasd32x16_h3_l2)
DEF_MUL_S2_PURE_ALIAS2(mulzasfd32ra_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzasfd32ra_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzasfd32s_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzasfd32s_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzasfd32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzasfd32x16_h3_l2)
DEF_MUL_S2_PURE_ALIAS2(mulzsad32_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzsad32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzsad32x16_h3_l2)
DEF_MUL_S2_PURE_ALIAS2(mulzsafd32ra_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzsafd32s_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzsafd32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzsafd32x16_h3_l2)
DEF_MUL_S2_PURE_ALIAS2(mulzssd32_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzssd32_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzssd32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzssd32x16_h3_l2)
DEF_MUL_S2_PURE_ALIAS2(mulzssfd32ra_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzssfd32ra_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzssfd32s_hh_ll)
DEF_MUL_S2_PURE_ALIAS2(mulzssfd32s_hl_lh)
DEF_MUL_S2_PURE_ALIAS2(mulzssfd32x16_h1_l0)
DEF_MUL_S2_PURE_ALIAS2(mulzssfd32x16_h3_l2)

static void translate_ae_mulaad32ep_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                             const uint32_t par[])
{
  translate_ae_mulaad32ep_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[3].in, 0);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[3].in, 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 prod_sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(prod_sum, p0, p1);
    tcg_gen_add_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, prod_sum);
    ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
  }
}

static void translate_ae_mulaad32usep_hl_lh_s2(DisasContext *dc, const OpcodeArg arg[],
                                               const uint32_t par[])
{
  translate_ae_mulaad32usep_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[3].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[3].in, 0);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 prod_sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(prod_sum, p0, p1);
    tcg_gen_add_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, prod_sum);
    ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
  }
}

static void translate_ae_mulssd32ep_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                             const uint32_t par[])
{
  translate_ae_mulssd32ep_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].out && arg[2].in && arg[3].in) {
    TCGv_i32 d0_lo = ae_extract_lane32((TCGv_i64)arg[2].in, 0);
    TCGv_i32 d0_hi = ae_extract_lane32((TCGv_i64)arg[2].in, 1);
    TCGv_i32 d1_lo = ae_extract_lane32((TCGv_i64)arg[3].in, 0);
    TCGv_i32 d1_hi = ae_extract_lane32((TCGv_i64)arg[3].in, 1);
    TCGv_i64 p0 = tcg_temp_new_i64();
    TCGv_i64 p1 = tcg_temp_new_i64();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 prod_sum = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, d0_lo);
    tcg_gen_ext_i32_i64(t1, d1_lo);
    tcg_gen_mul_i64(p0, t0, t1);
    tcg_gen_ext_i32_i64(t0, d0_hi);
    tcg_gen_ext_i32_i64(t1, d1_hi);
    tcg_gen_mul_i64(p1, t0, t1);
    tcg_gen_add_i64(prod_sum, p0, p1);
    tcg_gen_add_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, prod_sum);
    ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
  }
}

static void translate_ae_mulzaad32ep_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulzaad32ep_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
    ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
  }
}

static void translate_ae_mulzaad32usep_hl_lh_s2(DisasContext *dc, const OpcodeArg arg[],
                                                const uint32_t par[])
{
  translate_ae_mulzaad32usep_hl_lh(dc, arg, par);
  if (arg[0].out && arg[1].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
    ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
  }
}

static void translate_ae_mulzssd32ep_hh_ll_s2(DisasContext *dc, const OpcodeArg arg[],
                                              const uint32_t par[])
{
  translate_ae_mulzssd32ep_hh_ll(dc, arg, par);
  if (arg[0].out && arg[1].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
    ae_set_ep_from_i64(arg[0].out, (TCGv_i64)arg[1].out);
  }
}

/* _s2 wrappers for aaaa groups. */
static void translate_ae_mulaaaaq16_s2(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
  translate_ae_mulaaaaq16(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in) {
    add_mulaaaaq16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                       (TCGv_i64)arg[2].in);
  }
}

static void translate_ae_mulzaaaaq16_s2(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
  translate_ae_mulzaaaaq16(dc, arg, par);
  if (arg[0].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, 1);
  }
  if (arg[1].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
  }
}

static void translate_ae_mulaaaaq32x16_s2(DisasContext *dc, const OpcodeArg arg[],
                                          const uint32_t par[])
{
  translate_ae_mulaaaaq32x16(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in && arg[3].in) {
    add_mulaaaaq32x16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                          (TCGv_i64)arg[2].in, (TCGv_i64)arg[3].in);
  }
}

static void translate_ae_mulzaaaaq32x16_s2(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
  translate_ae_mulzaaaaq32x16(dc, arg, par);
  if (arg[0].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, 1);
  }
  if (arg[1].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
  }
}

static void translate_ae_mulaaaafq32x16_s2(DisasContext *dc, const OpcodeArg arg[],
                                           const uint32_t par[])
{
  translate_ae_mulaaaafq32x16(dc, arg, par);
  if (arg[0].out && arg[1].in && arg[2].in && arg[3].in) {
    add_mulaaaaq32x16_sum((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in,
                          (TCGv_i64)arg[2].in, (TCGv_i64)arg[3].in);
  }
}

static void translate_ae_mulzaaaafq32x16_s2(DisasContext *dc, const OpcodeArg arg[],
                                            const uint32_t par[])
{
  translate_ae_mulzaaaafq32x16(dc, arg, par);
  if (arg[0].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].out, 1);
  }
  if (arg[1].out) {
    tcg_gen_shli_i64((TCGv_i64)arg[1].out, (TCGv_i64)arg[1].out, 1);
  }
}

#undef DEF_MUL_S2_PURE_ALIAS2
#undef DEF_MUL_S2_MAC_ALIAS2

/* ----------------------------------------------------------------
 * ae_mulpp_32x32x2_1 / ae_mulpp_32x32x2_2:
 *   Pipeline parallel 32×32 multiply — produces a 64-bit product
 *   accumulated into the AE accumulator register.
 *   Map to translate_ae_mulac32 (32-bit complex accumulate baseline).
 * ---------------------------------------------------------------- */
static void translate_ae_mulpp_32x32x2(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    translate_ae_mulac32(dc, arg, par);
}

/* ----------------------------------------------------------------
 * ae_add32x27:
 *   Add two AE registers with 27-bit truncation/saturation.
 *   The saturation is a hardware detail; at the simulation level we
 *   use plain 32-bit lane addition (same as ae_add32).
 * ---------------------------------------------------------------- */
static void translate_ae_add32x27(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    translate_ae_add32(dc, arg, par);
}

/* ----------------------------------------------------------------
 * ae_addbrba32:
 *   Scalar AR add form: arr = art + ars.
 *   Bit-reverse addressing side-effects are not modeled in this
 *   baseline translator; we preserve the integer dataflow semantics.
 * ---------------------------------------------------------------- */
static void translate_ae_addbrba32(DisasContext *dc, const OpcodeArg arg[],
                  const uint32_t par[])
{
  (void)dc;
  (void)par;

  if (arg[0].out && arg[1].in && arg[2].in) {
    tcg_gen_add_i32(arg[0].out, arg[1].in, arg[2].in);
  }
}

/* ----------------------------------------------------------------
 * ae_leftshift32x5:
 *   Logical left shift of both 32-bit lanes of an AE register by 5.
 * ---------------------------------------------------------------- */
static void translate_ae_leftshift32x5(DisasContext *dc, const OpcodeArg arg[],
                                        const uint32_t par[])
{
    if (arg[0].out && arg[1].in) {
        TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
        TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
        tcg_gen_shli_i32(lo, lo, 5);
        tcg_gen_shli_i32(hi, hi, 5);
        ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    }
}

  /* ----------------------------------------------------------------
   * ae_shift32x4 / ae_shift32x5:
   *   Baseline fixed-lane shifts used by HiFi shifter pipeline aliases.
   * ---------------------------------------------------------------- */
  static void translate_ae_shift32x4(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
  {
    if (arg[0].out && arg[1].in) {
      TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
      TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
      tcg_gen_shli_i32(lo, lo, 4);
      tcg_gen_shli_i32(hi, hi, 4);
      ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    }
  }

  static void translate_ae_shift32x5(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
  {
    if (arg[0].out && arg[1].in) {
      TCGv_i32 lo = ae_extract_lane32((TCGv_i64)arg[1].in, 0);
      TCGv_i32 hi = ae_extract_lane32((TCGv_i64)arg[1].in, 1);
      tcg_gen_shli_i32(lo, lo, 5);
      tcg_gen_shli_i32(hi, hi, 5);
      ae_pack_lanes32((TCGv_i64)arg[0].out, lo, hi);
    }
  }

  /* ----------------------------------------------------------------
   * ae_shift_d / ae_shift_d0 / ae_shift_sd:
   *   64-bit shifter baseline over AE data register values.
   * ---------------------------------------------------------------- */
  static void translate_ae_shift_d(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
  {
    (void)dc;
    (void)par;

    if (arg[0].out && arg[1].in) {
      if (arg[2].in) {
        TCGv_i64 sh64 = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(sh64, arg[2].in);
        tcg_gen_andi_i64(sh64, sh64, 63);
        tcg_gen_shl_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in, sh64);
      } else {
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
      }
    }
  }

  static void translate_ae_shift_d0(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    (void)dc;
    (void)par;

    if (arg[0].out && arg[1].in) {
      tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
    }
  }

  static void translate_ae_shift_sd(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
  {
    (void)dc;
    (void)par;

    if (arg[0].out && arg[1].in) {
      if (arg[2].in) {
        TCGv_i32 sh = tcg_temp_new_i32();
        TCGv_i32 abs_sh = tcg_temp_new_i32();
        TCGv_i64 sh64 = tcg_temp_new_i64();
        TCGv_i64 left = tcg_temp_new_i64();
        TCGv_i64 right = tcg_temp_new_i64();

        tcg_gen_mov_i32(sh, arg[2].in);
        tcg_gen_neg_i32(abs_sh, sh);
        tcg_gen_movcond_i32(TCG_COND_GE, abs_sh, sh, tcg_constant_i32(0), sh, abs_sh);
        tcg_gen_andi_i32(abs_sh, abs_sh, 63);
        tcg_gen_extu_i32_i64(sh64, abs_sh);
        tcg_gen_shl_i64(left, (TCGv_i64)arg[1].in, sh64);
        tcg_gen_sar_i64(right, (TCGv_i64)arg[1].in, sh64);
        tcg_gen_movcond_i64(TCG_COND_GE,
                  (TCGv_i64)arg[0].out,
                  tcg_constant_i64(0),
                  tcg_constant_i64(0),
                  left,
                  right);
      } else {
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
      }
    }
  }

/* ----------------------------------------------------------------
 * ae_selimm / ae_selimm.N:
   *   Per-lane select between two AE 64-bit registers.
   *   For output lane k (k=0..3), bit k of imm selects source:
   *     0 -> arg[2] lane k
   *     1 -> arg[1] lane k
 * ---------------------------------------------------------------- */
static void translate_ae_selimm(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    (void)dc;
    (void)par;

    if (arg[0].out && arg[1].in && arg[2].in) {
      uint32_t sel = (uint32_t)arg[3].imm;
      TCGv_i32 lanes[4];
      TCGv_i32 lo32 = tcg_temp_new_i32();
      TCGv_i32 hi32 = tcg_temp_new_i32();
      TCGv_i64 lo64 = tcg_temp_new_i64();
      TCGv_i64 hi64 = tcg_temp_new_i64();
      int k;

      for (k = 0; k < 4; k++) {
        TCGv_i64 src = (sel & (1u << k)) ? (TCGv_i64)arg[1].in
                         : (TCGv_i64)arg[2].in;
        lanes[k] = ae_extract_lane16s(src, k);
      }

      tcg_gen_deposit_i32(lo32, lanes[0], lanes[1], 16, 16);
      tcg_gen_deposit_i32(hi32, lanes[2], lanes[3], 16, 16);
      tcg_gen_extu_i32_i64(lo64, lo32);
      tcg_gen_extu_i32_i64(hi64, hi32);
      tcg_gen_shli_i64(hi64, hi64, 32);
      tcg_gen_or_i64((TCGv_i64)arg[0].out, lo64, hi64);
    }
}

  /* ----------------------------------------------------------------
   * ae_fld_* pseudo-ops (initial collapse set):
   *   These are field/decode carrier pseudo-operations. We model them as
   *   explicit passthrough dataflow rather than generic alias fallback.
   * ---------------------------------------------------------------- */
  static void translate_ae_fld_passthrough(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
  {
    (void)dc;
    (void)par;

    if (!arg[0].out) {
      return;
    }

    if (arg[0].num_bits > 32) {
      if (arg[0].in && arg[0].num_bits > 32) {
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].in);
      } else if (arg[1].in && arg[1].num_bits > 32) {
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
      } else if (arg[1].in) {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(t, arg[1].in);
        tcg_gen_mov_i64((TCGv_i64)arg[0].out, t);
      }
    } else {
      if (arg[0].in && arg[0].num_bits <= 32) {
        tcg_gen_mov_i32(arg[0].out, arg[0].in);
      } else if (arg[1].in && arg[1].num_bits <= 32) {
        tcg_gen_mov_i32(arg[0].out, arg[1].in);
      } else if (arg[1].in && arg[1].num_bits > 32) {
        tcg_gen_extrl_i64_i32(arg[0].out, (TCGv_i64)arg[1].in);
      }
    }
  }

/*
 * Carrier semantics helpers for common HiFi operand classes that represent
 * concrete data movement between AR/AE resources.
 */
static void translate_ae_carrier_ar_to_dr_v(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
  (void)dc;
  (void)par;

  if (!arg[0].out || arg[0].num_bits <= 32) {
    return;
  }

  if (arg[1].in && arg[2].in && arg[1].num_bits <= 32 && arg[2].num_bits <= 32) {
    TCGv_i64 hi = tcg_temp_new_i64();
    TCGv_i64 lo = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(hi, arg[1].in);
    tcg_gen_extu_i32_i64(lo, arg[2].in);
    tcg_gen_shli_i64(hi, hi, 32);
    tcg_gen_or_i64((TCGv_i64)arg[0].out, hi, lo);
    return;
  }

  translate_ae_fld_passthrough(dc, arg, par);
}

static void translate_ae_carrier_dr_to_ar_v0(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
  (void)dc;
  (void)par;

  if (arg[0].out && arg[0].num_bits <= 32 && arg[1].in && arg[1].num_bits > 32) {
    tcg_gen_extrl_i64_i32(arg[0].out, (TCGv_i64)arg[1].in);
    return;
  }

  translate_ae_fld_passthrough(dc, arg, par);
}

static void translate_ae_carrier_to_dr_v(DisasContext *dc, const OpcodeArg arg[],
                     const uint32_t par[])
{
  (void)dc;
  (void)par;

  if (arg[0].out && arg[0].num_bits > 32) {
    if (arg[1].in && arg[1].num_bits > 32) {
      tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
      return;
    }

    if (arg[1].in && arg[2].in && arg[1].num_bits <= 32 && arg[2].num_bits <= 32) {
      TCGv_i64 hi = tcg_temp_new_i64();
      TCGv_i64 lo = tcg_temp_new_i64();
      tcg_gen_extu_i32_i64(hi, arg[1].in);
      tcg_gen_extu_i32_i64(lo, arg[2].in);
      tcg_gen_shli_i64(hi, hi, 32);
      tcg_gen_or_i64((TCGv_i64)arg[0].out, hi, lo);
      return;
    }
  }

  translate_ae_fld_passthrough(dc, arg, par);
}

static void translate_ae_carrier_slot(DisasContext *dc, const OpcodeArg arg[],
                    const uint32_t par[])
{
  const OpcodeArg *src = NULL;

  (void)dc;

  switch (par[0] & 3) {
  case 0:
    src = &arg[1];
    break;
  case 1:
    src = &arg[2];
    break;
  case 2:
    src = &arg[3];
    break;
  default:
    src = &arg[4];
    break;
  }

  if (!arg[0].out || !src->in) {
    translate_ae_fld_passthrough(dc, arg, par);
    return;
  }

  if (arg[0].num_bits > 32) {
    if (src->num_bits > 32) {
      tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)src->in);
    } else {
      tcg_gen_extu_i32_i64((TCGv_i64)arg[0].out, src->in);
    }
  } else {
    if (src->num_bits > 32) {
      tcg_gen_extrl_i64_i32(arg[0].out, (TCGv_i64)src->in);
    } else {
      tcg_gen_mov_i32(arg[0].out, src->in);
    }
  }
}

static void translate_ae_carrier_valign(DisasContext *dc, const OpcodeArg arg[],
                      const uint32_t par[])
{
  (void)dc;
  (void)par;

  if (arg[0].out && arg[0].num_bits > 32) {
    if (arg[1].in && arg[1].num_bits > 32) {
      tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[1].in);
      return;
    }

    if (arg[0].in && arg[0].num_bits > 32) {
      tcg_gen_mov_i64((TCGv_i64)arg[0].out, (TCGv_i64)arg[0].in);
      return;
    }
  }

  translate_ae_fld_passthrough(dc, arg, par);
}

static void translate_ae_zalign64(DisasContext *dc, const OpcodeArg arg[],
                   const uint32_t par[])
{
  (void)dc;
  (void)par;

  if (arg[0].out && arg[0].num_bits > 32) {
    tcg_gen_movi_i64((TCGv_i64)arg[0].out, 0);
  }
}

/*
 * translate_ae_vldsht  --  shift-load byte from streaming pointer
 *
 *   AE_VLDSHT aeq:  aeq = (aeq << 8) | mem8[AE_BITPTR]; AE_BITPTR++
 *
 *   arg[0]  AE_DR  in+out  -- accumulator register (read-shifted-write)
 */
static void translate_ae_vldsht(DisasContext *dc, const OpcodeArg arg[],
                 const uint32_t par[])
{
  (void)par;

  if (arg[0].out) {
    TCGv_i32 byte32 = tcg_temp_new_i32();
    TCGv_i64 byte64 = tcg_temp_new_i64();
    TCGv_i64 shifted = tcg_temp_new_i64();

    tcg_gen_qemu_ld_i32(byte32, cpu_AE_BITPTR, dc->cring, MO_UB);
    tcg_gen_addi_i32(cpu_AE_BITPTR, cpu_AE_BITPTR, 1);

    if (arg[0].in) {
      tcg_gen_shli_i64(shifted, (TCGv_i64)arg[0].in, 8);
    } else {
      tcg_gen_movi_i64(shifted, 0);
    }
    tcg_gen_extu_i32_i64(byte64, byte32);
    tcg_gen_or_i64((TCGv_i64)arg[0].out, shifted, byte64);
  }
}

/*
 * translate_ae_decode_field_noop  --  explicit baseline for decode-only fields
 *
 * These pseudo-operands contribute immediate/format selection during decode but
 * do not move data or mutate architectural state during translation.
 */
static void translate_ae_decode_field_noop(DisasContext *dc, const OpcodeArg arg[],
                       const uint32_t par[])
{
  (void)dc;
  (void)arg;
  (void)par;
}

/*
 * =========================================================
 * HiFi opcode translator table
 * =========================================================
 */
static const XtensaOpcodeOps hifi_ops[] = {
    /* -- move instructions -- */
    { .name = "ae_mov",        .translate = translate_ae_mov_generic },
    { .name = "ae_movae",      .translate = translate_ae_mov_generic },
    { .name = "ae_movea",      .translate = translate_ae_mov_generic },
    { .name = "ae_movasar",    .translate = translate_ae_movasar },
    { .name = "ae_movsard7",   .translate = translate_ae_movasar },
    { .name = "ae_movsara7x2", .translate = translate_ae_movsara },
    { .name = "ae_movi",       .translate = translate_ae_movi_generic },
    { .name = "ae_movvfcrfsr", .translate = translate_ae_movvfcrfsr },
    { .name = "ae_movfcrfsrv", .translate = translate_ae_movfcrfsrv },

    /* -- HiFi AE user/state register access -- */
    { .name = "rur.ae_ovf_sar",      .translate = translate_ae_rur_ovf_sar },
    { .name = "wur.ae_ovf_sar",      .translate = translate_ae_wur_ovf_sar },
    { .name = "xsr.ae_ovf_sar",      .translate = translate_ae_xsr_ovf_sar },
    { .name = "rur.ae_bithead",      .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_BITHEAD} },
    { .name = "wur.ae_bithead",      .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_BITHEAD} },
    { .name = "xsr.ae_bithead",      .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_BITHEAD} },
    { .name = "rur.ae_ts_fts_bu_bp", .translate = translate_ae_rur_ts_fts_bu_bp },
    { .name = "wur.ae_ts_fts_bu_bp", .translate = translate_ae_wur_ts_fts_bu_bp },
    { .name = "xsr.ae_ts_fts_bu_bp", .translate = translate_ae_xsr_ts_fts_bu_bp },
    { .name = "rur.ae_cw_sd_no",     .translate = translate_ae_rur_cw_sd_no },
    { .name = "wur.ae_cw_sd_no",     .translate = translate_ae_wur_cw_sd_no },
    { .name = "xsr.ae_cw_sd_no",     .translate = translate_ae_xsr_cw_sd_no },
    { .name = "rur.ae_cbegin0",      .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_CBEGIN0} },
    { .name = "wur.ae_cbegin0",      .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_CBEGIN0} },
    { .name = "xsr.ae_cbegin0",      .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_CBEGIN0} },
    { .name = "rur.ae_cend0",        .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_CEND0} },
    { .name = "wur.ae_cend0",        .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_CEND0} },
    { .name = "xsr.ae_cend0",        .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_CEND0} },
    { .name = "rur.ae_cbegin1",      .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_CBEGIN1} },
    { .name = "wur.ae_cbegin1",      .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_CBEGIN1} },
    { .name = "xsr.ae_cbegin1",      .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_CBEGIN1} },
    { .name = "rur.ae_cend1",        .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_CEND1} },
    { .name = "wur.ae_cend1",        .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_CEND1} },
    { .name = "xsr.ae_cend1",        .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_CEND1} },
    { .name = "rur.ae_overflow",     .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_OVERFLOW} },
    { .name = "wur.ae_overflow",     .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_OVERFLOW} },
    { .name = "xsr.ae_overflow",     .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_OVERFLOW} },
    { .name = "rur.ae_sar",          .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_SAR} },
    { .name = "wur.ae_sar",          .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_SAR} },
    { .name = "xsr.ae_sar",          .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_SAR} },
    { .name = "rur.ae_bitptr",       .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_BITPTR} },
    { .name = "wur.ae_bitptr",       .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_BITPTR} },
    { .name = "xsr.ae_bitptr",       .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_BITPTR} },
    { .name = "rur.ae_bitsused",     .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_BITSUSED} },
    { .name = "wur.ae_bitsused",     .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_BITSUSED} },
    { .name = "xsr.ae_bitsused",     .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_BITSUSED} },
    { .name = "rur.ae_tablesize",    .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_TABLESIZE} },
    { .name = "wur.ae_tablesize",    .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_TABLESIZE} },
    { .name = "xsr.ae_tablesize",    .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_TABLESIZE} },
    { .name = "rur.ae_first_ts",     .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_FIRST_TS} },
    { .name = "wur.ae_first_ts",     .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_FIRST_TS} },
    { .name = "xsr.ae_first_ts",     .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_FIRST_TS} },
    { .name = "rur.ae_nextoffset",   .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_NEXTOFFSET} },
    { .name = "wur.ae_nextoffset",   .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_NEXTOFFSET} },
    { .name = "xsr.ae_nextoffset",   .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_NEXTOFFSET} },
    { .name = "rur.ae_searchdone",   .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_SEARCHDONE} },
    { .name = "wur.ae_searchdone",   .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_SEARCHDONE} },
    { .name = "xsr.ae_searchdone",   .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_SEARCHDONE} },
    { .name = "rur.ae_cwrap",        .translate = translate_ae_rur_state,
      .par = (const uint32_t[]){AE_USR_CWRAP} },
    { .name = "wur.ae_cwrap",        .translate = translate_ae_wur_state,
      .par = (const uint32_t[]){AE_USR_CWRAP} },
    { .name = "xsr.ae_cwrap",        .translate = translate_ae_xsr_state,
      .par = (const uint32_t[]){AE_USR_CWRAP} },

    /* -- 64-bit loads -- */
    { .name = "ae_l32x2.i",    .translate = translate_ae_l64_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2.ip",   .translate = translate_ae_l64_ip,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2.x",    .translate = translate_ae_l64_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2.xp",   .translate = translate_ae_l64_xp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2.xc",   .translate = translate_ae_l64_xc,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2.xc1",  .translate = translate_ae_l64_xc1,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2.ric",  .translate = translate_ae_l32x2_ric,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_l32x2.ric1", .translate = translate_ae_l32x2_ric1,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },

    { .name = "ae_l64.i",      .translate = translate_ae_l64_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l64.ip",     .translate = translate_ae_l64_ip,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l64.x",      .translate = translate_ae_l64_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l64.xp",     .translate = translate_ae_l64_xp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l64.xc",     .translate = translate_ae_l64_xc,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l64.xc1",    .translate = translate_ae_l64_xc1,
      .op_flags = XTENSA_OP_LOAD },

    { .name = "ae_l16x4.i",    .translate = translate_ae_l64_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x4.ip",   .translate = translate_ae_l64_ip,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x4.x",    .translate = translate_ae_l64_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x4.xp",   .translate = translate_ae_l64_xp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x4.xc",   .translate = translate_ae_l64_xc,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x4.xc1",  .translate = translate_ae_l64_xc1,
      .op_flags = XTENSA_OP_LOAD },

    /* ae_l32x2f24 loads */
    { .name = "ae_l32x2f24.i",    .translate = translate_ae_l32x2f24_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.ip",   .translate = translate_ae_l32x2f24_ip,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.x",    .translate = translate_ae_l32x2f24_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.xp",   .translate = translate_ae_l32x2f24_xp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.xc",   .translate = translate_ae_l32x2f24_xc,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.xc1",  .translate = translate_ae_l32x2f24_xc1,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.ri",   .translate = translate_ae_l32x2f24_ri,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.ric",  .translate = translate_ae_l32x2f24_ri,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.ric1", .translate = translate_ae_l32x2f24_ri,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32x2f24.rip",  .translate = translate_ae_l32x2f24_rip,
      .op_flags = XTENSA_OP_LOAD },

    /* -- 32-bit loads -- */
    { .name = "ae_l32.i",      .translate = translate_ae_l32_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32.ip",     .translate = translate_ae_l32_ip,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32.x",      .translate = translate_ae_l32_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32.xp",     .translate = translate_ae_l32_xp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32.xc",     .translate = translate_ae_l32_xc,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32.xc1",    .translate = translate_ae_l32_xc1,
      .op_flags = XTENSA_OP_LOAD },

    /* ae_l32f24 (treated same as ae_l32 for simulation) */
    { .name = "ae_l32f24.i",   .translate = translate_ae_l32_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32f24.ip",  .translate = translate_ae_l32_ip,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32f24.x",   .translate = translate_ae_l32_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32f24.xp",  .translate = translate_ae_l32_xp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32f24.xc",  .translate = translate_ae_l32_xc,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32f24.xc1", .translate = translate_ae_l32_xc1,
      .op_flags = XTENSA_OP_LOAD },

    /* -- 16-bit loads -- */
    { .name = "ae_l16.i",      .translate = translate_ae_l16_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16.ip",     .translate = translate_ae_l16_ip,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16.x",      .translate = translate_ae_l16_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16.xp",     .translate = translate_ae_l16_xp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16.xc",     .translate = translate_ae_l16_xc,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16.xc1",    .translate = translate_ae_l16_xc1,
      .op_flags = XTENSA_OP_LOAD },

    { .name = "ae_l16si.n",    .translate = translate_ae_l16si_n,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16ui.n",    .translate = translate_ae_l16ui_n,
      .op_flags = XTENSA_OP_LOAD },

    /* -- 64-bit stores -- */
    { .name = "ae_s32x2.i",    .translate = translate_ae_s64_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2.ip",   .translate = translate_ae_s64_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2.x",    .translate = translate_ae_s64_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2.xp",   .translate = translate_ae_s64_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2.xc",   .translate = translate_ae_s64_xc,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2.xc1",  .translate = translate_ae_s32x2_xc1,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2.ric",  .translate = translate_ae_s32x2_ric,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_s32x2.ric1", .translate = translate_ae_s32x2_ric1,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },

    { .name = "ae_s64.i",      .translate = translate_ae_s64_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s64.ip",     .translate = translate_ae_s64_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s64.x",      .translate = translate_ae_s64_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s64.xp",     .translate = translate_ae_s64_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s64.xc",     .translate = translate_ae_s64_xc,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s64.xc1",    .translate = translate_ae_s64_xc1,
      .op_flags = XTENSA_OP_STORE },

    { .name = "ae_s16x4.i",    .translate = translate_ae_s64_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x4.ip",   .translate = translate_ae_s64_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x4.x",    .translate = translate_ae_s64_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x4.xp",   .translate = translate_ae_s64_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x4.xc",   .translate = translate_ae_s64_xc,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x4.xc1",  .translate = translate_ae_s64_xc1,
      .op_flags = XTENSA_OP_STORE },

    /* ae_s32x2f24 stores */
    { .name = "ae_s32x2f24.i",    .translate = translate_ae_s32x2f24_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2f24.ip",   .translate = translate_ae_s32x2f24_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2f24.x",    .translate = translate_ae_s32x2f24_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2f24.xp",   .translate = translate_ae_s32x2f24_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2f24.xc",   .translate = translate_ae_s32x2f24_xc,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2f24.xc1",  .translate = translate_ae_s32x2f24_xc1,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2f24.ric",  .translate = translate_ae_s32x2f24_ric,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_s32x2f24.ric1", .translate = translate_ae_s32x2f24_ric1,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_s32x2f24.rip",  .translate = translate_ae_s32x2f24_rip,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },

    /* -- 32-bit stores (lower half of AE_DR) -- */
    { .name = "ae_s32.l.i",    .translate = translate_ae_s32_l_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32.l.ip",   .translate = translate_ae_s32_l_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32.l.x",    .translate = translate_ae_s32_l_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32.l.xp",   .translate = translate_ae_s32_l_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32.l.xc",   .translate = translate_ae_s32_l_xc,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32.l.xc1",  .translate = translate_ae_s32_l_xc1,
      .op_flags = XTENSA_OP_STORE },

    { .name = "ae_s32f24.l.i",  .translate = translate_ae_s32_l_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32f24.l.ip", .translate = translate_ae_s32_l_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32f24.l.x",  .translate = translate_ae_s32_l_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32f24.l.xp", .translate = translate_ae_s32_l_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32f24.l.xc", .translate = translate_ae_s32_l_xc,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32f24.l.xc1",.translate = translate_ae_s32_l_xc1,
      .op_flags = XTENSA_OP_STORE },

    /* -- 16-bit stores (lower 16 bits of AE_DR) -- */
    { .name = "ae_s16.0.i",    .translate = translate_ae_s16_0_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16.0.ip",   .translate = translate_ae_s16_0_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16.0.x",    .translate = translate_ae_s16_0_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16.0.xp",   .translate = translate_ae_s16_0_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16.0.xc",   .translate = translate_ae_s16_0_xc,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16.0.xc1",  .translate = translate_ae_s16_0_xc1,
      .op_flags = XTENSA_OP_STORE },

    { .name = "ae_s16i.n",     .translate = translate_ae_s16i_n,
      .op_flags = XTENSA_OP_STORE },

    /* -- alignment register ops -- */
    { .name = "ae_la64.pp",    .translate = translate_ae_la64_pp,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_lalign64.i", .translate = translate_ae_lalign64_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_salign64.i", .translate = translate_ae_salign64_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_sa64pos.fp", .translate = translate_ae_sa64_fp },
    { .name = "ae_sa64neg.fp", .translate = translate_ae_sa64_fp },
    { .name = "ae_movalign",   .translate = translate_ae_movalign },

    /* -- streaming alignment loads: ae_la32x2.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_la32x2.ic",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2.ic1",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2.ip",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2.rip",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la32x2.ric",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la32x2.ric1", .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment loads: ae_la16x4.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_la16x4.ic",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la16x4.ic1",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la16x4.ip",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la16x4.rip",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la16x4.ric",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la16x4.ric1", .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment loads: ae_la32x2f24.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_la32x2f24.ic",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2f24.ic1",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2f24.ip",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2f24.rip",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la32x2f24.ric",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la32x2f24.ric1", .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment loads: ae_la24.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_la24.ic",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24.ic1",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24.ip",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24.rip",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la24.ric",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la24.ric1", .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment loads: ae_la24x2.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_la24x2.ic",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24x2.ic1",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24x2.ip",   .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24x2.rip",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la24x2.ric",  .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la24x2.ric1", .translate = translate_ae_la64_align_load,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- alignment load prime with AR advance: ae_la{24,24x2,32x2,16x4}{pos,neg}.pc -- */
    { .name = "ae_la24pos.pc",    .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24x2pos.pc",  .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2pos.pc",  .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la16x4pos.pc",  .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24neg.pc",    .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la24x2neg.pc",  .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la32x2neg.pc",  .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la16x4neg.pc",  .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la24pos.pc1",   .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24x2pos.pc1", .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la32x2pos.pc1", .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la16x4pos.pc1", .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){8} },
    { .name = "ae_la24neg.pc1",   .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la24x2neg.pc1", .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la32x2neg.pc1", .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_la16x4neg.pc1", .translate = translate_ae_la_align_prime_pc,
      .op_flags = XTENSA_OP_LOAD, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment stores: ae_sa32x2.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_sa32x2.ic",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa32x2.ic1",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa32x2.ip",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa32x2.rip",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa32x2.ric",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa32x2.ric1", .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment stores: ae_sa16x4.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_sa16x4.ic",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa16x4.ic1",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa16x4.ip",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa16x4.rip",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa16x4.ric",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa16x4.ric1", .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment stores: ae_sa32x2f24.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_sa32x2f24.ic",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa32x2f24.ic1",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa32x2f24.ip",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa32x2f24.rip",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa32x2f24.ric",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa32x2f24.ric1", .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment stores: ae_sa24.l.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_sa24.l.ic",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa24.l.ic1",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa24.l.ip",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa24.l.rip",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa24.l.ric",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa24.l.ric1", .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },

    /* -- streaming alignment stores: ae_sa24x2.{ic,ic1,ip,rip,ric,ric1} -- */
    { .name = "ae_sa24x2.ic",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa24x2.ic1",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa24x2.ip",   .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){8} },
    { .name = "ae_sa24x2.rip",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa24x2.ric",  .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    { .name = "ae_sa24x2.ric1", .translate = translate_ae_sa64_align_store,
      .op_flags = XTENSA_OP_STORE, .par = (const uint32_t[]){(uint32_t)-8} },
    /* --- Properly-implemented HiFi4 additions --- */
    /* --- HiFi4 additional ops: logical --- */
    { .name = "ae_and",  .translate = translate_ae_and },
    { .name = "ae_or",   .translate = translate_ae_or },
    { .name = "ae_xor",  .translate = translate_ae_xor },
    { .name = "ae_nand", .translate = translate_ae_nand },
    /* --- 64-bit arithmetic --- */
    { .name = "ae_add64",  .translate = translate_ae_add64 },
    { .name = "ae_sub64",  .translate = translate_ae_sub64 },
    { .name = "ae_neg64",  .translate = translate_ae_neg64 },
    { .name = "ae_abs64",  .translate = translate_ae_abs64 },
    /* --- 64-bit shifts (immediate) --- */
    { .name = "ae_slai64", .translate = translate_ae_slai64 },
    { .name = "ae_srli64", .translate = translate_ae_srli64 },
    { .name = "ae_srai64", .translate = translate_ae_srai64 },
    /* --- 64-bit shifts (register) --- */
    { .name = "ae_sra64_32", .translate = translate_ae_sra64_32 },
    { .name = "ae_srla64",   .translate = translate_ae_srla64 },
    { .name = "ae_slaa64",   .translate = translate_ae_slaa64 },
    /* --- Move: AR <-> AE_DR 32-bit --- */
    { .name = "ae_movda32",   .translate = translate_ae_movda32 },
    { .name = "ae_movda32x2", .translate = translate_ae_movda32x2 },
    { .name = "ae_movad32.l", .translate = translate_ae_movad32l },
    { .name = "ae_movad32.h", .translate = translate_ae_movad32h },
    /* --- Move: AR <-> AE_DR 16-bit --- */
    { .name = "ae_movda16",   .translate = translate_ae_movda16 },
    { .name = "ae_movda16x2", .translate = translate_ae_movda16x2 },
    { .name = "ae_movad16.0", .translate = translate_ae_movad16,
      .par = (const uint32_t[]){0} },
    { .name = "ae_movad16.1", .translate = translate_ae_movad16,
      .par = (const uint32_t[]){1} },
    { .name = "ae_movad16.2", .translate = translate_ae_movad16,
      .par = (const uint32_t[]){2} },
    { .name = "ae_movad16.3", .translate = translate_ae_movad16,
      .par = (const uint32_t[]){3} },
    /* --- Move: AE_VALIGN --- */
    { .name = "ae_moveep", .translate = translate_ae_moveep },
    /* --- Move: boolean (baseline dataflow model, both directions) --- */
    { .name = "ae_movab",    .translate = translate_ae_movab,
      .par = (const uint32_t[]){1} },
    { .name = "ae_movab2",   .translate = translate_ae_movab,
      .par = (const uint32_t[]){2} },
    { .name = "ae_movab4",   .translate = translate_ae_movab,
      .par = (const uint32_t[]){4} },
    { .name = "ae_movba",    .translate = translate_ae_movba },
    { .name = "ae_movba1x2", .translate = translate_ae_movba },
    { .name = "ae_movba2",   .translate = translate_ae_movba },
    { .name = "ae_movba4",   .translate = translate_ae_movba },
    { .name = "ae_movb2",    .translate = translate_ae_movba },
    { .name = "ae_movb4",    .translate = translate_ae_movba },
     /* --- Move: conditional (bool regs not modeled) --- */
    { .name = "ae_movf16x4", .translate = translate_ae_movft,
      .par = (const uint32_t[]){0} },
    { .name = "ae_movf32x2", .translate = translate_ae_movft,
      .par = (const uint32_t[]){0} },
    { .name = "ae_movf64",   .translate = translate_ae_movft,
      .par = (const uint32_t[]){0} },
    { .name = "ae_movt16x4", .translate = translate_ae_movft,
      .par = (const uint32_t[]){1} },
    { .name = "ae_movt32x2", .translate = translate_ae_movft,
      .par = (const uint32_t[]){1} },
    { .name = "ae_movt64",   .translate = translate_ae_movft,
      .par = (const uint32_t[]){1} },
      /* --- Loads: ae_l16m --- */
      { .name = "ae_l16m.i",   .translate = translate_ae_l16m_i,
        .op_flags = XTENSA_OP_LOAD },
      { .name = "ae_l16m.iu",  .translate = translate_ae_l16m_iu,
        .op_flags = XTENSA_OP_LOAD },
      { .name = "ae_l16m.x",   .translate = translate_ae_l16m_x,
        .op_flags = XTENSA_OP_LOAD },
      { .name = "ae_l16m.xu",  .translate = translate_ae_l16m_xu,
        .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16m.xc",  .translate = translate_ae_l16m_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16m.xc1", .translate = translate_ae_l16m_x,
      .op_flags = XTENSA_OP_LOAD },
    /* --- Loads: ae_l16x2m --- */
    { .name = "ae_l16x2m.i",   .translate = translate_ae_l16x2m_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x2m.iu",  .translate = translate_ae_l16x2m_iu,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x2m.x",   .translate = translate_ae_l16x2m_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x2m.xu",  .translate = translate_ae_l16x2m_xu,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x2m.xc",  .translate = translate_ae_l16x2m_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l16x2m.xc1", .translate = translate_ae_l16x2m_x,
      .op_flags = XTENSA_OP_LOAD },
    /* --- Loads: ae_l32m --- */
    { .name = "ae_l32m.i",   .translate = translate_ae_l32m_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32m.iu",  .translate = translate_ae_l32m_iu,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32m.x",   .translate = translate_ae_l32m_x,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32m.xu",  .translate = translate_ae_l32m_xu,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l32m.xc",  .translate = translate_ae_l32m_x,
      .op_flags = XTENSA_OP_LOAD },
    /* --- Loads: ae_l8x4f --- */
    { .name = "ae_l8x4f.i",  .translate = translate_ae_l8x4f_i,
      .op_flags = XTENSA_OP_LOAD },
    { .name = "ae_l8x4f.ip", .translate = translate_ae_l8x4f_ip,
      .op_flags = XTENSA_OP_LOAD },
    /* --- Stores: ae_s16m --- */
    { .name = "ae_s16m.l.i",   .translate = translate_ae_s16m_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16m.l.iu",  .translate = translate_ae_s16m_iu,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16m.l.x",   .translate = translate_ae_s16m_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16m.l.xu",  .translate = translate_ae_s16m_xu,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16m.l.xc",  .translate = translate_ae_s16m_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16m.l.xc1", .translate = translate_ae_s16m_x,
      .op_flags = XTENSA_OP_STORE },
    /* --- Stores: ae_s16x2m --- */
    { .name = "ae_s16x2m.i",   .translate = translate_ae_s16x2m_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x2m.iu",  .translate = translate_ae_s16x2m_iu,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x2m.x",   .translate = translate_ae_s16x2m_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x2m.xu",  .translate = translate_ae_s16x2m_xu,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x2m.xc",  .translate = translate_ae_s16x2m_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s16x2m.xc1", .translate = translate_ae_s16x2m_x,
      .op_flags = XTENSA_OP_STORE },
    /* --- Stores: ae_s32m --- */
    { .name = "ae_s32m.i",  .translate = translate_ae_s32m_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32m.iu", .translate = translate_ae_s32m_iu,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32m.x",  .translate = translate_ae_s32m_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32m.xu", .translate = translate_ae_s32m_xu,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32m.xc", .translate = translate_ae_s32m_x,
      .op_flags = XTENSA_OP_STORE },
    /* --- Stores: ae_s32ra64s --- */
    { .name = "ae_s32ra64s.i",   .translate = translate_ae_s32ra64s_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32ra64s.ip",  .translate = translate_ae_s32ra64s_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32ra64s.x",   .translate = translate_ae_s32ra64s_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32ra64s.xp",  .translate = translate_ae_s32ra64s_xp,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32ra64s.xc",  .translate = translate_ae_s32ra64s_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32ra64s.xc1", .translate = translate_ae_s32ra64s_x,
      .op_flags = XTENSA_OP_STORE },
    /* --- Stores: ae_s32x2rng --- */
    { .name = "ae_s32x2rng.i",  .translate = translate_ae_s32x2rng_i,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2rng.ip", .translate = translate_ae_s32x2rng_ip,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2rng.x",  .translate = translate_ae_s32x2rng_x,
      .op_flags = XTENSA_OP_STORE },
    { .name = "ae_s32x2rng.xp", .translate = translate_ae_s32x2rng_xp,
      .op_flags = XTENSA_OP_STORE },
    /* --- Zero/sign extension --- */
    { .name = "ae_zext16", .translate = translate_ae_zext16 },
    { .name = "ae_sext16", .translate = translate_ae_sext16 },
    { .name = "ae_sext32", .translate = translate_ae_sext32 },
    /* --- Circular address --- */
    { .name = "ae_addcirc.xc",  .translate = translate_ae_addcirc },
    { .name = "ae_addcirc.xc1", .translate = translate_ae_addcirc },
    { .name = "ae_addicirc",    .translate = translate_ae_addicirc },

    /* --- NOP stubs for remaining HiFi4 ops --- */
    { .name = "ae_abs16s", .translate = translate_ae_abs16s },
    { .name = "ae_abs24s", .translate = translate_ae_abs24s },
    { .name = "ae_abs32",  .translate = translate_ae_abs32  },
    { .name = "ae_abs32s", .translate = translate_ae_abs32s },
    { .name = "ae_abs64s", .translate = translate_ae_abs64s },
    { .name = "ae_abssq56s", .translate = translate_ae_abssq56s },
    { .name = "ae_add16",  .translate = translate_ae_add16  },
    { .name = "ae_add16s", .translate = translate_ae_add16s },
    { .name = "ae_add24s", .translate = translate_ae_add24s },
    { .name = "ae_add32",  .translate = translate_ae_add32  },
    { .name = "ae_add32_hl_lh",  .translate = translate_ae_add32_hl_lh },
    { .name = "ae_add32s", .translate = translate_ae_add32s },
    { .name = "ae_add32s_hl_lh", .translate = translate_ae_add32s_hl_lh },
    { .name = "ae_add32x27", .translate = translate_ae_add32x27 },
    { .name = "ae_add64s", .translate = translate_ae_add64s },
    { .name = "ae_add72", .translate = translate_ae_add72 },
    { .name = "ae_add72x64", .translate = translate_ae_add72x64 },
    { .name = "ae_addandsub32s", .translate = translate_ae_addandsub32s },
    { .name = "ae_addandsubrng32", .translate = translate_ae_addandsubrng32 },
    { .name = "ae_addbrba32", .translate = translate_ae_addbrba32 },
    { .name = "ae_addrng32", .translate = translate_ae_addrng32 },
    { .name = "ae_addsq56s", .translate = translate_ae_addsq56s },
    { .name = "ae_addsub32", .translate = translate_ae_addsub32 },
    { .name = "ae_addsub32s", .translate = translate_ae_addsub32s },
    { .name = "ae_ar_to_dr_v", .translate = translate_ae_carrier_ar_to_dr_v },
    { .name = "ae_arth_v", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_arth_v0", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_arth_v1", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_bitswap", .translate = translate_ae_bitswap },
    { .name = "ae_calcrng1", .translate = translate_ae_calcrng,
      .par = (const uint32_t[]){1} },
    { .name = "ae_calcrng2", .translate = translate_ae_calcrng,
      .par = (const uint32_t[]){2} },
    { .name = "ae_calcrng3", .translate = translate_ae_calcrng,
      .par = (const uint32_t[]){3} },
    { .name = "ae_clamps16", .translate = translate_ae_clamps16 },
    { .name = "ae_cmov_v", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_cmov_v0", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_cvt32x2f16.10",  .translate = translate_ae_cvt32x2f16_10 },
    { .name = "ae_cvt32x2f16.32",  .translate = translate_ae_cvt32x2f16_32 },
    { .name = "ae_cvt48a32",        .translate = translate_ae_cvt48a32 },
    { .name = "ae_cvt48f32.h",      .translate = translate_ae_cvtp32s_h },
    { .name = "ae_cvt48f32.l",      .translate = translate_ae_cvtp32s_l },
    { .name = "ae_cvt64a32",        .translate = translate_ae_cvt48a32 },
    { .name = "ae_cvt64f32.h",      .translate = translate_ae_cvtp32s_h },
    { .name = "ae_cvta32f24s.h",    .translate = translate_ae_cvta32f24s_h },
    { .name = "ae_cvta32f24s.l",    .translate = translate_ae_cvta32f24s_l },
    { .name = "ae_cvtp24a16x2.hh",  .translate = translate_ae_cvtp24a16x2_hh },
    { .name = "ae_cvtp24a16x2.hl",  .translate = translate_ae_cvtp24a16x2_hl },
    { .name = "ae_cvtp24a16x2.lh",  .translate = translate_ae_cvtp24a16x2_lh },
    { .name = "ae_cvtp24a16x2.ll",  .translate = translate_ae_cvtp24a16x2_ll },
    { .name = "ae_cvtq56a32s",      .translate = translate_ae_cvt48a32 },
    { .name = "ae_cvtq56p32s.h",    .translate = translate_ae_cvtp32s_h },
    { .name = "ae_cvtq56p32s.l",    .translate = translate_ae_cvtp32s_l },
    { .name = "ae_db", .translate = translate_ae_db,
      .par = (const uint32_t[]){0} },
    { .name = "ae_db.ic", .translate = translate_ae_db,
      .par = (const uint32_t[]){0} },
    { .name = "ae_db.ic1", .translate = translate_ae_db,
      .par = (const uint32_t[]){0} },
    { .name = "ae_db.ip", .translate = translate_ae_db,
      .par = (const uint32_t[]){1} },
    { .name = "ae_dbi", .translate = translate_ae_dbi,
      .par = (const uint32_t[]){0} },
    { .name = "ae_dbi.ic", .translate = translate_ae_dbi,
      .par = (const uint32_t[]){0} },
    { .name = "ae_dbi.ic1", .translate = translate_ae_dbi,
      .par = (const uint32_t[]){0} },
    { .name = "ae_dbi.ip", .translate = translate_ae_dbi,
      .par = (const uint32_t[]){1} },
    { .name = "ae_div64d32.h", .translate = translate_ae_div64d32,
      .par = (const uint32_t[]){0} },
    { .name = "ae_div64d32.l", .translate = translate_ae_div64d32,
      .par = (const uint32_t[]){1} },
    { .name = "ae_dr_to_ar_v0", .translate = translate_ae_carrier_dr_to_ar_v0 },
    { .name = "ae_dr_to_dr_v", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_dr_to_dr_v0", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_dr_to_dr_v1", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_eq16", .translate = translate_ae_cmp16,
      .par = (const uint32_t[]){TCG_COND_EQ} },
    { .name = "ae_eq32", .translate = translate_ae_cmp32,
      .par = (const uint32_t[]){TCG_COND_EQ} },
    { .name = "ae_eq64", .translate = translate_ae_cmp64,
      .par = (const uint32_t[]){TCG_COND_EQ} },
    { .name = "ae_fld_Inst16b_12", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_Inst16b_15_13", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_ar_to_dr_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_arth_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_arth_v0", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_arth_v1", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_cmov_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_cmov_v0", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_cmpp_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_cmpp_v0", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_cmpp_v1", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_dr_to_ar_v0", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_dr_to_dr_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_dr_to_dr_v0", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_dr_to_dr_v1", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_fhba4", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_fhba4_2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_imm2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_immls16", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_immls32", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_immls64", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_immls64half", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_immls64pos", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_ls_av", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_ls_su", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_ls_uu", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_ls_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_ls_v1", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_ls_v2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_osa16", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_osa32", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_osa64", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_pks_d", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_pks_s", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_selimm", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_selimm.N", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_shift_d", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_shift_d0", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_shift_sd", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_to_dr_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_to_dr_v0", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_tp7", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_uu_uu", .translate = translate_ae_decode_field_noop },
    { .name = "ae_fld_uu_v", .translate = translate_ae_decode_field_noop },
    { .name = "ae_format48", .translate = translate_ae_decode_field_noop },
    { .name = "ae_format48_2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_format48_3", .translate = translate_ae_decode_field_noop },
    { .name = "ae_format88", .translate = translate_ae_decode_field_noop },
    { .name = "ae_format88_2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_format88_3", .translate = translate_ae_decode_field_noop },
    { .name = "ae_format88_4", .translate = translate_ae_decode_field_noop },
    { .name = "ae_imm2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_immls16", .translate = translate_ae_decode_field_noop },
    { .name = "ae_immls32", .translate = translate_ae_decode_field_noop },
    { .name = "ae_immls64", .translate = translate_ae_decode_field_noop },
    { .name = "ae_immls64half", .translate = translate_ae_decode_field_noop },
    { .name = "ae_immls64neg", .translate = translate_ae_decode_field_noop },
    { .name = "ae_immls64pos", .translate = translate_ae_decode_field_noop },
    { .name = "ae_lb", .translate = translate_ae_lb },
    { .name = "ae_lbi", .translate = translate_ae_lbi },
    { .name = "ae_lbk", .translate = translate_ae_lbk },
    { .name = "ae_lbki", .translate = translate_ae_lbki },
    { .name = "ae_lbs", .translate = translate_ae_lbs },
    { .name = "ae_lbsi", .translate = translate_ae_lbsi },
    { .name = "ae_le16", .translate = translate_ae_cmp16,
      .par = (const uint32_t[]){TCG_COND_LE} },
    { .name = "ae_le32", .translate = translate_ae_cmp32,
      .par = (const uint32_t[]){TCG_COND_LE} },
    { .name = "ae_le64", .translate = translate_ae_cmp64,
      .par = (const uint32_t[]){TCG_COND_LE} },
    { .name = "ae_leftshift32x5", .translate = translate_ae_leftshift32x5 },
    { .name = "ae_ls_av", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_ls_su", .translate = translate_ae_carrier_valign },
    { .name = "ae_ls_uu", .translate = translate_ae_carrier_valign },
    { .name = "ae_ls_v",  .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_ls_v1", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_ls_v2", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_lt16", .translate = translate_ae_cmp16,
      .par = (const uint32_t[]){TCG_COND_LT} },
    { .name = "ae_lt32", .translate = translate_ae_cmp32,
      .par = (const uint32_t[]){TCG_COND_LT} },
    { .name = "ae_lt64", .translate = translate_ae_cmp64,
      .par = (const uint32_t[]){TCG_COND_LT} },
    { .name = "ae_max32", .translate = translate_ae_minmax32,
      .par = (const uint32_t[]){1} },
    { .name = "ae_max64", .translate = translate_ae_minmax64,
      .par = (const uint32_t[]){1} },
    { .name = "ae_maxabs32s", .translate = translate_ae_minmaxabs32s,
      .par = (const uint32_t[]){1} },
    { .name = "ae_maxabs64s", .translate = translate_ae_minmaxabs64s,
      .par = (const uint32_t[]){1} },
    { .name = "ae_min32", .translate = translate_ae_minmax32,
      .par = (const uint32_t[]){0} },
    { .name = "ae_min64", .translate = translate_ae_minmax64,
      .par = (const uint32_t[]){0} },
    { .name = "ae_minabs32s", .translate = translate_ae_minmaxabs32s,
      .par = (const uint32_t[]){0} },
    { .name = "ae_minabs64s", .translate = translate_ae_minmaxabs64s,
      .par = (const uint32_t[]){0} },
    { .name = "ae_mul16.00", .translate = translate_ae_mul16_00 },
    { .name = "ae_mul16.00_s2", .translate = translate_ae_mul16_00_s2 },
    { .name = "ae_mul16x4", .translate = translate_ae_mulf16x4ss },
    { .name = "ae_mul32.hh", .translate = translate_ae_mul32_hh },
    { .name = "ae_mul32.hh_s2", .translate = translate_ae_mul32_hh_s2 },
    { .name = "ae_mul32.lh", .translate = translate_ae_mul32_lh },
    { .name = "ae_mul32.lh_s2", .translate = translate_ae_mul32_lh_s2 },
    { .name = "ae_mul32.ll", .translate = translate_ae_mul32_ll },
    { .name = "ae_mul32.ll_s2", .translate = translate_ae_mul32_ll_s2 },
    { .name = "ae_mul32ep.hh", .translate = translate_ae_mul32ep_hh },
    { .name = "ae_mul32ep.hh_s2", .translate = translate_ae_mul32ep_hh_s2 },
    { .name = "ae_mul32js",  .translate = translate_ae_mul32js },
    { .name = "ae_mul32u.ll", .translate = translate_ae_mul32u_ll },
    { .name = "ae_mul32usep.lh", .translate = translate_ae_mul32usep_lh },
    { .name = "ae_mul32usep.ll", .translate = translate_ae_mul32usep_ll },
    { .name = "ae_mul32x16.h0", .translate = translate_ae_mul32x16_h0 },
    { .name = "ae_mul32x16.h0_s2", .translate = translate_ae_mul32x16_h0_s2 },
    { .name = "ae_mul32x16.h1", .translate = translate_ae_mul32x16_h1 },
    { .name = "ae_mul32x16.h1_s2", .translate = translate_ae_mul32x16_h1_s2 },
    { .name = "ae_mul32x16.h2", .translate = translate_ae_mul32x16_h2 },
    { .name = "ae_mul32x16.h2_s2", .translate = translate_ae_mul32x16_h2_s2 },
    { .name = "ae_mul32x16.h3", .translate = translate_ae_mul32x16_h3 },
    { .name = "ae_mul32x16.h3_s2", .translate = translate_ae_mul32x16_h3_s2 },
    { .name = "ae_mul32x16.l0", .translate = translate_ae_mul32x16_l0 },
    { .name = "ae_mul32x16.l0_s2", .translate = translate_ae_mul32x16_l0_s2 },
    { .name = "ae_mul32x16.l1", .translate = translate_ae_mul32x16_l1 },
    { .name = "ae_mul32x16.l1_s2", .translate = translate_ae_mul32x16_l1_s2 },
    { .name = "ae_mul32x16.l2", .translate = translate_ae_mul32x16_l2 },
    { .name = "ae_mul32x16.l2_s2", .translate = translate_ae_mul32x16_l2_s2 },
    { .name = "ae_mul32x16.l3", .translate = translate_ae_mul32x16_l3 },
    { .name = "ae_mul32x16.l3_s2", .translate = translate_ae_mul32x16_l3_s2 },
    { .name = "ae_mula16.00", .translate = translate_ae_mula16_00 },
    { .name = "ae_mula16.00_s2", .translate = translate_ae_mula16_00_s2 },
    { .name = "ae_mula16x4", .translate = translate_ae_mulaf16x4ss },
    { .name = "ae_mula32.hh", .translate = translate_ae_mula32_hh },
    { .name = "ae_mula32.hh_s2", .translate = translate_ae_mula32_hh_s2 },
    { .name = "ae_mula32.lh", .translate = translate_ae_mula32_lh },
    { .name = "ae_mula32.lh_s2", .translate = translate_ae_mula32_lh_s2 },
    { .name = "ae_mula32.ll", .translate = translate_ae_mula32_ll },
    { .name = "ae_mula32.ll_s2", .translate = translate_ae_mula32_ll_s2 },
    { .name = "ae_mula32ep.hh", .translate = translate_ae_mula32ep_hh },
    { .name = "ae_mula32ep.hh_s2", .translate = translate_ae_mula32ep_hh_s2 },
    { .name = "ae_mula32u.ll", .translate = translate_ae_mula32u_ll },
    { .name = "ae_mula32usep.lh", .translate = translate_ae_mula32usep_lh },
    { .name = "ae_mula32usep.ll", .translate = translate_ae_mula32usep_ll },
    { .name = "ae_mula32x16.h0", .translate = translate_ae_mula32x16_h0 },
    { .name = "ae_mula32x16.h0_s2", .translate = translate_ae_mula32x16_h0_s2 },
    { .name = "ae_mula32x16.h1", .translate = translate_ae_mula32x16_h1 },
    { .name = "ae_mula32x16.h1_s2", .translate = translate_ae_mula32x16_h1_s2 },
    { .name = "ae_mula32x16.h2", .translate = translate_ae_mula32x16_h2 },
    { .name = "ae_mula32x16.h2_s2", .translate = translate_ae_mula32x16_h2_s2 },
    { .name = "ae_mula32x16.h3", .translate = translate_ae_mula32x16_h3 },
    { .name = "ae_mula32x16.h3_s2", .translate = translate_ae_mula32x16_h3_s2 },
    { .name = "ae_mula32x16.l0", .translate = translate_ae_mula32x16_l0 },
    { .name = "ae_mula32x16.l0_s2", .translate = translate_ae_mula32x16_l0_s2 },
    { .name = "ae_mula32x16.l1", .translate = translate_ae_mula32x16_l1 },
    { .name = "ae_mula32x16.l1_s2", .translate = translate_ae_mula32x16_l1_s2 },
    { .name = "ae_mula32x16.l2", .translate = translate_ae_mula32x16_l2 },
    { .name = "ae_mula32x16.l2_s2", .translate = translate_ae_mula32x16_l2_s2 },
    { .name = "ae_mula32x16.l3", .translate = translate_ae_mula32x16_l3 },
    { .name = "ae_mula32x16.l3_s2", .translate = translate_ae_mula32x16_l3_s2 },
    { .name = "ae_mulaaaafq32x16", .translate = translate_ae_mulaaaafq32x16 },
    { .name = "ae_mulaaaafq32x16_s2", .translate = translate_ae_mulaaaafq32x16_s2 },
    { .name = "ae_mulaaaaq16", .translate = translate_ae_mulaaaaq16 },
    { .name = "ae_mulaaaaq16_s2", .translate = translate_ae_mulaaaaq16_s2 },
    { .name = "ae_mulaaaaq32x16", .translate = translate_ae_mulaaaaq32x16 },
    { .name = "ae_mulaaaaq32x16_s2", .translate = translate_ae_mulaaaaq32x16_s2 },
    { .name = "ae_mulaad32.hh.ll", .translate = translate_ae_mulaad32_hh_ll },
    { .name = "ae_mulaad32.hh.ll_s2", .translate = translate_ae_mulaad32_hh_ll_s2 },
    { .name = "ae_mulaad32.hl.lh", .translate = translate_ae_mulaad32_hl_lh },
    { .name = "ae_mulaad32.hl.lh_s2", .translate = translate_ae_mulaad32_hl_lh_s2 },
    { .name = "ae_mulaad32ep.hh.ll", .translate = translate_ae_mulaad32ep_hh_ll },
    { .name = "ae_mulaad32ep.hh.ll_s2", .translate = translate_ae_mulaad32ep_hh_ll_s2 },
    { .name = "ae_mulaad32usep.hl.lh", .translate = translate_ae_mulaad32usep_hl_lh },
    { .name = "ae_mulaad32usep.hl.lh_s2", .translate = translate_ae_mulaad32usep_hl_lh_s2 },
    { .name = "ae_mulaad32x16.h0.l1", .translate = translate_ae_mulaad32x16_h0_l1 },
    { .name = "ae_mulaad32x16.h0.l1_s2", .translate = translate_ae_mulaad32x16_h0_l1_s2 },
    { .name = "ae_mulaad32x16.h1.l0", .translate = translate_ae_mulaad32x16_h1_l0 },
    { .name = "ae_mulaad32x16.h1.l0_s2", .translate = translate_ae_mulaad32x16_h1_l0_s2 },
    { .name = "ae_mulaad32x16.h2.l3", .translate = translate_ae_mulaad32x16_h2_l3 },
    { .name = "ae_mulaad32x16.h2.l3_s2", .translate = translate_ae_mulaad32x16_h2_l3_s2 },
    { .name = "ae_mulaad32x16.h3.l2", .translate = translate_ae_mulaad32x16_h3_l2 },
    { .name = "ae_mulaad32x16.h3.l2_s2", .translate = translate_ae_mulaad32x16_h3_l2_s2 },
    { .name = "ae_mulaafd16ss.11_00", .translate = translate_ae_mulaafd16ss_11_00 },
    { .name = "ae_mulaafd16ss.11_00_s2", .translate = translate_ae_mulaafd16ss_11_00_s2 },
    { .name = "ae_mulaafd16ss.13_02", .translate = translate_ae_mulaafd16ss_13_02 },
    { .name = "ae_mulaafd16ss.13_02_s2", .translate = translate_ae_mulaafd16ss_13_02_s2 },
    { .name = "ae_mulaafd16ss.33_22", .translate = translate_ae_mulaafd16ss_33_22 },
    { .name = "ae_mulaafd16ss.33_22_s2", .translate = translate_ae_mulaafd16ss_33_22_s2 },
    { .name = "ae_mulaafd32ra.hh.ll", .translate = translate_ae_mulaafd32ra_hh_ll },
    { .name = "ae_mulaafd32ra.hh.ll_s2", .translate = translate_ae_mulaafd32ra_hh_ll_s2 },
    { .name = "ae_mulaafd32ra.hl.lh", .translate = translate_ae_mulaafd32ra_hl_lh },
    { .name = "ae_mulaafd32ra.hl.lh_s2", .translate = translate_ae_mulaafd32ra_hl_lh_s2 },
    { .name = "ae_mulaafd32s.hh.ll", .translate = translate_ae_mulaafd32s_hh_ll },
    { .name = "ae_mulaafd32s.hh.ll_s2", .translate = translate_ae_mulaafd32s_hh_ll_s2 },
    { .name = "ae_mulaafd32s.hl.lh", .translate = translate_ae_mulaafd32s_hl_lh },
    { .name = "ae_mulaafd32s.hl.lh_s2", .translate = translate_ae_mulaafd32s_hl_lh_s2 },
    { .name = "ae_mulaafd32x16.h0.l1", .translate = translate_ae_mulaafd32x16_h0_l1 },
    { .name = "ae_mulaafd32x16.h0.l1_s2", .translate = translate_ae_mulaafd32x16_h0_l1_s2 },
    { .name = "ae_mulaafd32x16.h1.l0", .translate = translate_ae_mulaafd32x16_h1_l0 },
    { .name = "ae_mulaafd32x16.h1.l0_s2", .translate = translate_ae_mulaafd32x16_h1_l0_s2 },
    { .name = "ae_mulaafd32x16.h2.l3", .translate = translate_ae_mulaafd32x16_h2_l3 },
    { .name = "ae_mulaafd32x16.h2.l3_s2", .translate = translate_ae_mulaafd32x16_h2_l3_s2 },
    { .name = "ae_mulaafd32x16.h3.l2", .translate = translate_ae_mulaafd32x16_h3_l2 },
    { .name = "ae_mulaafd32x16.h3.l2_s2", .translate = translate_ae_mulaafd32x16_h3_l2_s2 },
    { .name = "ae_mulac32", .translate = translate_ae_mulac32 },
    { .name = "ae_mulac32x16.h", .translate = translate_ae_mulac32x16_h },
    { .name = "ae_mulac32x16.l", .translate = translate_ae_mulac32x16_l },
    { .name = "ae_mulaf16ss.00", .translate = translate_ae_mulaf16ss_00 },
    { .name = "ae_mulaf16ss.00_s2", .translate = translate_ae_mulaf16ss_00_s2 },
    { .name = "ae_mulaf16ss.10", .translate = translate_ae_mulaf16ss_10 },
    { .name = "ae_mulaf16ss.10_s2", .translate = translate_ae_mulaf16ss_10_s2 },
    { .name = "ae_mulaf16ss.11", .translate = translate_ae_mulaf16ss_11 },
    { .name = "ae_mulaf16ss.11_s2", .translate = translate_ae_mulaf16ss_11_s2 },
    { .name = "ae_mulaf16ss.20", .translate = translate_ae_mulaf16ss_20 },
    { .name = "ae_mulaf16ss.20_s2", .translate = translate_ae_mulaf16ss_20_s2 },
    { .name = "ae_mulaf16ss.21", .translate = translate_ae_mulaf16ss_21 },
    { .name = "ae_mulaf16ss.21_s2", .translate = translate_ae_mulaf16ss_21_s2 },
    { .name = "ae_mulaf16ss.22", .translate = translate_ae_mulaf16ss_22 },
    { .name = "ae_mulaf16ss.22_s2", .translate = translate_ae_mulaf16ss_22_s2 },
    { .name = "ae_mulaf16ss.30", .translate = translate_ae_mulaf16ss_30 },
    { .name = "ae_mulaf16ss.30_s2", .translate = translate_ae_mulaf16ss_30_s2 },
    { .name = "ae_mulaf16ss.31", .translate = translate_ae_mulaf16ss_31 },
    { .name = "ae_mulaf16ss.31_s2", .translate = translate_ae_mulaf16ss_31_s2 },
    { .name = "ae_mulaf16ss.32", .translate = translate_ae_mulaf16ss_32 },
    { .name = "ae_mulaf16ss.32_s2", .translate = translate_ae_mulaf16ss_32_s2 },
    { .name = "ae_mulaf16ss.33", .translate = translate_ae_mulaf16ss_33 },
    { .name = "ae_mulaf16ss.33_s2", .translate = translate_ae_mulaf16ss_33_s2 },
    { .name = "ae_mulaf16x4ss", .translate = translate_ae_mulaf16x4ss },
    { .name = "ae_mulaf32r.hh", .translate = translate_ae_mulaf32_hh },
    { .name = "ae_mulaf32r.hh_s2", .translate = translate_ae_mulaf32_hh_s2 },
    { .name = "ae_mulaf32r.lh", .translate = translate_ae_mulaf32_lh },
    { .name = "ae_mulaf32r.lh_s2", .translate = translate_ae_mulaf32_lh_s2 },
    { .name = "ae_mulaf32r.ll", .translate = translate_ae_mulaf32_ll },
    { .name = "ae_mulaf32r.ll_s2", .translate = translate_ae_mulaf32_ll_s2 },
    { .name = "ae_mulaf32ra.hh", .translate = translate_ae_mulaf32_hh },
    { .name = "ae_mulaf32ra.hh_s2", .translate = translate_ae_mulaf32_hh_s2 },
    { .name = "ae_mulaf32ra.lh", .translate = translate_ae_mulaf32_lh },
    { .name = "ae_mulaf32ra.lh_s2", .translate = translate_ae_mulaf32_lh_s2 },
    { .name = "ae_mulaf32ra.ll", .translate = translate_ae_mulaf32_ll },
    { .name = "ae_mulaf32ra.ll_s2", .translate = translate_ae_mulaf32_ll_s2 },
    { .name = "ae_mulaf32s.hh", .translate = translate_ae_mulaf32_hh },
    { .name = "ae_mulaf32s.hh_s2", .translate = translate_ae_mulaf32_hh_s2 },
    { .name = "ae_mulaf32s.lh", .translate = translate_ae_mulaf32_lh },
    { .name = "ae_mulaf32s.lh_s2", .translate = translate_ae_mulaf32_lh_s2 },
    { .name = "ae_mulaf32s.ll", .translate = translate_ae_mulaf32_ll },
    { .name = "ae_mulaf32s.ll_s2", .translate = translate_ae_mulaf32_ll_s2 },
    { .name = "ae_mulaf32x16.h0", .translate = translate_ae_mulaf32x16_h0 },
    { .name = "ae_mulaf32x16.h0_s2", .translate = translate_ae_mulaf32x16_h0_s2 },
    { .name = "ae_mulaf32x16.h1", .translate = translate_ae_mulaf32x16_h1 },
    { .name = "ae_mulaf32x16.h1_s2", .translate = translate_ae_mulaf32x16_h1_s2 },
    { .name = "ae_mulaf32x16.h2", .translate = translate_ae_mulaf32x16_h2 },
    { .name = "ae_mulaf32x16.h2_s2", .translate = translate_ae_mulaf32x16_h2_s2 },
    { .name = "ae_mulaf32x16.h3", .translate = translate_ae_mulaf32x16_h3 },
    { .name = "ae_mulaf32x16.h3_s2", .translate = translate_ae_mulaf32x16_h3_s2 },
    { .name = "ae_mulaf32x16.l0", .translate = translate_ae_mulaf32x16_l0 },
    { .name = "ae_mulaf32x16.l0_s2", .translate = translate_ae_mulaf32x16_l0_s2 },
    { .name = "ae_mulaf32x16.l1", .translate = translate_ae_mulaf32x16_l1 },
    { .name = "ae_mulaf32x16.l1_s2", .translate = translate_ae_mulaf32x16_l1_s2 },
    { .name = "ae_mulaf32x16.l2", .translate = translate_ae_mulaf32x16_l2 },
    { .name = "ae_mulaf32x16.l2_s2", .translate = translate_ae_mulaf32x16_l2_s2 },
    { .name = "ae_mulaf32x16.l3", .translate = translate_ae_mulaf32x16_l3 },
    { .name = "ae_mulaf32x16.l3_s2", .translate = translate_ae_mulaf32x16_l3_s2 },
    { .name = "ae_mulaf48q32sp16s.l", .translate = translate_ae_mulaf48q32sp16s_l },
    { .name = "ae_mulaf48q32sp16s.l_s2", .translate = translate_ae_mulaf48q32sp16s_l_s2 },
    { .name = "ae_mulaf48q32sp16u.l", .translate = translate_ae_mulaf48q32sp16u_l },
    { .name = "ae_mulaf48q32sp16u.l_s2", .translate = translate_ae_mulaf48q32sp16u_l_s2 },
    { .name = "ae_mulafc24ra", .translate = translate_ae_mulafc24ra },
    { .name = "ae_mulafc32ras", .translate = translate_ae_mulafc32ras },
    { .name = "ae_mulafc32x16ras.h", .translate = translate_ae_mulafc32x16ras_h },
    { .name = "ae_mulafc32x16ras.l", .translate = translate_ae_mulafc32x16ras_l },
    { .name = "ae_mulafd32x16x2.fir.hh", .translate = translate_ae_mulafd32x16x2_fir_hh },
    { .name = "ae_mulafd32x16x2.fir.hl", .translate = translate_ae_mulafd32x16x2_fir_hl },
    { .name = "ae_mulafd32x16x2.fir.lh", .translate = translate_ae_mulafd32x16x2_fir_lh },
    { .name = "ae_mulafd32x16x2.fir.ll", .translate = translate_ae_mulafd32x16x2_fir_ll },
    { .name = "ae_mulafd32x2ra.fir.h", .translate = translate_ae_mulafd32x2ra_fir_h },
    { .name = "ae_mulafd32x2ra.fir.l", .translate = translate_ae_mulafd32x2ra_fir_l },
    { .name = "ae_mulafd32x2s.fir.h", .translate = translate_ae_mulafd32x2s_fir_h },
    { .name = "ae_mulafd32x2s.fir.l", .translate = translate_ae_mulafd32x2s_fir_l },
    { .name = "ae_mulafp24x2r", .translate = translate_ae_mulafp24x2r },
    { .name = "ae_mulafp24x2r_s2", .translate = translate_ae_mulafp24x2r_s2 },
    { .name = "ae_mulafp24x2ra", .translate = translate_ae_mulafp24x2ra },
    { .name = "ae_mulafp24x2ra_s2", .translate = translate_ae_mulafp24x2ra_s2 },
    { .name = "ae_mulafp32x16x2ras.h", .translate = translate_ae_mulafp32x16x2ras_h },
    { .name = "ae_mulafp32x16x2ras.h_s2", .translate = translate_ae_mulafp32x16x2ras_h_s2 },
    { .name = "ae_mulafp32x16x2ras.l", .translate = translate_ae_mulafp32x16x2ras_l },
    { .name = "ae_mulafp32x16x2ras.l_s2", .translate = translate_ae_mulafp32x16x2ras_l_s2 },
    { .name = "ae_mulafp32x16x2rs.h", .translate = translate_ae_mulafp32x16x2rs_h },
    { .name = "ae_mulafp32x16x2rs.h_s2", .translate = translate_ae_mulafp32x16x2rs_h_s2 },
    { .name = "ae_mulafp32x16x2rs.l", .translate = translate_ae_mulafp32x16x2rs_l },
    { .name = "ae_mulafp32x16x2rs.l_s2", .translate = translate_ae_mulafp32x16x2rs_l_s2 },
    { .name = "ae_mulafp32x16x2s.h", .translate = translate_ae_mulafp32x16x2s_h },
    { .name = "ae_mulafp32x16x2s.h_s2", .translate = translate_ae_mulafp32x16x2s_h_s2 },
    { .name = "ae_mulafp32x16x2s.l", .translate = translate_ae_mulafp32x16x2s_l },
    { .name = "ae_mulafp32x16x2s.l_s2", .translate = translate_ae_mulafp32x16x2s_l_s2 },
    { .name = "ae_mulafp32x2ras", .translate = translate_ae_mulafp32x2ras },
    { .name = "ae_mulafp32x2ras_s2", .translate = translate_ae_mulafp32x2ras_s2 },
    { .name = "ae_mulafp32x2rs", .translate = translate_ae_mulafp32x2rs },
    { .name = "ae_mulafp32x2rs_s2", .translate = translate_ae_mulafp32x2rs_s2 },
    { .name = "ae_mulap32x16x2.h", .translate = translate_ae_mulap32x16x2_h },
    { .name = "ae_mulap32x16x2.h_s2", .translate = translate_ae_mulap32x16x2_h_s2 },
    { .name = "ae_mulap32x16x2.l", .translate = translate_ae_mulap32x16x2_l },
    { .name = "ae_mulap32x16x2.l_s2", .translate = translate_ae_mulap32x16x2_l_s2 },
    { .name = "ae_mulap32x2", .translate = translate_ae_mulap32x2 },
    { .name = "ae_mulap32x2_s2", .translate = translate_ae_mulap32x2_s2 },
    { .name = "ae_mulaq32sp16s.l", .translate = translate_ae_mulaq32sp16s_l },
    { .name = "ae_mulaq32sp16s.l_s2", .translate = translate_ae_mulaq32sp16s_l_s2 },
    { .name = "ae_mulaq32sp16u.l", .translate = translate_ae_mulaq32sp16u_l },
    { .name = "ae_mulaq32sp16u.l_s2", .translate = translate_ae_mulaq32sp16u_l_s2 },
    { .name = "ae_mulas32f48p16s.hh", .translate = translate_ae_mulas32f48p16s_hh },
    { .name = "ae_mulas32f48p16s.hh_s2", .translate = translate_ae_mulas32f48p16s_hh_s2 },
    { .name = "ae_mulas32f48p16s.lh", .translate = translate_ae_mulas32f48p16s_lh },
    { .name = "ae_mulas32f48p16s.lh_s2", .translate = translate_ae_mulas32f48p16s_lh_s2 },
    { .name = "ae_mulas32f48p16s.ll", .translate = translate_ae_mulas32f48p16s_ll },
    { .name = "ae_mulas32f48p16s.ll_s2", .translate = translate_ae_mulas32f48p16s_ll_s2 },
    { .name = "ae_mulasd32.hh.ll", .translate = translate_ae_mulasd32_hh_ll },
    { .name = "ae_mulasd32.hh.ll_s2", .translate = translate_ae_mulasd32_hh_ll_s2 },
    { .name = "ae_mulasd32.hl.lh", .translate = translate_ae_mulasd32_hl_lh },
    { .name = "ae_mulasd32.hl.lh_s2", .translate = translate_ae_mulasd32_hl_lh_s2 },
    { .name = "ae_mulasd32x16.h1.l0", .translate = translate_ae_mulasd32x16_h1_l0 },
    { .name = "ae_mulasd32x16.h1.l0_s2", .translate = translate_ae_mulasd32x16_h1_l0_s2 },
    { .name = "ae_mulasd32x16.h3.l2", .translate = translate_ae_mulasd32x16_h3_l2 },
    { .name = "ae_mulasd32x16.h3.l2_s2", .translate = translate_ae_mulasd32x16_h3_l2_s2 },
    { .name = "ae_mulasfd32ra.hh.ll", .translate = translate_ae_mulasfd32ra_hh_ll },
    { .name = "ae_mulasfd32ra.hh.ll_s2", .translate = translate_ae_mulasfd32ra_hh_ll_s2 },
    { .name = "ae_mulasfd32ra.hl.lh", .translate = translate_ae_mulasfd32ra_hl_lh },
    { .name = "ae_mulasfd32ra.hl.lh_s2", .translate = translate_ae_mulasfd32ra_hl_lh_s2 },
    { .name = "ae_mulasfd32s.hh.ll", .translate = translate_ae_mulasfd32s_hh_ll },
    { .name = "ae_mulasfd32s.hh.ll_s2", .translate = translate_ae_mulasfd32s_hh_ll_s2 },
    { .name = "ae_mulasfd32s.hl.lh", .translate = translate_ae_mulasfd32s_hl_lh },
    { .name = "ae_mulasfd32s.hl.lh_s2", .translate = translate_ae_mulasfd32s_hl_lh_s2 },
    { .name = "ae_mulasfd32x16.h1.l0", .translate = translate_ae_mulasfd32x16_h1_l0 },
    { .name = "ae_mulasfd32x16.h1.l0_s2", .translate = translate_ae_mulasfd32x16_h1_l0_s2 },
    { .name = "ae_mulasfd32x16.h3.l2", .translate = translate_ae_mulasfd32x16_h3_l2 },
    { .name = "ae_mulasfd32x16.h3.l2_s2", .translate = translate_ae_mulasfd32x16_h3_l2_s2 },
    { .name = "ae_mulc32", .translate = translate_ae_mulc32 },
    { .name = "ae_mulc32x16.h", .translate = translate_ae_mulc32x16_h },
    { .name = "ae_mulc32x16.l", .translate = translate_ae_mulc32x16_l },
    { .name = "ae_mulf16ss.00", .translate = translate_ae_mulf16ss_00 },
    { .name = "ae_mulf16ss.00_s2", .translate = translate_ae_mulf16ss_00_s2 },
    { .name = "ae_mulf16ss.10", .translate = translate_ae_mulf16ss_10 },
    { .name = "ae_mulf16ss.10_s2", .translate = translate_ae_mulf16ss_10_s2 },
    { .name = "ae_mulf16ss.11", .translate = translate_ae_mulf16ss_11 },
    { .name = "ae_mulf16ss.11_s2", .translate = translate_ae_mulf16ss_11_s2 },
    { .name = "ae_mulf16ss.20", .translate = translate_ae_mulf16ss_20 },
    { .name = "ae_mulf16ss.20_s2", .translate = translate_ae_mulf16ss_20_s2 },
    { .name = "ae_mulf16ss.21", .translate = translate_ae_mulf16ss_21 },
    { .name = "ae_mulf16ss.21_s2", .translate = translate_ae_mulf16ss_21_s2 },
    { .name = "ae_mulf16ss.22", .translate = translate_ae_mulf16ss_22 },
    { .name = "ae_mulf16ss.22_s2", .translate = translate_ae_mulf16ss_22_s2 },
    { .name = "ae_mulf16ss.30", .translate = translate_ae_mulf16ss_30 },
    { .name = "ae_mulf16ss.30_s2", .translate = translate_ae_mulf16ss_30_s2 },
    { .name = "ae_mulf16ss.31", .translate = translate_ae_mulf16ss_31 },
    { .name = "ae_mulf16ss.31_s2", .translate = translate_ae_mulf16ss_31_s2 },
    { .name = "ae_mulf16ss.32", .translate = translate_ae_mulf16ss_32 },
    { .name = "ae_mulf16ss.32_s2", .translate = translate_ae_mulf16ss_32_s2 },
    { .name = "ae_mulf16ss.33", .translate = translate_ae_mulf16ss_33 },
    { .name = "ae_mulf16ss.33_s2", .translate = translate_ae_mulf16ss_33_s2 },
    { .name = "ae_mulf16x4ss", .translate = translate_ae_mulf16x4ss },
    { .name = "ae_mulf32r.hh", .translate = translate_ae_mulf32_hh },
    { .name = "ae_mulf32r.hh_s2", .translate = translate_ae_mulf32_hh_s2 },
    { .name = "ae_mulf32r.lh", .translate = translate_ae_mulf32_lh },
    { .name = "ae_mulf32r.lh_s2", .translate = translate_ae_mulf32_lh_s2 },
    { .name = "ae_mulf32r.ll", .translate = translate_ae_mulf32_ll },
    { .name = "ae_mulf32r.ll_s2", .translate = translate_ae_mulf32_ll_s2 },
    { .name = "ae_mulf32ra.hh", .translate = translate_ae_mulf32_hh },
    { .name = "ae_mulf32ra.hh_s2", .translate = translate_ae_mulf32_hh_s2 },
    { .name = "ae_mulf32ra.lh", .translate = translate_ae_mulf32_lh },
    { .name = "ae_mulf32ra.lh_s2", .translate = translate_ae_mulf32_lh_s2 },
    { .name = "ae_mulf32ra.ll", .translate = translate_ae_mulf32_ll },
    { .name = "ae_mulf32ra.ll_s2", .translate = translate_ae_mulf32_ll_s2 },
    { .name = "ae_mulf32s.hh", .translate = translate_ae_mulf32_hh },
    { .name = "ae_mulf32s.hh_s2", .translate = translate_ae_mulf32_hh_s2 },
    { .name = "ae_mulf32s.lh", .translate = translate_ae_mulf32_lh },
    { .name = "ae_mulf32s.lh_s2", .translate = translate_ae_mulf32_lh_s2 },
    { .name = "ae_mulf32s.ll", .translate = translate_ae_mulf32_ll },
    { .name = "ae_mulf32s.ll_s2", .translate = translate_ae_mulf32_ll_s2 },
    { .name = "ae_mulf32x16.h0", .translate = translate_ae_mulf32x16_h0 },
    { .name = "ae_mulf32x16.h0_s2", .translate = translate_ae_mulf32x16_h0_s2 },
    { .name = "ae_mulf32x16.h1", .translate = translate_ae_mulf32x16_h1 },
    { .name = "ae_mulf32x16.h1_s2", .translate = translate_ae_mulf32x16_h1_s2 },
    { .name = "ae_mulf32x16.h2", .translate = translate_ae_mulf32x16_h2 },
    { .name = "ae_mulf32x16.h2_s2", .translate = translate_ae_mulf32x16_h2_s2 },
    { .name = "ae_mulf32x16.h3", .translate = translate_ae_mulf32x16_h3 },
    { .name = "ae_mulf32x16.h3_s2", .translate = translate_ae_mulf32x16_h3_s2 },
    { .name = "ae_mulf32x16.l0", .translate = translate_ae_mulf32x16_l0 },
    { .name = "ae_mulf32x16.l0_s2", .translate = translate_ae_mulf32x16_l0_s2 },
    { .name = "ae_mulf32x16.l1", .translate = translate_ae_mulf32x16_l1 },
    { .name = "ae_mulf32x16.l1_s2", .translate = translate_ae_mulf32x16_l1_s2 },
    { .name = "ae_mulf32x16.l2", .translate = translate_ae_mulf32x16_l2 },
    { .name = "ae_mulf32x16.l2_s2", .translate = translate_ae_mulf32x16_l2_s2 },
    { .name = "ae_mulf32x16.l3", .translate = translate_ae_mulf32x16_l3 },
    { .name = "ae_mulf32x16.l3_s2", .translate = translate_ae_mulf32x16_l3_s2 },
    { .name = "ae_mulf48q32sp16s.l", .translate = translate_ae_mulf48q32sp16s_l },
    { .name = "ae_mulf48q32sp16s.l_s2", .translate = translate_ae_mulf48q32sp16s_l_s2 },
    { .name = "ae_mulf48q32sp16u.l", .translate = translate_ae_mulf48q32sp16u_l },
    { .name = "ae_mulf48q32sp16u.l_s2", .translate = translate_ae_mulf48q32sp16u_l_s2 },
    { .name = "ae_mulfc24ra", .translate = translate_ae_mulfc24ra },
    { .name = "ae_mulfc32ras", .translate = translate_ae_mulfc32ras },
    { .name = "ae_mulfc32x16ras.h", .translate = translate_ae_mulfc32x16ras_h },
    { .name = "ae_mulfc32x16ras.l", .translate = translate_ae_mulfc32x16ras_l },
    { .name = "ae_mulfd32x16x2.fir.hh", .translate = translate_ae_mulfd32x16x2_fir_hh },
    { .name = "ae_mulfd32x16x2.fir.hl", .translate = translate_ae_mulfd32x16x2_fir_hl },
    { .name = "ae_mulfd32x16x2.fir.lh", .translate = translate_ae_mulfd32x16x2_fir_lh },
    { .name = "ae_mulfd32x16x2.fir.ll", .translate = translate_ae_mulfd32x16x2_fir_ll },
    { .name = "ae_mulfd32x2ra.fir.h", .translate = translate_ae_mulfd32x2ra_fir_h },
    { .name = "ae_mulfd32x2ra.fir.l", .translate = translate_ae_mulfd32x2ra_fir_l },
    { .name = "ae_mulfd32x2s.fir.h", .translate = translate_ae_mulfd32x2s_fir_h },
    { .name = "ae_mulfd32x2s.fir.l", .translate = translate_ae_mulfd32x2s_fir_l },
    { .name = "ae_mulfp16x4ras", .translate = translate_ae_mulfp16x4ras },
    { .name = "ae_mulfp16x4s", .translate = translate_ae_mulfp16x4s },
    { .name = "ae_mulfp24x2r", .translate = translate_ae_mulfp24x2r },
    { .name = "ae_mulfp24x2r_s2", .translate = translate_ae_mulfp24x2r_s2 },
    { .name = "ae_mulfp24x2ra", .translate = translate_ae_mulfp24x2ra },
    { .name = "ae_mulfp24x2ra_s2", .translate = translate_ae_mulfp24x2ra_s2 },
    { .name = "ae_mulfp32x16x2ras.h", .translate = translate_ae_mulfp32x16x2ras_h },
    { .name = "ae_mulfp32x16x2ras.h_s2", .translate = translate_ae_mulfp32x16x2ras_h_s2 },
    { .name = "ae_mulfp32x16x2ras.l", .translate = translate_ae_mulfp32x16x2ras_l },
    { .name = "ae_mulfp32x16x2ras.l_s2", .translate = translate_ae_mulfp32x16x2ras_l_s2 },
    { .name = "ae_mulfp32x16x2rs.h", .translate = translate_ae_mulfp32x16x2rs_h },
    { .name = "ae_mulfp32x16x2rs.h_s2", .translate = translate_ae_mulfp32x16x2rs_h_s2 },
    { .name = "ae_mulfp32x16x2rs.l", .translate = translate_ae_mulfp32x16x2rs_l },
    { .name = "ae_mulfp32x16x2rs.l_s2", .translate = translate_ae_mulfp32x16x2rs_l_s2 },
    { .name = "ae_mulfp32x16x2s.h", .translate = translate_ae_mulfp32x16x2s_h },
    { .name = "ae_mulfp32x16x2s.h_s2", .translate = translate_ae_mulfp32x16x2s_h_s2 },
    { .name = "ae_mulfp32x16x2s.l", .translate = translate_ae_mulfp32x16x2s_l },
    { .name = "ae_mulfp32x16x2s.l_s2", .translate = translate_ae_mulfp32x16x2s_l_s2 },
    { .name = "ae_mulfp32x2ras", .translate = translate_ae_mulfp32x2ras },
    { .name = "ae_mulfp32x2ras_s2", .translate = translate_ae_mulfp32x2ras_s2 },
    { .name = "ae_mulfp32x2rs", .translate = translate_ae_mulfp32x2rs },
    { .name = "ae_mulfp32x2rs_s2", .translate = translate_ae_mulfp32x2rs_s2 },
    { .name = "ae_mulp32x16x2.h", .translate = translate_ae_mulp32x16x2_h },
    { .name = "ae_mulp32x16x2.h_s2", .translate = translate_ae_mulp32x16x2_h_s2 },
    { .name = "ae_mulp32x16x2.l", .translate = translate_ae_mulp32x16x2_l },
    { .name = "ae_mulp32x16x2.l_s2", .translate = translate_ae_mulp32x16x2_l_s2 },
    { .name = "ae_mulp32x2", .translate = translate_ae_mulp32x2 },
    { .name = "ae_mulp32x2_s2", .translate = translate_ae_mulp32x2_s2 },
    { .name = "ae_mulpp_32x32x2_1", .translate = translate_ae_mulpp_32x32x2 },
    { .name = "ae_mulpp_32x32x2_2", .translate = translate_ae_mulpp_32x32x2 },
    { .name = "ae_mulq32sp16s.l", .translate = translate_ae_mulq32sp16s_l },
    { .name = "ae_mulq32sp16s.l_s2", .translate = translate_ae_mulq32sp16s_l_s2 },
    { .name = "ae_mulq32sp16u.l", .translate = translate_ae_mulq32sp16u_l },
    { .name = "ae_mulq32sp16u.l_s2", .translate = translate_ae_mulq32sp16u_l_s2 },
    { .name = "ae_muls16x4", .translate = translate_ae_muls16x4 },
    { .name = "ae_muls32.hh", .translate = translate_ae_muls32_hh },
    { .name = "ae_muls32.hh_s2", .translate = translate_ae_muls32_hh_s2 },
    { .name = "ae_muls32.lh", .translate = translate_ae_muls32_lh },
    { .name = "ae_muls32.lh_s2", .translate = translate_ae_muls32_lh_s2 },
    { .name = "ae_muls32.ll", .translate = translate_ae_muls32_ll },
    { .name = "ae_muls32.ll_s2", .translate = translate_ae_muls32_ll_s2 },
    { .name = "ae_muls32ep.hh", .translate = translate_ae_muls32ep_hh },
    { .name = "ae_muls32ep.hh_s2", .translate = translate_ae_muls32ep_hh_s2 },
    { .name = "ae_muls32f48p16s.hh", .translate = translate_ae_muls32f48p16s_hh },
    { .name = "ae_muls32f48p16s.hh_s2", .translate = translate_ae_muls32f48p16s_hh_s2 },
    { .name = "ae_muls32f48p16s.lh", .translate = translate_ae_muls32f48p16s_lh },
    { .name = "ae_muls32f48p16s.lh_s2", .translate = translate_ae_muls32f48p16s_lh_s2 },
    { .name = "ae_muls32f48p16s.ll", .translate = translate_ae_muls32f48p16s_ll },
    { .name = "ae_muls32f48p16s.ll_s2", .translate = translate_ae_muls32f48p16s_ll_s2 },
    { .name = "ae_muls32u.ll", .translate = translate_ae_muls32u_ll },
    { .name = "ae_muls32x16.h0", .translate = translate_ae_muls32x16_h0 },
    { .name = "ae_muls32x16.h0_s2", .translate = translate_ae_muls32x16_h0_s2 },
    { .name = "ae_muls32x16.h1", .translate = translate_ae_muls32x16_h1 },
    { .name = "ae_muls32x16.h1_s2", .translate = translate_ae_muls32x16_h1_s2 },
    { .name = "ae_muls32x16.h2", .translate = translate_ae_muls32x16_h2 },
    { .name = "ae_muls32x16.h2_s2", .translate = translate_ae_muls32x16_h2_s2 },
    { .name = "ae_muls32x16.h3", .translate = translate_ae_muls32x16_h3 },
    { .name = "ae_muls32x16.h3_s2", .translate = translate_ae_muls32x16_h3_s2 },
    { .name = "ae_muls32x16.l0", .translate = translate_ae_muls32x16_l0 },
    { .name = "ae_muls32x16.l0_s2", .translate = translate_ae_muls32x16_l0_s2 },
    { .name = "ae_muls32x16.l1", .translate = translate_ae_muls32x16_l1 },
    { .name = "ae_muls32x16.l1_s2", .translate = translate_ae_muls32x16_l1_s2 },
    { .name = "ae_muls32x16.l2", .translate = translate_ae_muls32x16_l2 },
    { .name = "ae_muls32x16.l2_s2", .translate = translate_ae_muls32x16_l2_s2 },
    { .name = "ae_muls32x16.l3", .translate = translate_ae_muls32x16_l3 },
    { .name = "ae_muls32x16.l3_s2", .translate = translate_ae_muls32x16_l3_s2 },
    { .name = "ae_mulsad32.hh.ll", .translate = translate_ae_mulsad32_hh_ll },
    { .name = "ae_mulsad32.hh.ll_s2", .translate = translate_ae_mulsad32_hh_ll_s2 },
    { .name = "ae_mulsad32x16.h1.l0", .translate = translate_ae_mulsad32x16_h1_l0 },
    { .name = "ae_mulsad32x16.h1.l0_s2", .translate = translate_ae_mulsad32x16_h1_l0_s2 },
    { .name = "ae_mulsad32x16.h3.l2", .translate = translate_ae_mulsad32x16_h3_l2 },
    { .name = "ae_mulsad32x16.h3.l2_s2", .translate = translate_ae_mulsad32x16_h3_l2_s2 },
    { .name = "ae_mulsafd32ra.hh.ll", .translate = translate_ae_mulsafd32ra_hh_ll },
    { .name = "ae_mulsafd32ra.hh.ll_s2", .translate = translate_ae_mulsafd32ra_hh_ll_s2 },
    { .name = "ae_mulsafd32s.hh.ll", .translate = translate_ae_mulsafd32s_hh_ll },
    { .name = "ae_mulsafd32s.hh.ll_s2", .translate = translate_ae_mulsafd32s_hh_ll_s2 },
    { .name = "ae_mulsafd32x16.h1.l0", .translate = translate_ae_mulsafd32x16_h1_l0 },
    { .name = "ae_mulsafd32x16.h1.l0_s2", .translate = translate_ae_mulsafd32x16_h1_l0_s2 },
    { .name = "ae_mulsafd32x16.h3.l2", .translate = translate_ae_mulsafd32x16_h3_l2 },
    { .name = "ae_mulsafd32x16.h3.l2_s2", .translate = translate_ae_mulsafd32x16_h3_l2_s2 },
    { .name = "ae_mulsf16ss.00", .translate = translate_ae_mulsf16ss_00 },
    { .name = "ae_mulsf16ss.00_s2", .translate = translate_ae_mulsf16ss_00_s2 },
    { .name = "ae_mulsf16ss.10", .translate = translate_ae_mulsf16ss_10 },
    { .name = "ae_mulsf16ss.10_s2", .translate = translate_ae_mulsf16ss_10_s2 },
    { .name = "ae_mulsf16ss.11", .translate = translate_ae_mulsf16ss_11 },
    { .name = "ae_mulsf16ss.11_s2", .translate = translate_ae_mulsf16ss_11_s2 },
    { .name = "ae_mulsf16ss.20", .translate = translate_ae_mulsf16ss_20 },
    { .name = "ae_mulsf16ss.20_s2", .translate = translate_ae_mulsf16ss_20_s2 },
    { .name = "ae_mulsf16ss.21", .translate = translate_ae_mulsf16ss_21 },
    { .name = "ae_mulsf16ss.21_s2", .translate = translate_ae_mulsf16ss_21_s2 },
    { .name = "ae_mulsf16ss.22", .translate = translate_ae_mulsf16ss_22 },
    { .name = "ae_mulsf16ss.22_s2", .translate = translate_ae_mulsf16ss_22_s2 },
    { .name = "ae_mulsf16ss.30", .translate = translate_ae_mulsf16ss_30 },
    { .name = "ae_mulsf16ss.30_s2", .translate = translate_ae_mulsf16ss_30_s2 },
    { .name = "ae_mulsf16ss.31", .translate = translate_ae_mulsf16ss_31 },
    { .name = "ae_mulsf16ss.31_s2", .translate = translate_ae_mulsf16ss_31_s2 },
    { .name = "ae_mulsf16ss.32", .translate = translate_ae_mulsf16ss_32 },
    { .name = "ae_mulsf16ss.32_s2", .translate = translate_ae_mulsf16ss_32_s2 },
    { .name = "ae_mulsf16ss.33", .translate = translate_ae_mulsf16ss_33 },
    { .name = "ae_mulsf16ss.33_s2", .translate = translate_ae_mulsf16ss_33_s2 },
    { .name = "ae_mulsf16x4ss", .translate = translate_ae_mulsf16x4ss },
    { .name = "ae_mulsf32r.hh", .translate = translate_ae_mulsf32_hh },
    { .name = "ae_mulsf32r.hh_s2", .translate = translate_ae_mulsf32_hh_s2 },
    { .name = "ae_mulsf32r.lh", .translate = translate_ae_mulsf32_lh },
    { .name = "ae_mulsf32r.lh_s2", .translate = translate_ae_mulsf32_lh_s2 },
    { .name = "ae_mulsf32r.ll", .translate = translate_ae_mulsf32_ll },
    { .name = "ae_mulsf32r.ll_s2", .translate = translate_ae_mulsf32_ll_s2 },
    { .name = "ae_mulsf32ra.hh", .translate = translate_ae_mulsf32_hh },
    { .name = "ae_mulsf32ra.hh_s2", .translate = translate_ae_mulsf32_hh_s2 },
    { .name = "ae_mulsf32ra.lh", .translate = translate_ae_mulsf32_lh },
    { .name = "ae_mulsf32ra.lh_s2", .translate = translate_ae_mulsf32_lh_s2 },
    { .name = "ae_mulsf32ra.ll", .translate = translate_ae_mulsf32_ll },
    { .name = "ae_mulsf32ra.ll_s2", .translate = translate_ae_mulsf32_ll_s2 },
    { .name = "ae_mulsf32s.hh", .translate = translate_ae_mulsf32_hh },
    { .name = "ae_mulsf32s.hh_s2", .translate = translate_ae_mulsf32_hh_s2 },
    { .name = "ae_mulsf32s.lh", .translate = translate_ae_mulsf32_lh },
    { .name = "ae_mulsf32s.lh_s2", .translate = translate_ae_mulsf32_lh_s2 },
    { .name = "ae_mulsf32s.ll", .translate = translate_ae_mulsf32_ll },
    { .name = "ae_mulsf32s.ll_s2", .translate = translate_ae_mulsf32_ll_s2 },
    { .name = "ae_mulsf32x16.h0", .translate = translate_ae_mulsf32x16_h0 },
    { .name = "ae_mulsf32x16.h0_s2", .translate = translate_ae_mulsf32x16_h0_s2 },
    { .name = "ae_mulsf32x16.h1", .translate = translate_ae_mulsf32x16_h1 },
    { .name = "ae_mulsf32x16.h1_s2", .translate = translate_ae_mulsf32x16_h1_s2 },
    { .name = "ae_mulsf32x16.h2", .translate = translate_ae_mulsf32x16_h2 },
    { .name = "ae_mulsf32x16.h2_s2", .translate = translate_ae_mulsf32x16_h2_s2 },
    { .name = "ae_mulsf32x16.h3", .translate = translate_ae_mulsf32x16_h3 },
    { .name = "ae_mulsf32x16.h3_s2", .translate = translate_ae_mulsf32x16_h3_s2 },
    { .name = "ae_mulsf32x16.l0", .translate = translate_ae_mulsf32x16_l0 },
    { .name = "ae_mulsf32x16.l0_s2", .translate = translate_ae_mulsf32x16_l0_s2 },
    { .name = "ae_mulsf32x16.l1", .translate = translate_ae_mulsf32x16_l1 },
    { .name = "ae_mulsf32x16.l1_s2", .translate = translate_ae_mulsf32x16_l1_s2 },
    { .name = "ae_mulsf32x16.l2", .translate = translate_ae_mulsf32x16_l2 },
    { .name = "ae_mulsf32x16.l2_s2", .translate = translate_ae_mulsf32x16_l2_s2 },
    { .name = "ae_mulsf32x16.l3", .translate = translate_ae_mulsf32x16_l3 },
    { .name = "ae_mulsf32x16.l3_s2", .translate = translate_ae_mulsf32x16_l3_s2 },
    { .name = "ae_mulsf48q32sp16s.l", .translate = translate_ae_mulsf48q32sp16s_l },
    { .name = "ae_mulsf48q32sp16s.l_s2", .translate = translate_ae_mulsf48q32sp16s_l_s2 },
    { .name = "ae_mulsf48q32sp16u.l", .translate = translate_ae_mulsf48q32sp16u_l },
    { .name = "ae_mulsf48q32sp16u.l_s2", .translate = translate_ae_mulsf48q32sp16u_l_s2 },
    { .name = "ae_mulsfp24x2r", .translate = translate_ae_mulsfp24x2r },
    { .name = "ae_mulsfp24x2r_s2", .translate = translate_ae_mulsfp24x2r_s2 },
    { .name = "ae_mulsfp24x2ra", .translate = translate_ae_mulsfp24x2ra },
    { .name = "ae_mulsfp24x2ra_s2", .translate = translate_ae_mulsfp24x2ra_s2 },
    { .name = "ae_mulsfp32x16x2ras.h", .translate = translate_ae_mulsfp32x16x2ras_h },
    { .name = "ae_mulsfp32x16x2ras.h_s2", .translate = translate_ae_mulsfp32x16x2ras_h_s2 },
    { .name = "ae_mulsfp32x16x2ras.l", .translate = translate_ae_mulsfp32x16x2ras_l },
    { .name = "ae_mulsfp32x16x2ras.l_s2", .translate = translate_ae_mulsfp32x16x2ras_l_s2 },
    { .name = "ae_mulsfp32x16x2rs.h", .translate = translate_ae_mulsfp32x16x2rs_h },
    { .name = "ae_mulsfp32x16x2rs.h_s2", .translate = translate_ae_mulsfp32x16x2rs_h_s2 },
    { .name = "ae_mulsfp32x16x2rs.l", .translate = translate_ae_mulsfp32x16x2rs_l },
    { .name = "ae_mulsfp32x16x2rs.l_s2", .translate = translate_ae_mulsfp32x16x2rs_l_s2 },
    { .name = "ae_mulsfp32x16x2s.h", .translate = translate_ae_mulsfp32x16x2s_h },
    { .name = "ae_mulsfp32x16x2s.h_s2", .translate = translate_ae_mulsfp32x16x2s_h_s2 },
    { .name = "ae_mulsfp32x16x2s.l", .translate = translate_ae_mulsfp32x16x2s_l },
    { .name = "ae_mulsfp32x16x2s.l_s2", .translate = translate_ae_mulsfp32x16x2s_l_s2 },
    { .name = "ae_mulsfp32x2ras", .translate = translate_ae_mulsfp32x2ras },
    { .name = "ae_mulsfp32x2ras_s2", .translate = translate_ae_mulsfp32x2ras_s2 },
    { .name = "ae_mulsfp32x2rs", .translate = translate_ae_mulsfp32x2rs },
    { .name = "ae_mulsfp32x2rs_s2", .translate = translate_ae_mulsfp32x2rs_s2 },
    { .name = "ae_mulsp32x16x2.h", .translate = translate_ae_mulsp32x16x2_h },
    { .name = "ae_mulsp32x16x2.h_s2", .translate = translate_ae_mulsp32x16x2_h_s2 },
    { .name = "ae_mulsp32x16x2.l", .translate = translate_ae_mulsp32x16x2_l },
    { .name = "ae_mulsp32x16x2.l_s2", .translate = translate_ae_mulsp32x16x2_l_s2 },
    { .name = "ae_mulsp32x2", .translate = translate_ae_mulsp32x2 },
    { .name = "ae_mulsp32x2_s2", .translate = translate_ae_mulsp32x2_s2 },
    { .name = "ae_mulsq32sp16s.l", .translate = translate_ae_mulsq32sp16s_l },
    { .name = "ae_mulsq32sp16s.l_s2", .translate = translate_ae_mulsq32sp16s_l_s2 },
    { .name = "ae_mulsq32sp16u.l", .translate = translate_ae_mulsq32sp16u_l },
    { .name = "ae_mulsq32sp16u.l_s2", .translate = translate_ae_mulsq32sp16u_l_s2 },
    { .name = "ae_mulss32f48p16s.hh", .translate = translate_ae_mulss32f48p16s_hh },
    { .name = "ae_mulss32f48p16s.hh_s2", .translate = translate_ae_mulss32f48p16s_hh_s2 },
    { .name = "ae_mulss32f48p16s.lh", .translate = translate_ae_mulss32f48p16s_lh },
    { .name = "ae_mulss32f48p16s.lh_s2", .translate = translate_ae_mulss32f48p16s_lh_s2 },
    { .name = "ae_mulss32f48p16s.ll", .translate = translate_ae_mulss32f48p16s_ll },
    { .name = "ae_mulss32f48p16s.ll_s2", .translate = translate_ae_mulss32f48p16s_ll_s2 },
    { .name = "ae_mulssd32.hh.ll", .translate = translate_ae_mulssd32_hh_ll },
    { .name = "ae_mulssd32.hh.ll_s2", .translate = translate_ae_mulssd32_hh_ll_s2 },
    { .name = "ae_mulssd32.hl.lh", .translate = translate_ae_mulssd32_hl_lh },
    { .name = "ae_mulssd32.hl.lh_s2", .translate = translate_ae_mulssd32_hl_lh_s2 },
    { .name = "ae_mulssd32ep.hh.ll", .translate = translate_ae_mulssd32ep_hh_ll },
    { .name = "ae_mulssd32ep.hh.ll_s2", .translate = translate_ae_mulssd32ep_hh_ll_s2 },
    { .name = "ae_mulssd32x16.h1.l0", .translate = translate_ae_mulssd32x16_h1_l0 },
    { .name = "ae_mulssd32x16.h1.l0_s2", .translate = translate_ae_mulssd32x16_h1_l0_s2 },
    { .name = "ae_mulssd32x16.h3.l2", .translate = translate_ae_mulssd32x16_h3_l2 },
    { .name = "ae_mulssd32x16.h3.l2_s2", .translate = translate_ae_mulssd32x16_h3_l2_s2 },
    { .name = "ae_mulssfd16ss.11_00", .translate = translate_ae_mulssfd16ss_11_00 },
    { .name = "ae_mulssfd16ss.11_00_s2", .translate = translate_ae_mulssfd16ss_11_00_s2 },
    { .name = "ae_mulssfd16ss.13_02", .translate = translate_ae_mulssfd16ss_13_02 },
    { .name = "ae_mulssfd16ss.13_02_s2", .translate = translate_ae_mulssfd16ss_13_02_s2 },
    { .name = "ae_mulssfd16ss.33_22", .translate = translate_ae_mulssfd16ss_33_22 },
    { .name = "ae_mulssfd16ss.33_22_s2", .translate = translate_ae_mulssfd16ss_33_22_s2 },
    { .name = "ae_mulssfd32ra.hh.ll", .translate = translate_ae_mulssfd32ra_hh_ll },
    { .name = "ae_mulssfd32ra.hh.ll_s2", .translate = translate_ae_mulssfd32ra_hh_ll_s2 },
    { .name = "ae_mulssfd32ra.hl.lh", .translate = translate_ae_mulssfd32ra_hl_lh },
    { .name = "ae_mulssfd32ra.hl.lh_s2", .translate = translate_ae_mulssfd32ra_hl_lh_s2 },
    { .name = "ae_mulssfd32s.hh.ll", .translate = translate_ae_mulssfd32s_hh_ll },
    { .name = "ae_mulssfd32s.hh.ll_s2", .translate = translate_ae_mulssfd32s_hh_ll_s2 },
    { .name = "ae_mulssfd32s.hl.lh", .translate = translate_ae_mulssfd32s_hl_lh },
    { .name = "ae_mulssfd32s.hl.lh_s2", .translate = translate_ae_mulssfd32s_hl_lh_s2 },
    { .name = "ae_mulssfd32x16.h1.l0", .translate = translate_ae_mulssfd32x16_h1_l0 },
    { .name = "ae_mulssfd32x16.h1.l0_s2", .translate = translate_ae_mulssfd32x16_h1_l0_s2 },
    { .name = "ae_mulssfd32x16.h3.l2", .translate = translate_ae_mulssfd32x16_h3_l2 },
    { .name = "ae_mulssfd32x16.h3.l2_s2", .translate = translate_ae_mulssfd32x16_h3_l2_s2 },
    { .name = "ae_mulzaaaafq32x16", .translate = translate_ae_mulzaaaafq32x16 },
    { .name = "ae_mulzaaaafq32x16_s2", .translate = translate_ae_mulzaaaafq32x16_s2 },
    { .name = "ae_mulzaaaaq16", .translate = translate_ae_mulzaaaaq16 },
    { .name = "ae_mulzaaaaq16_s2", .translate = translate_ae_mulzaaaaq16_s2 },
    { .name = "ae_mulzaaaaq32x16", .translate = translate_ae_mulzaaaaq32x16 },
    { .name = "ae_mulzaaaaq32x16_s2", .translate = translate_ae_mulzaaaaq32x16_s2 },
    { .name = "ae_mulzaad32.hh.ll", .translate = translate_ae_mulzaad32_hh_ll },
    { .name = "ae_mulzaad32.hh.ll_s2", .translate = translate_ae_mulzaad32_hh_ll_s2 },
    { .name = "ae_mulzaad32.hl.lh", .translate = translate_ae_mulzaad32_hl_lh },
    { .name = "ae_mulzaad32.hl.lh_s2", .translate = translate_ae_mulzaad32_hl_lh_s2 },
    { .name = "ae_mulzaad32ep.hh.ll", .translate = translate_ae_mulzaad32ep_hh_ll },
    { .name = "ae_mulzaad32ep.hh.ll_s2", .translate = translate_ae_mulzaad32ep_hh_ll_s2 },
    { .name = "ae_mulzaad32usep.hl.lh", .translate = translate_ae_mulzaad32usep_hl_lh },
    { .name = "ae_mulzaad32usep.hl.lh_s2", .translate = translate_ae_mulzaad32usep_hl_lh_s2 },
    { .name = "ae_mulzaad32x16.h0.l1", .translate = translate_ae_mulzaad32x16_h0_l1 },
    { .name = "ae_mulzaad32x16.h0.l1_s2", .translate = translate_ae_mulzaad32x16_h0_l1_s2 },
    { .name = "ae_mulzaad32x16.h1.l0", .translate = translate_ae_mulzaad32x16_h1_l0 },
    { .name = "ae_mulzaad32x16.h1.l0_s2", .translate = translate_ae_mulzaad32x16_h1_l0_s2 },
    { .name = "ae_mulzaad32x16.h2.l3", .translate = translate_ae_mulzaad32x16_h2_l3 },
    { .name = "ae_mulzaad32x16.h2.l3_s2", .translate = translate_ae_mulzaad32x16_h2_l3_s2 },
    { .name = "ae_mulzaad32x16.h3.l2", .translate = translate_ae_mulzaad32x16_h3_l2 },
    { .name = "ae_mulzaad32x16.h3.l2_s2", .translate = translate_ae_mulzaad32x16_h3_l2_s2 },
    { .name = "ae_mulzaafd16ss.11_00", .translate = translate_ae_mulzaafd16ss_11_00 },
    { .name = "ae_mulzaafd16ss.11_00_s2", .translate = translate_ae_mulzaafd16ss_11_00_s2 },
    { .name = "ae_mulzaafd16ss.13_02", .translate = translate_ae_mulzaafd16ss_13_02 },
    { .name = "ae_mulzaafd16ss.13_02_s2", .translate = translate_ae_mulzaafd16ss_13_02_s2 },
    { .name = "ae_mulzaafd16ss.33_22", .translate = translate_ae_mulzaafd16ss_33_22 },
    { .name = "ae_mulzaafd16ss.33_22_s2", .translate = translate_ae_mulzaafd16ss_33_22_s2 },
    { .name = "ae_mulzaafd32ra.hh.ll", .translate = translate_ae_mulzaafd32ra_hh_ll },
    { .name = "ae_mulzaafd32ra.hh.ll_s2", .translate = translate_ae_mulzaafd32ra_hh_ll_s2 },
    { .name = "ae_mulzaafd32ra.hl.lh", .translate = translate_ae_mulzaafd32ra_hl_lh },
    { .name = "ae_mulzaafd32ra.hl.lh_s2", .translate = translate_ae_mulzaafd32ra_hl_lh_s2 },
    { .name = "ae_mulzaafd32s.hh.ll", .translate = translate_ae_mulzaafd32s_hh_ll },
    { .name = "ae_mulzaafd32s.hh.ll_s2", .translate = translate_ae_mulzaafd32s_hh_ll_s2 },
    { .name = "ae_mulzaafd32s.hl.lh", .translate = translate_ae_mulzaafd32s_hl_lh },
    { .name = "ae_mulzaafd32s.hl.lh_s2", .translate = translate_ae_mulzaafd32s_hl_lh_s2 },
    { .name = "ae_mulzaafd32x16.h0.l1", .translate = translate_ae_mulzaafd32x16_h0_l1 },
    { .name = "ae_mulzaafd32x16.h0.l1_s2", .translate = translate_ae_mulzaafd32x16_h0_l1_s2 },
    { .name = "ae_mulzaafd32x16.h1.l0", .translate = translate_ae_mulzaafd32x16_h1_l0 },
    { .name = "ae_mulzaafd32x16.h1.l0_s2", .translate = translate_ae_mulzaafd32x16_h1_l0_s2 },
    { .name = "ae_mulzaafd32x16.h2.l3", .translate = translate_ae_mulzaafd32x16_h2_l3 },
    { .name = "ae_mulzaafd32x16.h2.l3_s2", .translate = translate_ae_mulzaafd32x16_h2_l3_s2 },
    { .name = "ae_mulzaafd32x16.h3.l2", .translate = translate_ae_mulzaafd32x16_h3_l2 },
    { .name = "ae_mulzaafd32x16.h3.l2_s2", .translate = translate_ae_mulzaafd32x16_h3_l2_s2 },
    { .name = "ae_mulzasd32.hh.ll", .translate = translate_ae_mulzasd32_hh_ll },
    { .name = "ae_mulzasd32.hh.ll_s2", .translate = translate_ae_mulzasd32_hh_ll_s2 },
    { .name = "ae_mulzasd32.hl.lh", .translate = translate_ae_mulzasd32_hl_lh },
    { .name = "ae_mulzasd32.hl.lh_s2", .translate = translate_ae_mulzasd32_hl_lh_s2 },
    { .name = "ae_mulzasd32x16.h1.l0", .translate = translate_ae_mulzasd32x16_h1_l0 },
    { .name = "ae_mulzasd32x16.h1.l0_s2", .translate = translate_ae_mulzasd32x16_h1_l0_s2 },
    { .name = "ae_mulzasd32x16.h3.l2", .translate = translate_ae_mulzasd32x16_h3_l2 },
    { .name = "ae_mulzasd32x16.h3.l2_s2", .translate = translate_ae_mulzasd32x16_h3_l2_s2 },
    { .name = "ae_mulzasfd32ra.hh.ll", .translate = translate_ae_mulzasfd32ra_hh_ll },
    { .name = "ae_mulzasfd32ra.hh.ll_s2", .translate = translate_ae_mulzasfd32ra_hh_ll_s2 },
    { .name = "ae_mulzasfd32ra.hl.lh", .translate = translate_ae_mulzasfd32ra_hl_lh },
    { .name = "ae_mulzasfd32ra.hl.lh_s2", .translate = translate_ae_mulzasfd32ra_hl_lh_s2 },
    { .name = "ae_mulzasfd32s.hh.ll", .translate = translate_ae_mulzasfd32s_hh_ll },
    { .name = "ae_mulzasfd32s.hh.ll_s2", .translate = translate_ae_mulzasfd32s_hh_ll_s2 },
    { .name = "ae_mulzasfd32s.hl.lh", .translate = translate_ae_mulzasfd32s_hl_lh },
    { .name = "ae_mulzasfd32s.hl.lh_s2", .translate = translate_ae_mulzasfd32s_hl_lh_s2 },
    { .name = "ae_mulzasfd32x16.h1.l0", .translate = translate_ae_mulzasfd32x16_h1_l0 },
    { .name = "ae_mulzasfd32x16.h1.l0_s2", .translate = translate_ae_mulzasfd32x16_h1_l0_s2 },
    { .name = "ae_mulzasfd32x16.h3.l2", .translate = translate_ae_mulzasfd32x16_h3_l2 },
    { .name = "ae_mulzasfd32x16.h3.l2_s2", .translate = translate_ae_mulzasfd32x16_h3_l2_s2 },
    { .name = "ae_mulzsad32.hh.ll", .translate = translate_ae_mulzsad32_hh_ll },
    { .name = "ae_mulzsad32.hh.ll_s2", .translate = translate_ae_mulzsad32_hh_ll_s2 },
    { .name = "ae_mulzsad32x16.h1.l0", .translate = translate_ae_mulzsad32x16_h1_l0 },
    { .name = "ae_mulzsad32x16.h1.l0_s2", .translate = translate_ae_mulzsad32x16_h1_l0_s2 },
    { .name = "ae_mulzsad32x16.h3.l2", .translate = translate_ae_mulzsad32x16_h3_l2 },
    { .name = "ae_mulzsad32x16.h3.l2_s2", .translate = translate_ae_mulzsad32x16_h3_l2_s2 },
    { .name = "ae_mulzsafd32ra.hh.ll", .translate = translate_ae_mulzsafd32ra_hh_ll },
    { .name = "ae_mulzsafd32ra.hh.ll_s2", .translate = translate_ae_mulzsafd32ra_hh_ll_s2 },
    { .name = "ae_mulzsafd32s.hh.ll", .translate = translate_ae_mulzsafd32s_hh_ll },
    { .name = "ae_mulzsafd32s.hh.ll_s2", .translate = translate_ae_mulzsafd32s_hh_ll_s2 },
    { .name = "ae_mulzsafd32x16.h1.l0", .translate = translate_ae_mulzsafd32x16_h1_l0 },
    { .name = "ae_mulzsafd32x16.h1.l0_s2", .translate = translate_ae_mulzsafd32x16_h1_l0_s2 },
    { .name = "ae_mulzsafd32x16.h3.l2", .translate = translate_ae_mulzsafd32x16_h3_l2 },
    { .name = "ae_mulzsafd32x16.h3.l2_s2", .translate = translate_ae_mulzsafd32x16_h3_l2_s2 },
    { .name = "ae_mulzssd32.hh.ll", .translate = translate_ae_mulzssd32_hh_ll },
    { .name = "ae_mulzssd32.hh.ll_s2", .translate = translate_ae_mulzssd32_hh_ll_s2 },
    { .name = "ae_mulzssd32.hl.lh", .translate = translate_ae_mulzssd32_hl_lh },
    { .name = "ae_mulzssd32.hl.lh_s2", .translate = translate_ae_mulzssd32_hl_lh_s2 },
    { .name = "ae_mulzssd32ep.hh.ll", .translate = translate_ae_mulzssd32ep_hh_ll },
    { .name = "ae_mulzssd32ep.hh.ll_s2", .translate = translate_ae_mulzssd32ep_hh_ll_s2 },
    { .name = "ae_mulzssd32x16.h1.l0", .translate = translate_ae_mulzssd32x16_h1_l0 },
    { .name = "ae_mulzssd32x16.h1.l0_s2", .translate = translate_ae_mulzssd32x16_h1_l0_s2 },
    { .name = "ae_mulzssd32x16.h3.l2", .translate = translate_ae_mulzssd32x16_h3_l2 },
    { .name = "ae_mulzssd32x16.h3.l2_s2", .translate = translate_ae_mulzssd32x16_h3_l2_s2 },
    { .name = "ae_mulzssfd16ss.11_00", .translate = translate_ae_mulzssfd16ss_11_00 },
    { .name = "ae_mulzssfd16ss.11_00_s2", .translate = translate_ae_mulzssfd16ss_11_00_s2 },
    { .name = "ae_mulzssfd16ss.13_02", .translate = translate_ae_mulzssfd16ss_13_02 },
    { .name = "ae_mulzssfd16ss.13_02_s2", .translate = translate_ae_mulzssfd16ss_13_02_s2 },
    { .name = "ae_mulzssfd16ss.33_22", .translate = translate_ae_mulzssfd16ss_33_22 },
    { .name = "ae_mulzssfd16ss.33_22_s2", .translate = translate_ae_mulzssfd16ss_33_22_s2 },
    { .name = "ae_mulzssfd32ra.hh.ll", .translate = translate_ae_mulzssfd32ra_hh_ll },
    { .name = "ae_mulzssfd32ra.hh.ll_s2", .translate = translate_ae_mulzssfd32ra_hh_ll_s2 },
    { .name = "ae_mulzssfd32ra.hl.lh", .translate = translate_ae_mulzssfd32ra_hl_lh },
    { .name = "ae_mulzssfd32ra.hl.lh_s2", .translate = translate_ae_mulzssfd32ra_hl_lh_s2 },
    { .name = "ae_mulzssfd32s.hh.ll", .translate = translate_ae_mulzssfd32s_hh_ll },
    { .name = "ae_mulzssfd32s.hh.ll_s2", .translate = translate_ae_mulzssfd32s_hh_ll_s2 },
    { .name = "ae_mulzssfd32s.hl.lh", .translate = translate_ae_mulzssfd32s_hl_lh },
    { .name = "ae_mulzssfd32s.hl.lh_s2", .translate = translate_ae_mulzssfd32s_hl_lh_s2 },
    { .name = "ae_mulzssfd32x16.h1.l0", .translate = translate_ae_mulzssfd32x16_h1_l0 },
    { .name = "ae_mulzssfd32x16.h1.l0_s2", .translate = translate_ae_mulzssfd32x16_h1_l0_s2 },
    { .name = "ae_mulzssfd32x16.h3.l2", .translate = translate_ae_mulzssfd32x16_h3_l2 },
    { .name = "ae_mulzssfd32x16.h3.l2_s2", .translate = translate_ae_mulzssfd32x16_h3_l2_s2 },
    { .name = "ae_neg16s", .translate = translate_ae_neg16s },
    { .name = "ae_neg24s", .translate = translate_ae_neg24s },
    { .name = "ae_neg32",  .translate = translate_ae_neg32  },
    { .name = "ae_neg32s", .translate = translate_ae_neg32s },
    { .name = "ae_neg64s", .translate = translate_ae_neg64s },
    { .name = "ae_negsq56s", .translate = translate_ae_negsq56s },
    { .name = "ae_nsa64", .translate = translate_ae_nsa64 },
    { .name = "ae_nsaz16.0", .translate = translate_ae_nsaz16_0 },
    { .name = "ae_nsaz32.l", .translate = translate_ae_nsaz32l },
    { .name = "ae_ohba", .translate = translate_ae_decode_field_noop },
    { .name = "ae_ohba2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_opnd_tp7", .translate = translate_ae_decode_field_noop },
    { .name = "ae_osa16", .translate = translate_ae_decode_field_noop },
    { .name = "ae_osa32", .translate = translate_ae_decode_field_noop },
    { .name = "ae_osa64", .translate = translate_ae_decode_field_noop },
    { .name = "ae_pks_d", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_pks_s", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_pksr24", .translate = translate_ae_pksr24 },
    { .name = "ae_pksr32", .translate = translate_ae_pksr32 },
    { .name = "ae_pksrf32", .translate = translate_ae_pksrf32 },
    { .name = "ae_rng32x2", .translate = translate_ae_rng32x2 },
    { .name = "ae_round16x4f32sasym",  .translate = translate_ae_round16x4f32 },
    { .name = "ae_round16x4f32ssym",   .translate = translate_ae_round16x4f32 },
    { .name = "ae_round24x2f48sasym",  .translate = translate_ae_round24x2f48 },
    { .name = "ae_round24x2f48ssym",   .translate = translate_ae_round24x2f48 },
    { .name = "ae_round32x2f48sasym",  .translate = translate_ae_round32x2f48 },
    { .name = "ae_round32x2f48ssym",   .translate = translate_ae_round32x2f48 },
    { .name = "ae_round32x2f64sasym",  .translate = translate_ae_round32x2f64 },
    { .name = "ae_round32x2f64ssym",   .translate = translate_ae_round32x2f64 },
    { .name = "ae_roundsp16f24asym",   .translate = translate_ae_roundsp16f24 },
    { .name = "ae_roundsp16f24sym",    .translate = translate_ae_roundsp16f24 },
    { .name = "ae_roundsp16q48x2asym", .translate = translate_ae_roundsp16q48x2 },
    { .name = "ae_roundsp16q48x2sym",  .translate = translate_ae_roundsp16q48x2 },
    { .name = "ae_roundsq32f48asym",   .translate = translate_ae_roundsq32f48 },
    { .name = "ae_roundsq32f48sym",    .translate = translate_ae_roundsq32f48 },
    { .name = "ae_s24ra64s.i", .translate = translate_ae_s24ra64s_i },
    { .name = "ae_s24ra64s.ip", .translate = translate_ae_s24ra64s_ip },
    { .name = "ae_s24ra64s.x", .translate = translate_ae_s24ra64s_x },
    { .name = "ae_s24ra64s.xc", .translate = translate_ae_s24ra64s_x },
    { .name = "ae_s24ra64s.xc1", .translate = translate_ae_s24ra64s_x },
    { .name = "ae_s24ra64s.xp", .translate = translate_ae_s24ra64s_xp },
    { .name = "ae_s24x2ra64s.ip", .translate = translate_ae_s24x2ra64s_ip },
    { .name = "ae_s32x2ra64s.ip", .translate = translate_ae_s32x2ra64s_ip },
    { .name = "ae_sat16x4", .translate = translate_ae_sat16x4 },
    { .name = "ae_sat24s",  .translate = translate_ae_sat24s  },
    { .name = "ae_sat48s",  .translate = translate_ae_sat48s  },
    { .name = "ae_sat64s",  .translate = translate_ae_sat64s  },
    { .name = "ae_satq56s", .translate = translate_ae_satq56s },
    /* ae_sb family temporarily disabled: infrastructure issue in HiFi slot encoding (ae3_slot0)
       even NOP translator hangs due to operand extraction failure. Tracked as separate issue.
       ae_sbi and ae_sbf work fine. */
    /* TODO: Fix root cause in operand extraction for multi-operand HiFi instructions */
    /*
    { .name = "ae_sb", .translate = translate_ae_sb,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sb.ic", .translate = translate_ae_sb,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sb.ic1", .translate = translate_ae_sb,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sb.ip", .translate = translate_ae_sb,
      .par = (const uint32_t[]){1} },
    */
    { .name = "ae_sbf", .translate = translate_ae_sbf,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sbf.ic", .translate = translate_ae_sbf,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sbf.ic1", .translate = translate_ae_sbf,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sbf.ip", .translate = translate_ae_sbf,
      .par = (const uint32_t[]){1} },
    { .name = "ae_sbi", .translate = translate_ae_sbi,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sbi.ic", .translate = translate_ae_sbi,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sbi.ic1", .translate = translate_ae_sbi,
      .par = (const uint32_t[]){0} },
    { .name = "ae_sbi.ip", .translate = translate_ae_sbi,
      .par = (const uint32_t[]){1} },
    { .name = "ae_sel16i",   .translate = translate_ae_sel16i },
    { .name = "ae_sel16i.n", .translate = translate_ae_sel16i_n },
    { .name = "ae_selimm", .translate = translate_ae_selimm },
    { .name = "ae_selimm.N", .translate = translate_ae_selimm },
    { .name = "ae_sext32x2d16.10", .translate = translate_ae_cvt32x2f16_10 },
    { .name = "ae_sext32x2d16.32", .translate = translate_ae_cvt32x2f16_32 },
    { .name = "ae_sext72",         .translate = translate_ae_sext72 },
    { .name = "ae_sha32", .translate = translate_ae_sha32 },
    { .name = "ae_shift32x4", .translate = translate_ae_shift32x4 },
    { .name = "ae_shift32x5", .translate = translate_ae_shift32x5 },
    { .name = "ae_shift_d", .translate = translate_ae_shift_d },
    { .name = "ae_shift_d0", .translate = translate_ae_shift_d0 },
    { .name = "ae_shift_sd", .translate = translate_ae_shift_sd },
    { .name = "ae_shortswap", .translate = translate_ae_shortswap },
    { .name = "ae_slaa16s", .translate = translate_ae_slaa16s },
    { .name = "ae_slaa32", .translate = translate_ae_slaa32 },
    { .name = "ae_slaa32s", .translate = translate_ae_slaa32s },
    { .name = "ae_slaa64s", .translate = translate_ae_slaa64s },
    { .name = "ae_slaaq56", .translate = translate_ae_slaaq56 },
    { .name = "ae_slaasq56s", .translate = translate_ae_slaasq56s },
    { .name = "ae_slai16s", .translate = translate_ae_slai16s },
    { .name = "ae_slai24", .translate = translate_ae_slai24 },
    { .name = "ae_slai24s", .translate = translate_ae_slai24s },
    { .name = "ae_slai32", .translate = translate_ae_slai32 },
    { .name = "ae_slai32s", .translate = translate_ae_slai32s },
    { .name = "ae_slai64s", .translate = translate_ae_slai64s },
    { .name = "ae_slai72", .translate = translate_ae_slai72 },
    { .name = "ae_slaisq56s", .translate = translate_ae_slaisq56s },
    { .name = "ae_slas24", .translate = translate_ae_slas24 },
    { .name = "ae_slas24s", .translate = translate_ae_slas24s },
    { .name = "ae_slas32", .translate = translate_ae_slas32 },
    { .name = "ae_slas32s", .translate = translate_ae_slas32s },
    { .name = "ae_slas64", .translate = translate_ae_slas64 },
    { .name = "ae_slas64s", .translate = translate_ae_slas64s },
    { .name = "ae_slasq56", .translate = translate_ae_slasq56 },
    { .name = "ae_slassq56s", .translate = translate_ae_slassq56s },
    { .name = "ae_slot0", .translate = translate_ae_carrier_slot,
      .par = (const uint32_t[]){0} },
    { .name = "ae_slot1", .translate = translate_ae_carrier_slot,
      .par = (const uint32_t[]){1} },
    { .name = "ae_slot2", .translate = translate_ae_carrier_slot,
      .par = (const uint32_t[]){2} },
    { .name = "ae_slot3", .translate = translate_ae_carrier_slot,
      .par = (const uint32_t[]){3} },
    { .name = "ae_sraa16rs", .translate = translate_ae_sraa16rs },
    { .name = "ae_sraa16s", .translate = translate_ae_sraa16s },
    { .name = "ae_sraa32", .translate = translate_ae_sraa32 },
    { .name = "ae_sraa32rs", .translate = translate_ae_sraa32rs },
    { .name = "ae_sraa32s", .translate = translate_ae_sraa32s },
    { .name = "ae_sraa64", .translate = translate_ae_sraa64 },
    { .name = "ae_sraaq56", .translate = translate_ae_sraaq56 },
    { .name = "ae_srai16", .translate = translate_ae_srai16 },
    { .name = "ae_srai16r", .translate = translate_ae_srai16r },
    { .name = "ae_srai24", .translate = translate_ae_srai24 },
    { .name = "ae_srai32", .translate = translate_ae_srai32 },
    { .name = "ae_srai32r", .translate = translate_ae_srai32r },
    { .name = "ae_srai72", .translate = translate_ae_srai72 },
    { .name = "ae_sras24", .translate = translate_ae_sras24 },
    { .name = "ae_sras32", .translate = translate_ae_sras32 },
    { .name = "ae_sras64", .translate = translate_ae_sras64 },
    { .name = "ae_srasq56", .translate = translate_ae_srasq56 },
    { .name = "ae_srla32", .translate = translate_ae_srla32 },
    { .name = "ae_srlaq56", .translate = translate_ae_srlaq56 },
    { .name = "ae_srli24", .translate = translate_ae_srli24 },
    { .name = "ae_srli32", .translate = translate_ae_srli32 },
    { .name = "ae_srls24", .translate = translate_ae_srls24 },
    { .name = "ae_srls32", .translate = translate_ae_srls32 },
    { .name = "ae_srls64", .translate = translate_ae_srls64 },
    { .name = "ae_srlsq56", .translate = translate_ae_srlsq56 },
    { .name = "ae_sub16",  .translate = translate_ae_sub16  },
    { .name = "ae_sub16s", .translate = translate_ae_sub16s },
    { .name = "ae_sub24s", .translate = translate_ae_sub24s },
    { .name = "ae_sub32",  .translate = translate_ae_sub32  },
    { .name = "ae_sub32s", .translate = translate_ae_sub32s },
    { .name = "ae_sub64s", .translate = translate_ae_sub64s },
    { .name = "ae_sub72", .translate = translate_ae_sub72 },
    { .name = "ae_sub72x64", .translate = translate_ae_sub72x64 },
    { .name = "ae_subadd32", .translate = translate_ae_subadd32 },
    { .name = "ae_subadd32s", .translate = translate_ae_subadd32s },
    { .name = "ae_subrng32", .translate = translate_ae_subrng32 },
    { .name = "ae_subsq56s", .translate = translate_ae_subsq56s },
    { .name = "ae_to_dr_v", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_to_dr_v0", .translate = translate_ae_carrier_to_dr_v },
    { .name = "ae_trunca16p24s.h", .translate = translate_ae_trunca16p24s,
      .par = (const uint32_t[]){1} },
    { .name = "ae_trunca16p24s.l", .translate = translate_ae_trunca16p24s,
      .par = (const uint32_t[]){0} },
    { .name = "ae_trunca32f64s.l", .translate = translate_ae_trunca32f64s_l },
    { .name = "ae_trunca32q48",     .translate = translate_ae_trunca32q48 },
    { .name = "ae_trunca32x2f64s",  .translate = translate_ae_trunca32x2f64s },
    { .name = "ae_trunci32f64s.l",  .translate = translate_ae_trunci32f64s_l },
    { .name = "ae_trunci32x2f64s",  .translate = translate_ae_trunci32x2f64s },
    { .name = "ae_truncp24q48x2",   .translate = translate_ae_truncp24q48x2 },
    { .name = "ae_truncq32",        .translate = translate_ae_truncq32 },
    { .name = "ae_truncp16",        .translate = translate_ae_truncp16 },
    { .name = "ae_truncp24a32x2",   .translate = translate_ae_truncp24a32x2 },
    { .name = "ae_uimm2x2", .translate = translate_ae_decode_field_noop },
    { .name = "ae_uu_uu", .translate = translate_ae_carrier_valign },
    { .name = "ae_uu_v", .translate = translate_ae_carrier_valign },
    { .name = "ae_vldl16c", .translate = translate_ae_vldl16c },
    { .name = "ae_vldl16c.ic", .translate = translate_ae_vldl16c },
    { .name = "ae_vldl16c.ic1", .translate = translate_ae_vldl16c },
    { .name = "ae_vldl16c.ip", .translate = translate_ae_vldl16c,
      .par = (const uint32_t[]){1} },
    { .name = "ae_vldl16t", .translate = translate_ae_vldl16t },
    { .name = "ae_vldl32t", .translate = translate_ae_vldl32t },
    { .name = "ae_vldsht", .translate = translate_ae_vldsht },
    { .name = "ae_vlel16t", .translate = translate_ae_vlel16t },
    { .name = "ae_vlel32t", .translate = translate_ae_vlel32t },
    { .name = "ae_vles16c", .translate = translate_ae_vles16c },
    { .name = "ae_vles16c.ic", .translate = translate_ae_vles16c },
    { .name = "ae_vles16c.ic1", .translate = translate_ae_vles16c },
    { .name = "ae_vles16c.ip", .translate = translate_ae_vles16c,
      .par = (const uint32_t[]){1} },
    { .name = "ae_zalign64", .translate = translate_ae_zalign64 },


};

const XtensaOpcodeTranslators xtensa_hifi_opcodes = {
    .num_opcodes = ARRAY_SIZE(hifi_ops),
    .opcode = hifi_ops,
};
