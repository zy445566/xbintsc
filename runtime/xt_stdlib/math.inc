/* ------------------------------------------------------------------------- */
/* Math                                                                      */
/* ------------------------------------------------------------------------- */

xt_value xt_math_call(xt_value name, int32_t argc, xt_value *argv) {
  XT_PROFILE_TICK(XT_PROFILE_MATH);
  const char *fn = xt_string_data(name);
  if (!fn) return xt_number(NAN);
  double a = xt_to_number(xt_arg_at(argc, argv, 0));
  double b = xt_to_number(xt_arg_at(argc, argv, 1));
  /* Dispatch on the first byte before comparing names. A flat chain over ~35
   * names paid up to 35 `strcmp`s per call, and numeric code calls
   * `Math.imul`/`floor`/`exp` hundreds of millions of times (procedural noise,
   * geometry), so the difference is seconds of startup on a large page. */
  switch (fn[0]) {
    case 'a':
      if (strcmp(fn, "abs") == 0) return xt_number(fabs(a));
      if (strcmp(fn, "acos") == 0) return xt_number(acos(a));
      if (strcmp(fn, "acosh") == 0) return xt_number(acosh(a));
      if (strcmp(fn, "asin") == 0) return xt_number(asin(a));
      if (strcmp(fn, "asinh") == 0) return xt_number(asinh(a));
      if (strcmp(fn, "atan") == 0) return xt_number(atan(a));
      if (strcmp(fn, "atan2") == 0) return xt_number(atan2(a, b));
      if (strcmp(fn, "atanh") == 0) return xt_number(atanh(a));
      break;
    case 'c':
      if (strcmp(fn, "cbrt") == 0) return xt_number(cbrt(a));
      if (strcmp(fn, "ceil") == 0) return xt_number(ceil(a));
      if (strcmp(fn, "clz32") == 0) {
        uint32_t v = (uint32_t)xt_to_int32(xt_arg_at(argc, argv, 0));
        int count = 0;
        if (v == 0) count = 32;
        else { while (!(v & 0x80000000u)) { v <<= 1; count++; } }
        return xt_number((double)count);
      }
      if (strcmp(fn, "cos") == 0) return xt_number(cos(a));
      if (strcmp(fn, "cosh") == 0) return xt_number(cosh(a));
      break;
    case 'e':
      if (strcmp(fn, "exp") == 0) return xt_number(exp(a));
      if (strcmp(fn, "expm1") == 0) return xt_number(expm1(a));
      break;
    case 'f':
      if (strcmp(fn, "floor") == 0) return xt_number(floor(a));
      if (strcmp(fn, "fround") == 0) return xt_number((double)(float)a);
      break;
    case 'h':
      if (strcmp(fn, "hypot") == 0) {
        /* `Math.hypot(...values)` takes any number of arguments (ECMA-262
         * 21.3.2.18): every argument participates, NaN wins, and the sum is
         * scaled by the largest magnitude so `hypot(1e308, 1e308)` does not
         * overflow. */
        double scale = 0.0;
        for (int32_t i = 0; i < argc; i++) {
          double v = fabs(xt_to_number(argv[i]));
          if (isnan(v)) return xt_number(NAN);
          if (v > scale) scale = v;
        }
        if (scale == INFINITY) return xt_number(INFINITY);
        if (scale == 0.0) return xt_number(0.0);
        double sum = 0.0;
        for (int32_t i = 0; i < argc; i++) {
          double v = xt_to_number(argv[i]) / scale;
          sum += v * v;
        }
        return xt_number(scale * sqrt(sum));
      }
      break;
    case 'i':
      if (strcmp(fn, "imul") == 0)
        return xt_number((double)((int32_t)xt_to_int32(xt_arg_at(argc, argv, 0)) *
                                   (int32_t)xt_to_int32(xt_arg_at(argc, argv, 1))));
      break;
    case 'l':
      if (strcmp(fn, "log") == 0) return xt_number(log(a));
      if (strcmp(fn, "log10") == 0) return xt_number(log10(a));
      if (strcmp(fn, "log1p") == 0) return xt_number(log1p(a));
      if (strcmp(fn, "log2") == 0) return xt_number(log2(a));
      break;
    case 'm':
      if (strcmp(fn, "max") == 0) {
        double result = -INFINITY;
        for (int32_t i = 0; i < argc; i++) {
          double v = xt_to_number(argv[i]);
          if (isnan(v)) return xt_number(NAN);
          if (v > result) result = v;
        }
        return xt_number(result);
      }
      if (strcmp(fn, "min") == 0) {
        double result = INFINITY;
        for (int32_t i = 0; i < argc; i++) {
          double v = xt_to_number(argv[i]);
          if (isnan(v)) return xt_number(NAN);
          if (v < result) result = v;
        }
        return xt_number(result);
      }
      break;
    case 'p':
      if (strcmp(fn, "pow") == 0) return xt_number(pow(a, b));
      break;
    case 'r':
      if (strcmp(fn, "random") == 0) return xt_number(xt_random_double());
      if (strcmp(fn, "round") == 0) return xt_number(floor(a + 0.5));
      break;
    case 's':
      if (strcmp(fn, "sign") == 0) {
        if (isnan(a)) return xt_number(NAN);
        return xt_number(a > 0 ? 1.0 : a < 0 ? -1.0 : a);
      }
      if (strcmp(fn, "sin") == 0) return xt_number(sin(a));
      if (strcmp(fn, "sinh") == 0) return xt_number(sinh(a));
      if (strcmp(fn, "sqrt") == 0) return xt_number(sqrt(a));
      break;
    case 't':
      if (strcmp(fn, "tan") == 0) return xt_number(tan(a));
      if (strcmp(fn, "tanh") == 0) return xt_number(tanh(a));
      if (strcmp(fn, "trunc") == 0) return xt_number(trunc(a));
      break;
    default:
      break;
  }
  return xt_number(NAN);
}


/* -- direct call forms ----------------------------------------------------- */

/*
 * `Math.floor(x)` used to lower to `xt_math_call("floor", 1, &x)`: a name
 * dispatch plus an argument array for a call whose arity and name the compiler
 * already knows. A numeric kernel calls these hundreds of times per pixel, so
 * codegen emits the fixed-arity form below instead — the arguments arrive in
 * registers and there is no name to resolve. `min`/`max` stay variadic and keep
 * `xt_math_call`. Semantics are identical: ToNumber first, then the platform
 * operation.
 */
#define XT_MATH_UNARY(name, expression) \
  xt_value xt_math1_##name(xt_value arg) { \
    double x = xt_to_number(arg); \
    return xt_number(expression); \
  }

#define XT_MATH_BINARY(name, expression) \
  xt_value xt_math2_##name(xt_value left, xt_value right) { \
    double x = xt_to_number(left); \
    double y = xt_to_number(right); \
    return xt_number(expression); \
  }

XT_MATH_UNARY(abs, fabs(x))
XT_MATH_UNARY(floor, floor(x))
XT_MATH_UNARY(ceil, ceil(x))
XT_MATH_UNARY(round, floor(x + 0.5))
XT_MATH_UNARY(trunc, trunc(x))
XT_MATH_UNARY(sqrt, sqrt(x))
XT_MATH_UNARY(cbrt, cbrt(x))
XT_MATH_UNARY(exp, exp(x))
XT_MATH_UNARY(log, log(x))
XT_MATH_UNARY(log2, log2(x))
XT_MATH_UNARY(log10, log10(x))
XT_MATH_UNARY(log1p, log1p(x))
XT_MATH_UNARY(expm1, expm1(x))
XT_MATH_UNARY(sinh, sinh(x))
XT_MATH_UNARY(cosh, cosh(x))
XT_MATH_UNARY(tanh, tanh(x))
XT_MATH_UNARY(asinh, asinh(x))
XT_MATH_UNARY(acosh, acosh(x))
XT_MATH_UNARY(atanh, atanh(x))
XT_MATH_UNARY(fround, (double)(float)x)
XT_MATH_UNARY(sin, sin(x))
XT_MATH_UNARY(cos, cos(x))
XT_MATH_UNARY(tan, tan(x))
XT_MATH_UNARY(asin, asin(x))
XT_MATH_UNARY(acos, acos(x))
XT_MATH_UNARY(atan, atan(x))

XT_MATH_BINARY(pow, pow(x, y))
XT_MATH_BINARY(atan2, atan2(x, y))
XT_MATH_BINARY(hypot, hypot(x, y))

#undef XT_MATH_UNARY
#undef XT_MATH_BINARY

xt_value xt_math1_clz32(xt_value arg) {
  uint32_t v = (uint32_t)xt_to_int32(arg);
  int count = 0;
  if (v == 0) {
    count = 32;
  } else {
    while (!(v & 0x80000000u)) {
      v <<= 1;
      count++;
    }
  }
  return xt_number((double)count);
}

xt_value xt_math1_sign(xt_value arg) {
  double x = xt_to_number(arg);
  if (isnan(x)) return xt_number(NAN);
  return xt_number(x > 0 ? 1.0 : x < 0 ? -1.0 : x);
}

xt_value xt_math0_random(void) { return xt_number(xt_random_double()); }

xt_value xt_math2_imul(xt_value left, xt_value right) {
  int32_t a = (int32_t)xt_to_int32(left);
  int32_t b = (int32_t)xt_to_int32(right);
  return xt_number((double)(a * b));
}

/* -- first-class Math ------------------------------------------------------ */

/* `Math` is lowered to `xt_math_call` at call sites, but it is also an object
 * in JavaScript: `const cos = Math.cos`, `[1, 2].map(Math.max)` and
 * `const { sqrt, PI } = Math` all read members as values. The singleton below
 * holds one native closure per function, plus the constants. */

/* `M_PI` and friends are not portable: MSVC's `<math.h>` only defines them
 * when `_USE_MATH_DEFINES` is set before the include, and strict-ANSI libcs
 * hide them as well. Each literal below is the correctly rounded double of the
 * mathematical constant, so it is bit-identical to the `M_*` macro on hosts
 * that do provide one. */
static const double XT_MATH_PI = 3.14159265358979323846;
static const double XT_MATH_E = 2.71828182845904523536;
static const double XT_MATH_LN2 = 0.69314718055994530942;
static const double XT_MATH_LN10 = 2.30258509299404568402;
static const double XT_MATH_LOG2E = 1.44269504088896340736;
static const double XT_MATH_LOG10E = 0.43429448190325182765;
static const double XT_MATH_SQRT2 = 1.41421356237309504880;
static const double XT_MATH_SQRT1_2 = 0.70710678118654752440;

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
  xt_object_set(object, xt_string_from_cstr("PI"), xt_number(XT_MATH_PI));
  xt_object_set(object, xt_string_from_cstr("E"), xt_number(XT_MATH_E));
  xt_object_set(object, xt_string_from_cstr("LN2"), xt_number(XT_MATH_LN2));
  xt_object_set(object, xt_string_from_cstr("LN10"), xt_number(XT_MATH_LN10));
  xt_object_set(object, xt_string_from_cstr("LOG2E"), xt_number(XT_MATH_LOG2E));
  xt_object_set(object, xt_string_from_cstr("LOG10E"), xt_number(XT_MATH_LOG10E));
  xt_object_set(object, xt_string_from_cstr("SQRT2"), xt_number(XT_MATH_SQRT2));
  xt_object_set(object, xt_string_from_cstr("SQRT1_2"), xt_number(XT_MATH_SQRT1_2));
  g_math_object = object;
  xt_gc_add_root(&g_math_object);
  return object;
}
