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
