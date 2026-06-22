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
        tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[2].imm);
    }
}

