/* ------------------------------------------------------------------------- */
/* Comparison                                                                */
/* ------------------------------------------------------------------------- */

/** Mixed numeric/bigint comparison; returns -1/0/1, or 2 for unordered. */
static int xt_compare_numeric(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_compare(a, b);
  if (XT_IS_BIGINT(a)) {
    if (XT_IS_NUMBER(b)) return xt_bigint_compare_double(a, xt_to_double(b));
    if (XT_IS_STRING(b)) {
      double n = xt_to_number(b);
      if (isnan(n)) return 2;
      return xt_bigint_compare_double(a, n);
    }
    if (b == XT_TRUE) return xt_bigint_compare_double(a, 1.0);
    if (b == XT_FALSE) return xt_bigint_compare_double(a, 0.0);
    return 2;
  }
  if (XT_IS_BIGINT(b)) {
    int cmp = xt_compare_numeric(b, a);
    return cmp == 2 ? 2 : -cmp;
  }
  double x = xt_to_number(a);
  double y = xt_to_number(b);
  if (isnan(x) || isnan(y)) return 2;
  return x < y ? -1 : (x > y ? 1 : 0);
}

xt_value xt_lt(xt_value a, xt_value b) {
  a = xt_to_primitive(a);
  b = xt_to_primitive(b);
  if (XT_IS_STRING(a) && XT_IS_STRING(b)) {
    xt_string *x = xt_as_string(a);
    xt_string *y = xt_as_string(b);
    size_t n = x->length < y->length ? x->length : y->length;
    int cmp = memcmp(x->data, y->data, n);
    if (cmp == 0) return xt_bool(x->length < y->length);
    return xt_bool(cmp < 0);
  }
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) return xt_bool(xt_compare_numeric(a, b) == -1);
  return xt_bool(xt_to_number(a) < xt_to_number(b));
}

xt_value xt_le(xt_value a, xt_value b) {
  a = xt_to_primitive(a);
  b = xt_to_primitive(b);
  if (XT_IS_STRING(a) && XT_IS_STRING(b)) {
    xt_string *x = xt_as_string(a);
    xt_string *y = xt_as_string(b);
    size_t n = x->length < y->length ? x->length : y->length;
    int cmp = memcmp(x->data, y->data, n);
    if (cmp == 0) return xt_bool(x->length <= y->length);
    return xt_bool(cmp < 0);
  }
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    int cmp = xt_compare_numeric(a, b);
    return xt_bool(cmp != 2 && cmp <= 0);
  }
  return xt_bool(xt_to_number(a) <= xt_to_number(b));
}

xt_value xt_gt(xt_value a, xt_value b) { return xt_lt(b, a); }
xt_value xt_ge(xt_value a, xt_value b) { return xt_le(b, a); }

static int xt_loose_equals(xt_value a, xt_value b) {
  if (xt_is_symbol(a) || xt_is_symbol(b)) return a == b;
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) return xt_to_double(a) == xt_to_double(b);
  if (XT_IS_STRING(a) && XT_IS_STRING(b)) return xt_string_equals(xt_as_string(a), xt_as_string(b));
  if (a == b) return 1;
  if ((a == XT_NULL && b == XT_UNDEFINED) || (a == XT_UNDEFINED && b == XT_NULL)) return 1;
  /* Abstract equality converts an object operand to a primitive (via
     ToPrimitive) before comparing, so `[] == 0`, `[1] == 1` and
     `{} == "[object Object]"` are all true. */
  if (XT_IS_OBJECT(a) || XT_IS_ARRAY(a) || XT_IS_FUNCTION(a)) {
    if (XT_IS_OBJECT(b) || XT_IS_ARRAY(b) || XT_IS_FUNCTION(b)) return 0;
    return xt_loose_equals(xt_to_string(a), b);
  }
  if (XT_IS_OBJECT(b) || XT_IS_ARRAY(b) || XT_IS_FUNCTION(b)) {
    return xt_loose_equals(a, xt_to_string(b));
  }
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_value big = XT_IS_BIGINT(a) ? a : b;
    xt_value other = XT_IS_BIGINT(a) ? b : a;
    if (XT_IS_BIGINT(other)) return xt_bigint_compare(big, other) == 0;
    if (XT_IS_NUMBER(other)) return xt_bigint_compare_double(big, xt_to_double(other)) == 0;
    if (XT_IS_STRING(other)) {
      int cmp = xt_compare_numeric(big, other);
      return cmp == 0;
    }
    if (other == XT_TRUE) return xt_bigint_compare_double(big, 1.0) == 0;
    if (other == XT_FALSE) return xt_bigint_compare_double(big, 0.0) == 0;
    return 0;
  }
  if (XT_IS_BOOL(a)) return xt_loose_equals(xt_number(xt_to_number(a)), b);
  if (XT_IS_BOOL(b)) return xt_loose_equals(a, xt_number(xt_to_number(b)));
  if (XT_IS_STRING(a) && XT_IS_NUMBER(b)) return xt_to_number(a) == xt_to_double(b);
  if (XT_IS_NUMBER(a) && XT_IS_STRING(b)) return xt_to_double(a) == xt_to_number(b);
  return 0;
}

xt_value xt_eq(xt_value a, xt_value b) { return xt_bool(xt_loose_equals(a, b)); }
xt_value xt_ne(xt_value a, xt_value b) { return xt_bool(!xt_loose_equals(a, b)); }

xt_value xt_seq(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) return xt_bool(xt_to_double(a) == xt_to_double(b));
  if (XT_IS_STRING(a) && XT_IS_STRING(b)) return xt_bool(xt_string_equals(xt_as_string(a), xt_as_string(b)));
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bool(xt_bigint_compare(a, b) == 0);
  return xt_bool(a == b);
}

xt_value xt_sne(xt_value a, xt_value b) { return xt_bool(!xt_truthy(xt_seq(a, b))); }
xt_value xt_not(xt_value a) { return xt_bool(!xt_truthy(a)); }
