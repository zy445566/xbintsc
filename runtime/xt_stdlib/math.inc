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


/* -- first-class Math ------------------------------------------------------ */

/* `Math` is lowered to `xt_math_call` at call sites, but it is also an object
 * in JavaScript: `const cos = Math.cos`, `[1, 2].map(Math.max)` and
 * `const { sqrt, PI } = Math` all read members as values. The singleton below
 * holds one native closure per function, plus the constants. */

static const char *const XT_MATH_FUNCTIONS[] = {
    "abs",    "floor",  "ceil",    "round", "trunc", "sqrt",  "cbrt",  "pow",
    "exp",    "log",    "log2",    "log10", "log1p", "expm1", "sinh",  "cosh",
    "tanh",   "asinh",  "acosh",   "atanh", "fround", "imul", "clz32", "sin",
    "cos",    "tan",    "asin",    "acos",  "atan",  "atan2", "hypot", "sign",
    "random", "min",    "max",
};

static xt_value g_math_object;

static xt_value xt_math_bridge(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  return xt_math_call(xt_closure_env(env, 0), argc, argv);
}

xt_value xt_math_object(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (g_math_object != 0) return g_math_object;
  xt_value object = xt_object_new();
  for (size_t i = 0; i < sizeof(XT_MATH_FUNCTIONS) / sizeof(XT_MATH_FUNCTIONS[0]); i++) {
    /* The closure environment owns its own copy: the string is stored by the
     * object, but a rooted value is cheap and keeps the lifetime obvious. */
    xt_value name = xt_string_from_cstr(XT_MATH_FUNCTIONS[i]);
    xt_value closure = xt_closure_new((void *)xt_math_bridge, 1, &name);
    xt_function_set_metadata(closure, name, 1);
    xt_object_set(object, name, closure);
  }
  xt_object_set(object, xt_string_from_cstr("PI"), xt_number(M_PI));
  xt_object_set(object, xt_string_from_cstr("E"), xt_number(M_E));
  xt_object_set(object, xt_string_from_cstr("LN2"), xt_number(M_LN2));
  xt_object_set(object, xt_string_from_cstr("LN10"), xt_number(M_LN10));
  xt_object_set(object, xt_string_from_cstr("LOG2E"), xt_number(M_LOG2E));
  xt_object_set(object, xt_string_from_cstr("LOG10E"), xt_number(M_LOG10E));
  xt_object_set(object, xt_string_from_cstr("SQRT2"), xt_number(M_SQRT2));
  xt_object_set(object, xt_string_from_cstr("SQRT1_2"), xt_number(M_SQRT1_2));
  g_math_object = object;
  xt_gc_add_root(&g_math_object);
  return object;
}
