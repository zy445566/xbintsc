/*
 * Node.js `Buffer` for xbintsc.
 *
 * xbintsc has no dedicated binary value type, so a Buffer is represented as a
 * plain object: indexed properties hold the individual bytes and a `length`
 * property records the size. Instance methods come from a shared prototype of
 * native closures, added by `xt_buffer_static` (`Buffer.from`, `Buffer.alloc`,
 * ...) and the `Buffer` constructor.
 */

#include "../node_common.h"

#include <math.h>

static xt_value xt_buffer_proto(void);

/* -- byte access ---------------------------------------------------------- */

static char *xt_buffer_key(uint32_t index, char *buffer, size_t size) {
  snprintf(buffer, size, "%u", index);
  return buffer;
}

static uint32_t xt_buffer_length(xt_value buffer) {
  double length = xt_to_number(xt_object_get_cstr(buffer, "length"));
  if (length < 0) return 0;
  return (uint32_t)length;
}

static unsigned char xt_buffer_byte(xt_value buffer, uint32_t index) {
  char key[24];
  xt_value value = xt_object_get_cstr(buffer, xt_buffer_key(index, key, sizeof(key)));
  return (unsigned char)((int32_t)xt_to_number(value) & 0xff);
}

static void xt_buffer_put(xt_value buffer, uint32_t index, unsigned char byte) {
  char key[24];
  xt_object_set(buffer, xt_string_from_cstr(xt_buffer_key(index, key, sizeof(key))), xt_number((double)byte));
}

static xt_value xt_buffer_new(uint32_t length, const unsigned char *data) {
  xt_value buffer = xt_object_new_with_proto(xt_buffer_proto());
  for (uint32_t i = 0; i < length; i++) xt_buffer_put(buffer, i, data ? data[i] : 0);
  xt_node_set(buffer, "length", xt_number((double)length));
  return buffer;
}

static int xt_buffer_is(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return xt_truthy(xt_seq(xt_object_get_prototype(value), xt_buffer_proto()));
}

/* -- construction --------------------------------------------------------- */

static xt_value xt_buffer_from_bytes(const unsigned char *data, size_t length) {
  return xt_buffer_new((uint32_t)length, data);
}

static xt_value xt_buffer_from(xt_value value, const char *encoding) {
  if (xt_buffer_is(value)) {
    uint32_t length = xt_buffer_length(value);
    unsigned char *copy = (unsigned char *)malloc(length ? length : 1);
    for (uint32_t i = 0; i < length; i++) copy[i] = xt_buffer_byte(value, i);
    xt_value result = xt_buffer_new(length, copy);
    free(copy);
    return result;
  }
  if (XT_IS_ARRAY(value)) {
    uint32_t length = (uint32_t)xt_to_number(xt_array_length(value));
    xt_value result = xt_buffer_new(length, NULL);
    for (uint32_t i = 0; i < length; i++) {
      xt_value item = xt_array_get(value, xt_number((double)i));
      xt_buffer_put(result, i, (unsigned char)((int32_t)xt_to_number(item) & 0xff));
    }
    return result;
  }
  xt_value text = xt_to_string(value);
  xt_string *string = xt_as_string(text);
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  if (!bytes) return xt_buffer_new(0, NULL);
  xt_value result = xt_buffer_from_bytes(bytes, decoded);
  free(bytes);
  return result;
}

static xt_value xt_buffer_from_args(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_buffer_new(0, NULL);
  const char *encoding = argc > 1 ? xt_node_encoding(argv[1]) : NULL;
  return xt_buffer_from(argv[0], encoding);
}

static xt_value xt_buffer_alloc(uint32_t size, xt_value fill, const char *encoding) {
  xt_value buffer = xt_buffer_new(size, NULL);
  if (XT_IS_UNDEFINED(fill)) return buffer;
  if (XT_IS_NUMBER(fill)) {
    unsigned char byte = (unsigned char)((int32_t)xt_to_number(fill) & 0xff);
    for (uint32_t i = 0; i < size; i++) xt_buffer_put(buffer, i, byte);
    return buffer;
  }
  xt_value text = xt_to_string(fill);
  xt_string *string = xt_as_string(text);
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  if (!bytes || decoded == 0) {
    free(bytes);
    return buffer;
  }
  for (uint32_t i = 0; i < size; i++) xt_buffer_put(buffer, i, bytes[i % decoded]);
  free(bytes);
  return buffer;
}

/* -- static API ----------------------------------------------------------- */

xt_value xt_buffer_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_buffer_new(0, NULL);
  if (XT_IS_NUMBER(argv[0])) {
    return xt_buffer_alloc((uint32_t)xt_to_number(argv[0]), XT_UNDEFINED, NULL);
  }
  return xt_buffer_from_args(argc, argv);
}

xt_value xt_buffer_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "from") == 0 || strcmp(fn, "of") == 0) return xt_buffer_from_args(argc, argv);
  if (strcmp(fn, "alloc") == 0 || strcmp(fn, "allocUnsafe") == 0 || strcmp(fn, "allocUnsafeSlow") == 0) {
    uint32_t size = argc > 0 ? (uint32_t)xt_to_number(argv[0]) : 0;
    const char *encoding = argc > 2 ? xt_node_encoding(argv[2]) : NULL;
    return xt_buffer_alloc(size, argc > 1 ? argv[1] : XT_UNDEFINED, encoding);
  }
  if (strcmp(fn, "isBuffer") == 0) return xt_bool(xt_buffer_is(xt_arg_at(argc, argv, 0)));
  if (strcmp(fn, "byteLength") == 0) {
    xt_value value = xt_arg_at(argc, argv, 0);
    if (xt_buffer_is(value)) return xt_number((double)xt_buffer_length(value));
    const char *encoding = argc > 1 ? xt_node_encoding(argv[1]) : NULL;
    xt_value text = xt_to_string(value);
    xt_string *string = xt_as_string(text);
    if (encoding && strcmp(encoding, "hex") == 0) return xt_number((double)(string->length / 2));
    if (encoding && strcmp(encoding, "base64") == 0) return xt_number((double)(string->length / 4 * 3));
    return xt_number((double)string->length);
  }
  if (strcmp(fn, "concat") == 0) {
    xt_value list = xt_arg_at(argc, argv, 0);
    uint32_t total = 0;
    uint32_t count = XT_IS_ARRAY(list) ? (uint32_t)xt_to_number(xt_array_length(list)) : 0;
    for (uint32_t i = 0; i < count; i++) total += xt_buffer_length(xt_array_get(list, xt_number((double)i)));
    if (argc > 1 && XT_IS_NUMBER(argv[1])) total = (uint32_t)xt_to_number(argv[1]);
    xt_value result = xt_buffer_new(total, NULL);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < count && offset < total; i++) {
      xt_value item = xt_array_get(list, xt_number((double)i));
      uint32_t length = xt_buffer_length(item);
      for (uint32_t j = 0; j < length && offset < total; j++) xt_buffer_put(result, offset++, xt_buffer_byte(item, j));
    }
    return result;
  }
  if (strcmp(fn, "compare") == 0) {
    xt_value a = xt_arg_at(argc, argv, 0);
    xt_value b = xt_arg_at(argc, argv, 1);
    uint32_t la = xt_buffer_length(a), lb = xt_buffer_length(b);
    uint32_t n = la < lb ? la : lb;
    for (uint32_t i = 0; i < n; i++) {
      unsigned char x = xt_buffer_byte(a, i), y = xt_buffer_byte(b, i);
      if (x != y) return xt_number(x < y ? -1 : 1);
    }
    return xt_number(la < lb ? -1 : la > lb ? 1 : 0);
  }
  return xt_undefined();
}

/* -- instance methods ----------------------------------------------------- */

static xt_value xt_buffer_method_to_string(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *encoding = argc > 0 ? xt_node_encoding(argv[0]) : NULL;
  uint32_t length = xt_buffer_length(thisValue);
  uint32_t start = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint32_t end = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : length;
  if (end > length) end = length;
  if (start > end) start = end;
  unsigned char *bytes = (unsigned char *)malloc(end - start + 1);
  for (uint32_t i = start; i < end; i++) bytes[i - start] = xt_buffer_byte(thisValue, i);
  xt_value result = xt_node_encode(bytes, end - start, encoding);
  free(bytes);
  return result;
}

static xt_value xt_buffer_method_to_json(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value object = xt_object_new();
  xt_value type = xt_string_from_cstr("Buffer");
  xt_object_set(object, xt_string_from_cstr("type"), type);
  xt_value data = xt_array_new(0, NULL);
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length; i++) xt_array_push(data, xt_number((double)xt_buffer_byte(thisValue, i)));
  xt_object_set(object, xt_string_from_cstr("data"), data);
  return object;
}

static xt_value xt_buffer_method_slice(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint32_t length = xt_buffer_length(thisValue);
  int32_t start = argc > 0 ? (int32_t)xt_to_number(argv[0]) : 0;
  int32_t end = argc > 1 ? (int32_t)xt_to_number(argv[1]) : (int32_t)length;
  if (start < 0) start += (int32_t)length;
  if (end < 0) end += (int32_t)length;
  if (start < 0) start = 0;
  if (end > (int32_t)length) end = (int32_t)length;
  if (end < start) end = start;
  unsigned char *bytes = (unsigned char *)malloc((size_t)(end - start) + 1);
  for (int32_t i = start; i < end; i++) bytes[i - start] = xt_buffer_byte(thisValue, (uint32_t)i);
  xt_value result = xt_buffer_from_bytes(bytes, (size_t)(end - start));
  free(bytes);
  return result;
}

static xt_value xt_buffer_method_equals(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value other = xt_arg_at(argc, argv, 0);
  if (!xt_buffer_is(other)) return xt_bool(0);
  uint32_t length = xt_buffer_length(thisValue);
  if (length != xt_buffer_length(other)) return xt_bool(0);
  for (uint32_t i = 0; i < length; i++) {
    if (xt_buffer_byte(thisValue, i) != xt_buffer_byte(other, i)) return xt_bool(0);
  }
  return xt_bool(1);
}

static xt_value xt_buffer_method_compare(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value other = xt_arg_at(argc, argv, 0);
  uint32_t la = xt_buffer_length(thisValue), lb = xt_buffer_length(other);
  uint32_t n = la < lb ? la : lb;
  for (uint32_t i = 0; i < n; i++) {
    unsigned char x = xt_buffer_byte(thisValue, i), y = xt_buffer_byte(other, i);
    if (x != y) return xt_number(x < y ? -1 : 1);
  }
  return xt_number(la < lb ? -1 : la > lb ? 1 : 0);
}

static xt_value xt_buffer_method_copy(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value target = xt_arg_at(argc, argv, 0);
  if (!xt_buffer_is(target)) return xt_number(0);
  uint32_t targetStart = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint32_t sourceStart = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : 0;
  uint32_t sourceEnd = argc > 3 ? (uint32_t)xt_to_number(argv[3]) : xt_buffer_length(thisValue);
  uint32_t targetLength = xt_buffer_length(target);
  uint32_t written = 0;
  for (uint32_t i = sourceStart; i < sourceEnd && targetStart + written < targetLength; i++) {
    xt_buffer_put(target, targetStart + written, xt_buffer_byte(thisValue, i));
    written++;
  }
  return xt_number((double)written);
}

static xt_value xt_buffer_method_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value text = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_string *string = xt_as_string(text);
  uint32_t offset = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  const char *encoding = argc > 3 ? xt_node_encoding(argv[3]) : NULL;
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  uint32_t available = xt_buffer_length(thisValue);
  uint32_t written = 0;
  for (size_t i = 0; i < decoded && offset + written < available; i++) {
    xt_buffer_put(thisValue, offset + written, bytes[i]);
    written++;
  }
  free(bytes);
  return xt_number((double)written);
}

static xt_value xt_buffer_method_fill(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint32_t length = xt_buffer_length(thisValue);
  uint32_t start = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint32_t end = argc > 2 ? (uint32_t)xt_to_number(argv[2]) : length;
  if (end > length) end = length;
  unsigned char byte = 0;
  if (XT_IS_STRING(xt_arg_at(argc, argv, 0))) {
    const char *text = xt_string_data(xt_arg_at(argc, argv, 0));
    if (text && text[0]) byte = (unsigned char)text[0];
  } else if (argc > 0) {
    byte = (unsigned char)((int32_t)xt_to_number(argv[0]) & 0xff);
  }
  for (uint32_t i = start; i < end; i++) xt_buffer_put(thisValue, i, byte);
  return thisValue;
}

static xt_value xt_buffer_method_reverse(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length / 2; i++) {
    unsigned char a = xt_buffer_byte(thisValue, i);
    xt_buffer_put(thisValue, i, xt_buffer_byte(thisValue, length - 1 - i));
    xt_buffer_put(thisValue, length - 1 - i, a);
  }
  return thisValue;
}

/* -- search --------------------------------------------------------------- */

static const unsigned char *xt_buffer_search_needle(xt_value value, const char *encoding, size_t *length,
                                                     unsigned char *single) {
  if (XT_IS_NUMBER(value)) {
    *single = (unsigned char)((int32_t)xt_to_number(value) & 0xff);
    *length = 1;
    return single;
  }
  if (xt_buffer_is(value)) {
    uint32_t n = xt_buffer_length(value);
    unsigned char *bytes = (unsigned char *)malloc(n ? n : 1);
    for (uint32_t i = 0; i < n; i++) bytes[i] = xt_buffer_byte(value, i);
    *length = n;
    return bytes;
  }
  xt_value text = xt_to_string(value);
  xt_string *string = xt_as_string(text);
  size_t decoded = 0;
  unsigned char *bytes = xt_node_decode(string->data, string->length, encoding, &decoded);
  *length = decoded;
  return bytes;
}

static xt_value xt_buffer_method_index_of(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv,
                                           int reverse) {
  (void)env;
  const char *encoding = argc > 2 ? xt_node_encoding(argv[2]) : NULL;
  size_t needleLength = 0;
  unsigned char single = 0;
  const unsigned char *needle = xt_buffer_search_needle(xt_arg_at(argc, argv, 0), encoding, &needleLength, &single);
  uint32_t length = xt_buffer_length(thisValue);
  int32_t start = argc > 1 ? (int32_t)xt_to_number(argv[1]) : (reverse ? (int32_t)length - 1 : 0);
  if (start < 0) start += (int32_t)length;
  if (start < 0) start = 0;
  if (needleLength == 0 || needleLength > length) {
    if (!xt_buffer_is(xt_arg_at(argc, argv, 0))) free((void *)needle);
    return xt_number(-1);
  }
  int found = -1;
  if (reverse) {
    if (start > (int32_t)length - 1) start = (int32_t)length - 1;
    for (int32_t i = start; i >= 0; i--) {
      int match = 1;
      for (size_t j = 0; j < needleLength; j++) {
        if ((uint32_t)(i + (int32_t)j) >= length || xt_buffer_byte(thisValue, (uint32_t)(i + (int32_t)j)) != needle[j]) {
          match = 0;
          break;
        }
      }
      if (match) {
        found = i;
        break;
      }
    }
  } else {
    for (uint32_t i = (uint32_t)start; i + needleLength <= length; i++) {
      int match = 1;
      for (size_t j = 0; j < needleLength; j++) {
        if (xt_buffer_byte(thisValue, i + (uint32_t)j) != needle[j]) {
          match = 0;
          break;
        }
      }
      if (match) {
        found = (int)i;
        break;
      }
    }
  }
  if (!xt_buffer_is(xt_arg_at(argc, argv, 0))) free((void *)needle);
  return xt_number((double)found);
}

static xt_value xt_buffer_method_index_of_fwd(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  return xt_buffer_method_index_of(thisValue, env, argc, argv, 0);
}

static xt_value xt_buffer_method_index_of_rev(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  return xt_buffer_method_index_of(thisValue, env, argc, argv, 1);
}

static xt_value xt_buffer_method_includes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value result = xt_buffer_method_index_of(thisValue, env, argc, argv, 0);
  return xt_bool(xt_to_number(result) >= 0);
}

/* -- integer readers / writers -------------------------------------------- */

static uint64_t xt_buffer_read_le(xt_value buffer, uint32_t offset, int bytes) {
  uint64_t value = 0;
  for (int i = 0; i < bytes; i++) value |= ((uint64_t)xt_buffer_byte(buffer, offset + (uint32_t)i)) << (8 * i);
  return value;
}

static uint64_t xt_buffer_read_be(xt_value buffer, uint32_t offset, int bytes) {
  uint64_t value = 0;
  for (int i = 0; i < bytes; i++) value = (value << 8) | xt_buffer_byte(buffer, offset + (uint32_t)i);
  return value;
}

static void xt_buffer_write_le(xt_value buffer, uint32_t offset, int bytes, uint64_t value) {
  for (int i = 0; i < bytes; i++) xt_buffer_put(buffer, offset + (uint32_t)i, (unsigned char)((value >> (8 * i)) & 0xff));
}

static void xt_buffer_write_be(xt_value buffer, uint32_t offset, int bytes, uint64_t value) {
  for (int i = 0; i < bytes; i++) xt_buffer_put(buffer, offset + (uint32_t)i, (unsigned char)((value >> (8 * (bytes - 1 - i))) & 0xff));
}

static xt_value xt_buffer_read_method(xt_value thisValue, int32_t argc, xt_value *argv, int bytes, int little, int signedValue) {
  uint32_t offset = argc > 0 ? (uint32_t)xt_to_number(argv[0]) : 0;
  uint64_t raw = little ? xt_buffer_read_le(thisValue, offset, bytes) : xt_buffer_read_be(thisValue, offset, bytes);
  double result;
  if (signedValue) {
    int64_t signedRaw = (int64_t)raw;
    int bits = bytes * 8;
    if (bits < 64 && (raw & (1ULL << (bits - 1)))) signedRaw = (int64_t)(raw | (~0ULL << bits));
    result = (double)signedRaw;
  } else {
    result = (double)raw;
  }
  return xt_number(result);
}

static xt_value xt_buffer_write_method(xt_value thisValue, int32_t argc, xt_value *argv, int bytes, int little) {
  uint32_t offset = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint64_t value = (uint64_t)(int64_t)xt_to_number(xt_arg_at(argc, argv, 0));
  if (little) xt_buffer_write_le(thisValue, offset, bytes, value);
  else xt_buffer_write_be(thisValue, offset, bytes, value);
  return xt_number((double)(offset + (uint32_t)bytes));
}

#define XT_BUFFER_DEFINE_IO(name, bytes, little, signedValue)                                                    \
  static xt_value xt_buffer_read_##name(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {        \
    (void)env;                                                                                                   \
    return xt_buffer_read_method(thisValue, argc, argv, bytes, little, signedValue);                            \
  }                                                                                                              \
  static xt_value xt_buffer_write_##name(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {       \
    (void)env;                                                                                                   \
    return xt_buffer_write_method(thisValue, argc, argv, bytes, little);                                        \
  }

#define XT_BUFFER_DEFINE_SIGNED_IO(name, bytes, little)                                                          \
  XT_BUFFER_DEFINE_IO(name, bytes, little, 0)                                                                    \
  static xt_value xt_buffer_read_##name##s(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {     \
    (void)env;                                                                                                   \
    return xt_buffer_read_method(thisValue, argc, argv, bytes, little, 1);                                      \
  }

XT_BUFFER_DEFINE_SIGNED_IO(u8, 1, 1)
XT_BUFFER_DEFINE_SIGNED_IO(u16, 2, 1)
static xt_value xt_buffer_read_u16be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 2, 0, 0);
}
static xt_value xt_buffer_write_u16be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_write_method(thisValue, argc, argv, 2, 0);
}
static xt_value xt_buffer_read_u16bes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 2, 0, 1);
}
XT_BUFFER_DEFINE_SIGNED_IO(u32, 4, 1)
static xt_value xt_buffer_read_u32be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 4, 0, 0);
}
static xt_value xt_buffer_write_u32be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_write_method(thisValue, argc, argv, 4, 0);
}
static xt_value xt_buffer_read_u32bes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 4, 0, 1);
}

static xt_value xt_buffer_read_float(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv, int little, int doublePrecision) {
  (void)env;
  (void)thisValue;
  (void)argc;
  (void)argv;
  (void)little;
  (void)doublePrecision;
  return xt_number(0);
}

static xt_value xt_buffer_method_keys(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value result = xt_array_new(0, NULL);
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length; i++) xt_array_push(result, xt_number((double)i));
  return result;
}

static xt_value xt_buffer_method_values(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value result = xt_array_new(0, NULL);
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length; i++) xt_array_push(result, xt_number((double)xt_buffer_byte(thisValue, i)));
  return result;
}

/* -- prototype ------------------------------------------------------------ */

static xt_value xt_buffer_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_define_method(proto, "toString", (void *)xt_buffer_method_to_string);
  xt_node_define_method(proto, "toJSON", (void *)xt_buffer_method_to_json);
  xt_node_define_method(proto, "slice", (void *)xt_buffer_method_slice);
  xt_node_define_method(proto, "subarray", (void *)xt_buffer_method_slice);
  xt_node_define_method(proto, "equals", (void *)xt_buffer_method_equals);
  xt_node_define_method(proto, "compare", (void *)xt_buffer_method_compare);
  xt_node_define_method(proto, "copy", (void *)xt_buffer_method_copy);
  xt_node_define_method(proto, "write", (void *)xt_buffer_method_write);
  xt_node_define_method(proto, "fill", (void *)xt_buffer_method_fill);
  xt_node_define_method(proto, "reverse", (void *)xt_buffer_method_reverse);
  xt_node_define_method(proto, "indexOf", (void *)xt_buffer_method_index_of_fwd);
  xt_node_define_method(proto, "lastIndexOf", (void *)xt_buffer_method_index_of_rev);
  xt_node_define_method(proto, "includes", (void *)xt_buffer_method_includes);
  xt_node_define_method(proto, "keys", (void *)xt_buffer_method_keys);
  xt_node_define_method(proto, "values", (void *)xt_buffer_method_values);
  xt_node_define_method(proto, "readUInt8", (void *)xt_buffer_read_u8);
  xt_node_define_method(proto, "readInt8", (void *)xt_buffer_read_u8s);
  xt_node_define_method(proto, "writeUInt8", (void *)xt_buffer_write_u8);
  xt_node_define_method(proto, "writeInt8", (void *)xt_buffer_write_u8);
  xt_node_define_method(proto, "readUInt16LE", (void *)xt_buffer_read_u16);
  xt_node_define_method(proto, "readUInt16BE", (void *)xt_buffer_read_u16be);
  xt_node_define_method(proto, "readInt16LE", (void *)xt_buffer_read_u16s);
  xt_node_define_method(proto, "readInt16BE", (void *)xt_buffer_read_u16bes);
  xt_node_define_method(proto, "writeUInt16LE", (void *)xt_buffer_write_u16);
  xt_node_define_method(proto, "writeUInt16BE", (void *)xt_buffer_write_u16be);
  xt_node_define_method(proto, "readUInt32LE", (void *)xt_buffer_read_u32);
  xt_node_define_method(proto, "readUInt32BE", (void *)xt_buffer_read_u32be);
  xt_node_define_method(proto, "readInt32LE", (void *)xt_buffer_read_u32s);
  xt_node_define_method(proto, "readInt32BE", (void *)xt_buffer_read_u32bes);
  xt_node_define_method(proto, "writeUInt32LE", (void *)xt_buffer_write_u32);
  xt_node_define_method(proto, "writeUInt32BE", (void *)xt_buffer_write_u32be);
  (void)xt_buffer_read_float;
  return proto;
}

/* Enforce prototype creation on first use so `Buffer.from` marks the object. */
void xt_buffer_ensure_proto(void) { (void)xt_buffer_proto(); }

/* Cross-module helpers used by the net/http/dgram modules. */
int xt_node_is_buffer(xt_value value) { return xt_buffer_is(value); }

unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength) {
  uint32_t length = xt_buffer_length(value);
  unsigned char *bytes = (unsigned char *)malloc(length ? length : 1);
  for (uint32_t i = 0; i < length; i++) bytes[i] = xt_buffer_byte(value, i);
  *outLength = length;
  return bytes;
}
