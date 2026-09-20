/*
 * xbintsc runtime — extended standard library.
 *
 * Additional Array/String/Number/Object methods and statics, JSON, and basic
 * Map / Set / Date / RegExp implementations.
 */

#include "rt_internal.h"

#include <ctype.h>
#include <math.h>
#if defined(_WIN32)
#include "xt_regex.h"
#else
#include <regex.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * `timegm` and `gmtime_r` are POSIX extensions that MSVC's CRT does not
 * provide. Wrap the platform-specific equivalents so the Date implementation
 * below compiles everywhere.
 */
#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
static time_t xt_timegm(struct tm *tm) { return (time_t)_mkgmtime64(tm); }
static struct tm *xt_gmtime_r(const time_t *timer, struct tm *buf) {
  return gmtime_s(buf, timer) == 0 ? buf : NULL;
}
#else
static time_t xt_timegm(struct tm *tm) { return timegm(tm); }
static struct tm *xt_gmtime_r(const time_t *timer, struct tm *buf) { return gmtime_r(timer, buf); }
#endif

int xt_value_equals(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    double x = xt_to_double(a);
    double y = xt_to_double(b);
    if (isnan(x) && isnan(y)) return 1;
    return x == y;
  }
  return xt_truthy(xt_seq(a, b));
}

static xt_value xt_number_to_radix(double value, int radix) {
  if (radix < 2 || radix > 36 || value != floor(value) || fabs(value) > 9.0e15) {
    return xt_to_string(xt_number(value));
  }
  int negative = value < 0;
  unsigned long long n = (unsigned long long)(negative ? -value : value);
  static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  char buffer[80];
  int pos = 0;
  if (n == 0) buffer[pos++] = '0';
  while (n > 0) { buffer[pos++] = digits[n % (unsigned)radix]; n /= (unsigned)radix; }
  char out[80];
  int o = 0;
  if (negative) out[o++] = '-';
  while (pos > 0) out[o++] = buffer[--pos];
  return xt_string_new(out, (size_t)o);
}

/* -- array methods -------------------------------------------------------- */

static xt_value xt_array_at(xt_value value, xt_value indexValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  int32_t index = indexValue == XT_UNDEFINED ? 0 : xt_to_int32(indexValue);
  if (index < 0) index += (int32_t)array->length;
  if (index < 0 || (uint32_t)index >= array->length) return XT_UNDEFINED;
  return array->items[index];
}

static xt_value xt_array_find(xt_value value, xt_value fn, int mode) {
  xt_array *array = xt_as_array(value);
  if (!array) return (mode == 0 || mode == 2) ? XT_UNDEFINED : xt_number(-1);
  int reverse = (mode == 2 || mode == 3);
  for (uint32_t step = 0; step < array->length; step++) {
    int32_t i = reverse ? (int32_t)(array->length - 1 - step) : (int32_t)step;
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    if (xt_truthy(xt_closure_call(fn, 3, args))) return (mode == 0 || mode == 2) ? array->items[i] : xt_number((double)i);
  }
  return (mode == 0 || mode == 2) ? XT_UNDEFINED : xt_number(-1);
}

static xt_value xt_array_quantifier(xt_value value, xt_value fn, int every) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_bool(every ? 1 : 0);
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    int ok = xt_truthy(xt_closure_call(fn, 3, args));
    if (every && !ok) return XT_FALSE;
    if (!every && ok) return XT_TRUE;
  }
  return xt_bool(every ? 1 : 0);
}

static int xt_array_compare_values(xt_value a, xt_value b) {
  if (xt_truthy(xt_gt(a, b))) return 1;
  if (xt_truthy(xt_lt(a, b))) return -1;
  return 0;
}

static xt_value xt_array_sort(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length < 2) return value;
  int hasFn = XT_IS_FUNCTION(fn);
  for (uint32_t i = 1; i < array->length; i++) {
    xt_value key = array->items[i];
    int32_t j = (int32_t)i - 1;
    while (j >= 0) {
      int cmp;
      if (hasFn) {
        xt_value args[2] = {array->items[j], key};
        cmp = (int)xt_to_number(xt_closure_call(fn, 2, args));
      } else {
        cmp = xt_array_compare_values(array->items[j], key);
      }
      if (cmp <= 0) break;
      array->items[j + 1] = array->items[j];
      j--;
    }
    array->items[j + 1] = key;
  }
  return value;
}

static void xt_array_flatten_into(xt_value out, xt_value value, int depth) {
  xt_array *array = xt_as_array(value);
  if (!array) { xt_array_push(out, value); return; }
  for (uint32_t i = 0; i < array->length; i++) {
    if (depth > 0 && XT_IS_ARRAY(array->items[i])) xt_array_flatten_into(out, array->items[i], depth - 1);
    else xt_array_push(out, array->items[i]);
  }
}

static xt_value xt_array_flat_map(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  xt_value out = xt_array_new(0, NULL);
  if (!array) return out;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    xt_value mapped = xt_closure_call(fn, 3, args);
    if (XT_IS_ARRAY(mapped)) {
      xt_array *m = xt_as_array(mapped);
      for (uint32_t j = 0; j < m->length; j++) xt_array_push(out, m->items[j]);
    } else xt_array_push(out, mapped);
  }
  return out;
}

static xt_value xt_array_last_index_of(xt_value value, xt_value needle, xt_value fromValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_number(-1);
  int32_t from = fromValue == XT_UNDEFINED ? (int32_t)array->length - 1 : xt_to_int32(fromValue);
  if (from < 0) from += (int32_t)array->length;
  if (from >= (int32_t)array->length) from = (int32_t)array->length - 1;
  for (int32_t i = from; i >= 0; i--) if (xt_value_equals(array->items[i], needle)) return xt_number((double)i);
  return xt_number(-1);
}

static xt_value xt_array_fill(xt_value value, xt_value fillValue, xt_value startValue, xt_value endValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return value;
  int32_t len = (int32_t)array->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  if (start < 0) start += len;
  if (end < 0) end += len;
  if (start < 0) start = 0;
  if (end > len) end = len;
  for (int32_t i = start; i < end; i++) array->items[i] = fillValue;
  return value;
}

static xt_value xt_array_copy_within(xt_value value, xt_value targetValue, xt_value startValue, xt_value endValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return value;
  int32_t len = (int32_t)array->length;
  int32_t target = xt_to_int32(targetValue == XT_UNDEFINED ? xt_number(0) : targetValue);
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  if (target < 0) target += len;
  if (start < 0) start += len;
  if (end < 0) end += len;
  if (target < 0 || start < 0 || target >= len || start >= len) return value;
  if (end > len) end = len;
  int32_t count = end - start;
  if (count > len - target) count = len - target;
  if (count <= 0) return value;
  memmove(array->items + target, array->items + start, sizeof(xt_value) * (size_t)count);
  return value;
}

xt_value xt_ext_array_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "at") == 0) return xt_array_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "find") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 0);
  if (strcmp(method, "findIndex") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 1);
  if (strcmp(method, "findLast") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 2);
  if (strcmp(method, "findLastIndex") == 0) return xt_array_find(target, xt_arg_at(argc, argv, 0), 3);
  if (strcmp(method, "some") == 0) return xt_array_quantifier(target, xt_arg_at(argc, argv, 0), 0);
  if (strcmp(method, "every") == 0) return xt_array_quantifier(target, xt_arg_at(argc, argv, 0), 1);
  if (strcmp(method, "sort") == 0) return xt_array_sort(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "flat") == 0) {
    double depth = xt_arg_at(argc, argv, 0) == XT_UNDEFINED ? 1 : xt_to_number(argv[0]);
    if (depth < 0) depth = 0;
    xt_value out = xt_array_new(0, NULL);
    xt_array_flatten_into(out, target, (int)depth);
    return out;
  }
  if (strcmp(method, "flatMap") == 0) return xt_array_flat_map(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "lastIndexOf") == 0) return xt_array_last_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "fill") == 0) return xt_array_fill(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), xt_arg_at(argc, argv, 2));
  if (strcmp(method, "copyWithin") == 0) return xt_array_copy_within(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), xt_arg_at(argc, argv, 2));
  if (strcmp(method, "reduceRight") == 0) {
    xt_array *array = xt_as_array(target);
    if (!array) return XT_UNDEFINED;
    xt_value fn = xt_arg_at(argc, argv, 0);
    int32_t i = (int32_t)array->length - 1;
    xt_value acc;
    if (argc >= 2) acc = argv[1];
    else if (array->length > 0) acc = array->items[i--];
    else { xt_throw(xt_string_from_cstr("TypeError: Reduce of empty array with no initial value")); return XT_UNDEFINED; }
    for (; i >= 0; i--) {
      xt_value args[4] = {acc, array->items[i], xt_number((double)i), target};
      acc = xt_closure_call(fn, 4, args);
    }
    return acc;
  }
  if (strcmp(method, "toString") == 0) return xt_call_method(target, xt_string_from_cstr("join"), 0, NULL);
  if (strcmp(method, "keys") == 0 || strcmp(method, "values") == 0 || strcmp(method, "entries") == 0) {
    xt_array *array = xt_as_array(target);
    xt_value out = xt_array_new(0, NULL);
    if (!array) return out;
    int key = strcmp(method, "keys") == 0;
    int entries = strcmp(method, "entries") == 0;
    for (uint32_t i = 0; i < array->length; i++) {
      if (entries) {
        xt_value pair = xt_array_new(0, NULL);
        xt_array_push(pair, xt_number((double)i));
        xt_array_push(pair, array->items[i]);
        xt_array_push(out, pair);
      } else if (key) xt_array_push(out, xt_number((double)i));
      else xt_array_push(out, array->items[i]);
    }
    return out;
  }
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- string methods ------------------------------------------------------- */

static xt_value xt_string_pad(xt_value value, xt_value lengthValue, xt_value padValue, int start) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  int32_t target = xt_to_int32(lengthValue);
  if (target <= (int32_t)s->length) return value;
  xt_string *pad = xt_as_string(xt_to_string(padValue == XT_UNDEFINED ? xt_string_from_cstr(" ") : padValue));
  if (!pad || pad->length == 0) return value;
  size_t total = (size_t)target;
  xt_string *out = xt_string_alloc(total);
  size_t needed = total - s->length;
  if (start) {
    for (size_t i = 0; i < needed; i++) out->data[i] = pad->data[i % pad->length];
    memcpy(out->data + needed, s->data, s->length);
  } else {
    memcpy(out->data, s->data, s->length);
    for (size_t i = 0; i < needed; i++) out->data[s->length + i] = pad->data[i % pad->length];
  }
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_trim_side(xt_value value, int start) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  uint32_t begin = 0;
  uint32_t end = s->length;
  if (start) { while (begin < end && isspace((unsigned char)s->data[begin])) begin++; }
  else { while (end > begin && isspace((unsigned char)s->data[end - 1])) end--; }
  return xt_string_new(s->data + begin, end - begin);
}

static xt_value xt_string_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_UNDEFINED;
  int32_t index = indexValue == XT_UNDEFINED ? 0 : xt_to_int32(indexValue);
  if (index < 0) index += (int32_t)s->length;
  if (index < 0 || (uint32_t)index >= s->length) return XT_UNDEFINED;
  return xt_string_new(s->data + index, 1);
}

static xt_value xt_string_replace_all(xt_value value, xt_value searchValue, xt_value replacementValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  xt_string *replacement = xt_as_string(xt_to_string(replacementValue));
  if (!search || search->length == 0) return value;
  size_t capacity = s->length + replacement->length * 4 + 16;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t length = 0;
  uint32_t i = 0;
  while (i < s->length) {
    if (i + search->length <= s->length && memcmp(s->data + i, search->data, search->length) == 0) {
      if (length + replacement->length + 2 > capacity) { capacity = (length + replacement->length + 2) * 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
      memcpy(out + length, replacement->data, replacement->length);
      length += replacement->length;
      i += search->length;
    } else {
      if (length + 2 > capacity) { capacity *= 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
      out[length++] = s->data[i++];
    }
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

xt_value xt_ext_string_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "at") == 0) return xt_string_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "padStart") == 0) return xt_string_pad(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 1);
  if (strcmp(method, "padEnd") == 0) return xt_string_pad(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 0);
  if (strcmp(method, "trimStart") == 0) return xt_string_trim_side(target, 1);
  if (strcmp(method, "trimEnd") == 0) return xt_string_trim_side(target, 0);
  if (strcmp(method, "replaceAll") == 0) return xt_string_replace_all(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "localeCompare") == 0) {
    xt_string *a = xt_as_string(target);
    xt_string *b = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
    int cmp = strcmp(a ? a->data : "", b ? b->data : "");
    return xt_number(cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
  }
  if (strcmp(method, "codePointAt") == 0) {
    xt_string *s = xt_as_string(target);
    if (!s) return XT_UNDEFINED;
    int32_t index = xt_to_int32(xt_arg_at(argc, argv, 0));
    if (index < 0 || (uint32_t)index >= s->length) return XT_UNDEFINED;
    return xt_number((double)(unsigned char)s->data[index]);
  }
  if (strcmp(method, "valueOf") == 0 || strcmp(method, "toString") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- number methods ------------------------------------------------------- */

xt_value xt_ext_number_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  double n = xt_to_double(target);
  if (strcmp(method, "toFixed") == 0) {
    int precision = argc >= 1 ? xt_to_int32(argv[0]) : 0;
    if (precision < 0) precision = 0;
    if (precision > 100) precision = 100;
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%.*f", precision, n);
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toPrecision") == 0) {
    if (argc == 0 || argv[0] == XT_UNDEFINED) return xt_to_string(target);
    int precision = xt_to_int32(argv[0]);
    if (precision < 1) precision = 1;
    if (precision > 100) precision = 100;
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%.*g", precision, n);
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toExponential") == 0) {
    int precision = argc >= 1 ? xt_to_int32(argv[0]) : 6;
    if (precision < 0) precision = 0;
    if (precision > 100) precision = 100;
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%.*e", precision, n);
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toString") == 0) {
    int radix = argc >= 1 ? xt_to_int32(argv[0]) : 10;
    return xt_number_to_radix(n, radix);
  }
  if (strcmp(method, "valueOf") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}

xt_value xt_ext_object_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "hasOwnProperty") == 0) return xt_object_has_own(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "toString") == 0) return xt_string_from_cstr("[object Object]");
  if (strcmp(method, "valueOf") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- Object statics ------------------------------------------------------- */

xt_value xt_object_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "keys") == 0) return xt_object_keys(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "values") == 0) return xt_object_values(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "entries") == 0) return xt_object_entries(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "assign") == 0) return xt_object_assign(argc, argv);
  if (strcmp(fn, "freeze") == 0) return xt_object_freeze(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "isFrozen") == 0) return xt_bool(xt_object_is_frozen(xt_arg_at(argc, argv, 0)));
  if (strcmp(fn, "fromEntries") == 0) return xt_object_from_entries(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "getPrototypeOf") == 0) return xt_object_get_prototype(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "setPrototypeOf") == 0) return xt_object_set_prototype(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(fn, "hasOwn") == 0) return xt_object_has_own(xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(fn, "is") == 0) {
    xt_value a = xt_arg_at(argc, argv, 0);
    xt_value b = xt_arg_at(argc, argv, 1);
    if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
      double x = xt_to_double(a), y = xt_to_double(b);
      if (isnan(x) && isnan(y)) return XT_TRUE;
      if (x == 0 && y == 0) return xt_bool(signbit(x) == signbit(y));
      return xt_bool(x == y);
    }
    return xt_seq(a, b);
  }
  if (strcmp(fn, "create") == 0) {
    xt_value proto = xt_arg_at(argc, argv, 0);
    return xt_object_new_with_proto(proto == XT_NULL ? XT_UNDEFINED : proto);
  }
  return XT_UNDEFINED;
}

/* -- Array statics -------------------------------------------------------- */

xt_value xt_array_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "isArray") == 0) return xt_bool(XT_IS_ARRAY(xt_arg_at(argc, argv, 0)));
  if (strcmp(fn, "of") == 0) return xt_array_new(argc, argv);
  if (strcmp(fn, "from") == 0) {
    xt_value source = xt_arg_at(argc, argv, 0);
    xt_value mapper = xt_arg_at(argc, argv, 1);
    xt_value out = xt_array_new(0, NULL);
    if (XT_IS_ARRAY(source)) {
      xt_array *array = xt_as_array(source);
      for (uint32_t i = 0; i < array->length; i++) {
        xt_value item = array->items[i];
        if (XT_IS_FUNCTION(mapper)) { xt_value args[2] = {item, xt_number((double)i)}; item = xt_closure_call(mapper, 2, args); }
        xt_array_push(out, item);
      }
    } else if (XT_IS_STRING(source)) {
      xt_string *s = xt_as_string(source);
      for (uint32_t i = 0; i < s->length; i++) {
        xt_value item = xt_string_new(s->data + i, 1);
        if (XT_IS_FUNCTION(mapper)) { xt_value args[2] = {item, xt_number((double)i)}; item = xt_closure_call(mapper, 2, args); }
        xt_array_push(out, item);
      }
    } else {
      double length = xt_to_number(xt_get(source, xt_string_from_cstr("length")));
      for (int32_t i = 0; i < (int32_t)length; i++) {
        xt_value item = xt_get(source, xt_number((double)i));
        if (XT_IS_FUNCTION(mapper)) { xt_value args[2] = {item, xt_number((double)i)}; item = xt_closure_call(mapper, 2, args); }
        xt_array_push(out, item);
      }
    }
    return out;
  }
  return XT_UNDEFINED;
}

/* -- Number / String statics ---------------------------------------------- */

xt_value xt_number_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "isInteger") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    double d = xt_to_double(v);
    return xt_bool(isfinite(d) && floor(d) == d);
  }
  if (strcmp(fn, "isSafeInteger") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    double d = xt_to_double(v);
    return xt_bool(isfinite(d) && floor(d) == d && fabs(d) <= 9007199254740991.0);
  }
  if (strcmp(fn, "isFinite") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    return xt_bool(isfinite(xt_to_double(v)));
  }
  if (strcmp(fn, "isNaN") == 0) {
    xt_value v = xt_arg_at(argc, argv, 0);
    if (!XT_IS_NUMBER(v)) return XT_FALSE;
    return xt_bool(isnan(xt_to_double(v)));
  }
  if (strcmp(fn, "parseFloat") == 0) return xt_parse_float(argc, argv);
  if (strcmp(fn, "parseInt") == 0) return xt_parse_int(argc, argv);
  if (strcmp(fn, "MAX_SAFE_INTEGER") == 0) return xt_number(9007199254740991.0);
  if (strcmp(fn, "MIN_SAFE_INTEGER") == 0) return xt_number(-9007199254740991.0);
  if (strcmp(fn, "MAX_VALUE") == 0) return xt_number(1.7976931348623157e308);
  if (strcmp(fn, "MIN_VALUE") == 0) return xt_number(5e-324);
  if (strcmp(fn, "EPSILON") == 0) return xt_number(2.220446049250313e-16);
  if (strcmp(fn, "POSITIVE_INFINITY") == 0) return xt_number(INFINITY);
  if (strcmp(fn, "NEGATIVE_INFINITY") == 0) return xt_number(-INFINITY);
  if (strcmp(fn, "NaN") == 0) return xt_number(NAN);
  return XT_UNDEFINED;
}

xt_value xt_string_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "fromCharCode") == 0 || strcmp(fn, "fromCodePoint") == 0) {
    xt_string *out = xt_string_alloc((size_t)argc * 4 + 1);
    size_t length = 0;
    for (int32_t i = 0; i < argc; i++) {
      unsigned code = (unsigned)xt_to_int32(argv[i]);
      char buffer[4];
      int bytes = 0;
      if (code < 0x80) buffer[bytes++] = (char)code;
      else if (code < 0x800) { buffer[bytes++] = (char)(0xC0 | (code >> 6)); buffer[bytes++] = (char)(0x80 | (code & 0x3F)); }
      else if (code < 0x10000) { buffer[bytes++] = (char)(0xE0 | (code >> 12)); buffer[bytes++] = (char)(0x80 | ((code >> 6) & 0x3F)); buffer[bytes++] = (char)(0x80 | (code & 0x3F)); }
      else { buffer[bytes++] = (char)(0xF0 | (code >> 18)); buffer[bytes++] = (char)(0x80 | ((code >> 12) & 0x3F)); buffer[bytes++] = (char)(0x80 | ((code >> 6) & 0x3F)); buffer[bytes++] = (char)(0x80 | (code & 0x3F)); }
      memcpy(out->data + length, buffer, (size_t)bytes);
      length += (size_t)bytes;
    }
    out->length = (uint32_t)length;
    return XT_FROM_PTR(XT_TAG_STRING, out);
  }
  if (strcmp(fn, "raw") == 0) {
    xt_value strings = xt_arg_at(argc, argv, 0);
    xt_value raw = xt_object_get(strings, xt_string_from_cstr("raw"));
    xt_value out = xt_string_from_cstr("");
    double length = xt_to_number(xt_get(raw, xt_string_from_cstr("length")));
    for (int32_t i = 0; i < (int32_t)length; i++) {
      out = xt_add(out, xt_get(raw, xt_number((double)i)));
      if (i + 1 < (int32_t)length && i + 1 < argc) out = xt_add(out, xt_to_string(argv[i + 1]));
    }
    return out;
  }
  return XT_UNDEFINED;
}

/* -- JSON ----------------------------------------------------------------- */

typedef struct { const char *data; size_t length; size_t pos; } xt_json_cursor;

static void xt_json_skip_ws(xt_json_cursor *c) {
  while (c->pos < c->length) {
    char ch = c->data[c->pos];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') c->pos++;
    else break;
  }
}

static xt_value xt_json_parse_value(xt_json_cursor *c);

static xt_value xt_json_parse_string(xt_json_cursor *c) {
  c->pos++;
  size_t capacity = 32, length = 0;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  while (c->pos < c->length) {
    char ch = c->data[c->pos++];
    if (ch == '"') break;
    if (ch == '\\' && c->pos < c->length) {
      char esc = c->data[c->pos++];
      if (esc == 'n') ch = '\n';
      else if (esc == 't') ch = '\t';
      else if (esc == 'r') ch = '\r';
      else if (esc == 'b') ch = '\b';
      else if (esc == 'f') ch = '\f';
      else if (esc == 'u') {
        unsigned code = 0;
        for (int i = 0; i < 4 && c->pos < c->length; i++) {
          char h = c->data[c->pos++];
          code <<= 4;
          if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
          else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
        }
        if (code >= 0x80) {
          char tmp[4];
          int bytes = 0;
          if (code < 0x800) { tmp[bytes++] = (char)(0xC0 | (code >> 6)); tmp[bytes++] = (char)(0x80 | (code & 0x3F)); }
          else { tmp[bytes++] = (char)(0xE0 | (code >> 12)); tmp[bytes++] = (char)(0x80 | ((code >> 6) & 0x3F)); tmp[bytes++] = (char)(0x80 | (code & 0x3F)); }
          if (length + (size_t)bytes + 1 > capacity) { capacity = (length + (size_t)bytes + 1) * 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
          memcpy(out + length, tmp, (size_t)bytes);
          length += (size_t)bytes;
          continue;
        }
        ch = (char)code;
      } else ch = esc;
    }
    if (length + 2 > capacity) { capacity *= 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
    out[length++] = ch;
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

static xt_value xt_json_parse_number(xt_json_cursor *c) {
  size_t start = c->pos;
  if (c->pos < c->length && (c->data[c->pos] == '-' || c->data[c->pos] == '+')) c->pos++;
  while (c->pos < c->length && (isdigit((unsigned char)c->data[c->pos]) || c->data[c->pos] == '.' ||
                                c->data[c->pos] == 'e' || c->data[c->pos] == 'E' ||
                                c->data[c->pos] == '+' || c->data[c->pos] == '-')) c->pos++;
  char buffer[64];
  size_t n = c->pos - start;
  if (n >= sizeof(buffer)) n = sizeof(buffer) - 1;
  memcpy(buffer, c->data + start, n);
  buffer[n] = '\0';
  return xt_number(strtod(buffer, NULL));
}

static xt_value xt_json_parse_value(xt_json_cursor *c) {
  xt_json_skip_ws(c);
  if (c->pos >= c->length) return XT_UNDEFINED;
  char ch = c->data[c->pos];
  if (ch == '"') return xt_json_parse_string(c);
  if (ch == '{') {
    c->pos++;
    xt_value object = xt_object_new();
    xt_json_skip_ws(c);
    if (c->pos < c->length && c->data[c->pos] == '}') { c->pos++; return object; }
    for (;;) {
      xt_json_skip_ws(c);
      if (c->pos >= c->length || c->data[c->pos] != '"') break;
      xt_value key = xt_json_parse_string(c);
      xt_json_skip_ws(c);
      if (c->pos < c->length && c->data[c->pos] == ':') c->pos++;
      xt_value value = xt_json_parse_value(c);
      xt_object_set(object, key, value);
      xt_json_skip_ws(c);
      if (c->pos < c->length && c->data[c->pos] == ',') { c->pos++; continue; }
      if (c->pos < c->length && c->data[c->pos] == '}') c->pos++;
      break;
    }
    return object;
  }
  if (ch == '[') {
    c->pos++;
    xt_value array = xt_array_new(0, NULL);
    xt_json_skip_ws(c);
    if (c->pos < c->length && c->data[c->pos] == ']') { c->pos++; return array; }
    for (;;) {
      xt_array_push(array, xt_json_parse_value(c));
      xt_json_skip_ws(c);
      if (c->pos < c->length && c->data[c->pos] == ',') { c->pos++; continue; }
      if (c->pos < c->length && c->data[c->pos] == ']') c->pos++;
      break;
    }
    return array;
  }
  if (c->pos + 4 <= c->length && memcmp(c->data + c->pos, "true", 4) == 0) { c->pos += 4; return XT_TRUE; }
  if (c->pos + 5 <= c->length && memcmp(c->data + c->pos, "false", 5) == 0) { c->pos += 5; return XT_FALSE; }
  if (c->pos + 4 <= c->length && memcmp(c->data + c->pos, "null", 4) == 0) { c->pos += 4; return XT_NULL; }
  return xt_json_parse_number(c);
}

xt_value xt_json_parse(int32_t argc, xt_value *argv) {
  xt_value input = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_string *s = xt_as_string(input);
  if (!s) return XT_UNDEFINED;
  xt_json_cursor cursor = {s->data, s->length, 0};
  return xt_json_parse_value(&cursor);
}

static void xt_json_stringify_value(xt_value value, int indent, int depth, xt_value *out);

static void xt_json_indent(xt_value *out, int indent, int depth) {
  if (indent <= 0) return;
  *out = xt_add(*out, xt_string_from_cstr("\n"));
  for (int i = 0; i < indent * depth; i++) *out = xt_add(*out, xt_string_from_cstr(" "));
}

static void xt_json_stringify_array(xt_value value, int indent, int depth, xt_value *out) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length == 0) { *out = xt_add(*out, xt_string_from_cstr("[]")); return; }
  *out = xt_add(*out, xt_string_from_cstr("["));
  for (uint32_t i = 0; i < array->length; i++) {
    if (i > 0) *out = xt_add(*out, xt_string_from_cstr(","));
    xt_json_indent(out, indent, depth + 1);
    xt_json_stringify_value(array->items[i], indent, depth + 1, out);
  }
  xt_json_indent(out, indent, depth);
  *out = xt_add(*out, xt_string_from_cstr("]"));
}

static void xt_json_stringify_object(xt_value value, int indent, int depth, xt_value *out) {
  xt_value keys = xt_object_keys(value);
  xt_array *array = xt_as_array(keys);
  if (!array || array->length == 0) { *out = xt_add(*out, xt_string_from_cstr("{}")); return; }
  *out = xt_add(*out, xt_string_from_cstr("{"));
  for (uint32_t i = 0; i < array->length; i++) {
    if (i > 0) *out = xt_add(*out, xt_string_from_cstr(","));
    xt_json_indent(out, indent, depth + 1);
    xt_value key = array->items[i];
    *out = xt_add(*out, xt_string_from_cstr("\""));
    *out = xt_add(*out, key);
    *out = xt_add(*out, xt_string_from_cstr("\":"));
    if (indent > 0) *out = xt_add(*out, xt_string_from_cstr(" "));
    xt_json_stringify_value(xt_object_get(value, key), indent, depth + 1, out);
  }
  xt_json_indent(out, indent, depth);
  *out = xt_add(*out, xt_string_from_cstr("}"));
}

static void xt_json_stringify_value(xt_value value, int indent, int depth, xt_value *out) {
  if (value == XT_UNDEFINED || value == XT_NULL || XT_IS_FUNCTION(value)) {
    *out = xt_add(*out, xt_string_from_cstr("null"));
    return;
  }
  if (XT_IS_BOOL(value)) { *out = xt_add(*out, value == XT_TRUE ? xt_string_from_cstr("true") : xt_string_from_cstr("false")); return; }
  if (XT_IS_NUMBER(value)) {
    double d = xt_to_double(value);
    if (!isfinite(d)) *out = xt_add(*out, xt_string_from_cstr("null"));
    else *out = xt_add(*out, xt_to_string(value));
    return;
  }
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    *out = xt_add(*out, xt_string_from_cstr("\""));
    for (uint32_t i = 0; i < s->length; i++) {
      char ch = s->data[i];
      if (ch == '"') *out = xt_add(*out, xt_string_from_cstr("\\\""));
      else if (ch == '\\') *out = xt_add(*out, xt_string_from_cstr("\\\\"));
      else if (ch == '\n') *out = xt_add(*out, xt_string_from_cstr("\\n"));
      else if (ch == '\r') *out = xt_add(*out, xt_string_from_cstr("\\r"));
      else if (ch == '\t') *out = xt_add(*out, xt_string_from_cstr("\\t"));
      else *out = xt_add(*out, xt_string_new(&ch, 1));
    }
    *out = xt_add(*out, xt_string_from_cstr("\""));
    return;
  }
  if (XT_IS_ARRAY(value)) { xt_json_stringify_array(value, indent, depth, out); return; }
  if (XT_IS_OBJECT(value)) { xt_json_stringify_object(value, indent, depth, out); return; }
  *out = xt_add(*out, xt_string_from_cstr("null"));
}

xt_value xt_json_stringify(int32_t argc, xt_value *argv) {
  int indent = 0;
  xt_value space = xt_arg_at(argc, argv, 1);
  if (XT_IS_NUMBER(space)) indent = xt_to_int32(space);
  else if (XT_IS_STRING(space)) indent = (int)((xt_string *)XT_GET_PTR(space))->length;
  if (indent < 0) indent = 0;
  if (indent > 10) indent = 10;
  xt_value out = xt_string_from_cstr("");
  xt_json_stringify_value(xt_arg_at(argc, argv, 0), indent, 0, &out);
  return out;
}

/* -- Map ------------------------------------------------------------------ */

int xt_is_map(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_MAP;
}

int32_t xt_map_size(xt_value value) {
  if (!xt_is_map(value)) return 0;
  return (int32_t)((xt_map *)XT_GET_PTR(value))->count;
}

static int32_t xt_map_index(xt_map *map, xt_value key) {
  for (uint32_t i = 0; i < map->count; i++) if (xt_value_equals(map->keys[i], key)) return (int32_t)i;
  return -1;
}

static void xt_map_set(xt_map *map, xt_value key, xt_value value) {
  int32_t index = xt_map_index(map, key);
  if (index >= 0) { map->values[index] = value; return; }
  if (map->count >= map->capacity) {
    uint32_t capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    map->keys = (xt_value *)realloc(map->keys, sizeof(xt_value) * capacity);
    map->values = (xt_value *)realloc(map->values, sizeof(xt_value) * capacity);
    if (!map->keys || !map->values) abort();
    map->capacity = capacity;
  }
  map->keys[map->count] = key;
  map->values[map->count] = value;
  map->count++;
}

static int xt_map_delete(xt_map *map, xt_value key) {
  int32_t index = xt_map_index(map, key);
  if (index < 0) return 0;
  for (uint32_t i = (uint32_t)index + 1; i < map->count; i++) {
    map->keys[i - 1] = map->keys[i];
    map->values[i - 1] = map->values[i];
  }
  map->count--;
  return 1;
}

xt_value xt_map_ctor(int32_t argc, xt_value *argv) {
  xt_map *map = (xt_map *)xt_alloc(sizeof(xt_map), XT_OBJECT_KIND_MAP);
  map->keys = NULL; map->values = NULL; map->count = 0; map->capacity = 0;
  xt_value iterable = xt_arg_at(argc, argv, 0);
  if (XT_IS_ARRAY(iterable)) {
    xt_array *array = xt_as_array(iterable);
    for (uint32_t i = 0; i < array->length; i++) {
      xt_array *pair = xt_as_array(array->items[i]);
      if (pair && pair->length >= 2) xt_map_set(map, pair->items[0], pair->items[1]);
    }
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, map);
}

static xt_value xt_map_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  xt_map *map = (xt_map *)XT_GET_PTR(target);
  if (strcmp(method, "set") == 0) { xt_map_set(map, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1)); return target; }
  if (strcmp(method, "get") == 0) {
    int32_t index = xt_map_index(map, xt_arg_at(argc, argv, 0));
    return index >= 0 ? map->values[index] : XT_UNDEFINED;
  }
  if (strcmp(method, "has") == 0) return xt_bool(xt_map_index(map, xt_arg_at(argc, argv, 0)) >= 0);
  if (strcmp(method, "delete") == 0) return xt_bool(xt_map_delete(map, xt_arg_at(argc, argv, 0)));
  if (strcmp(method, "clear") == 0) { map->count = 0; return XT_UNDEFINED; }
  if (strcmp(method, "forEach") == 0) {
    xt_value fn = xt_arg_at(argc, argv, 0);
    for (uint32_t i = 0; i < map->count; i++) { xt_value args[3] = {map->values[i], map->keys[i], target}; xt_closure_call(fn, 3, args); }
    return XT_UNDEFINED;
  }
  if (strcmp(method, "keys") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < map->count; i++) xt_array_push(out, map->keys[i]);
    return out;
  }
  if (strcmp(method, "values") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < map->count; i++) xt_array_push(out, map->values[i]);
    return out;
  }
  if (strcmp(method, "entries") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < map->count; i++) {
      xt_value pair = xt_array_new(0, NULL);
      xt_array_push(pair, map->keys[i]);
      xt_array_push(pair, map->values[i]);
      xt_array_push(out, pair);
    }
    return out;
  }
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- Set ------------------------------------------------------------------ */

int xt_is_set(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_SET;
}

int32_t xt_set_size(xt_value value) {
  if (!xt_is_set(value)) return 0;
  return (int32_t)((xt_set_object *)XT_GET_PTR(value))->count;
}

static int32_t xt_set_index(xt_set_object *set, xt_value key) {
  for (uint32_t i = 0; i < set->count; i++) if (xt_value_equals(set->values[i], key)) return (int32_t)i;
  return -1;
}

static void xt_set_add(xt_set_object *set, xt_value value) {
  if (xt_set_index(set, value) >= 0) return;
  if (set->count >= set->capacity) {
    uint32_t capacity = set->capacity == 0 ? 8 : set->capacity * 2;
    set->values = (xt_value *)realloc(set->values, sizeof(xt_value) * capacity);
    if (!set->values) abort();
    set->capacity = capacity;
  }
  set->values[set->count++] = value;
}

static int xt_set_delete(xt_set_object *set, xt_value value) {
  int32_t index = xt_set_index(set, value);
  if (index < 0) return 0;
  for (uint32_t i = (uint32_t)index + 1; i < set->count; i++) set->values[i - 1] = set->values[i];
  set->count--;
  return 1;
}

xt_value xt_set_ctor(int32_t argc, xt_value *argv) {
  xt_set_object *set = (xt_set_object *)xt_alloc(sizeof(xt_set_object), XT_OBJECT_KIND_SET);
  set->values = NULL; set->count = 0; set->capacity = 0;
  xt_value iterable = xt_arg_at(argc, argv, 0);
  if (XT_IS_ARRAY(iterable)) {
    xt_array *array = xt_as_array(iterable);
    for (uint32_t i = 0; i < array->length; i++) xt_set_add(set, array->items[i]);
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, set);
}

static xt_value xt_set_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  xt_set_object *set = (xt_set_object *)XT_GET_PTR(target);
  if (strcmp(method, "add") == 0) { xt_set_add(set, xt_arg_at(argc, argv, 0)); return target; }
  if (strcmp(method, "has") == 0) return xt_bool(xt_set_index(set, xt_arg_at(argc, argv, 0)) >= 0);
  if (strcmp(method, "delete") == 0) return xt_bool(xt_set_delete(set, xt_arg_at(argc, argv, 0)));
  if (strcmp(method, "clear") == 0) { set->count = 0; return XT_UNDEFINED; }
  if (strcmp(method, "forEach") == 0) {
    xt_value fn = xt_arg_at(argc, argv, 0);
    for (uint32_t i = 0; i < set->count; i++) { xt_value args[3] = {set->values[i], set->values[i], target}; xt_closure_call(fn, 3, args); }
    return XT_UNDEFINED;
  }
  if (strcmp(method, "values") == 0 || strcmp(method, "keys") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < set->count; i++) xt_array_push(out, set->values[i]);
    return out;
  }
  if (strcmp(method, "entries") == 0) {
    xt_value out = xt_array_new(0, NULL);
    for (uint32_t i = 0; i < set->count; i++) {
      xt_value pair = xt_array_new(0, NULL);
      xt_array_push(pair, set->values[i]);
      xt_array_push(pair, set->values[i]);
      xt_array_push(out, pair);
    }
    return out;
  }
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- Date ----------------------------------------------------------------- */

int xt_is_date(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_DATE;
}

xt_value xt_date_ctor(int32_t argc, xt_value *argv) {
  xt_date *date = (xt_date *)xt_alloc(sizeof(xt_date), XT_OBJECT_KIND_DATE);
  if (argc == 0) date->time = (double)time(NULL) * 1000.0;
  else if (argc == 1) date->time = XT_IS_STRING(argv[0]) ? 0.0 : xt_to_number(argv[0]);
  else {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = xt_to_int32(argv[0]) - 1900;
    tm.tm_mon = xt_to_int32(argv[1]);
    tm.tm_mday = argc > 2 ? xt_to_int32(argv[2]) : 1;
    tm.tm_hour = argc > 3 ? xt_to_int32(argv[3]) : 0;
    tm.tm_min = argc > 4 ? xt_to_int32(argv[4]) : 0;
    tm.tm_sec = argc > 5 ? xt_to_int32(argv[5]) : 0;
    tm.tm_isdst = -1;
    date->time = (double)mktime(&tm) * 1000.0;
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, date);
}

xt_value xt_date_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "now") == 0) return xt_number((double)time(NULL) * 1000.0);
  if (strcmp(fn, "parse") == 0) return xt_number(0);
  if (strcmp(fn, "UTC") == 0) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = xt_to_int32(xt_arg_at(argc, argv, 0)) - 1900;
    tm.tm_mon = xt_to_int32(xt_arg_at(argc, argv, 1));
    tm.tm_mday = argc > 2 ? xt_to_int32(xt_arg_at(argc, argv, 2)) : 1;
    return xt_number((double)xt_timegm(&tm) * 1000.0);
  }
  return XT_UNDEFINED;
}

static xt_value xt_date_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  xt_date *date = (xt_date *)XT_GET_PTR(target);
  time_t seconds = (time_t)(date->time / 1000.0);
  struct tm tm;
  xt_gmtime_r(&seconds, &tm);
  if (strcmp(method, "getTime") == 0 || strcmp(method, "valueOf") == 0) return xt_number(date->time);
  const char *name = method;
  char utc[32];
  if (strncmp(method, "getUTC", 6) == 0) {
    snprintf(utc, sizeof(utc), "get%s", method + 6);
    name = utc;
  }
  if (strcmp(name, "getFullYear") == 0) return xt_number(tm.tm_year + 1900);
  if (strcmp(name, "getMonth") == 0) return xt_number(tm.tm_mon);
  if (strcmp(name, "getDate") == 0) return xt_number(tm.tm_mday);
  if (strcmp(name, "getDay") == 0) return xt_number(tm.tm_wday);
  if (strcmp(name, "getHours") == 0) return xt_number(tm.tm_hour);
  if (strcmp(name, "getMinutes") == 0) return xt_number(tm.tm_min);
  if (strcmp(name, "getSeconds") == 0) return xt_number(tm.tm_sec);
  if (strcmp(name, "getMilliseconds") == 0) return xt_number(fmod(date->time, 1000.0));
  if (strcmp(method, "toISOString") == 0) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)fmod(date->time, 1000.0));
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toJSON") == 0 || strcmp(method, "toString") == 0) return xt_call_method(target, xt_string_from_cstr("toISOString"), 0, NULL);
  *handled = 0;
  return XT_UNDEFINED;
}

/* -- RegExp (POSIX ERE subset) -------------------------------------------- */

int xt_is_regexp(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_REGEXP;
}

xt_value xt_regexp_ctor(int32_t argc, xt_value *argv) {
  xt_regexp *regexp = (xt_regexp *)xt_alloc(sizeof(xt_regexp), XT_OBJECT_KIND_REGEXP);
  xt_value source = xt_arg_at(argc, argv, 0);
  xt_value flags = xt_arg_at(argc, argv, 1);
  if (XT_IS_OBJECT(source) && ((xt_object *)XT_GET_PTR(source))->header.kind == XT_OBJECT_KIND_REGEXP) {
    xt_regexp *other = (xt_regexp *)XT_GET_PTR(source);
    regexp->source = other->source;
    regexp->flags = flags == XT_UNDEFINED ? other->flags : xt_to_string(flags);
  } else {
    regexp->source = source == XT_UNDEFINED ? xt_string_from_cstr("(?:)") : xt_to_string(source);
    regexp->flags = flags == XT_UNDEFINED ? xt_string_from_cstr("") : xt_to_string(flags);
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, regexp);
}

xt_value xt_error_ctor(int32_t argc, xt_value *argv) {
  xt_value error = xt_object_new();
  xt_value name = xt_arg_at(argc, argv, 1);
  xt_value message = xt_arg_at(argc, argv, 0);
  xt_object_set(error, xt_string_from_cstr("name"),
                name == XT_UNDEFINED ? xt_string_from_cstr("Error") : name);
  xt_object_set(error, xt_string_from_cstr("message"),
                message == XT_UNDEFINED ? xt_string_from_cstr("") : message);
  return error;
}

static xt_value xt_regexp_test(xt_value target, xt_value input) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(target);
  xt_string *pattern = xt_as_string(regexp->source);
  xt_string *text = xt_as_string(xt_to_string(input));
  int flags = REG_EXTENDED;
  xt_string *f = xt_as_string(regexp->flags);
  if (f) for (uint32_t i = 0; i < f->length; i++) if (f->data[i] == 'i') flags |= REG_ICASE;
  regex_t compiled;
  if (!pattern || !text || regcomp(&compiled, pattern->data, flags) != 0) return XT_FALSE;
  int ok = regexec(&compiled, text->data, 0, NULL, 0) == 0;
  regfree(&compiled);
  return xt_bool(ok);
}

xt_value xt_ext_container_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  xt_object *obj = (xt_object *)XT_GET_PTR(target);
  if (obj->header.kind == XT_OBJECT_KIND_MAP) return xt_map_method(target, method, argc, argv, handled);
  if (obj->header.kind == XT_OBJECT_KIND_SET) return xt_set_method(target, method, argc, argv, handled);
  if (obj->header.kind == XT_OBJECT_KIND_DATE) return xt_date_method(target, method, argc, argv, handled);
  if (obj->header.kind == XT_OBJECT_KIND_REGEXP) {
    xt_regexp *regexp = (xt_regexp *)obj;
    *handled = 1;
    if (strcmp(method, "test") == 0) return xt_regexp_test(target, xt_arg_at(argc, argv, 0));
    if (strcmp(method, "exec") == 0) {
      if (xt_truthy(xt_regexp_test(target, xt_arg_at(argc, argv, 0)))) {
        xt_value out = xt_array_new(0, NULL);
        xt_array_push(out, xt_arg_at(argc, argv, 0));
        return out;
      }
      return XT_NULL;
    }
    if (strcmp(method, "toString") == 0) {
      xt_value out = xt_string_from_cstr("/");
      out = xt_add(out, regexp->source);
      out = xt_add(out, xt_string_from_cstr("/"));
      out = xt_add(out, regexp->flags);
      return out;
    }
    *handled = 0;
    return XT_UNDEFINED;
  }
  *handled = 0;
  return XT_UNDEFINED;
}
