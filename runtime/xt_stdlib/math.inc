/* ------------------------------------------------------------------------- */
/* Math                                                                      */
/* ------------------------------------------------------------------------- */

xt_value xt_math_call(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  double a = xt_to_number(xt_arg_at(argc, argv, 0));
  double b = xt_to_number(xt_arg_at(argc, argv, 1));
  if (!fn) return xt_number(NAN);
  if (strcmp(fn, "abs") == 0) return xt_number(fabs(a));
  if (strcmp(fn, "floor") == 0) return xt_number(floor(a));
  if (strcmp(fn, "ceil") == 0) return xt_number(ceil(a));
  if (strcmp(fn, "round") == 0) return xt_number(floor(a + 0.5));
  if (strcmp(fn, "trunc") == 0) return xt_number(trunc(a));
  if (strcmp(fn, "sqrt") == 0) return xt_number(sqrt(a));
  if (strcmp(fn, "cbrt") == 0) return xt_number(cbrt(a));
  if (strcmp(fn, "pow") == 0) return xt_number(pow(a, b));
  if (strcmp(fn, "exp") == 0) return xt_number(exp(a));
  if (strcmp(fn, "log") == 0) return xt_number(log(a));
  if (strcmp(fn, "log2") == 0) return xt_number(log2(a));
  if (strcmp(fn, "log10") == 0) return xt_number(log10(a));
  if (strcmp(fn, "log1p") == 0) return xt_number(log1p(a));
  if (strcmp(fn, "expm1") == 0) return xt_number(expm1(a));
  if (strcmp(fn, "sinh") == 0) return xt_number(sinh(a));
  if (strcmp(fn, "cosh") == 0) return xt_number(cosh(a));
  if (strcmp(fn, "tanh") == 0) return xt_number(tanh(a));
  if (strcmp(fn, "asinh") == 0) return xt_number(asinh(a));
  if (strcmp(fn, "acosh") == 0) return xt_number(acosh(a));
  if (strcmp(fn, "atanh") == 0) return xt_number(atanh(a));
  if (strcmp(fn, "fround") == 0) return xt_number((double)(float)a);
  if (strcmp(fn, "imul") == 0) return xt_number((double)((int32_t)xt_to_int32(xt_arg_at(argc, argv, 0)) * (int32_t)xt_to_int32(xt_arg_at(argc, argv, 1))));
  if (strcmp(fn, "clz32") == 0) {
    uint32_t v = (uint32_t)xt_to_int32(xt_arg_at(argc, argv, 0));
    int count = 0;
    if (v == 0) count = 32;
    else { while (!(v & 0x80000000u)) { v <<= 1; count++; } }
    return xt_number((double)count);
  }
  if (strcmp(fn, "sin") == 0) return xt_number(sin(a));
  if (strcmp(fn, "cos") == 0) return xt_number(cos(a));
  if (strcmp(fn, "tan") == 0) return xt_number(tan(a));
  if (strcmp(fn, "asin") == 0) return xt_number(asin(a));
  if (strcmp(fn, "acos") == 0) return xt_number(acos(a));
  if (strcmp(fn, "atan") == 0) return xt_number(atan(a));
  if (strcmp(fn, "atan2") == 0) return xt_number(atan2(a, b));
  if (strcmp(fn, "hypot") == 0) return xt_number(hypot(a, b));
  if (strcmp(fn, "sign") == 0) {
    if (isnan(a)) return xt_number(NAN);
    return xt_number(a > 0 ? 1.0 : a < 0 ? -1.0 : a);
  }
  if (strcmp(fn, "random") == 0) return xt_number((double)rand() / ((double)RAND_MAX + 1.0));
  if (strcmp(fn, "min") == 0) {
    double result = INFINITY;
    for (int32_t i = 0; i < argc; i++) {
      double v = xt_to_number(argv[i]);
      if (isnan(v)) return xt_number(NAN);
      if (v < result) result = v;
    }
    return xt_number(result);
  }
  if (strcmp(fn, "max") == 0) {
    double result = -INFINITY;
    for (int32_t i = 0; i < argc; i++) {
      double v = xt_to_number(argv[i]);
      if (isnan(v)) return xt_number(NAN);
      if (v > result) result = v;
    }
    return xt_number(result);
  }
  return xt_number(NAN);
}

