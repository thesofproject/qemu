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
 * ae_salign128.i  u, as, imm
 * Store a 128-bit AE_VALIGN register to [as + imm] (low 64 bits first,
 * high 64 bits at +8). Used by the HiFi5 coprocessor context-save path.
 */
static void translate_ae_salign128_i(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, MO_TEUQ);
    tcg_gen_addi_i32(addr, addr, 8);
    tcg_gen_qemu_st_i64(cpu_AE_VALIGN_HI[arg[0].imm], addr, dc->cring, MO_TEUQ);
}

/*
 * ae_lalign128.i  u, as, imm
 * Load a 128-bit AE_VALIGN register from [as + imm] (low 64 bits first,
 * high 64 bits at +8). Used by the HiFi5 coprocessor context-restore path.
 */
static void translate_ae_lalign128_i(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, MO_TEUQ);
    tcg_gen_addi_i32(addr, addr, 8);
    tcg_gen_qemu_ld_i64(cpu_AE_VALIGN_HI[arg[0].imm], addr, dc->cring, MO_TEUQ);
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

/*
 * translate_ae_movdrzbvc  --  pack HiFi5 bit-stream / NN context state to AE_DR
 *
 * ae_movdrzbvc(aed_out):
 *   Save the opaque 64-bit ZBVC context state into an AE_DR register so the
 *   coprocessor context-save path can spill it to memory. The matching
 *   ae_movzbvcdr restores it. No implemented AE instruction produces this
 *   state, so it is faithfully preserved as an opaque 64-bit blob.
 *
 *   arg[0]  AE_DR  out  -- destination register
 */
static void translate_ae_movdrzbvc(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (!arg[0].out) {
        return;
    }
    tcg_gen_mov_i64(arg[0].out, cpu_AE_ZBVC);
}

/*
 * translate_ae_movzbvcdr  --  unpack AE_DR back into HiFi5 ZBVC context state
 *
 * ae_movzbvcdr(aed_in): inverse of ae_movdrzbvc.
 *
 *   arg[0]  AE_DR  in  -- source register
 */
static void translate_ae_movzbvcdr(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    if (!arg[0].in) {
        return;
    }
    tcg_gen_mov_i64(cpu_AE_ZBVC, arg[0].in);
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

