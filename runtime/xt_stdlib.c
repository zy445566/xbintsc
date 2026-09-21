/*
 * xbintsc runtime — standard library.
 *
 * Array and String prototype methods, method dispatch (`xt_call_method`) and
 * the `Math` namespace.
 */

#include "rt_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Standard library: Array / String methods                                  */
/* ------------------------------------------------------------------------- */

xt_value xt_arg_at(int32_t argc, xt_value *argv, int32_t index) {
  if (!argv || index < 0 || index >= argc) return XT_UNDEFINED;
  return argv[index];
}

static int xt_same_value_zero(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    double x = xt_to_double(a);
    double y = xt_to_double(b);
    if (isnan(x) && isnan(y)) return 1;
    return x == y;
  }
  return xt_truthy(xt_seq(a, b));
}

xt_array *xt_as_array(xt_value value) {
  if (XT_IS_ARRAY(value)) return (xt_array *)XT_GET_PTR(value);
  return NULL;
}

/* -- array methods -------------------------------------------------------- */

static xt_value xt_array_pop(xt_value value) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length == 0) return XT_UNDEFINED;
  return array->items[--array->length];
}

static xt_value xt_array_shift(xt_value value) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length == 0) return XT_UNDEFINED;
  xt_value first = array->items[0];
  for (uint32_t i = 1; i < array->length; i++) array->items[i - 1] = array->items[i];
  array->length--;
  return first;
}

static xt_value xt_array_unshift(xt_value value, int32_t argc, xt_value *argv) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  if (argc <= 0) return xt_number((double)array->length);
  xt_array_reserve(array, array->length + (uint32_t)argc);
  for (uint32_t i = array->length; i-- > 0;) array->items[i + argc] = array->items[i];
  for (int32_t i = 0; i < argc; i++) array->items[i] = argv[i];
  array->length += (uint32_t)argc;
  return xt_number((double)array->length);
}

static void xt_buffer_append(char **out, size_t *length, size_t *capacity, const char *data, size_t size) {
  if (*length + size + 1 > *capacity) {
    while (*length + size + 1 > *capacity) *capacity = (*capacity) * 2;
    *out = (char *)realloc(*out, *capacity);
    if (!*out) abort();
  }
  memcpy(*out + *length, data, size);
  *length += size;
  (*out)[*length] = '\0';
}

static xt_value xt_array_join(xt_value value, xt_value separator) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_string_from_cstr("");
  xt_string *sep = xt_as_string(xt_to_string(separator == XT_UNDEFINED ? xt_string_from_cstr(",") : separator));
  size_t capacity = 32;
  size_t length = 0;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  out[0] = '\0';
  for (uint32_t i = 0; i < array->length; i++) {
    if (i > 0) xt_buffer_append(&out, &length, &capacity, sep->data, sep->length);
    xt_value item = array->items[i];
    if (item == XT_UNDEFINED || item == XT_NULL) continue;
    xt_string *str = xt_as_string(xt_to_string(item));
    xt_buffer_append(&out, &length, &capacity, str->data, str->length);
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

static void xt_normalize_slice(int32_t len, int32_t *start, int32_t *end) {
  if (*start < 0) *start += len;
  if (*start < 0) *start = 0;
  if (*start > len) *start = len;
  if (*end < 0) *end += len;
  if (*end < 0) *end = 0;
  if (*end > len) *end = len;
  if (*end < *start) *end = *start;
}

static xt_value xt_array_slice(xt_value value, xt_value startValue, xt_value endValue) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  int32_t len = (int32_t)array->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  xt_normalize_slice(len, &start, &end);
  return xt_array_new(end - start, array->items + start);
}

static xt_value xt_array_index_of(xt_value value, xt_value needle, xt_value fromValue, int sameValueZero) {
  xt_array *array = xt_as_array(value);
  if (!array) return xt_number(-1);
  int32_t from = fromValue == XT_UNDEFINED ? 0 : xt_to_int32(fromValue);
  if (from < 0) from += (int32_t)array->length;
  if (from < 0) from = 0;
  for (uint32_t i = (uint32_t)from; i < array->length; i++) {
    int equal = sameValueZero ? xt_same_value_zero(array->items[i], needle) : xt_truthy(xt_seq(array->items[i], needle));
    if (equal) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_array_concat(xt_value value, int32_t argc, xt_value *argv) {
  xt_array *array = xt_as_array(value);
  xt_value result = xt_array_new(0, NULL);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) xt_array_push(result, array->items[i]);
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_ARRAY(argv[i])) {
      xt_array *other = xt_as_array(argv[i]);
      for (uint32_t j = 0; j < other->length; j++) xt_array_push(result, other->items[j]);
    } else {
      xt_array_push(result, argv[i]);
    }
  }
  return result;
}

static xt_value xt_array_for_each(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    xt_closure_call(fn, 3, args);
  }
  return XT_UNDEFINED;
}

static xt_value xt_array_map(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  xt_value result = xt_array_new(0, NULL);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    xt_array_push(result, xt_closure_call(fn, 3, args));
  }
  return result;
}

static xt_value xt_array_filter(xt_value value, xt_value fn) {
  xt_array *array = xt_as_array(value);
  xt_value result = xt_array_new(0, NULL);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value args[3] = {array->items[i], xt_number((double)i), value};
    if (xt_truthy(xt_closure_call(fn, 3, args))) xt_array_push(result, array->items[i]);
  }
  return result;
}

static xt_value xt_array_reduce(xt_value value, xt_value fn, int32_t hasInitial, xt_value initial) {
  xt_array *array = xt_as_array(value);
  if (!array) return XT_UNDEFINED;
  uint32_t i = 0;
  xt_value accumulator;
  if (hasInitial) {
    accumulator = initial;
  } else if (array->length > 0) {
    accumulator = array->items[0];
    i = 1;
  } else {
    xt_throw(xt_string_from_cstr("TypeError: Reduce of empty array with no initial value"));
    return XT_UNDEFINED;
  }
  for (; i < array->length; i++) {
    xt_value args[4] = {accumulator, array->items[i], xt_number((double)i), value};
    accumulator = xt_closure_call(fn, 4, args);
  }
  return accumulator;
}

/* -- string methods ------------------------------------------------------- */

static int32_t xt_string_find(xt_string *haystack, xt_string *needle, int32_t from) {
  if (from < 0) from = 0;
  if ((uint32_t)from > haystack->length) return -1;
  if (needle->length == 0) return from;
  for (uint32_t i = (uint32_t)from; i + needle->length <= haystack->length; i++) {
    if (memcmp(haystack->data + i, needle->data, needle->length) == 0) return (int32_t)i;
  }
  return -1;
}

static xt_value xt_string_char_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t index = xt_to_int32(indexValue);
  if (index < 0 || (uint32_t)index >= s->length) return xt_string_from_cstr("");
  return xt_string_new(s->data + index, 1);
}

static xt_value xt_string_char_code_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_number(NAN);
  int32_t index = xt_to_int32(indexValue);
  if (index < 0 || (uint32_t)index >= s->length) return xt_number(NAN);
  return xt_number((double)(unsigned char)s->data[index]);
}

static xt_value xt_string_index_of(xt_value value, xt_value needleValue, xt_value fromValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_number(-1);
  xt_string *needle = xt_as_string(xt_to_string(needleValue));
  int32_t from = fromValue == XT_UNDEFINED ? 0 : xt_to_int32(fromValue);
  return xt_number((double)xt_string_find(s, needle, from));
}

static xt_value xt_string_slice(xt_value value, xt_value startValue, xt_value endValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)s->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  xt_normalize_slice(len, &start, &end);
  return xt_string_new(s->data + start, (size_t)(end - start));
}

static xt_value xt_string_substring(xt_value value, xt_value startValue, xt_value endValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)s->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  if (start < 0) start = 0;
  if (start > len) start = len;
  if (end < 0) end = 0;
  if (end > len) end = len;
  if (start > end) {
    int32_t swap = start;
    start = end;
    end = swap;
  }
  return xt_string_new(s->data + start, (size_t)(end - start));
}

static xt_value xt_string_substr(xt_value value, xt_value startValue, xt_value lengthValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)s->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  if (start < 0) start += len;
  if (start < 0) start = 0;
  if (start > len) start = len;
  int32_t length = lengthValue == XT_UNDEFINED ? len - start : xt_to_int32(lengthValue);
  if (length < 0) length = 0;
  if (start + length > len) length = len - start;
  return xt_string_new(s->data + start, (size_t)length);
}

static xt_value xt_string_split(xt_value value, xt_value separatorValue) {
  xt_value result = xt_array_new(0, NULL);
  xt_string *s = xt_as_string(value);
  if (!s) return result;
  if (separatorValue == XT_UNDEFINED) {
    xt_array_push(result, value);
    return result;
  }
  xt_string *sep = xt_as_string(xt_to_string(separatorValue));
  if (sep->length == 0) {
    for (uint32_t i = 0; i < s->length; i++) xt_array_push(result, xt_string_new(s->data + i, 1));
    return result;
  }
  uint32_t start = 0;
  uint32_t i = 0;
  while (i + sep->length <= s->length) {
    if (memcmp(s->data + i, sep->data, sep->length) == 0) {
      xt_array_push(result, xt_string_new(s->data + start, i - start));
      i += sep->length;
      start = i;
    } else {
      i++;
    }
  }
  xt_array_push(result, xt_string_new(s->data + start, s->length - start));
  return result;
}

static xt_value xt_string_case(xt_value value, int upper) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *out = xt_string_alloc(s->length);
  for (uint32_t i = 0; i < s->length; i++) {
    unsigned char c = (unsigned char)s->data[i];
    out->data[i] = (char)(upper ? toupper((int)c) : tolower((int)c));
  }
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_trim(xt_value value) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  uint32_t start = 0;
  uint32_t end = s->length;
  while (start < end && isspace((unsigned char)s->data[start])) start++;
  while (end > start && isspace((unsigned char)s->data[end - 1])) end--;
  return xt_string_new(s->data + start, end - start);
}

static xt_value xt_string_replace(xt_value value, xt_value searchValue, xt_value replacementValue) {
  if (xt_is_regexp(searchValue)) return xt_regexp_replace(value, searchValue, replacementValue);
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  xt_string *replacement = xt_as_string(xt_to_string(replacementValue));
  int32_t at = xt_string_find(s, search, 0);
  if (at < 0) return value;
  size_t total = s->length - search->length + replacement->length;
  xt_string *out = xt_string_alloc(total);
  memcpy(out->data, s->data, (size_t)at);
  memcpy(out->data + at, replacement->data, replacement->length);
  memcpy(out->data + at + replacement->length, s->data + at + search->length,
         s->length - (size_t)at - search->length);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_repeat(xt_value value, xt_value countValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  int32_t count = xt_to_int32(countValue);
  if (count < 0) {
    xt_throw(xt_string_from_cstr("RangeError: Invalid count value"));
    return XT_UNDEFINED;
  }
  xt_string *out = xt_string_alloc((size_t)s->length * (size_t)count);
  for (int32_t i = 0; i < count; i++) memcpy(out->data + (size_t)i * s->length, s->data, s->length);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_starts_with(xt_value value, xt_value searchValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_FALSE;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  if (search->length > s->length) return XT_FALSE;
  return xt_bool(memcmp(s->data, search->data, search->length) == 0);
}

static xt_value xt_string_ends_with(xt_value value, xt_value searchValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_FALSE;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  if (search->length > s->length) return XT_FALSE;
  return xt_bool(memcmp(s->data + s->length - search->length, search->data, search->length) == 0);
}

static xt_value xt_string_concat_args(xt_value value, int32_t argc, xt_value *argv) {
  xt_value result = XT_IS_STRING(value) ? value : xt_to_string(value);
  for (int32_t i = 0; i < argc; i++) result = xt_add(result, xt_to_string(argv[i]));
  return result;
}

/* -- method dispatch ------------------------------------------------------ */

static xt_value xt_array_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "push") == 0) {
    xt_value result = xt_number(0);
    for (int32_t i = 0; i < argc; i++) result = xt_array_push(target, argv[i]);
    if (argc == 0) result = xt_number((double)xt_as_array(target)->length);
    return result;
  }
  if (strcmp(method, "pop") == 0) return xt_array_pop(target);
  if (strcmp(method, "shift") == 0) return xt_array_shift(target);
  if (strcmp(method, "unshift") == 0) return xt_array_unshift(target, argc, argv);
  if (strcmp(method, "join") == 0) return xt_array_join(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "slice") == 0) return xt_array_slice(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "indexOf") == 0) return xt_array_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 0);
  if (strcmp(method, "includes") == 0) return xt_bool(xt_truthy(xt_ne(xt_array_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 1), xt_number(-1))));
  if (strcmp(method, "concat") == 0) return xt_array_concat(target, argc, argv);
  if (strcmp(method, "forEach") == 0) return xt_array_for_each(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "map") == 0) return xt_array_map(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "filter") == 0) return xt_array_filter(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "reduce") == 0) {
    int32_t hasInitial = argc >= 2 ? 1 : 0;
    return xt_array_reduce(target, xt_arg_at(argc, argv, 0), hasInitial, xt_arg_at(argc, argv, 1));
  }
  if (strcmp(method, "reverse") == 0) {
    xt_array *array = xt_as_array(target);
    for (uint32_t i = 0; i + 1 < array->length; i++, array->length--) {
      xt_value tmp = array->items[i];
      array->items[i] = array->items[array->length - 1];
      array->items[array->length - 1] = tmp;
    }
    return target;
  }
  *handled = 0;
  return XT_UNDEFINED;
}

static xt_value xt_string_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "charAt") == 0) return xt_string_char_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "charCodeAt") == 0) return xt_string_char_code_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "indexOf") == 0) return xt_string_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "includes") == 0) return xt_bool(xt_truthy(xt_ne(xt_string_index_of(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1)), xt_number(-1))));
  if (strcmp(method, "slice") == 0) return xt_string_slice(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "substring") == 0) return xt_string_substring(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "substr") == 0) return xt_string_substr(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "split") == 0) return xt_string_split(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "toUpperCase") == 0) return xt_string_case(target, 1);
  if (strcmp(method, "toLowerCase") == 0) return xt_string_case(target, 0);
  if (strcmp(method, "trim") == 0) return xt_string_trim(target);
  if (strcmp(method, "replace") == 0) return xt_string_replace(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "repeat") == 0) return xt_string_repeat(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "startsWith") == 0) return xt_string_starts_with(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "endsWith") == 0) return xt_string_ends_with(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "concat") == 0) return xt_string_concat_args(target, argc, argv);
  *handled = 0;
  return XT_UNDEFINED;
}

xt_value xt_call_method(xt_value target, xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return XT_UNDEFINED;
  int handled = 0;
  if (XT_IS_OBJECT(target)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(target);
    int kind = obj->header.kind;
    if (kind == XT_OBJECT_KIND_PROMISE) {
      xt_value result = xt_promise_method(target, method, argc, argv, &handled);
      if (handled) return result;
    }
    if (kind != XT_OBJECT_KIND_OBJECT && kind != XT_OBJECT_KIND_FUNCTION) {
      xt_value result = xt_ext_container_method(target, method, argc, argv, &handled);
      if (handled) return result;
    }
    xt_value fn = xt_object_get(target, name);
    if (XT_IS_FUNCTION(fn)) return xt_call_with_this(fn, target, argc, argv);
    xt_value result = xt_ext_object_method(target, method, argc, argv, &handled);
    if (handled) return result;
    {
      char message[256];
      snprintf(message, sizeof(message), "TypeError: object has no callable method '%s'", method);
      xt_throw(xt_string_from_cstr(message));
    }
    return XT_UNDEFINED;
  }
  if (XT_IS_FUNCTION(target)) {
    xt_value fn = xt_object_get(target, name);
    if (XT_IS_FUNCTION(fn)) return xt_call_with_this(fn, target, argc, argv);
    {
      char message[256];
      snprintf(message, sizeof(message), "TypeError: function has no callable method '%s'", method);
      xt_throw(xt_string_from_cstr(message));
    }
    return XT_UNDEFINED;
  }
  if (XT_IS_ARRAY(target)) {
    xt_value result = xt_ext_array_method(target, method, argc, argv, &handled);
    if (handled) return result;
    return xt_array_method(target, method, argc, argv, &handled);
  }
  if (XT_IS_STRING(target)) {
    xt_value result = xt_ext_string_method(target, method, argc, argv, &handled);
    if (handled) return result;
    return xt_string_method(target, method, argc, argv, &handled);
  }
  if (XT_IS_BIGINT(target)) {
    xt_value result = xt_bigint_method(target, method, argc, argv, &handled);
    if (handled) return result;
  }
  if (XT_IS_NUMBER(target)) {
    xt_value result = xt_ext_number_method(target, method, argc, argv, &handled);
    if (handled) return result;
  }
  return XT_UNDEFINED;
}

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

