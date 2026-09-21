/* ------------------------------------------------------------------------- */
/* Public constructors and statics                                           */
/* ------------------------------------------------------------------------- */

xt_value xt_bigint_ctor(int32_t argc, xt_value *argv) {
  if (argc < 1) {
    bi_throw_type("Cannot convert undefined to a BigInt");
    return bi_wrap(bi_zero());
  }
  return bi_from_xt_value(argv[0]);
}

static xt_value bi_as_n(int32_t bits, xt_value value, int signedResult) {
  if (!XT_IS_BIGINT(value)) value = bi_from_xt_value(value);
  if (bits <= 0) return xt_bigint_from_i64(0);
  uint32_t count = ((uint32_t)bits + 31u) / 32u;
  uint32_t *tmp = (uint32_t *)calloc(count ? count : 1, sizeof(uint32_t));
  if (!tmp) abort();
  bi_to_twos(BI(value), tmp, count);
  uint32_t rem = (uint32_t)bits & 31u;
  if (rem) {
    uint32_t mask = (1u << rem) - 1u;
    tmp[count - 1] &= mask;
    if (signedResult && (tmp[count - 1] & (1u << (rem - 1)))) {
      tmp[count - 1] |= ~mask;
    }
  }
  xt_bigint *r = signedResult ? bi_from_twos(tmp, count) : bi_from_unsigned(tmp, count);
  free(tmp);
  return bi_wrap(r);
}

xt_value xt_bigint_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "asIntN") == 0) {
    int32_t bits = argc >= 1 ? xt_to_int32(argv[0]) : 0;
    xt_value value = xt_arg_at(argc, argv, 1);
    return bi_as_n(bits, value, 1);
  }
  if (strcmp(fn, "asUintN") == 0) {
    int32_t bits = argc >= 1 ? xt_to_int32(argv[0]) : 0;
    xt_value value = xt_arg_at(argc, argv, 1);
    return bi_as_n(bits, value, 0);
  }
  return XT_UNDEFINED;
}

xt_value xt_bigint_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "toString") == 0) {
    int radix = argc >= 1 && argv[0] != XT_UNDEFINED ? xt_to_int32(argv[0]) : 10;
    return xt_bigint_to_string_radix(target, radix);
  }
  if (strcmp(method, "toLocaleString") == 0) return xt_bigint_to_decimal(target);
  if (strcmp(method, "valueOf") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}
