/* xbintsc runtime - typed array element conversion, storage and helpers.
 * Included by `xt_typed_array.c` as part of a single translation unit. */

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

const char *xt_typed_array_name_of(int kind) {
  if (kind < 0 || kind >= XT_TA_KIND_COUNT) return NULL;
  return xt_ta_names[kind];
}

/* Public wrappers for extensions (ext_gui's audio decoder reads a Float32Array's
 * bytes). `.buffer`/`.byteOffset` are accessors on the typed-array prototypes,
 * so a raw `xt_object_get_cstr` cannot reach them. */
int xt_is_typed_array(xt_value value) { return xt_typed_array_kind_of(value) >= 0; }

xt_value xt_typed_array_backing_buffer(xt_value value) {
  return xt_typed_array_kind_of(value) >= 0 ? xt_typed_array_buffer_of(value) : XT_UNDEFINED;
}

uint32_t xt_typed_array_view_offset(xt_value value) {
  return xt_typed_array_kind_of(value) >= 0 ? xt_typed_array_byte_offset_of(value) : 0;
}

/* -- buffer-backed views -------------------------------------------------- */

/* The backing ArrayBuffer and byte offset live under private symbols, so a
 * typed array's observable own properties stay `length`/`byteLength`/
 * `byteOffset` plus `.buffer` (handled in access.inc). */
static xt_value xt_ta_buffer_key_value = XT_UNDEFINED;
static xt_value xt_ta_offset_key_value = XT_UNDEFINED;

static xt_value xt_ta_buffer_key(void) {
  if (!xt_is_symbol(xt_ta_buffer_key_value)) {
    xt_ta_buffer_key_value = xt_symbol_new(xt_string_from_cstr("__typedArrayBuffer"));
    xt_gc_add_root(&xt_ta_buffer_key_value);
  }
  return xt_ta_buffer_key_value;
}

static xt_value xt_ta_offset_key(void) {
  if (!xt_is_symbol(xt_ta_offset_key_value)) {
    xt_ta_offset_key_value = xt_symbol_new(xt_string_from_cstr("__typedArrayOffset"));
    xt_gc_add_root(&xt_ta_offset_key_value);
  }
  return xt_ta_offset_key_value;
}

xt_value xt_typed_array_buffer_of(xt_value array) {
  if (xt_typed_array_kind_of(array) < 0) return XT_UNDEFINED;
  return xt_object_get(array, xt_ta_buffer_key());
}

uint32_t xt_typed_array_byte_offset_of(xt_value array) {
  if (xt_typed_array_kind_of(array) < 0) return 0;
  return (uint32_t)xt_to_number(xt_object_get(array, xt_ta_offset_key()));
}

xt_value xt_typed_array_read(xt_value array, uint32_t index) {
  int kind = xt_typed_array_kind_of(array);
  if (kind < 0) return XT_UNDEFINED;
  xt_value buffer = xt_typed_array_buffer_of(array);
  if (!xt_is_array_buffer(buffer)) {
    char key[24];
    snprintf(key, sizeof(key), "%u", index);
    return xt_object_get_cstr(array, key);
  }
  uint32_t width = (uint32_t)xt_ta_bytes[kind];
  uint32_t offset = xt_typed_array_byte_offset_of(array) + index * width;
  if (offset + width > xt_array_buffer_byte_length(buffer)) return XT_UNDEFINED;
  uint8_t raw[8];
  for (uint32_t i = 0; i < width; i++) raw[i] = xt_array_buffer_get_byte(buffer, offset + i);
  switch (kind) {
    case XT_TA_U8:
      return xt_number((double)raw[0]);
    case XT_TA_I8:
      return xt_number((double)(int8_t)raw[0]);
    case XT_TA_U8C:
      return xt_number((double)raw[0]);
    case XT_TA_U16: {
      uint16_t value;
      memcpy(&value, raw, sizeof(value));
      return xt_number((double)value);
    }
    case XT_TA_I16: {
      int16_t value;
      memcpy(&value, raw, sizeof(value));
      return xt_number((double)value);
    }
    case XT_TA_U32: {
      uint32_t value;
      memcpy(&value, raw, sizeof(value));
      return xt_number((double)value);
    }
    case XT_TA_I32: {
      int32_t value;
      memcpy(&value, raw, sizeof(value));
      return xt_number((double)value);
    }
    case XT_TA_F32: {
      float value;
      memcpy(&value, raw, sizeof(value));
      return xt_number((double)value);
    }
    default: {
      double value;
      memcpy(&value, raw, sizeof(value));
      return xt_number(value);
    }
  }
}

void xt_typed_array_write(xt_value array, uint32_t index, xt_value value) {
  int kind = xt_typed_array_kind_of(array);
  if (kind < 0) return;
  xt_value buffer = xt_typed_array_buffer_of(array);
  if (!xt_is_array_buffer(buffer)) {
    char key[24];
    snprintf(key, sizeof(key), "%u", index);
    xt_object_set(array, xt_string_from_cstr(key),
                  xt_number(xt_typed_array_coerce(kind, xt_to_number(value))));
    return;
  }
  uint32_t width = (uint32_t)xt_ta_bytes[kind];
  uint32_t offset = xt_typed_array_byte_offset_of(array) + index * width;
  if (offset + width > xt_array_buffer_byte_length(buffer)) return;
  double coerced = xt_typed_array_coerce(kind, xt_to_number(value));
  uint8_t raw[8];
  memset(raw, 0, sizeof(raw));
  switch (kind) {
    case XT_TA_U8:
    case XT_TA_U8C:
      raw[0] = (uint8_t)coerced;
      break;
    case XT_TA_I8:
      raw[0] = (uint8_t)(int8_t)coerced;
      break;
    case XT_TA_U16: {
      uint16_t stored = (uint16_t)coerced;
      memcpy(raw, &stored, sizeof(stored));
      break;
    }
    case XT_TA_I16: {
      int16_t stored = (int16_t)coerced;
      memcpy(raw, &stored, sizeof(stored));
      break;
    }
    case XT_TA_U32: {
      uint32_t stored = (uint32_t)coerced;
      memcpy(raw, &stored, sizeof(stored));
      break;
    }
    case XT_TA_I32: {
      int32_t stored = (int32_t)coerced;
      memcpy(raw, &stored, sizeof(stored));
      break;
    }
    case XT_TA_F32: {
      float stored = (float)coerced;
      memcpy(raw, &stored, sizeof(stored));
      break;
    }
    default: {
      double stored = coerced;
      memcpy(raw, &stored, sizeof(stored));
      break;
    }
  }
  for (uint32_t i = 0; i < width; i++) xt_array_buffer_set_byte(buffer, offset + i, raw[i]);
}

/* -- element storage ------------------------------------------------------ */

static uint32_t xt_ta_length(xt_value array) {
  double length = xt_to_number(xt_object_get_cstr(array, "length"));
  if (!(length > 0)) return 0;
  return (uint32_t)length;
}

static xt_value xt_ta_element(xt_value array, uint32_t index) {
  if (xt_is_array_buffer(xt_typed_array_buffer_of(array))) {
    return xt_typed_array_read(array, index);
  }
  char key[24];
  snprintf(key, sizeof(key), "%u", index);
  return xt_object_get_cstr(array, key);
}

static void xt_ta_store(xt_value array, int kind, uint32_t index, double value) {
  if (xt_is_array_buffer(xt_typed_array_buffer_of(array))) {
    xt_typed_array_write(array, index, xt_number(value));
    return;
  }
  char key[24];
  snprintf(key, sizeof(key), "%u", index);
  xt_object_set(array, xt_string_from_cstr(key), xt_number(xt_typed_array_coerce(kind, value)));
}

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

