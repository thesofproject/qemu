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
 * The _s2 suffix does not double the product for these MAC variants — the
 * hardware behavior matches the base form (acc + product).  Simply delegate
 * to the base translator unchanged.
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

