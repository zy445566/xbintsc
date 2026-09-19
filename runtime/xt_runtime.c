/*
 * xtsc runtime implementation.
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
 *    xtsc targets today; swap for a hash map when the profile demands it.
 */

#include "rt.h"

#include <math.h>
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
    fprintf(stderr, "xtsc: out of memory allocating %zu bytes\n", size);
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
    fprintf(stderr, "xtsc: out of memory allocating string\n");
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
    fprintf(stderr, "xtsc: out of memory growing object\n");
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
  if (!XT_IS_OBJECT(value)) return xt_array_new(0, NULL);
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  xt_array *array = (xt_array *)XT_GET_PTR(xt_array_new(0, NULL));
  for (uint32_t i = 0; i < obj->count; i++) {
    array->items[array->length++] = XT_FROM_PTR(XT_TAG_STRING, obj->properties[i].key);
  }
  return XT_FROM_PTR(XT_TAG_ARRAY, array);
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
    fprintf(stderr, "xtsc: out of memory growing array\n");
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

void xt_throw(xt_value v) {
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
