/* ------------------------------------------------------------------------- */
/* Conversions                                                               */
/* ------------------------------------------------------------------------- */

double xt_bigint_to_double_value(xt_value value) {
  const xt_bigint *b = BI(value);
  double result = 0.0;
  for (uint32_t i = b->length; i-- > 0;) result = result * 4294967296.0 + (double)b->limbs[i];
  return b->sign < 0 ? -result : result;
}

static xt_bigint *bi_from_abs_double(double value) {
  xt_bigint *r = bi_alloc(8);
  uint32_t count = 0;
  while (value >= 1.0) {
    double remainder = fmod(value, 4294967296.0);
    if (count >= r->capacity) bi_reserve(r, r->capacity * 2);
    r->limbs[count++] = (uint32_t)remainder;
    value = floor(value / 4294967296.0);
  }
  r->length = count;
  r->sign = count ? 1 : 0;
  bi_normalize(r);
  return r;
}

xt_value xt_bigint_from_double(double value) {
  if (isnan(value) || isinf(value) || value != floor(value)) {
    bi_throw_range("The number cannot be converted to a BigInt because it is not an integer");
    return bi_wrap(bi_zero());
  }
  if (value == 0.0) return bi_wrap(bi_zero());
  int negative = value < 0;
  xt_bigint *r = bi_from_abs_double(negative ? -value : value);
  if (r->length) r->sign = negative ? -1 : 1;
  return bi_wrap(r);
}

static xt_value bi_from_xt_value(xt_value value) {
  if (XT_IS_BIGINT(value)) return value;
  if (XT_IS_NUMBER(value)) return xt_bigint_from_double(xt_to_double(value));
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    return xt_bigint_from_string(s->data, s->length);
  }
  if (value == XT_TRUE) return xt_bigint_from_i64(1);
  if (value == XT_FALSE) return xt_bigint_from_i64(0);
  bi_throw_type("Cannot convert value to a BigInt");
  return bi_wrap(bi_zero());
}

