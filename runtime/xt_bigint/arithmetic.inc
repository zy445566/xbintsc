/* ------------------------------------------------------------------------- */
/* Arithmetic                                                                */
/* ------------------------------------------------------------------------- */

xt_value xt_bigint_neg(xt_value value) {
  xt_bigint *r = bi_copy(BI(value));
  r->sign = -r->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_add(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (a->sign == 0) return bv;
  if (b->sign == 0) return av;
  if (a->sign == b->sign) {
    xt_bigint *r = bi_add_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  int cmp = bi_cmp_mag(a, b);
  if (cmp == 0) return bi_wrap(bi_zero());
  if (cmp > 0) {
    xt_bigint *r = bi_sub_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  xt_bigint *r = bi_sub_mag(b, a);
  r->sign = b->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_sub(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign == 0) return av;
  if (a->sign == 0) return xt_bigint_neg(bv);
  if (a->sign != b->sign) {
    xt_bigint *r = bi_add_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  int cmp = bi_cmp_mag(a, b);
  if (cmp == 0) return bi_wrap(bi_zero());
  if (cmp > 0) {
    xt_bigint *r = bi_sub_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  xt_bigint *r = bi_sub_mag(b, a);
  r->sign = -a->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_mul(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (a->sign == 0 || b->sign == 0) return bi_wrap(bi_zero());
  xt_bigint *r = bi_mul_mag(a, b);
  r->sign = a->sign * b->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_div(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign == 0) {
    bi_throw_range("Division by zero");
    return bi_wrap(bi_zero());
  }
  if (a->sign == 0) return bi_wrap(bi_zero());
  xt_bigint *q = NULL;
  xt_bigint *r = NULL;
  bi_divmod_mag(a, b, &q, &r);
  q->sign = q->length ? a->sign * b->sign : 0;
  return bi_wrap(q);
}

xt_value xt_bigint_mod(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign == 0) {
    bi_throw_range("Division by zero");
    return bi_wrap(bi_zero());
  }
  if (a->sign == 0) return bi_wrap(bi_zero());
  xt_bigint *q = NULL;
  xt_bigint *r = NULL;
  bi_divmod_mag(a, b, &q, &r);
  r->sign = r->length ? a->sign : 0;
  return bi_wrap(r);
}

xt_value xt_bigint_pow(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign < 0) {
    bi_throw_range("Exponent must be non-negative");
    return bi_wrap(bi_zero());
  }
  if (b->sign == 0) return xt_bigint_from_i64(1);
  if (a->sign == 0) return bi_wrap(bi_zero());
  if (b->length > 2) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }
  uint64_t exponent = b->limbs[0];
  if (b->length == 2) exponent |= (uint64_t)b->limbs[1] << 32;
  if (exponent > 0xffffffffULL) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }

  xt_bigint *result = bi_from_u64(1, 1);
  xt_bigint *base = bi_copy(a);
  base->sign = 1;
  uint32_t exp = (uint32_t)exponent;
  while (exp > 0) {
    if (exp & 1) {
      xt_bigint *next = bi_mul_mag(result, base);
      next->sign = 1;
      result = next;
    }
    exp >>= 1;
    if (exp) {
      xt_bigint *next = bi_mul_mag(base, base);
      next->sign = 1;
      base = next;
    }
  }
  result->sign = (a->sign < 0 && (exponent & 1)) ? -1 : 1;
  if (result->length == 0) result->sign = 0;
  return bi_wrap(result);
}

