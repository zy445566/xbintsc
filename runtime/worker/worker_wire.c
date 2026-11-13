/*
 * xbintsc runtime — worker wire helpers.
 *
 * Growable text buffers, JSON string escaping and base64, shared by the message
 * codec (`worker_codec.c`). Kept apart from the codec so each file stays small
 * and the byte-level helpers can be tested through the same round trips.
 */

#include "worker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void xt_wrk_buffer_reserve(xt_wrk_buffer *buffer, size_t extra) {
  if (buffer->length + extra + 1 <= buffer->capacity) return;
  size_t next = buffer->capacity ? buffer->capacity : 128;
  while (next < buffer->length + extra + 1) next *= 2;
  char *grown = (char *)realloc(buffer->data, next);
  if (!grown) abort();
  buffer->data = grown;
  buffer->capacity = next;
}

void xt_wrk_buffer_put(xt_wrk_buffer *buffer, const char *text, size_t length) {
  if (length == 0) return;
  xt_wrk_buffer_reserve(buffer, length);
  memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
  buffer->data[buffer->length] = 0;
}

void xt_wrk_buffer_puts(xt_wrk_buffer *buffer, const char *text) {
  xt_wrk_buffer_put(buffer, text, text ? strlen(text) : 0);
}

void xt_wrk_buffer_putc(xt_wrk_buffer *buffer, char value) {
  xt_wrk_buffer_reserve(buffer, 1);
  buffer->data[buffer->length++] = value;
  buffer->data[buffer->length] = 0;
}

void xt_wrk_buffer_put_uint(xt_wrk_buffer *buffer, unsigned long value) {
  char text[32];
  snprintf(text, sizeof(text), "%lu", value);
  xt_wrk_buffer_puts(buffer, text);
}

/* JSON string escaping. Non-ASCII bytes are copied through (the runtime keeps
 * UTF-8/WTF-8 valid text); control characters become `\u00XX`. */
void xt_wrk_buffer_put_string(xt_wrk_buffer *buffer, const char *text, size_t length) {
  xt_wrk_buffer_putc(buffer, '"');
  for (size_t i = 0; i < length; i++) {
    unsigned char byte = (unsigned char)text[i];
    switch (byte) {
      case '"': xt_wrk_buffer_put(buffer, "\\\"", 2); break;
      case '\\': xt_wrk_buffer_put(buffer, "\\\\", 2); break;
      case '\b': xt_wrk_buffer_put(buffer, "\\b", 2); break;
      case '\f': xt_wrk_buffer_put(buffer, "\\f", 2); break;
      case '\n': xt_wrk_buffer_put(buffer, "\\n", 2); break;
      case '\r': xt_wrk_buffer_put(buffer, "\\r", 2); break;
      case '\t': xt_wrk_buffer_put(buffer, "\\t", 2); break;
      default:
        if (byte < 0x20) {
          char escape[7];
          snprintf(escape, sizeof(escape), "\\u%04x", byte);
          xt_wrk_buffer_puts(buffer, escape);
        } else {
          xt_wrk_buffer_putc(buffer, (char)byte);
        }
    }
  }
  xt_wrk_buffer_putc(buffer, '"');
}

/* -- base64 --------------------------------------------------------------- */

static const char xt_wrk_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void xt_wrk_buffer_put_base64(xt_wrk_buffer *buffer, const uint8_t *data, size_t length) {
  size_t i = 0;
  while (i + 2 < length) {
    uint32_t chunk = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    char out[4] = {xt_wrk_base64_alphabet[(chunk >> 18) & 63],
                   xt_wrk_base64_alphabet[(chunk >> 12) & 63],
                   xt_wrk_base64_alphabet[(chunk >> 6) & 63],
                   xt_wrk_base64_alphabet[chunk & 63]};
    xt_wrk_buffer_put(buffer, out, 4);
    i += 3;
  }
  if (length - i == 1) {
    uint32_t chunk = (uint32_t)data[i] << 16;
    char out[4] = {xt_wrk_base64_alphabet[(chunk >> 18) & 63],
                   xt_wrk_base64_alphabet[(chunk >> 12) & 63], '=', '='};
    xt_wrk_buffer_put(buffer, out, 4);
  } else if (length - i == 2) {
    uint32_t chunk = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    char out[4] = {xt_wrk_base64_alphabet[(chunk >> 18) & 63],
                   xt_wrk_base64_alphabet[(chunk >> 12) & 63],
                   xt_wrk_base64_alphabet[(chunk >> 6) & 63], '='};
    xt_wrk_buffer_put(buffer, out, 4);
  }
}

static int xt_wrk_base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

uint8_t *xt_wrk_decode_base64(const char *text, size_t length, size_t *out_length) {
  uint8_t *bytes = (uint8_t *)malloc(length / 4 * 3 + 4);
  if (!bytes) abort();
  size_t written = 0;
  uint32_t accumulator = 0;
  int bits = 0;
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '=') break;
    int value = xt_wrk_base64_value(text[i]);
    if (value < 0) continue;
    accumulator = (accumulator << 6) | (uint32_t)value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes[written++] = (uint8_t)((accumulator >> bits) & 0xff);
    }
  }
  *out_length = written;
  return bytes;
}

/* Byte width of a typed-array element, or 0 for an unknown name. */
int xt_wrk_width_of_typed_array(const char *name) {
  if (!name) return 0;
  if (strcmp(name, "Uint8Array") == 0 || strcmp(name, "Int8Array") == 0 ||
      strcmp(name, "Uint8ClampedArray") == 0) {
    return 1;
  }
  if (strcmp(name, "Uint16Array") == 0 || strcmp(name, "Int16Array") == 0) return 2;
  if (strcmp(name, "Uint32Array") == 0 || strcmp(name, "Int32Array") == 0 ||
      strcmp(name, "Float32Array") == 0) {
    return 4;
  }
  if (strcmp(name, "Float64Array") == 0) return 8;
  return 0;
}
