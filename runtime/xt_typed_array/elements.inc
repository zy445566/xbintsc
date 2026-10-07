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

