/*
 * xbintsc runtime - typed arrays.
 *
 * JavaScript TypedArrays are modelled as plain property-bag objects with one
 * numbered property per element plus `length`, `byteLength` and `byteOffset`.
 * Every write is coerced to the array's element type, so
 * `new Uint8Array(2)[0] = 300` stores 44 exactly like ECMAScript and a
 * `Float32Array` round-trips through single precision.
 *
 * There is no shared `ArrayBuffer` backing store: `slice` / `subarray` return
 * independent copies and the `.buffer` property is not exposed. That matches
 * xbintsc's runtime, which has no distinct binary value type (strings are
 * UTF-8 byte arrays and `Buffer` is likewise a property bag).
 */

#include "rt_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  XT_TA_U8,
  XT_TA_I8,
  XT_TA_U8C,
  XT_TA_U16,
  XT_TA_I16,
  XT_TA_U32,
  XT_TA_I32,
  XT_TA_F32,
  XT_TA_F64,
  XT_TA_KIND_COUNT
};

static const int xt_ta_bytes[XT_TA_KIND_COUNT] = {1, 1, 1, 2, 2, 4, 4, 4, 8};

static xt_value xt_ta_protos[XT_TA_KIND_COUNT];

/* -- element conversion --------------------------------------------------- */

/* ECMAScript `ToUintN` / `ToIntN`: truncate, then wrap modulo 2^N. */
static double xt_ta_wrap(double value, double modulus) {
  if (isnan(value) || isinf(value)) return 0;
  double result = fmod(trunc(value), modulus);
  if (result < 0) result += modulus;
  return result;
}

double xt_typed_array_coerce(int kind, double value) {
  switch (kind) {
    case XT_TA_U8:
      return xt_ta_wrap(value, 256.0);
    case XT_TA_I8: {
      double wrapped = xt_ta_wrap(value, 256.0);
      return wrapped >= 128.0 ? wrapped - 256.0 : wrapped;
    }
    case XT_TA_U8C:
      if (isnan(value) || value <= 0) return 0;
      if (value >= 255) return 255;
      return rint(value);
    case XT_TA_U16:
      return xt_ta_wrap(value, 65536.0);
    case XT_TA_I16: {
      double wrapped = xt_ta_wrap(value, 65536.0);
      return wrapped >= 32768.0 ? wrapped - 65536.0 : wrapped;
    }
    case XT_TA_U32:
      return xt_ta_wrap(value, 4294967296.0);
    case XT_TA_I32: {
      double wrapped = xt_ta_wrap(value, 4294967296.0);
      return wrapped >= 2147483648.0 ? wrapped - 4294967296.0 : wrapped;
    }
    case XT_TA_F32:
      return (double)(float)value;
    case XT_TA_F64:
    default:
      return value;
  }
}

int xt_typed_array_kind_of(xt_value value) {
  if (!XT_IS_OBJECT(value)) return -1;
  xt_object *object = (xt_object *)XT_GET_PTR(value);
  if (object->header.kind != XT_OBJECT_KIND_OBJECT) return -1;
  for (int kind = 0; kind < XT_TA_KIND_COUNT; kind++) {
    if (xt_ta_protos[kind] && object->prototype == xt_ta_protos[kind]) return kind;
  }
  return -1;
}

/* -- element storage ------------------------------------------------------ */

static uint32_t xt_ta_length(xt_value array) {
  double length = xt_to_number(xt_object_get_cstr(array, "length"));
  if (!(length > 0)) return 0;
  return (uint32_t)length;
}

static xt_value xt_ta_element(xt_value array, uint32_t index) {
  char key[24];
  snprintf(key, sizeof(key), "%u", index);
  return xt_object_get_cstr(array, key);
}

static void xt_ta_store(xt_value array, int kind, uint32_t index, double value) {
  char key[24];
  snprintf(key, sizeof(key), "%u", index);
  xt_object_set(array, xt_string_from_cstr(key), xt_number(xt_typed_array_coerce(kind, value)));
}

/* -- forward declarations ------------------------------------------------- */

static xt_value xt_ta_method_fill(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_set(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_slice(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_join(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_to_string(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_index_of(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_last_index_of(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_includes(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_for_each(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_map(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_filter(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_every(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_some(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_find(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_find_index(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_reduce(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_reverse(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_sort(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_copy_within(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_at(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_keys(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_values(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_entries(xt_value, xt_value, int32_t, xt_value *);

/* -- construction --------------------------------------------------------- */

static void xt_ta_define(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

static xt_value xt_ta_proto(int kind) {
  static int registered = 0;
  if (!registered) {
    registered = 1;
    xt_gc_add_root_range(xt_ta_protos, XT_TA_KIND_COUNT);
  }
  if (kind < 0 || kind >= XT_TA_KIND_COUNT) return XT_UNDEFINED;
  if (xt_ta_protos[kind]) return xt_ta_protos[kind];
  xt_value proto = xt_object_new();
  xt_ta_protos[kind] = proto;
  xt_object_set(proto, xt_string_from_cstr("BYTES_PER_ELEMENT"), xt_number((double)xt_ta_bytes[kind]));
  xt_ta_define(proto, "fill", (void *)xt_ta_method_fill);
  xt_ta_define(proto, "set", (void *)xt_ta_method_set);
  xt_ta_define(proto, "slice", (void *)xt_ta_method_slice);
  xt_ta_define(proto, "subarray", (void *)xt_ta_method_slice);
  xt_ta_define(proto, "join", (void *)xt_ta_method_join);
  xt_ta_define(proto, "toString", (void *)xt_ta_method_to_string);
  xt_ta_define(proto, "toLocaleString", (void *)xt_ta_method_to_string);
  xt_ta_define(proto, "indexOf", (void *)xt_ta_method_index_of);
  xt_ta_define(proto, "lastIndexOf", (void *)xt_ta_method_last_index_of);
  xt_ta_define(proto, "includes", (void *)xt_ta_method_includes);
  xt_ta_define(proto, "forEach", (void *)xt_ta_method_for_each);
  xt_ta_define(proto, "map", (void *)xt_ta_method_map);
  xt_ta_define(proto, "filter", (void *)xt_ta_method_filter);
  xt_ta_define(proto, "every", (void *)xt_ta_method_every);
  xt_ta_define(proto, "some", (void *)xt_ta_method_some);
  xt_ta_define(proto, "find", (void *)xt_ta_method_find);
  xt_ta_define(proto, "findIndex", (void *)xt_ta_method_find_index);
  xt_ta_define(proto, "reduce", (void *)xt_ta_method_reduce);
  xt_ta_define(proto, "reverse", (void *)xt_ta_method_reverse);
  xt_ta_define(proto, "sort", (void *)xt_ta_method_sort);
  xt_ta_define(proto, "copyWithin", (void *)xt_ta_method_copy_within);
  xt_ta_define(proto, "at", (void *)xt_ta_method_at);
  xt_ta_define(proto, "keys", (void *)xt_ta_method_keys);
  xt_ta_define(proto, "values", (void *)xt_ta_method_values);
  xt_ta_define(proto, "entries", (void *)xt_ta_method_entries);
  return proto;
}

static xt_value xt_ta_new(int kind, uint32_t length) {
  xt_value array = xt_object_new_with_proto(xt_ta_proto(kind));
  xt_object_set(array, xt_string_from_cstr("length"), xt_number((double)length));
  xt_object_set(array, xt_string_from_cstr("byteLength"), xt_number((double)length * xt_ta_bytes[kind]));
  xt_object_set(array, xt_string_from_cstr("byteOffset"), xt_number(0));
  for (uint32_t i = 0; i < length; i++) xt_ta_store(array, kind, i, 0);
  return array;
}

/* Length of any value a typed array can be built from, or 0 when it is not
 * array-like (strings/iterables are intentionally not treated as element
 * sources yet). */
static int xt_ta_length_of(xt_value value, uint32_t *out) {
  if (XT_IS_ARRAY(value)) {
    *out = (uint32_t)xt_to_number(xt_array_length(value));
    return 1;
  }
  if (XT_IS_STRING(value)) {
    *out = xt_as_string(value)->length;
    return 1;
  }
  if (xt_typed_array_kind_of(value) >= 0) {
    *out = xt_ta_length(value);
    return 1;
  }
  if (XT_IS_OBJECT(value)) {
    xt_value length = xt_object_get_cstr(value, "length");
    if (length != XT_UNDEFINED) {
      double size = xt_to_number(length);
      *out = size > 0 ? (uint32_t)size : 0;
      return 1;
    }
  }
  return 0;
}

static xt_value xt_ta_from_value(int kind, xt_value value, xt_value mapFn, xt_value thisArg, int haveMap) {
  uint32_t length = 0;
  if (!xt_ta_length_of(value, &length)) length = 0;
  xt_value array = xt_ta_new(kind, length);
  for (uint32_t i = 0; i < length; i++) {
    xt_value item = xt_get(value, xt_number((double)i));
    if (haveMap) {
      xt_value args[3] = {item, xt_number((double)i), array};
      item = xt_call_with_this(mapFn, thisArg, 3, args);
    }
    xt_ta_store(array, kind, i, xt_to_number(item));
  }
  return array;
}

static xt_value xt_ta_ctor_impl(int kind, int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_ta_new(kind, 0);
  xt_value argument = argv[0];
  /* Objects (arrays / typed arrays / array-likes / iterables) are copied
   * element-wise; every other argument is a length via `ToIndex`. */
  if (XT_IS_ARRAY(argument) || XT_IS_OBJECT(argument)) {
    return xt_ta_from_value(kind, argument, XT_UNDEFINED, XT_UNDEFINED, 0);
  }
  double size = xt_to_number(argument);
  return xt_ta_new(kind, size > 0 ? (uint32_t)trunc(size) : 0);
}

static xt_value xt_ta_static_impl(int kind, xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return XT_UNDEFINED;
  if (strcmp(method, "from") == 0) {
    int haveMap = argc > 1 && XT_IS_FUNCTION(argv[1]);
    return xt_ta_from_value(kind, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), xt_arg_at(argc, argv, 2), haveMap);
  }
  if (strcmp(method, "of") == 0) {
    xt_value array = xt_ta_new(kind, (uint32_t)argc);
    for (int32_t i = 0; i < argc; i++) xt_ta_store(array, kind, (uint32_t)i, xt_to_number(argv[i]));
    return array;
  }
  return XT_UNDEFINED;
}

#define XT_TA_CTOR(suffix, kind) \
  xt_value xt_ta_##suffix##_ctor(int32_t argc, xt_value *argv) { return xt_ta_ctor_impl(kind, argc, argv); } \
  xt_value xt_ta_##suffix##_static(xt_value name, int32_t argc, xt_value *argv) { \
    return xt_ta_static_impl(kind, name, argc, argv); \
  }

XT_TA_CTOR(u8, XT_TA_U8)
XT_TA_CTOR(i8, XT_TA_I8)
XT_TA_CTOR(u8c, XT_TA_U8C)
XT_TA_CTOR(u16, XT_TA_U16)
XT_TA_CTOR(i16, XT_TA_I16)
XT_TA_CTOR(u32, XT_TA_U32)
XT_TA_CTOR(i32, XT_TA_I32)
XT_TA_CTOR(f32, XT_TA_F32)
XT_TA_CTOR(f64, XT_TA_F64)

/* -- helpers -------------------------------------------------------------- */

static int32_t xt_ta_relative(int32_t index, int32_t length) {
  if (index < 0) index += length;
  if (index < 0) index = 0;
  if (index > length) index = length;
  return index;
}

static xt_value xt_ta_append(xt_value array, int kind, xt_value value) {
  uint32_t length = xt_ta_length(array);
  xt_object_set(array, xt_string_from_cstr("length"), xt_number((double)(length + 1)));
  xt_object_set(array, xt_string_from_cstr("byteLength"), xt_number((double)(length + 1) * xt_ta_bytes[kind]));
  xt_ta_store(array, kind, length, xt_to_number(value));
  return array;
}

/* -- methods -------------------------------------------------------------- */

static xt_value xt_ta_method_fill(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t start = xt_ta_relative(argc > 1 ? xt_to_int32(argv[1]) : 0, length);
  int32_t end = xt_ta_relative(argc > 2 && argv[2] != XT_UNDEFINED ? xt_to_int32(argv[2]) : length, length);
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  for (int32_t i = start; i < end; i++) xt_ta_store(self, kind, (uint32_t)i, value);
  return self;
}

static xt_value xt_ta_method_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  xt_value source = xt_arg_at(argc, argv, 0);
  uint32_t sourceLength = 0;
  if (!xt_ta_length_of(source, &sourceLength)) return XT_UNDEFINED;
  int32_t offset = argc > 1 ? xt_to_int32(argv[1]) : 0;
  if (offset < 0 || offset + (int32_t)sourceLength > (int32_t)xt_ta_length(self)) {
    xt_throw(xt_string_from_cstr("RangeError: offset is out of bounds"));
    return XT_UNDEFINED;
  }
  for (uint32_t i = 0; i < sourceLength; i++) {
    xt_value item = xt_get(source, xt_number((double)i));
    xt_ta_store(self, kind, (uint32_t)(offset + (int32_t)i), xt_to_number(item));
  }
  return XT_UNDEFINED;
}

static xt_value xt_ta_method_slice(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t start = xt_ta_relative(argc > 0 && argv[0] != XT_UNDEFINED ? xt_to_int32(argv[0]) : 0, length);
  int32_t end = xt_ta_relative(argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : length, length);
  if (end < start) end = start;
  xt_value out = xt_ta_new(kind, (uint32_t)(end - start));
  for (int32_t i = start; i < end; i++) xt_ta_store(out, kind, (uint32_t)(i - start), xt_to_number(xt_ta_element(self, (uint32_t)i)));
  return out;
}

static xt_value xt_ta_method_join(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value separator = argc > 0 && argv[0] != XT_UNDEFINED ? xt_to_string(argv[0]) : xt_string_from_cstr(",");
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value result = xt_string_from_cstr("");
  for (int32_t i = 0; i < length; i++) {
    if (i > 0) result = xt_add(result, separator);
    result = xt_add(result, xt_to_string(xt_ta_element(self, (uint32_t)i)));
  }
  return result;
}

static xt_value xt_ta_method_to_string(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)argc;
  return xt_ta_method_join(self, env, 0, argv);
}

static xt_value xt_ta_method_index_of(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value needle = xt_arg_at(argc, argv, 0);
  int32_t from = argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : 0;
  if (from < 0) from += length;
  if (from < 0) from = 0;
  for (int32_t i = from; i < length; i++) {
    if (xt_value_equals(xt_ta_element(self, (uint32_t)i), needle)) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_ta_method_last_index_of(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value needle = xt_arg_at(argc, argv, 0);
  int32_t from = argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : length - 1;
  if (from < 0) from += length;
  if (from >= length) from = length - 1;
  for (int32_t i = from; i >= 0; i--) {
    if (xt_value_equals(xt_ta_element(self, (uint32_t)i), needle)) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_ta_method_includes(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value needle = xt_arg_at(argc, argv, 0);
  int32_t from = argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : 0;
  if (from < 0) from += length;
  if (from < 0) from = 0;
  for (int32_t i = from; i < length; i++) {
    if (xt_value_equals(xt_ta_element(self, (uint32_t)i), needle)) return XT_TRUE;
  }
  return XT_FALSE;
}

static xt_value xt_ta_method_for_each(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    xt_call_with_this(callback, thisArg, 3, args);
  }
  return XT_UNDEFINED;
}

static xt_value xt_ta_method_map(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_ta_new(kind, (uint32_t)length);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    xt_ta_store(out, kind, (uint32_t)i, xt_to_number(xt_call_with_this(callback, thisArg, 3, args)));
  }
  return out;
}

static xt_value xt_ta_method_filter(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return XT_UNDEFINED;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_ta_new(kind, 0);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) {
      xt_ta_append(out, kind, xt_ta_element(self, (uint32_t)i));
    }
  }
  return out;
}

static xt_value xt_ta_method_every(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (!xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return XT_FALSE;
  }
  return XT_TRUE;
}

static xt_value xt_ta_method_some(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return XT_TRUE;
  }
  return XT_FALSE;
}

static xt_value xt_ta_method_find(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value element = xt_ta_element(self, (uint32_t)i);
    xt_value args[3] = {element, xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return element;
  }
  return XT_UNDEFINED;
}

static xt_value xt_ta_method_find_index(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  xt_value thisArg = xt_arg_at(argc, argv, 1);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length; i++) {
    xt_value args[3] = {xt_ta_element(self, (uint32_t)i), xt_number((double)i), self};
    if (xt_truthy(xt_call_with_this(callback, thisArg, 3, args))) return xt_number((double)i);
  }
  return xt_number(-1);
}

static xt_value xt_ta_method_reduce(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value callback = xt_arg_at(argc, argv, 0);
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t index = 0;
  xt_value accumulator;
  if (argc > 1) {
    accumulator = argv[1];
  } else if (length > 0) {
    accumulator = xt_ta_element(self, 0);
    index = 1;
  } else {
    xt_throw(xt_string_from_cstr("TypeError: Reduce of empty array with no initial value"));
    return XT_UNDEFINED;
  }
  for (; index < length; index++) {
    xt_value args[4] = {accumulator, xt_ta_element(self, (uint32_t)index), xt_number((double)index), self};
    accumulator = xt_call_with_this(callback, XT_UNDEFINED, 4, args);
  }
  return accumulator;
}

static xt_value xt_ta_method_reverse(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 0; i < length / 2; i++) {
    double left = xt_to_number(xt_ta_element(self, (uint32_t)i));
    double right = xt_to_number(xt_ta_element(self, (uint32_t)(length - 1 - i)));
    xt_ta_store(self, kind, (uint32_t)i, right);
    xt_ta_store(self, kind, (uint32_t)(length - 1 - i), left);
  }
  return self;
}

static int xt_ta_compare(xt_value comparator, xt_value a, xt_value b) {
  if (XT_IS_FUNCTION(comparator)) {
    xt_value args[2] = {a, b};
    double result = xt_to_number(xt_call_with_this(comparator, XT_UNDEFINED, 2, args));
    return result < 0 ? -1 : result > 0 ? 1 : 0;
  }
  double x = xt_to_number(a);
  double y = xt_to_number(b);
  if (isnan(x)) return isnan(y) ? 0 : 1;
  if (isnan(y)) return -1;
  return x < y ? -1 : x > y ? 1 : 0;
}

static xt_value xt_ta_method_sort(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  xt_value comparator = xt_arg_at(argc, argv, 0);
  int32_t length = (int32_t)xt_ta_length(self);
  for (int32_t i = 1; i < length; i++) {
    xt_value key = xt_ta_element(self, (uint32_t)i);
    int32_t j = i - 1;
    while (j >= 0 && xt_ta_compare(comparator, xt_ta_element(self, (uint32_t)j), key) > 0) {
      xt_ta_store(self, kind, (uint32_t)(j + 1), xt_to_number(xt_ta_element(self, (uint32_t)j)));
      j--;
    }
    xt_ta_store(self, kind, (uint32_t)(j + 1), xt_to_number(key));
  }
  return self;
}

static xt_value xt_ta_method_copy_within(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int kind = xt_typed_array_kind_of(self);
  if (kind < 0) return self;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t target = xt_ta_relative(xt_to_int32(xt_arg_at(argc, argv, 0)), length);
  int32_t start = xt_ta_relative(argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : 0, length);
  int32_t end = xt_ta_relative(argc > 2 && argv[2] != XT_UNDEFINED ? xt_to_int32(argv[2]) : length, length);
  int32_t count = end - start;
  if (count > length - target) count = length - target;
  if (count <= 0 || target == start) return self;
  xt_value *temporary = (xt_value *)malloc(sizeof(xt_value) * (size_t)count);
  if (!temporary) return self;
  for (int32_t i = 0; i < count; i++) temporary[i] = xt_ta_element(self, (uint32_t)(start + i));
  for (int32_t i = 0; i < count; i++) xt_ta_store(self, kind, (uint32_t)(target + i), xt_to_number(temporary[i]));
  free(temporary);
  return self;
}

static xt_value xt_ta_method_at(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_ta_length(self);
  int32_t index = xt_to_int32(xt_arg_at(argc, argv, 0));
  if (index < 0) index += length;
  if (index < 0 || index >= length) return XT_UNDEFINED;
  return xt_ta_element(self, (uint32_t)index);
}

static xt_value xt_ta_method_keys(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_array_new(0, NULL);
  for (int32_t i = 0; i < length; i++) xt_array_push(out, xt_number((double)i));
  return out;
}

static xt_value xt_ta_method_values(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_array_new(0, NULL);
  for (int32_t i = 0; i < length; i++) xt_array_push(out, xt_ta_element(self, (uint32_t)i));
  return out;
}

static xt_value xt_ta_method_entries(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int32_t length = (int32_t)xt_ta_length(self);
  xt_value out = xt_array_new(0, NULL);
  for (int32_t i = 0; i < length; i++) {
    xt_value pair = xt_array_new(0, NULL);
    xt_array_push(pair, xt_number((double)i));
    xt_array_push(pair, xt_ta_element(self, (uint32_t)i));
    xt_array_push(out, pair);
  }
  return out;
}
