/*
 * Node.js `zlib` module for xbintsc.
 *
 * Only `createGzip` is implemented, and the "compression" uses DEFLATE stored
 * (uncompressed) blocks: the output is a fully valid gzip stream
 * (`zlib.gunzipSync`/`gunzip`/HTTP clients all accept it) even though it does
 * not shrink the payload. The descriptor is consumed by the `stream/promises`
 * `pipeline` implementation.
 */

#include "../node_common.h"

int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

xt_value xt_node_create_gzip(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value stream = xt_object_new();
  xt_node_set(stream, "_kind", xt_string_from_cstr("gzip"));
  return stream;
}

/* CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), the checksum Node's
 * `zlib.crc32` returns. */
static uint32_t xt_crc32_table[256];
static int xt_crc32_ready = 0;

static void xt_crc32_init(void) {
  if (xt_crc32_ready) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int bit = 0; bit < 8; bit++) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    xt_crc32_table[i] = c;
  }
  xt_crc32_ready = 1;
}

xt_value xt_node_crc32(int32_t argc, xt_value *argv) {
  xt_crc32_init();
  xt_value input = xt_arg_at(argc, argv, 0);
  uint32_t crc = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0u;
  crc ^= 0xFFFFFFFFu;

  const unsigned char *data = NULL;
  size_t length = 0;
  if (xt_node_is_buffer(input)) {
    data = xt_node_buffer_bytes(input, &length);
  } else {
    xt_value text = xt_to_string(input);
    data = (const unsigned char *)xt_string_data(text);
    length = (size_t)xt_string_length_value(text);
  }

  for (size_t i = 0; i < length; i++) {
    crc = xt_crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return xt_number((double)(crc ^ 0xFFFFFFFFu));
}

/* `zlib.constants` and `zlib.Z_DEFAULT_WINDOWBITS`: the compression flags and
 * the default window size the permessage-deflate path reads. */
xt_value xt_zlib_constants_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value object = xt_object_new();
  xt_node_set(object, "Z_NO_FLUSH", xt_number(0));
  xt_node_set(object, "Z_PARTIAL_FLUSH", xt_number(1));
  xt_node_set(object, "Z_SYNC_FLUSH", xt_number(2));
  xt_node_set(object, "Z_FULL_FLUSH", xt_number(3));
  xt_node_set(object, "Z_FINISH", xt_number(4));
  xt_node_set(object, "Z_BLOCK", xt_number(5));
  xt_node_set(object, "Z_TREES", xt_number(6));
  xt_node_set(object, "Z_OK", xt_number(0));
  xt_node_set(object, "Z_STREAM_END", xt_number(1));
  xt_node_set(object, "Z_NEED_DICT", xt_number(2));
  xt_node_set(object, "Z_ERRNO", xt_number(-1));
  xt_node_set(object, "Z_STREAM_ERROR", xt_number(-2));
  xt_node_set(object, "Z_DATA_ERROR", xt_number(-3));
  xt_node_set(object, "Z_MEM_ERROR", xt_number(-4));
  xt_node_set(object, "Z_BUF_ERROR", xt_number(-5));
  xt_node_set(object, "Z_VERSION_ERROR", xt_number(-6));
  xt_node_set(object, "Z_DEFAULT_COMPRESSION", xt_number(-1));
  xt_node_set(object, "Z_DEFAULT_STRATEGY", xt_number(0));
  xt_node_set(object, "Z_DEFAULT_WINDOWBITS", xt_number(15));
  xt_node_set(object, "Z_BINARY", xt_number(0));
  xt_node_set(object, "Z_TEXT", xt_number(1));
  xt_node_set(object, "Z_UNKNOWN", xt_number(2));
  return object;
}

xt_value xt_zlib_default_windowbits_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_number(15);
}
