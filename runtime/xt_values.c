/*
 * xbintsc runtime — values.
 *
 * Constructors, the string representation, JS conversions, arithmetic and
 * comparison operators. All of these operate on the NaN-boxed `xt_value`.
 */

#include "rt_internal.h"

#include <math.h>
#include <ctype.h>
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
 * ECMAScript number formatting.
 *
 * A double is rendered as the shortest decimal string that round-trips, then
 * laid out with JavaScript's notation rules: fixed notation unless the decimal
 * exponent is < -6 or >= 21, and a bare (unpadded, explicitly signed) exponent.
 * The `toFixed`/`toExponential`/`toPrecision` variants reuse the same renderer.
 */
static void xt_render_decimal(int negative, const char *digits, int nd, int exp10, int expo, char *buffer, size_t size) {
  size_t o = 0;
  if (negative && o + 1 < size) buffer[o++] = '-';
  if (expo) {
    if (o + 1 < size) buffer[o++] = digits[0];
    if (nd > 1) {
      if (o + 1 < size) buffer[o++] = '.';
      for (int i = 1; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
    }
    int written = snprintf(buffer + o, size - o, "e%c%d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
    if (written > 0) o += (size_t)written;
  } else if (exp10 >= 0) {
    int intDigits = exp10 + 1;
    if (nd <= intDigits) {
      for (int i = 0; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
      for (int i = nd; i < intDigits && o + 1 < size; i++) buffer[o++] = '0';
    } else {
      for (int i = 0; i < intDigits && o + 1 < size; i++) buffer[o++] = digits[i];
      if (o + 1 < size) buffer[o++] = '.';
      for (int i = intDigits; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
    }
  } else {
    if (o + 1 < size) buffer[o++] = '0';
    if (o + 1 < size) buffer[o++] = '.';
    for (int i = 0; i < -exp10 - 1 && o + 1 < size; i++) buffer[o++] = '0';
    for (int i = 0; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
  }
  buffer[o] = '\0';
}

/* Split a `%e`-formatted string into its significant digits and exponent. */
static void xt_parse_scientific(const char *sci, char *digits, int *nd, int *exp10) {
  int n = 0;
  *exp10 = 0;
  const char *q = sci;
  while (*q && *q != 'e' && *q != 'E') {
    if (*q >= '0' && *q <= '9') digits[n++] = *q;
    q++;
  }
  if (*q == 'e' || *q == 'E') *exp10 = atoi(q + 1);
  *nd = n;
}

void xt_number_to_js_string(double d, char *buffer, size_t size) {
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
  int negative = d < 0;
  double x = negative ? -d : d;
  char sci[64];
  for (int p = 1; p <= 17; p++) {
    snprintf(sci, sizeof(sci), "%.*e", p - 1, x);
    if (strtod(sci, NULL) == x) break;
  }
  char digits[32];
  int nd = 0;
  int exp10 = 0;
  xt_parse_scientific(sci, digits, &nd, &exp10);
  while (nd > 1 && digits[nd - 1] == '0') nd--;
  xt_render_decimal(negative, digits, nd, exp10, exp10 < -6 || exp10 >= 21, buffer, size);
}

void xt_number_to_exponential(double d, int precision, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  int negative = d < 0;
  double x = negative ? -d : d;
  char sci[64];
  if (precision < 0) {
    /* No fractionDigits argument: use the shortest round-tripping form. */
    for (int p = 1; p <= 17; p++) {
      snprintf(sci, sizeof(sci), "%.*e", p - 1, x);
      if (strtod(sci, NULL) == x) break;
    }
  } else {
    snprintf(sci, sizeof(sci), "%.*e", precision, x);
  }
  char digits[64];
  int nd = 0;
  int exp10 = 0;
  xt_parse_scientific(sci, digits, &nd, &exp10);
  if (precision < 0) while (nd > 1 && digits[nd - 1] == '0') nd--;
  xt_render_decimal(negative, digits, nd, exp10, 1, buffer, size);
}

void xt_number_to_precision(double d, int precision, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  int negative = d < 0;
  double x = negative ? -d : d;
  char sci[512];
  snprintf(sci, sizeof(sci), "%.*e", precision - 1, x);
  char digits[512];
  int nd = 0;
  int exp10 = 0;
  xt_parse_scientific(sci, digits, &nd, &exp10);
  xt_render_decimal(negative, digits, nd, exp10, exp10 < -6 || exp10 >= precision, buffer, size);
}

void xt_number_to_fixed(double d, int precision, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  if (fabs(d) >= 1e21) {
    xt_number_to_js_string(d, buffer, size);
    return;
  }
  int negative = d < 0;
  double x = negative ? -d : d;
  /* 20 guard digits let us round exactly half away from zero, which is what
     ECMAScript specifies and what `printf`'s round-half-to-even does not. */
  char raw[1024];
  snprintf(raw, sizeof(raw), "%.*f", precision + 20, x);
  const char *dot = strchr(raw, '.');
  int intLen = dot ? (int)(dot - raw) : (int)strlen(raw);
  int len = intLen;
  if (precision > 0) len += 1 + precision;
  if (len >= (int)sizeof(raw)) len = (int)sizeof(raw) - 1;
  char work[1024];
  memcpy(work, raw, (size_t)len);
  work[len] = '\0';
  /* The digit that decides rounding is the first one we dropped. With a
     precision of zero that is the character after the decimal point. */
  int firstDropped = precision > 0 ? len : (dot ? intLen + 1 : len);
  if (dot && raw[firstDropped] >= '5') {
    int i = len - 1;
    while (i >= 0) {
      if (work[i] == '.') {
        i--;
        continue;
      }
      if (work[i] < '9') {
        work[i]++;
        break;
      }
      work[i] = '0';
      i--;
    }
    if (i < 0) {
      memmove(work + 1, work, strlen(work) + 1);
      work[0] = '1';
    }
  }
  if (negative) snprintf(buffer, size, "-%s", work);
  else snprintf(buffer, size, "%s", work);
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
    const char *begin = s->data;
    const char *end = s->data + s->length;
    while (begin < end && isspace((unsigned char)*begin)) begin++;
    while (end > begin && isspace((unsigned char)end[-1])) end--;
    size_t length = (size_t)(end - begin);
    if (length == 0) return 0.0;
    char stackbuf[128];
    char *tmp = length < sizeof(stackbuf) ? stackbuf : (char *)malloc(length + 1);
    memcpy(tmp, begin, length);
    tmp[length] = '\0';
    double result;
    int base = 0;
    if (length > 2 && tmp[0] == '0' && (tmp[1] == 'x' || tmp[1] == 'X')) base = 16;
    else if (length > 2 && tmp[0] == '0' && (tmp[1] == 'o' || tmp[1] == 'O')) base = 8;
    else if (length > 2 && tmp[0] == '0' && (tmp[1] == 'b' || tmp[1] == 'B')) base = 2;
    if (base != 0) {
      char *stop = NULL;
      unsigned long long parsed = strtoull(tmp + 2, &stop, base);
      result = (*stop == '\0' && stop != tmp + 2) ? (double)parsed : NAN;
    } else {
      char *stop = NULL;
      result = strtod(tmp, &stop);
      if (stop != tmp + length) result = NAN;
    }
    if (tmp != stackbuf) free(tmp);
    return result;
  }
  return NAN;
}

static xt_value xt_number_to_string_value(double d) {
  char buffer[64];
  xt_number_to_js_string(d, buffer, sizeof(buffer));
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
  if (xt_is_symbol(v)) {
    xt_throw(xt_string_from_cstr("TypeError: Cannot convert a Symbol value to a string"));
  }
  if (XT_IS_OBJECT(v) &&
      ((xt_object *)XT_GET_PTR(v))->header.kind == XT_OBJECT_KIND_ERROR) {
    return xt_error_to_string(v);
  }
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
  if (xt_is_symbol(v)) return xt_string_from_cstr("symbol");
  return xt_string_from_cstr("object");
}

/* ------------------------------------------------------------------------- */
/* Arithmetic                                                                */
/* ------------------------------------------------------------------------- */

static void xt_throw_mixed_bigint(void) {
  xt_throw(xt_string_from_cstr("TypeError: Cannot mix BigInt and other types, use explicit conversions"));
}

/* ToPrimitive for the operators: arrays/objects/functions stringify (their
   `valueOf` is the object itself, so `toString` decides). */
static int xt_is_object_like(xt_value v) { return XT_IS_OBJECT(v) || XT_IS_ARRAY(v) || XT_IS_FUNCTION(v); }

static xt_value xt_to_primitive(xt_value v) { return xt_is_object_like(v) ? xt_to_string(v) : v; }

xt_value xt_add(xt_value a, xt_value b) {
  a = xt_to_primitive(a);
  b = xt_to_primitive(b);
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

