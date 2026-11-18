/*
 * xbintsc runtime — ArrayBuffer and DataView.
 *
 * An ArrayBuffer is a plain object holding its bytes as an array of numbers
 * under a private symbol, plus a public `byteLength`. A DataView is another
 * plain object that points at the buffer and exposes the get/set accessors;
 * every accessor assembles the bytes explicitly, so the default big-endian
 * order and the optional `littleEndian` argument follow ECMAScript.
 *
 * Keeping both as ordinary objects means the garbage collector, property
 * lookup and the rest of the runtime need no new object kind. Typed arrays
 * are buffer-backed views over this object and share its bytes; `new
 * Uint8Array(buffer, offset, length)`, `.buffer` and `subarray` are handled by
 * xt_typed_array.c.
 */

#include "rt_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Storage: an ArrayBuffer is an ordinary object of kind
 * `XT_OBJECT_KIND_ARRAY_BUFFER` whose bytes live in one `calloc`ed block
 * (`buffer_bytes`). Typed arrays and DataViews index that block directly, so a
 * geometry loop reads memory instead of paying a property lookup per byte.
 *
 * The block belongs to the buffer object and the collector frees it when the
 * buffer is swept (see `xt_gc_release`). `byteLength` and `slice` are served by
 * `xt_get` from this kind instead of being own properties, so an ArrayBuffer has
 * no enumerable keys (`Object.keys(buffer)` is empty) like on the platform.
 */

static int xt_ab_is_buffer(xt_value value) {
  return XT_IS_OBJECT(value) &&
         ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_ARRAY_BUFFER;
}

static xt_value xt_ab_new(uint32_t length) {
  /* Instances sit on `ArrayBuffer.prototype`, so `buffer instanceof ArrayBuffer`
   * and `buffer.constructor` behave like the platform (the properties are still
   * served from the kind, not from the prototype). */
  xt_value proto = xt_builtin_prototype_for_kind(XT_OBJECT_KIND_ARRAY_BUFFER);
  xt_value buffer = XT_IS_OBJECT(proto) ? xt_object_new_with_proto(proto) : xt_object_new();
  xt_object *object = (xt_object *)XT_GET_PTR(buffer);
  object->header.kind = (uint8_t)XT_OBJECT_KIND_ARRAY_BUFFER;
  /* A zero-length buffer keeps a NULL block: nothing ever indexes it. */
  object->buffer_bytes = length > 0 ? (uint8_t *)calloc(1, length) : NULL;
  object->buffer_length = object->buffer_bytes != NULL ? length : 0;
  return buffer;
}

static xt_value xt_ab_slice(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);

/** The single `slice` method value `xt_get` hands out for an ArrayBuffer. */
xt_value xt_array_buffer_slice_value(void) {
  static xt_value cached = XT_UNDEFINED;
  if (XT_IS_FUNCTION(cached)) return cached;
  cached = xt_closure_new((void *)xt_ab_slice, 0, NULL);
  xt_gc_add_root(&cached);
  return cached;
}

/** The properties an ArrayBuffer serves from its kind rather than from a
 * property bag: `byteLength` and `slice`. `xt_get` and `xt_object_get` both
 * consult this, so a C helper and JavaScript code agree. */
xt_value xt_array_buffer_served_property(xt_value buffer, xt_value key) {
  if (!XT_IS_STRING(key)) return XT_UNDEFINED;
  xt_object *object = (xt_object *)XT_GET_PTR(buffer);
  xt_string *name = xt_as_string(key);
  if (name == NULL) return XT_UNDEFINED;
  if (name->length == 10 && memcmp(xt_string_bytes(name), "byteLength", 10) == 0) {
    return xt_number((double)object->buffer_length);
  }
  if (name->length == 5 && memcmp(xt_string_bytes(name), "slice", 5) == 0) {
    return xt_array_buffer_slice_value();
  }
  return XT_UNDEFINED;
}

xt_value xt_array_buffer_ctor(int32_t argc, xt_value *argv) {
  double length = xt_to_number(xt_arg_at(argc, argv, 0));
  if (!(length >= 0) || length > 4294967295.0) length = 0;
  return xt_ab_new((uint32_t)length);
}

xt_value xt_array_buffer_alloc(uint32_t byte_length) { return xt_ab_new(byte_length); }

int xt_is_array_buffer(xt_value value) { return xt_ab_is_buffer(value); }

uint32_t xt_array_buffer_byte_length(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  xt_object *object = (xt_object *)XT_GET_PTR(value);
  if (object->header.kind != XT_OBJECT_KIND_ARRAY_BUFFER) return 0;
  return object->buffer_length;
}

uint8_t *xt_array_buffer_bytes(xt_value buffer, uint32_t *byte_length) {
  if (byte_length != NULL) *byte_length = 0;
  if (!XT_IS_OBJECT(buffer)) return NULL;
  xt_object *object = (xt_object *)XT_GET_PTR(buffer);
  if (object->header.kind != XT_OBJECT_KIND_ARRAY_BUFFER) return NULL;
  if (byte_length != NULL) *byte_length = object->buffer_length;
  return object->buffer_bytes;
}

uint8_t xt_array_buffer_get_byte(xt_value buffer, uint32_t index) {
  uint32_t length = 0;
  uint8_t *bytes = xt_array_buffer_bytes(buffer, &length);
  if (bytes == NULL || index >= length) return 0;
  return bytes[index];
}

void xt_array_buffer_set_byte(xt_value buffer, uint32_t index, uint8_t value) {
  uint32_t length = 0;
  uint8_t *bytes = xt_array_buffer_bytes(buffer, &length);
  if (bytes == NULL || index >= length) return;
  bytes[index] = value;
}

/* Bulk byte access for decoders and wasm memories. */
uint32_t xt_array_buffer_copy_bytes_out(xt_value buffer, uint32_t offset, uint8_t *dst,
                                        uint32_t count) {
  uint32_t length = 0;
  uint8_t *bytes = xt_array_buffer_bytes(buffer, &length);
  if (bytes == NULL || dst == NULL) return 0;
  if (offset >= length) return 0;
  if (count > length - offset) count = length - offset;
  memcpy(dst, bytes + offset, count);
  return count;
}

uint32_t xt_array_buffer_copy_bytes_in(xt_value buffer, uint32_t offset, const uint8_t *src,
                                       uint32_t count) {
  uint32_t length = 0;
  uint8_t *bytes = xt_array_buffer_bytes(buffer, &length);
  if (bytes == NULL || src == NULL) return 0;
  if (offset >= length) return 0;
  if (count > length - offset) count = length - offset;
  memcpy(bytes + offset, src, count);
  return count;
}

static xt_value xt_ab_slice(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int32_t length = (int32_t)xt_array_buffer_byte_length(thisValue);
  int32_t start = argc > 0 && argv[0] != XT_UNDEFINED ? xt_to_int32(argv[0]) : 0;
  int32_t end = argc > 1 && argv[1] != XT_UNDEFINED ? xt_to_int32(argv[1]) : length;
  if (start < 0) start += length;
  if (end < 0) end += length;
  if (start < 0) start = 0;
  if (end > length) end = length;
  if (end < start) end = start;
  xt_value result = xt_ab_new((uint32_t)(end - start));
  xt_array_buffer_copy_bytes_out(thisValue, (uint32_t)start,
                                 xt_array_buffer_bytes(result, NULL), (uint32_t)(end - start));
  return result;
}

xt_value xt_array_buffer_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return XT_UNDEFINED;
  if (strcmp(method, "isView") == 0) {
    xt_value value = xt_arg_at(argc, argv, 0);
    if (xt_typed_array_kind_of(value) >= 0) return XT_TRUE;
    if (!XT_IS_OBJECT(value)) return XT_FALSE;
    if (((xt_object *)XT_GET_PTR(value))->header.kind != XT_OBJECT_KIND_OBJECT) return XT_FALSE;
    xt_value buffer = xt_object_get(value, xt_string_from_cstr("buffer"));
    return xt_bool(xt_ab_is_buffer(buffer) &&
                   xt_object_get(value, xt_string_from_cstr("byteLength")) != XT_UNDEFINED);
  }
  return XT_UNDEFINED;
}

/* -- DataView ------------------------------------------------------------- */

enum {
  XT_DV_U8,
  XT_DV_I8,
  XT_DV_U16,
  XT_DV_I16,
  XT_DV_U32,
  XT_DV_I32,
  XT_DV_F32,
  XT_DV_F64,
};

static uint32_t xt_dv_width(int kind) {
  switch (kind) {
    case XT_DV_U8:
    case XT_DV_I8:
      return 1;
    case XT_DV_U16:
    case XT_DV_I16:
      return 2;
    case XT_DV_U32:
    case XT_DV_I32:
    case XT_DV_F32:
      return 4;
    default:
      return 8;
  }
}

static int xt_dv_signed(int kind) {
  return kind == XT_DV_I8 || kind == XT_DV_I16 || kind == XT_DV_I32;
}

static xt_value xt_dv_buffer(xt_value view) {
  return xt_get(view, xt_string_from_cstr("buffer"));
}

static uint32_t xt_dv_offset(xt_value view) {
  return (uint32_t)xt_to_number(xt_get(view, xt_string_from_cstr("byteOffset")));
}

static uint32_t xt_dv_length(xt_value view) {
  return (uint32_t)xt_to_number(xt_get(view, xt_string_from_cstr("byteLength")));
}

static int xt_dv_check(xt_value view, xt_value indexValue, uint32_t width, uint32_t *out) {
  double raw = xt_to_number(indexValue);
  if (!(raw >= 0)) raw = -1;
  int64_t index = (int64_t)raw;
  if (index < 0 || (uint64_t)index + width > xt_dv_length(view)) {
    xt_value message = xt_string_from_cstr("Offset is outside the bounds of the DataView");
    xt_throw(xt_range_error_ctor(1, &message));
  }
  *out = xt_dv_offset(view) + (uint32_t)index;
  return 1;
}

static uint64_t xt_dv_read_bits(xt_value view, uint32_t offset, uint32_t width, int little) {
  uint32_t length = 0;
  uint8_t *bytes = xt_array_buffer_bytes(xt_dv_buffer(view), &length);
  uint64_t value = 0;
  if (bytes == NULL || offset + width > length) return 0;
  for (uint32_t i = 0; i < width; i++) {
    uint32_t index = little ? offset + width - 1 - i : offset + i;
    value = (value << 8) | (uint64_t)bytes[index];
  }
  return value;
}

static void xt_dv_write_bits(xt_value view, uint32_t offset, uint32_t width, uint64_t value,
                             int little) {
  uint32_t length = 0;
  uint8_t *bytes = xt_array_buffer_bytes(xt_dv_buffer(view), &length);
  if (bytes == NULL || offset + width > length) return;
  for (uint32_t i = 0; i < width; i++) {
    uint32_t shift = little ? i * 8 : (width - 1 - i) * 8;
    bytes[offset + i] = (uint8_t)((value >> shift) & 0xff);
  }
}

static xt_value xt_dv_method(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  int kind = (int)xt_to_number(xt_closure_env(env, 0));
  int setter = (int)xt_to_number(xt_closure_env(env, 1));
  uint32_t width = xt_dv_width(kind);
  uint32_t offset = 0;
  xt_dv_check(thisValue, xt_arg_at(argc, argv, 0), width, &offset);
  int little = argc > (setter ? 2 : 1) && xt_truthy(xt_arg_at(argc, argv, setter ? 2 : 1));
  if (setter) {
    double value = xt_to_number(xt_arg_at(argc, argv, 1));
    uint64_t bits;
    if (kind == XT_DV_F32) {
      float narrowed = (float)value;
      uint32_t raw;
      memcpy(&raw, &narrowed, sizeof(raw));
      bits = raw;
    } else if (kind == XT_DV_F64) {
      double raw = value;
      memcpy(&bits, &raw, sizeof(bits));
    } else if (kind == XT_DV_I8) {
      bits = (uint64_t)(uint8_t)(int8_t)xt_to_int32(xt_number(value));
    } else if (kind == XT_DV_I16) {
      bits = (uint64_t)(uint16_t)(int16_t)xt_to_int32(xt_number(value));
    } else if (kind == XT_DV_I32) {
      bits = (uint64_t)(uint32_t)(int32_t)xt_to_int32(xt_number(value));
    } else {
      bits = (uint64_t)(uint32_t)xt_to_int32(xt_number(value));
    }
    xt_dv_write_bits(thisValue, offset, width, bits, little);
    return XT_UNDEFINED;
  }
  uint64_t bits = xt_dv_read_bits(thisValue, offset, width, little);
  if (kind == XT_DV_F32) {
    uint32_t raw = (uint32_t)bits;
    float value;
    memcpy(&value, &raw, sizeof(value));
    return xt_number((double)value);
  }
  if (kind == XT_DV_F64) {
    double value;
    memcpy(&value, &bits, sizeof(value));
    return xt_number(value);
  }
  if (xt_dv_signed(kind)) {
    int64_t signedValue = (int64_t)bits;
    if (width < 8) {
      uint64_t signBit = 1ULL << (width * 8 - 1);
      if (bits & signBit) signedValue = (int64_t)(bits | (~0ULL << (width * 8)));
    }
    return xt_number((double)signedValue);
  }
  return xt_number((double)bits);
}

xt_value xt_data_view_ctor(int32_t argc, xt_value *argv) {
  xt_value buffer = xt_arg_at(argc, argv, 0);
  if (!xt_ab_is_buffer(buffer)) {
    xt_value message = xt_string_from_cstr("First argument to DataView constructor must be an ArrayBuffer");
    xt_throw(xt_type_error_ctor(1, &message));
  }
  uint32_t bufferLength = (uint32_t)xt_to_number(xt_get(buffer, xt_string_from_cstr("byteLength")));
  double rawOffset = argc > 1 ? xt_to_number(argv[1]) : 0;
  uint32_t offset = rawOffset > 0 ? (uint32_t)rawOffset : 0;
  double rawLength = argc > 2 && argv[2] != XT_UNDEFINED ? xt_to_number(argv[2]) : (double)(bufferLength - offset);
  uint32_t length = rawLength > 0 ? (uint32_t)rawLength : 0;
  if (offset > bufferLength || length > bufferLength - offset) {
    xt_value message = xt_string_from_cstr("Invalid DataView length");
    xt_throw(xt_range_error_ctor(1, &message));
  }
  xt_value view = xt_object_new();
  xt_object_set(view, xt_string_from_cstr("buffer"), buffer);
  xt_object_set(view, xt_string_from_cstr("byteOffset"), xt_number((double)offset));
  xt_object_set(view, xt_string_from_cstr("byteLength"), xt_number((double)length));
  static const char *const getters[] = {"getUint8", "getInt8", "getUint16", "getInt16",
                                        "getUint32", "getInt32", "getFloat32", "getFloat64"};
  static const char *const setters[] = {"setUint8", "setInt8", "setUint16", "setInt16",
                                        "setUint32", "setInt32", "setFloat32", "setFloat64"};
  for (int kind = 0; kind < 8; kind++) {
    for (int isSetter = 0; isSetter < 2; isSetter++) {
      xt_value environment[2] = {xt_number((double)kind), xt_number((double)isSetter)};
      xt_value closure = xt_closure_new((void *)xt_dv_method, 2, environment);
      xt_object_set(view, xt_string_from_cstr(isSetter ? setters[kind] : getters[kind]), closure);
    }
  }
  return view;
}

