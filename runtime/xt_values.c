/*
 * xbintsc runtime — values.
 *
 * Constructors, the string representation, JS conversions, arithmetic and
 * comparison operators. All of these operate on the NaN-boxed `xt_value`.
 */

#include "rt_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Constructors                                                              */
/* ------------------------------------------------------------------------- */

xt_value xt_undefined(void) { return XT_UNDEFINED; }
xt_value xt_null(void) { return XT_NULL; }
xt_value xt_bool(int b) { return b ? XT_TRUE : XT_FALSE; }
xt_value xt_number(double d) { return xt_from_double(d); }

xt_string *xt_string_alloc(size_t length) {
  xt_string *s = (xt_string *)xt_alloc(sizeof(xt_string), XT_OBJECT_KIND_STRING);
  s->length = (uint32_t)length;
  s->capacity = (uint32_t)length + 1;
  s->data = (char *)malloc(s->capacity);
  if (!s->data) {
    fprintf(stderr, "xbintsc: out of memory allocating string\n");
    abort();
  }
  s->data[length] = '\0';
  return s;
}

xt_value xt_string_new(const char *data, size_t len) {
  xt_string *s = xt_string_alloc(len);
  if (len > 0) memcpy(s->data, data, len);
  return XT_FROM_PTR(XT_TAG_STRING, s);
}

xt_value xt_string_from_cstr(const char *data) { return xt_string_new(data, strlen(data)); }

xt_string *xt_as_string(xt_value v);

const char *xt_string_data(xt_value value) {
  xt_string *s = xt_as_string(value);
  return s ? s->data : NULL;
}

int32_t xt_string_length_value(xt_value value) {
  xt_string *s = xt_as_string(value);
  return s ? (int32_t)s->length : -1;
}

/* ------------------------------------------------------------------------- */
/* Strings                                                                   */
/* ------------------------------------------------------------------------- */

xt_string *xt_as_string(xt_value v) {
  if (XT_IS_STRING(v)) return (xt_string *)XT_GET_PTR(v);
  return NULL;
}

int32_t xt_string_length(xt_string *s) { return (int32_t)s->length; }

static xt_value xt_string_concat(xt_string *a, xt_string *b) {
  size_t total = (size_t)a->length + (size_t)b->length;
  xt_string *out = xt_string_alloc(total);
  memcpy(out->data, a->data, a->length);
  memcpy(out->data + a->length, b->data, b->length);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

int xt_string_equals(xt_string *a, xt_string *b) {
  return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}

/*
 * Format a double the way JavaScript's ToString does for the common cases:
 * integers print without a decimal point, everything else uses the shortest
 * representation that round-trips.
 */
static void xt_format_double(double d, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  if (d == 0.0) {
    snprintf(buffer, size, "0");
    return;
  }
  if (d == floor(d) && fabs(d) < 1e21) {
    snprintf(buffer, size, "%.0f", d);
    return;
  }
  for (int precision = 1; precision <= 17; precision++) {
    snprintf(buffer, size, "%.*g", precision, d);
    if (strtod(buffer, NULL) == d) return;
  }
}

/* ------------------------------------------------------------------------- */
/* Conversions                                                               */
/* ------------------------------------------------------------------------- */

int xt_truthy(xt_value v) {
  if (XT_IS_NUMBER(v)) {
    double d = xt_to_double(v);
    return d != 0.0 && !isnan(d);
  }
  if (v == XT_UNDEFINED || v == XT_NULL) return 0;
  if (v == XT_FALSE) return 0;
  if (v == XT_TRUE) return 1;
  if (XT_IS_BIGINT(v)) return XT_GET_PTR(v) != NULL && ((xt_bigint *)XT_GET_PTR(v))->sign != 0;
  if (XT_IS_STRING(v)) return xt_as_string(v)->length != 0;
  return 1; /* objects, arrays and functions are truthy */
}

double xt_to_number(xt_value v) {
  if (XT_IS_NUMBER(v)) return xt_to_double(v);
  if (v == XT_TRUE) return 1.0;
  if (v == XT_FALSE) return 0.0;
  if (v == XT_UNDEFINED) return NAN;
  if (v == XT_NULL) return 0.0;
  if (XT_IS_BIGINT(v)) return xt_bigint_to_double_value(v);
  if (XT_IS_STRING(v)) {
    xt_string *s = xt_as_string(v);
    if (s->length == 0) return 0.0;
    char *end = NULL;
    double parsed = strtod(s->data, &end);
    if (end != s->data + s->length) return NAN;
    return parsed;
  }
  return NAN;
}

static xt_value xt_number_to_string_value(double d) {
  char buffer[64];
  xt_format_double(d, buffer, sizeof(buffer));
  return xt_string_from_cstr(buffer);
}

xt_value xt_to_string(xt_value v) {
  if (XT_IS_STRING(v)) return v;
  if (XT_IS_NUMBER(v)) return xt_number_to_string_value(xt_to_double(v));
  if (v == XT_UNDEFINED) return xt_string_from_cstr("undefined");
  if (v == XT_NULL) return xt_string_from_cstr("null");
  if (v == XT_TRUE) return xt_string_from_cstr("true");
  if (v == XT_FALSE) return xt_string_from_cstr("false");
  if (XT_IS_BIGINT(v)) return xt_bigint_to_decimal(v);
  if (XT_IS_ARRAY(v)) {
    xt_array *a = (xt_array *)XT_GET_PTR(v);
    /* Join elements with commas, like Array.prototype.toString. */
    size_t capacity = 32;
    char *out = (char *)malloc(capacity);
    size_t length = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < a->length; i++) {
      xt_value s = xt_to_string(a->items[i]);
      xt_string *str = xt_as_string(s);
      if (length + str->length + 2 > capacity) {
        capacity = (length + str->length + 2) * 2;
        out = (char *)realloc(out, capacity);
      }
      if (i > 0) out[length++] = ',';
      memcpy(out + length, str->data, str->length);
      length += str->length;
      out[length] = '\0';
    }
    xt_value result = xt_string_new(out, length);
    free(out);
    return result;
  }
  if (XT_IS_FUNCTION(v)) return xt_string_from_cstr("function () { [native code] }");
  return xt_string_from_cstr("[object Object]");
}

xt_value xt_typeof(xt_value v) {
  if (XT_IS_NUMBER(v)) return xt_string_from_cstr("number");
  if (XT_IS_STRING(v)) return xt_string_from_cstr("string");
  if (v == XT_TRUE || v == XT_FALSE) return xt_string_from_cstr("boolean");
  if (XT_IS_BIGINT(v)) return xt_string_from_cstr("bigint");
  if (v == XT_UNDEFINED) return xt_string_from_cstr("undefined");
  if (XT_IS_FUNCTION(v)) return xt_string_from_cstr("function");
  if (v == XT_NULL) return xt_string_from_cstr("object");
  return xt_string_from_cstr("object");
}

/* ------------------------------------------------------------------------- */
/* Arithmetic                                                                */
/* ------------------------------------------------------------------------- */

static void xt_throw_mixed_bigint(void) {
  xt_throw(xt_string_from_cstr("TypeError: Cannot mix BigInt and other types, use explicit conversions"));
}

xt_value xt_add(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    return xt_number(xt_to_double(a) + xt_to_double(b));
  }
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_add(a, b);
  /* JavaScript's `+` concatenates when either operand is a string. */
  if (XT_IS_STRING(a) || XT_IS_STRING(b)) {
    xt_value sa = xt_to_string(a);
    xt_value sb = xt_to_string(b);
    return xt_string_concat(xt_as_string(sa), xt_as_string(sb));
  }
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) + xt_to_number(b));
}

xt_value xt_sub(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_sub(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) - xt_to_number(b));
}

xt_value xt_mul(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_mul(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) * xt_to_number(b));
}

xt_value xt_div(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_div(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) / xt_to_number(b));
}

xt_value xt_mod(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_mod(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  double x = xt_to_number(a);
  double y = xt_to_number(b);
  return xt_number(fmod(x, y));
}

xt_value xt_pow(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_pow(a, b);
  return xt_number(pow(xt_to_number(a), xt_to_number(b)));
}

xt_value xt_neg(xt_value a) {
  if (XT_IS_BIGINT(a)) return xt_bigint_neg(a);
  return xt_number(-xt_to_number(a));
}

xt_value xt_pos(xt_value a) {
  if (XT_IS_BIGINT(a)) return a;
  return xt_number(xt_to_number(a));
}

int32_t xt_to_int32(xt_value v) {
  double d = xt_to_number(v);
  if (isnan(d) || isinf(d)) return 0;
  return (int32_t)(uint32_t)(int64_t)d;
}

xt_value xt_bit_and(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_bit_and(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) & xt_to_int32(b)));
}

xt_value xt_bit_or(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_bit_or(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) | xt_to_int32(b)));
}

xt_value xt_bit_xor(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_bit_xor(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) ^ xt_to_int32(b)));
}

xt_value xt_bit_not(xt_value a) {
  if (XT_IS_BIGINT(a)) return xt_bigint_bit_not(a);
  return xt_number((double)(~xt_to_int32(a)));
}

xt_value xt_shl(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_shl(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(int32_t)((uint32_t)xt_to_int32(a) << (xt_to_int32(b) & 31)));
}

xt_value xt_shr(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_shr(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) >> (xt_to_int32(b) & 31)));
}

xt_value xt_ushr(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw(xt_string_from_cstr("TypeError: BigInts have no unsigned right shift, use >> instead"));
    return XT_UNDEFINED;
  }
  return xt_number((double)(uint32_t)((uint32_t)xt_to_int32(a) >> (xt_to_int32(b) & 31)));
}

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

