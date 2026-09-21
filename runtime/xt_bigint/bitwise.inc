/* ------------------------------------------------------------------------- */
/* Bitwise operators                                                         */
/* ------------------------------------------------------------------------- */

typedef enum { BI_AND, BI_OR, BI_XOR } bi_bitop;

static xt_value bi_bitwise(xt_value av, xt_value bv, bi_bitop op) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  uint32_t count = (a->length > b->length ? a->length : b->length) + 1;
  uint32_t *x = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
  uint32_t *y = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
  if (!x || !y) abort();
  bi_to_twos(a, x, count);
  bi_to_twos(b, y, count);
  for (uint32_t i = 0; i < count; i++) {
    if (op == BI_AND) x[i] &= y[i];
    else if (op == BI_OR) x[i] |= y[i];
    else x[i] ^= y[i];
  }
  xt_bigint *r = bi_from_twos(x, count);
  free(x);
  free(y);
  return bi_wrap(r);
}

xt_value xt_bigint_bit_and(xt_value a, xt_value b) { return bi_bitwise(a, b, BI_AND); }
xt_value xt_bigint_bit_or(xt_value a, xt_value b) { return bi_bitwise(a, b, BI_OR); }
xt_value xt_bigint_bit_xor(xt_value a, xt_value b) { return bi_bitwise(a, b, BI_XOR); }

xt_value xt_bigint_bit_not(xt_value value) {
  /* ~a === -a - 1. */
  xt_value negated = xt_bigint_neg(value);
  xt_value one = xt_bigint_from_i64(1);
  return xt_bigint_sub(negated, one);
}

static int64_t bi_shift_count(xt_value value, int *overflow) {
  const xt_bigint *b = BI(value);
  *overflow = 0;
  if (b->length == 0) return 0;
  if (b->length > 2) {
    *overflow = 1;
    return 0;
  }
  uint64_t magnitude = b->limbs[0];
  if (b->length == 2) magnitude |= (uint64_t)b->limbs[1] << 32;
  if (magnitude > (uint64_t)INT64_MAX) {
    *overflow = 1;
    return 0;
  }
  return b->sign < 0 ? -(int64_t)magnitude : (int64_t)magnitude;
}

static xt_value bi_shl_count(xt_value value, uint32_t bits) {
  if (bits > BI_MAX_SHIFT) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }
  xt_bigint *r = bi_shl_bits(BI(value), bits);
  return bi_wrap(r);
}

static xt_value bi_shr_count(xt_value value, uint32_t bits) {
  const xt_bigint *a = BI(value);
  if (a->sign >= 0) {
    xt_bigint *r = bi_shr_bits(a, bits);
    if (r->length) r->sign = 1;
    return bi_wrap(r);
  }
  /* Arithmetic shift of a negative value: -( ((|a| - 1) >> bits) + 1 ). */
  xt_bigint *magnitude = bi_copy(a);
  magnitude->sign = 1;
  bi_sub_small(magnitude, 1);
  xt_bigint *shifted = bi_shr_bits(magnitude, bits);
  bi_add_small(shifted, 1);
  shifted->sign = -1;
  return bi_wrap(shifted);
}

xt_value xt_bigint_shl(xt_value av, xt_value bv) {
  int overflow = 0;
  int64_t count = bi_shift_count(bv, &overflow);
  if (overflow || count > (int64_t)BI_MAX_SHIFT) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }
  if (count < 0) return bi_shr_count(av, (uint32_t)(-count));
  return bi_shl_count(av, (uint32_t)count);
}

xt_value xt_bigint_shr(xt_value av, xt_value bv) {
  int overflow = 0;
  int64_t count = bi_shift_count(bv, &overflow);
  if (overflow || count > (int64_t)BI_MAX_SHIFT) {
    const xt_bigint *a = BI(av);
    if (a->sign < 0) return xt_bigint_from_i64(-1);
    return bi_wrap(bi_zero());
  }
  if (count < 0) return bi_shl_count(av, (uint32_t)(-count));
  return bi_shr_count(av, (uint32_t)count);
}

