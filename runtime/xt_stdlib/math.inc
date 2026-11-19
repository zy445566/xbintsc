/* ------------------------------------------------------------------------- */
/* Math                                                                      */
/* ------------------------------------------------------------------------- */

/*
 * Every `Math.<fn>` call reaches this one dispatcher, and a numeric kernel can
 * spend a large share of its time here (the Saltline terrain generator calls it
 * ~150 times per pixel). The name is a compile-time literal, so its interned
 * string address is stable; a small direct-mapped cache keyed by that address
 * turns the former chain of up to 35 `strcmp`s into one hash + pointer compare.
 * The cached name is re-checked with `strcmp` because a *dynamically* built
 * name may be collected and a different string allocated at the same address.
 */
enum {
  XT_MATH_ABS, XT_MATH_FLOOR, XT_MATH_CEIL, XT_MATH_ROUND, XT_MATH_TRUNC,
  XT_MATH_SQRT, XT_MATH_CBRT, XT_MATH_POW, XT_MATH_EXP, XT_MATH_LOG,
  XT_MATH_LOG2, XT_MATH_LOG10, XT_MATH_LOG1P, XT_MATH_EXPM1, XT_MATH_SINH,
  XT_MATH_COSH, XT_MATH_TANH, XT_MATH_ASINH, XT_MATH_ACOSH, XT_MATH_ATANH,
  XT_MATH_FROUND, XT_MATH_IMUL, XT_MATH_CLZ32, XT_MATH_SIN, XT_MATH_COS,
  XT_MATH_TAN, XT_MATH_ASIN, XT_MATH_ACOS, XT_MATH_ATAN, XT_MATH_ATAN2,
  XT_MATH_HYPOT, XT_MATH_SIGN, XT_MATH_RANDOM, XT_MATH_MIN, XT_MATH_MAX,
  XT_MATH_UNKNOWN
};

static const char *const XT_MATH_NAMES[] = {
    "abs",    "floor", "ceil",  "round", "trunc", "sqrt",  "cbrt",  "pow",
    "exp",    "log",   "log2",  "log10", "log1p", "expm1", "sinh",  "cosh",
    "tanh",   "asinh", "acosh", "atanh", "fround", "imul", "clz32", "sin",
    "cos",    "tan",   "asin",  "acos",  "atan",  "atan2", "hypot", "sign",
    "random", "min",   "max",
};

static int xt_math_parse(const char *fn) {
  if (strcmp(fn, "abs") == 0) return XT_MATH_ABS;
  if (strcmp(fn, "floor") == 0) return XT_MATH_FLOOR;
  if (strcmp(fn, "ceil") == 0) return XT_MATH_CEIL;
  if (strcmp(fn, "round") == 0) return XT_MATH_ROUND;
  if (strcmp(fn, "trunc") == 0) return XT_MATH_TRUNC;
  if (strcmp(fn, "sqrt") == 0) return XT_MATH_SQRT;
  if (strcmp(fn, "cbrt") == 0) return XT_MATH_CBRT;
  if (strcmp(fn, "pow") == 0) return XT_MATH_POW;
  if (strcmp(fn, "exp") == 0) return XT_MATH_EXP;
  if (strcmp(fn, "log") == 0) return XT_MATH_LOG;
  if (strcmp(fn, "log2") == 0) return XT_MATH_LOG2;
  if (strcmp(fn, "log10") == 0) return XT_MATH_LOG10;
  if (strcmp(fn, "log1p") == 0) return XT_MATH_LOG1P;
  if (strcmp(fn, "expm1") == 0) return XT_MATH_EXPM1;
  if (strcmp(fn, "sinh") == 0) return XT_MATH_SINH;
  if (strcmp(fn, "cosh") == 0) return XT_MATH_COSH;
  if (strcmp(fn, "tanh") == 0) return XT_MATH_TANH;
  if (strcmp(fn, "asinh") == 0) return XT_MATH_ASINH;
  if (strcmp(fn, "acosh") == 0) return XT_MATH_ACOSH;
  if (strcmp(fn, "atanh") == 0) return XT_MATH_ATANH;
  if (strcmp(fn, "fround") == 0) return XT_MATH_FROUND;
  if (strcmp(fn, "imul") == 0) return XT_MATH_IMUL;
  if (strcmp(fn, "clz32") == 0) return XT_MATH_CLZ32;
  if (strcmp(fn, "sin") == 0) return XT_MATH_SIN;
  if (strcmp(fn, "cos") == 0) return XT_MATH_COS;
  if (strcmp(fn, "tan") == 0) return XT_MATH_TAN;
  if (strcmp(fn, "asin") == 0) return XT_MATH_ASIN;
  if (strcmp(fn, "acos") == 0) return XT_MATH_ACOS;
  if (strcmp(fn, "atan") == 0) return XT_MATH_ATAN;
  if (strcmp(fn, "atan2") == 0) return XT_MATH_ATAN2;
  if (strcmp(fn, "hypot") == 0) return XT_MATH_HYPOT;
  if (strcmp(fn, "sign") == 0) return XT_MATH_SIGN;
  if (strcmp(fn, "random") == 0) return XT_MATH_RANDOM;
  if (strcmp(fn, "min") == 0) return XT_MATH_MIN;
  if (strcmp(fn, "max") == 0) return XT_MATH_MAX;
  return XT_MATH_UNKNOWN;
}

#define XT_MATH_CACHE_SLOTS 64
static struct {
  const char *name;
  int op;
} g_math_cache[XT_MATH_CACHE_SLOTS];

static int xt_math_op_of(const char *fn) {
  uintptr_t bits = (uintptr_t)fn;
  unsigned slot = (unsigned)((bits >> 3) ^ (bits >> 9)) & (XT_MATH_CACHE_SLOTS - 1);
  int op = g_math_cache[slot].op;
  if (op >= 0 && op < XT_MATH_UNKNOWN && g_math_cache[slot].name == fn &&
      strcmp(fn, XT_MATH_NAMES[op]) == 0) {
    return op;
  }
  op = xt_math_parse(fn);
  g_math_cache[slot].name = fn;
  g_math_cache[slot].op = op;
  return op;
}

xt_value xt_math_call(xt_value name, int32_t argc, xt_value *argv) {
  XT_PROFILE_TICK(XT_PROFILE_MATH);
  const char *fn = xt_string_data(name);
  if (!fn) return xt_number(NAN);
  double a = xt_to_number(xt_arg_at(argc, argv, 0));
  double b = xt_to_number(xt_arg_at(argc, argv, 1));
  switch (xt_math_op_of(fn)) {
    case XT_MATH_ABS: return xt_number(fabs(a));
    case XT_MATH_FLOOR: return xt_number(floor(a));
    case XT_MATH_CEIL: return xt_number(ceil(a));
    case XT_MATH_ROUND: return xt_number(floor(a + 0.5));
    case XT_MATH_TRUNC: return xt_number(trunc(a));
    case XT_MATH_SQRT: return xt_number(sqrt(a));
    case XT_MATH_CBRT: return xt_number(cbrt(a));
    case XT_MATH_POW: return xt_number(pow(a, b));
    case XT_MATH_EXP: return xt_number(exp(a));
    case XT_MATH_LOG: return xt_number(log(a));
    case XT_MATH_LOG2: return xt_number(log2(a));
    case XT_MATH_LOG10: return xt_number(log10(a));
    case XT_MATH_LOG1P: return xt_number(log1p(a));
    case XT_MATH_EXPM1: return xt_number(expm1(a));
    case XT_MATH_SINH: return xt_number(sinh(a));
    case XT_MATH_COSH: return xt_number(cosh(a));
    case XT_MATH_TANH: return xt_number(tanh(a));
    case XT_MATH_ASINH: return xt_number(asinh(a));
    case XT_MATH_ACOSH: return xt_number(acosh(a));
    case XT_MATH_ATANH: return xt_number(atanh(a));
    case XT_MATH_FROUND: return xt_number((double)(float)a);
    case XT_MATH_IMUL:
      return xt_number((double)((int32_t)xt_to_int32(xt_arg_at(argc, argv, 0)) *
                                 (int32_t)xt_to_int32(xt_arg_at(argc, argv, 1))));
    case XT_MATH_CLZ32: {
      uint32_t v = (uint32_t)xt_to_int32(xt_arg_at(argc, argv, 0));
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
    case XT_MATH_SIN: return xt_number(sin(a));
    case XT_MATH_COS: return xt_number(cos(a));
    case XT_MATH_TAN: return xt_number(tan(a));
    case XT_MATH_ASIN: return xt_number(asin(a));
    case XT_MATH_ACOS: return xt_number(acos(a));
    case XT_MATH_ATAN: return xt_number(atan(a));
    case XT_MATH_ATAN2: return xt_number(atan2(a, b));
    case XT_MATH_HYPOT: return xt_number(hypot(a, b));
    case XT_MATH_SIGN:
      if (isnan(a)) return xt_number(NAN);
      return xt_number(a > 0 ? 1.0 : a < 0 ? -1.0 : a);
    case XT_MATH_RANDOM: return xt_number(xt_random_double());
    case XT_MATH_MIN: {
      double result = INFINITY;
      for (int32_t i = 0; i < argc; i++) {
        double v = xt_to_number(argv[i]);
        if (isnan(v)) return xt_number(NAN);
        if (v < result) result = v;
      }
      return xt_number(result);
    }
    case XT_MATH_MAX: {
      double result = -INFINITY;
      for (int32_t i = 0; i < argc; i++) {
        double v = xt_to_number(argv[i]);
        if (isnan(v)) return xt_number(NAN);
        if (v > result) result = v;
      }
      return xt_number(result);
    }
    default: return xt_number(NAN);
  }
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
