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
