/*
 * xbintsc runtime implementation.
 *
 * Design notes
 * ------------
 *  * Values are 64-bit NaN-boxed words (`xt_value`). Numbers travel unboxed;
 *    everything else is a tagged pointer into and allocation arena.
 *  * The allocator is a bump arena. It never frees, which keeps the generated
 *    code free of write barriers and makes the first implementation small and
 *    predictable. Replacing it with a precise/conservative collector is an
 *    isolated change behind `xt_alloc` plus root registration.
 *  * Strings are UTF-8. JavaScript measures string length in UTF-16 code
 *    units; `xt_string_length` currently reports code points (see README).
 *  * Objects use a small linear property list. Fine for the language subset
 *    xbintsc targets today; swap for a hash map when the profile demands it.
 */

#include "rt.h"

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ------------------------------------------------------------------------- */
/* Allocation                                                                */
/* ------------------------------------------------------------------------- */

#define XT_OBJECT_KIND_STRING 1
#define XT_OBJECT_KIND_OBJECT 2
#define XT_OBJECT_KIND_ARRAY 3
#define XT_OBJECT_KIND_FUNCTION 4

typedef struct xt_header {
  uint8_t kind;
  uint8_t flags;
  uint16_t reserved;
  uint32_t size;
  struct xt_header *gc_next;
} xt_header;

typedef struct {
  xt_header header;
  uint32_t length;
  uint32_t capacity;
  char *data;
} xt_string;

typedef struct {
  xt_string *key;
  xt_value value;
} xt_property;

typedef struct {
  xt_header header;
  uint32_t count;
  uint32_t capacity;
  xt_property *properties;
} xt_object;

typedef struct {
  xt_header header;
  uint32_t length;
  uint32_t capacity;
  xt_value *items;
} xt_array;

typedef struct {
  xt_header header;
  xt_code_fn code;
  xt_value *environment;
  int32_t environment_count;
  int32_t arity;
  xt_string *name;
  xt_object *properties;
} xt_function;

static size_t g_heap_bytes = 0;
static size_t g_heap_allocations = 0;
static xt_header *g_heap_head = NULL;

static void *xt_alloc(size_t size, int kind) {
  xt_header *header = (xt_header *)calloc(1, size);
  if (!header) {
    fprintf(stderr, "xbintsc: out of memory allocating %zu bytes\n", size);
    abort();
  }
  header->kind = (uint8_t)kind;
  header->size = (uint32_t)size;
  header->gc_next = g_heap_head;
  g_heap_head = header;
  g_heap_bytes += size;
  g_heap_allocations++;
  return header;
}

size_t xt_heap_bytes(void) { return g_heap_bytes; }
size_t xt_heap_allocations(void) { return g_heap_allocations; }

/* ------------------------------------------------------------------------- */
/* Constructors                                                              */
/* ------------------------------------------------------------------------- */

xt_value xt_undefined(void) { return XT_UNDEFINED; }
xt_value xt_null(void) { return XT_NULL; }
xt_value xt_bool(int b) { return b ? XT_TRUE : XT_FALSE; }
xt_value xt_number(double d) { return xt_from_double(d); }

static xt_string *xt_string_alloc(size_t length) {
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

static xt_string *xt_as_string(xt_value v);

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

static xt_string *xt_as_string(xt_value v) {
  if (XT_IS_STRING(v)) return (xt_string *)XT_GET_PTR(v);
  return NULL;
}

static int32_t xt_string_length(xt_string *s) { return (int32_t)s->length; }

static xt_value xt_string_concat(xt_string *a, xt_string *b) {
  size_t total = (size_t)a->length + (size_t)b->length;
  xt_string *out = xt_string_alloc(total);
  memcpy(out->data, a->data, a->length);
  memcpy(out->data + a->length, b->data, b->length);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static int xt_string_equals(xt_string *a, xt_string *b) {
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
  return 1; /* objects, arrays, functions and non-empty strings are truthy */
}

double xt_to_number(xt_value v) {
  if (XT_IS_NUMBER(v)) return xt_to_double(v);
  if (v == XT_TRUE) return 1.0;
  if (v == XT_FALSE) return 0.0;
  if (v == XT_UNDEFINED) return NAN;
  if (v == XT_NULL) return 0.0;
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
  if (v == XT_UNDEFINED) return xt_string_from_cstr("undefined");
  if (XT_IS_FUNCTION(v)) return xt_string_from_cstr("function");
  if (v == XT_NULL) return xt_string_from_cstr("object");
  return xt_string_from_cstr("object");
}

/* ------------------------------------------------------------------------- */
/* Arithmetic                                                                */
/* ------------------------------------------------------------------------- */

xt_value xt_add(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    return xt_number(xt_to_double(a) + xt_to_double(b));
  }
  /* JavaScript's `+` concatenates when either operand is a string. */
  if (XT_IS_STRING(a) || XT_IS_STRING(b)) {
    xt_value sa = xt_to_string(a);
    xt_value sb = xt_to_string(b);
    return xt_string_concat(xt_as_string(sa), xt_as_string(sb));
  }
  return xt_number(xt_to_number(a) + xt_to_number(b));
}

xt_value xt_sub(xt_value a, xt_value b) { return xt_number(xt_to_number(a) - xt_to_number(b)); }
xt_value xt_mul(xt_value a, xt_value b) { return xt_number(xt_to_number(a) * xt_to_number(b)); }
xt_value xt_div(xt_value a, xt_value b) { return xt_number(xt_to_number(a) / xt_to_number(b)); }

xt_value xt_mod(xt_value a, xt_value b) {
  double x = xt_to_number(a);
  double y = xt_to_number(b);
  return xt_number(fmod(x, y));
}

xt_value xt_pow(xt_value a, xt_value b) { return xt_number(pow(xt_to_number(a), xt_to_number(b))); }
xt_value xt_neg(xt_value a) { return xt_number(-xt_to_number(a)); }
xt_value xt_pos(xt_value a) { return xt_number(xt_to_number(a)); }

static int32_t xt_to_int32(xt_value v) {
  double d = xt_to_number(v);
  if (isnan(d) || isinf(d)) return 0;
  return (int32_t)(uint32_t)(int64_t)d;
}

xt_value xt_bit_and(xt_value a, xt_value b) { return xt_number((double)(xt_to_int32(a) & xt_to_int32(b))); }
xt_value xt_bit_or(xt_value a, xt_value b) { return xt_number((double)(xt_to_int32(a) | xt_to_int32(b))); }
xt_value xt_bit_xor(xt_value a, xt_value b) { return xt_number((double)(xt_to_int32(a) ^ xt_to_int32(b))); }
xt_value xt_bit_not(xt_value a) { return xt_number((double)(~xt_to_int32(a))); }
xt_value xt_shl(xt_value a, xt_value b) { return xt_number((double)(int32_t)((uint32_t)xt_to_int32(a) << (xt_to_int32(b) & 31))); }
xt_value xt_shr(xt_value a, xt_value b) { return xt_number((double)(xt_to_int32(a) >> (xt_to_int32(b) & 31))); }
xt_value xt_ushr(xt_value a, xt_value b) {
  return xt_number((double)(uint32_t)((uint32_t)xt_to_int32(a) >> (xt_to_int32(b) & 31)));
}

/* ------------------------------------------------------------------------- */
/* Comparison                                                                */
/* ------------------------------------------------------------------------- */

xt_value xt_lt(xt_value a, xt_value b) {
  if (XT_IS_STRING(a) && XT_IS_STRING(b)) {
    xt_string *x = xt_as_string(a);
    xt_string *y = xt_as_string(b);
    size_t n = x->length < y->length ? x->length : y->length;
    int cmp = memcmp(x->data, y->data, n);
    if (cmp == 0) return xt_bool(x->length < y->length);
    return xt_bool(cmp < 0);
  }
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
  return xt_bool(xt_to_number(a) <= xt_to_number(b));
}

xt_value xt_gt(xt_value a, xt_value b) { return xt_lt(b, a); }
xt_value xt_ge(xt_value a, xt_value b) { return xt_le(b, a); }

static int xt_loose_equals(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) return xt_to_double(a) == xt_to_double(b);
  if (XT_IS_STRING(a) && XT_IS_STRING(b)) return xt_string_equals(xt_as_string(a), xt_as_string(b));
  if (a == b) return 1;
  if ((a == XT_NULL && b == XT_UNDEFINED) || (a == XT_UNDEFINED && b == XT_NULL)) return 1;
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
  return xt_bool(a == b);
}

xt_value xt_sne(xt_value a, xt_value b) { return xt_bool(!xt_truthy(xt_seq(a, b))); }
xt_value xt_not(xt_value a) { return xt_bool(!xt_truthy(a)); }

/* ------------------------------------------------------------------------- */
/* Objects                                                                   */
/* ------------------------------------------------------------------------- */

static void xt_object_reserve(xt_object *obj, uint32_t needed) {
  if (needed <= obj->capacity) return;
  uint32_t capacity = obj->capacity ? obj->capacity * 2 : 8;
  while (capacity < needed) capacity *= 2;
  obj->properties = (xt_property *)realloc(obj->properties, sizeof(xt_property) * capacity);
  if (!obj->properties) {
    fprintf(stderr, "xbintsc: out of memory growing object\n");
    abort();
  }
  obj->capacity = capacity;
}

xt_value xt_object_new(void) {
  xt_object *obj = (xt_object *)xt_alloc(sizeof(xt_object), XT_OBJECT_KIND_OBJECT);
  obj->count = 0;
  obj->capacity = 0;
  obj->properties = NULL;
  return XT_FROM_PTR(XT_TAG_OBJECT, obj);
}

static xt_property *xt_object_find(xt_object *obj, xt_string *key) {
  for (uint32_t i = 0; i < obj->count; i++) {
    if (xt_string_equals(obj->properties[i].key, key)) return &obj->properties[i];
  }
  return NULL;
}

xt_value xt_object_get(xt_value value, xt_value key) {
  if (!XT_IS_OBJECT(value)) return XT_UNDEFINED;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  xt_value keyString = xt_to_string(key);
  xt_property *prop = xt_object_find(obj, xt_as_string(keyString));
  return prop ? prop->value : XT_UNDEFINED;
}

xt_value xt_object_set(xt_value value, xt_value key, xt_value newValue) {
  if (!XT_IS_OBJECT(value)) return newValue;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  xt_value keyString = xt_to_string(key);
  xt_property *prop = xt_object_find(obj, xt_as_string(keyString));
  if (prop) {
    prop->value = newValue;
    return newValue;
  }
  xt_object_reserve(obj, obj->count + 1);
  obj->properties[obj->count].key = xt_as_string(keyString);
  obj->properties[obj->count].value = newValue;
  obj->count++;
  return newValue;
}

xt_value xt_object_has(xt_value value, xt_value key) {
  if (!XT_IS_OBJECT(value)) return XT_FALSE;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  xt_value keyString = xt_to_string(key);
  return xt_bool(xt_object_find(obj, xt_as_string(keyString)) != NULL);
}

xt_value xt_object_keys(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (XT_IS_OBJECT(value)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(value);
    for (uint32_t i = 0; i < obj->count; i++) {
      xt_array_push(result, XT_FROM_PTR(XT_TAG_STRING, obj->properties[i].key));
    }
    return result;
  }
  if (XT_IS_ARRAY(value)) {
    xt_array *array = (xt_array *)XT_GET_PTR(value);
    for (uint32_t i = 0; i < array->length; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_array_push(result, xt_string_from_cstr(buffer));
    }
    return result;
  }
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    for (uint32_t i = 0; i < s->length; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_array_push(result, xt_string_from_cstr(buffer));
    }
    return result;
  }
  return result;
}

/* Convenience used by codegen for `obj.prop`. */
xt_value xt_object_get_cstr(xt_value value, const char *key) {
  xt_value k = xt_string_from_cstr(key);
  return xt_object_get(value, k);
}

/* ------------------------------------------------------------------------- */
/* Arrays                                                                    */
/* ------------------------------------------------------------------------- */

static void xt_array_reserve(xt_array *array, uint32_t needed) {
  if (needed <= array->capacity) return;
  uint32_t capacity = array->capacity ? array->capacity * 2 : 8;
  while (capacity < needed) capacity *= 2;
  array->items = (xt_value *)realloc(array->items, sizeof(xt_value) * capacity);
  if (!array->items) {
    fprintf(stderr, "xbintsc: out of memory growing array\n");
    abort();
  }
  for (uint32_t i = array->capacity; i < capacity; i++) array->items[i] = XT_UNDEFINED;
  array->capacity = capacity;
}

xt_value xt_array_new(int32_t count, xt_value *items) {
  xt_array *array = (xt_array *)xt_alloc(sizeof(xt_array), XT_OBJECT_KIND_ARRAY);
  array->length = 0;
  array->capacity = 0;
  array->items = NULL;
  if (count > 0) {
    xt_array_reserve(array, (uint32_t)count);
    for (int32_t i = 0; i < count; i++) array->items[i] = items[i];
    array->length = (uint32_t)count;
  }
  return XT_FROM_PTR(XT_TAG_ARRAY, array);
}

xt_value xt_array_get(xt_value value, xt_value index) {
  if (!XT_IS_ARRAY(value)) return XT_UNDEFINED;
  xt_array *array = (xt_array *)XT_GET_PTR(value);
  int32_t i = xt_to_int32(index);
  if (i < 0 || (uint32_t)i >= array->length) return XT_UNDEFINED;
  return array->items[i];
}

xt_value xt_array_set(xt_value value, xt_value index, xt_value newValue) {
  if (!XT_IS_ARRAY(value)) return newValue;
  xt_array *array = (xt_array *)XT_GET_PTR(value);
  int32_t i = xt_to_int32(index);
  if (i < 0) return newValue;
  xt_array_reserve(array, (uint32_t)i + 1);
  for (uint32_t j = array->length; j < (uint32_t)i; j++) array->items[j] = XT_UNDEFINED;
  array->items[i] = newValue;
  if ((uint32_t)i >= array->length) array->length = (uint32_t)i + 1;
  return newValue;
}

xt_value xt_array_push(xt_value value, xt_value newValue) {
  if (!XT_IS_ARRAY(value)) return xt_number(0);
  xt_array *array = (xt_array *)XT_GET_PTR(value);
  xt_array_reserve(array, array->length + 1);
  array->items[array->length++] = newValue;
  return xt_number((double)array->length);
}

xt_value xt_array_length(xt_value value) {
  if (XT_IS_ARRAY(value)) return xt_number((double)((xt_array *)XT_GET_PTR(value))->length);
  if (XT_IS_STRING(value)) return xt_number((double)xt_string_length(xt_as_string(value)));
  return XT_UNDEFINED;
}

xt_value xt_array_spread(xt_value target, xt_value source) {
  if (!XT_IS_ARRAY(target) || !XT_IS_ARRAY(source)) return target;
  xt_array *dst = (xt_array *)XT_GET_PTR(target);
  xt_array *src = (xt_array *)XT_GET_PTR(source);
  xt_array_reserve(dst, dst->length + src->length);
  for (uint32_t i = 0; i < src->length; i++) dst->items[dst->length++] = src->items[i];
  return target;
}

/* ------------------------------------------------------------------------- */
/* Standard library: Array / String methods                                  */
/* ------------------------------------------------------------------------- */

static xt_value xt_arg_at(int32_t argc, xt_value *argv, int32_t index) {
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

static xt_array *xt_as_array(xt_value value) {
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

static xt_value xt_array_method(xt_value target, const char *method, int32_t argc, xt_value *argv) {
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
  return XT_UNDEFINED;
}

static xt_value xt_string_method(xt_value target, const char *method, int32_t argc, xt_value *argv) {
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
  return XT_UNDEFINED;
}

xt_value xt_call_method(xt_value target, xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return XT_UNDEFINED;
  if (XT_IS_OBJECT(target)) {
    xt_value fn = xt_object_get(target, name);
    if (XT_IS_FUNCTION(fn)) return xt_closure_call(fn, argc, argv);
  }
  if (XT_IS_ARRAY(target)) return xt_array_method(target, method, argc, argv);
  if (XT_IS_STRING(target)) return xt_string_method(target, method, argc, argv);
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

/* ------------------------------------------------------------------------- */
/* Object methods and global functions                                       */
/* ------------------------------------------------------------------------- */

xt_value xt_object_values(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (!XT_IS_OBJECT(value)) return result;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  for (uint32_t i = 0; i < obj->count; i++) xt_array_push(result, obj->properties[i].value);
  return result;
}

xt_value xt_object_entries(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (!XT_IS_OBJECT(value)) return result;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  for (uint32_t i = 0; i < obj->count; i++) {
    xt_value pair = xt_array_new(0, NULL);
    xt_array_push(pair, XT_FROM_PTR(XT_TAG_STRING, obj->properties[i].key));
    xt_array_push(pair, obj->properties[i].value);
    xt_array_push(result, pair);
  }
  return result;
}

xt_value xt_object_assign(int32_t argc, xt_value *argv) {
  if (argc <= 0) return xt_undefined();
  xt_value target = argv[0];
  if (!XT_IS_OBJECT(target)) return target;
  for (int32_t i = 1; i < argc; i++) {
    if (!XT_IS_OBJECT(argv[i])) continue;
    xt_object *source = (xt_object *)XT_GET_PTR(argv[i]);
    for (uint32_t j = 0; j < source->count; j++) {
      xt_object_set(target, XT_FROM_PTR(XT_TAG_STRING, source->properties[j].key), source->properties[j].value);
    }
  }
  return target;
}

/* Copy every own enumerable property of `source` onto `target` (`{...source}`). */
xt_value xt_object_spread(xt_value target, xt_value source) {
  xt_value args[2];
  args[0] = target;
  args[1] = source;
  return xt_object_assign(2, args);
}

xt_value xt_parse_int(int32_t argc, xt_value *argv) {
  xt_string *s = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
  int32_t radix = argc >= 2 ? xt_to_int32(argv[1]) : 0;
  char *end = NULL;
  long value = strtol(s->data, &end, radix);
  if (end == s->data) return xt_number(NAN);
  return xt_number((double)value);
}

xt_value xt_parse_float(int32_t argc, xt_value *argv) {
  xt_string *s = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
  char *end = NULL;
  double value = strtod(s->data, &end);
  if (end == s->data) return xt_number(NAN);
  return xt_number(value);
}

xt_value xt_is_nan(int32_t argc, xt_value *argv) {
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  return xt_bool(value != value);
}

xt_value xt_is_finite(int32_t argc, xt_value *argv) {
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  return xt_bool(!isnan(value) && !isinf(value));
}

xt_value xt_number_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_number(0);
  return xt_number(xt_to_number(argv[0]));
}

xt_value xt_string_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_string_from_cstr("");
  return xt_to_string(argv[0]);
}

xt_value xt_boolean_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return XT_FALSE;
  return xt_bool(xt_truthy(argv[0]));
}

xt_value xt_in(xt_value key, xt_value value) {
  if (XT_IS_OBJECT(value)) return xt_object_has(value, key);
  if (XT_IS_ARRAY(value)) {
    int32_t index = xt_to_int32(key);
    return xt_bool(index >= 0 && (uint32_t)index < xt_as_array(value)->length);
  }
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    xt_string *k = xt_as_string(xt_to_string(key));
    if (k->length == 6 && memcmp(k->data, "length", 6) == 0) return XT_TRUE;
    int32_t index = xt_to_int32(key);
    return xt_bool(index >= 0 && (uint32_t)index < s->length);
  }
  return XT_FALSE;
}

xt_value xt_delete(xt_value value, xt_value key) {
  if (XT_IS_OBJECT(value)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(value);
    xt_string *k = xt_as_string(xt_to_string(key));
    for (uint32_t i = 0; i < obj->count; i++) {
      if (xt_string_equals(obj->properties[i].key, k)) {
        for (uint32_t j = i + 1; j < obj->count; j++) obj->properties[j - 1] = obj->properties[j];
        obj->count--;
        break;
      }
    }
    return XT_TRUE;
  }
  if (XT_IS_ARRAY(value)) {
    xt_array_set(value, key, XT_UNDEFINED);
    return XT_TRUE;
  }
  return XT_TRUE;
}

xt_value xt_rest_args(int32_t argc, xt_value *argv, int32_t start) {
  if (start < 0) start = 0;
  int32_t count = argc - start;
  if (count < 0) count = 0;
  return xt_array_new(count, argv ? argv + start : NULL);
}

/* ------------------------------------------------------------------------- */
/* Generic member access                                                     */
/* ------------------------------------------------------------------------- */

xt_value xt_get(xt_value target, xt_value key) {
  if (XT_IS_ARRAY(target)) return xt_array_get(target, key);
  if (XT_IS_OBJECT(target)) return xt_object_get(target, key);
  if (XT_IS_STRING(target)) {
    /* Property access on a string: `.length` and numeric indexing. */
    xt_string *s = xt_as_string(target);
    if (XT_IS_STRING(key)) {
      xt_string *k = xt_as_string(key);
      if (k->length == 6 && memcmp(k->data, "length", 6) == 0) return xt_number((double)s->length);
    }
    int32_t index = xt_to_int32(key);
    if (index >= 0 && (uint32_t)index < s->length) return xt_string_new(s->data + index, 1);
    return XT_UNDEFINED;
  }
  return XT_UNDEFINED;
}

xt_value xt_set(xt_value target, xt_value key, xt_value value) {
  if (XT_IS_ARRAY(target)) return xt_array_set(target, key, value);
  if (XT_IS_OBJECT(target)) return xt_object_set(target, key, value);
  return value;
}

/* ------------------------------------------------------------------------- */
/* Boxes                                                                     */
/* ------------------------------------------------------------------------- */

xt_value xt_box_new(xt_value value) { return xt_array_new(1, &value); }

xt_value xt_box_get(xt_value box) { return xt_array_get(box, xt_number(0)); }

xt_value xt_box_set(xt_value box, xt_value value) { return xt_array_set(box, xt_number(0), value); }

xt_value xt_is_nullish(xt_value value) { return xt_bool(value == XT_UNDEFINED || value == XT_NULL); }

/* ------------------------------------------------------------------------- */
/* Functions                                                                 */
/* ------------------------------------------------------------------------- */

xt_value xt_arg(int32_t argc, xt_value *argv, int32_t index) {
  if (index < 0 || index >= argc) return XT_UNDEFINED;
  return argv[index];
}

xt_value xt_closure_new(void *fn, int32_t env_count, xt_value *env) {
  xt_function *function = (xt_function *)xt_alloc(sizeof(xt_function), XT_OBJECT_KIND_FUNCTION);
  function->code = (xt_code_fn)fn;
  function->environment_count = env_count;
  function->arity = -1;
  function->properties = NULL;
  function->environment = NULL;
  if (env_count > 0) {
    function->environment = (xt_value *)malloc(sizeof(xt_value) * (size_t)env_count);
    for (int32_t i = 0; i < env_count; i++) function->environment[i] = env[i];
  }
  return XT_FROM_PTR(XT_TAG_FUNCTION, function);
}

xt_value xt_closure_call(xt_value value, int32_t argc, xt_value *argv) {
  if (!XT_IS_FUNCTION(value)) {
    xt_throw(xt_string_from_cstr("TypeError: value is not a function"));
    return XT_UNDEFINED;
  }
  xt_function *function = (xt_function *)XT_GET_PTR(value);
  /* The closure value itself is threaded through as `env` so the callee can
   * read its captures with xt_closure_env(env, i). */
  return function->code(value, argc, argv);
}

xt_value xt_closure_env(xt_value value, int32_t index) {
  if (!XT_IS_FUNCTION(value)) return XT_UNDEFINED;
  xt_function *function = (xt_function *)XT_GET_PTR(value);
  if (index < 0 || index >= function->environment_count) return XT_UNDEFINED;
  return function->environment[index];
}

int32_t xt_closure_arity(xt_value value) {
  if (!XT_IS_FUNCTION(value)) return -1;
  return ((xt_function *)XT_GET_PTR(value))->arity;
}

/* ------------------------------------------------------------------------- */
/* Exceptions and output                                                     */
/* ------------------------------------------------------------------------- */

/*
 * `try`/`catch` is implemented with a stack of setjmp frames. The compiler
 * allocates a frame with `xt_try_enter`, calls `_setjmp` on it, and pops it
 * with `xt_try_leave`. `xt_throw` longjmps into the innermost frame when one
 * is active, and only prints + exits when the exception is uncaught.
 */
typedef struct xt_try_frame {
  jmp_buf buf;
  struct xt_try_frame *prev;
  xt_value exception;
} xt_try_frame;

static xt_try_frame *g_try_top = NULL;

void *xt_try_enter(void) {
  xt_try_frame *frame = (xt_try_frame *)calloc(1, sizeof(xt_try_frame));
  if (!frame) abort();
  frame->prev = g_try_top;
  frame->exception = XT_UNDEFINED;
  g_try_top = frame;
  return (void *)frame;
}

xt_value xt_try_exception(void *framePtr) {
  xt_try_frame *frame = (xt_try_frame *)framePtr;
  return frame ? frame->exception : XT_UNDEFINED;
}

void xt_try_leave(void *framePtr) {
  xt_try_frame *frame = (xt_try_frame *)framePtr;
  if (frame && g_try_top == frame) g_try_top = frame->prev;
}

void xt_throw(xt_value v) {
  if (g_try_top) {
    g_try_top->exception = v;
    _longjmp(g_try_top->buf, 1);
  }
  xt_value message = xt_to_string(v);
  xt_string *s = xt_as_string(message);
  fprintf(stderr, "Uncaught %s\n", s->data);
  exit(1);
}

static void xt_print_value(xt_value v, FILE *out) {
  if (XT_IS_STRING(v)) {
    xt_string *s = xt_as_string(v);
    fwrite(s->data, 1, s->length, out);
    return;
  }
  xt_value text = xt_to_string(v);
  xt_string *s = xt_as_string(text);
  fwrite(s->data, 1, s->length, out);
}

void xt_print(xt_value v) { xt_print_value(v, stdout); }
void xt_println(xt_value v) {
  xt_print_value(v, stdout);
  fputc('\n', stdout);
}

/*
 * console.log applies a Node-like inspection format: strings unquoted at the
 * top level, arrays as `[ a, b ]` and objects as `{ key: value }`.
 */
static void xt_inspect(xt_value v, FILE *out) {
  if (XT_IS_ARRAY(v)) {
    xt_array *array = (xt_array *)XT_GET_PTR(v);
    fputc('[', out);
    for (uint32_t i = 0; i < array->length; i++) {
      if (i > 0) fputs(", ", out);
      else fputc(' ', out);
      xt_inspect(array->items[i], out);
    }
    if (array->length > 0) fputc(' ', out);
    fputc(']', out);
    return;
  }
  if (XT_IS_OBJECT(v)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(v);
    fputc('{', out);
    for (uint32_t i = 0; i < obj->count; i++) {
      if (i > 0) fputs(", ", out);
      else fputc(' ', out);
      fwrite(obj->properties[i].key->data, 1, obj->properties[i].key->length, out);
      fputs(": ", out);
      xt_inspect(obj->properties[i].value, out);
    }
    if (obj->count > 0) fputc(' ', out);
    fputc('}', out);
    return;
  }
  xt_print_value(v, out);
}

void xt_console_log(int32_t argc, xt_value *argv) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', stdout);
    xt_inspect(argv[i], stdout);
  }
  fputc('\n', stdout);
}

static void xt_console_write(int32_t argc, xt_value *argv, FILE *out) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', out);
    xt_inspect(argv[i], out);
  }
  fputc('\n', out);
}

void xt_console_info(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stdout); }
void xt_console_warn(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stderr); }
void xt_console_error(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stderr); }
