/*
 * xbintsc runtime — value conversions shared by the WebAssembly bridge.
 */

#include "wasm.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void xt_wasm_set_error(char **error, const char *format, ...) {
  if (error == NULL) return;
  free(*error);
  *error = NULL;
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) {
    va_end(args);
    return;
  }
  char *buffer = (char *)malloc((size_t)length + 1);
  if (buffer != NULL) vsnprintf(buffer, (size_t)length + 1, format, args);
  va_end(args);
  *error = buffer;
}

xt_value xt_wasm_error(const char *kind, const char *message) {
  if (message == NULL) message = "unknown error";
  size_t kindLength = strlen(kind);
  size_t messageLength = strlen(message);
  char *text = (char *)malloc(kindLength + 2 + messageLength + 1);
  if (text == NULL) return xt_error_from_message(kind);
  memcpy(text, kind, kindLength);
  memcpy(text + kindLength, ": ", 2);
  memcpy(text + kindLength + 2, message, messageLength + 1);
  xt_value error = xt_error_from_message(text);
  free(text);
  return error;
}

xt_value xt_wasm_type_error(const char *message) {
  xt_value argument = xt_string_from_cstr(message);
  return xt_type_error_ctor(1, &argument);
}

static uint32_t xt_wasm_typed_array_width(int kind) {
  switch (kind) {
    case 0:
    case 1:
    case 2:
      return 1;
    case 3:
    case 4:
      return 2;
    case 5:
    case 6:
    case 7:
      return 4;
    case 8:
      return 8;
    default:
      return 0;
  }
}

int xt_wasm_is_buffer_source(xt_value value) {
  if (xt_is_array_buffer(value) || xt_typed_array_kind_of(value) >= 0) return 1;
  if (XT_IS_OBJECT(value)) {
    /* DataView (a plain object that names its backing ArrayBuffer). */
    if (xt_is_array_buffer(xt_get(value, xt_string_from_cstr("buffer")))) return 1;
    /* xbintsc's Node Buffer is a plain object with indexed bytes and a
     * `length` property, not a typed array. Accept that shape too so the
     * common `readFileSync`/`Buffer.from` input works. */
    xt_value length = xt_get(value, xt_string_from_cstr("length"));
    return XT_IS_NUMBER(length);
  }
  return 0;
}

int xt_wasm_bytes_from_value(xt_value value, uint8_t **bytes, uint32_t *length,
                             char **error) {
  *bytes = NULL;
  *length = 0;
  uint32_t byteLength = 0;
  uint32_t byteOffset = 0;
  xt_value buffer = value;
  int bufferLike = 0;
  if (xt_is_array_buffer(value)) {
    byteLength = xt_array_buffer_byte_length(value);
  } else {
    int kind = xt_typed_array_kind_of(value);
    uint32_t width = kind >= 0 ? xt_wasm_typed_array_width(kind) : 0;
    if (kind >= 0 && width != 0) {
      buffer = xt_typed_array_backing_buffer(value);
      byteOffset = xt_typed_array_view_offset(value);
      byteLength = xt_typed_array_length(value) * width;
    } else if (XT_IS_OBJECT(value) &&
               xt_is_array_buffer(xt_get(value, xt_string_from_cstr("buffer")))) {
      buffer = xt_get(value, xt_string_from_cstr("buffer"));
      xt_value offset = xt_get(value, xt_string_from_cstr("byteOffset"));
      xt_value viewLength = xt_get(value, xt_string_from_cstr("byteLength"));
      byteOffset = XT_IS_NUMBER(offset) ? (uint32_t)xt_to_int32(offset) : 0;
      byteLength = XT_IS_NUMBER(viewLength) ? (uint32_t)xt_to_int32(viewLength) : 0;
    } else if (XT_IS_OBJECT(value)) {
      xt_value lengthValue = xt_get(value, xt_string_from_cstr("length"));
      if (!XT_IS_NUMBER(lengthValue)) {
        xt_wasm_set_error(error, "WebAssembly: argument must be a BufferSource");
        return 0;
      }
      byteLength = (uint32_t)xt_to_int32(lengthValue);
      bufferLike = 1;
    } else {
      xt_wasm_set_error(error, "WebAssembly: argument must be a BufferSource");
      return 0;
    }
  }
  uint8_t *copy = (uint8_t *)malloc(byteLength == 0 ? 1 : byteLength);
  if (copy == NULL) {
    xt_wasm_set_error(error, "WebAssembly: out of memory while copying the module");
    return 0;
  }
  if (bufferLike) {
    for (uint32_t i = 0; i < byteLength; i++) {
      copy[i] = (uint8_t)(int32_t)xt_to_number(
          xt_get(value, xt_number((double)i)));
    }
  } else {
    uint32_t copied = xt_array_buffer_copy_bytes_out(buffer, byteOffset, copy, byteLength);
    if (copied != byteLength) {
      free(copy);
      xt_wasm_set_error(error, "WebAssembly: could not read the module bytes");
      return 0;
    }
  }
  *bytes = copy;
  *length = byteLength;
  return 1;
}

int64_t xt_wasm_to_i64(xt_value value) {
  if (!XT_IS_BIGINT(value)) {
    xt_throw(xt_wasm_type_error("WebAssembly: i64 arguments must be BigInt values"));
    return 0;
  }
  xt_value text = xt_to_string(value);
  const char *digits = xt_string_data(text);
  if (digits == NULL) return 0;
  int negative = digits[0] == '-';
  if (negative) digits++;
  uint64_t magnitude = 0;
  for (const char *cursor = digits; *cursor >= '0' && *cursor <= '9'; cursor++) {
    magnitude = magnitude * 10u + (uint64_t)(*cursor - '0');
  }
  return (int64_t)(negative ? (uint64_t)0 - magnitude : magnitude);
}

xt_value xt_wasm_from_i64(int64_t value) {
  char text[32];
  int length = snprintf(text, sizeof(text), "%" PRId64, value);
  if (length < 0) return xt_bigint_from_string("0", 1);
  return xt_bigint_from_string(text, (size_t)length);
}

xt_value xt_wasm_number_arg(int32_t argc, xt_value *argv, int32_t index) {
  return xt_arg_at(argc, argv, index);
}

xt_value xt_wasm_object_arg(int32_t argc, xt_value *argv, int32_t index) {
  xt_value value = xt_arg_at(argc, argv, index);
  if (XT_IS_OBJECT(value) || XT_IS_ARRAY(value) || XT_IS_FUNCTION(value)) return value;
  return XT_UNDEFINED;
}
