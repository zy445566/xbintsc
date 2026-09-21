/* ------------------------------------------------------------------------- */
/* Comparisons                                                               */
/* ------------------------------------------------------------------------- */

static int bi_cmp(const xt_bigint *a, const xt_bigint *b) {
  if (a->sign != b->sign) return a->sign < b->sign ? -1 : 1;
  if (a->sign == 0) return 0;
  int cmp = bi_cmp_mag(a, b);
  return a->sign < 0 ? -cmp : cmp;
}

int xt_bigint_compare(xt_value a, xt_value b) { return bi_cmp(BI(a), BI(b)); }

int xt_bigint_compare_double(xt_value value, double other) {
  const xt_bigint *a = BI(value);
  if (isnan(other)) return 2;
  if (isinf(other)) return other > 0 ? -1 : 1;

  double truncated = trunc(other);
  int otherSign = truncated == 0.0 ? 0 : (truncated < 0 ? -1 : 1);
  xt_bigint *b = bi_from_abs_double(fabs(truncated));
  if (b->length) b->sign = otherSign;

  int cmp = bi_cmp(a, b);
  if (other != truncated && cmp == 0) return other > truncated ? -1 : 1;
  return cmp;
}

int xt_is_bigint(xt_value value) { return XT_IS_BIGINT(value); }

