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
xt_value xt_buffer_ctor(int32_t argc, xt_value *argv);
void xt_node_buffer_set(xt_value value, uint32_t index, unsigned char byte);

xt_value xt_node_create_gzip(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value stream = xt_object_new();
  xt_node_set(stream, "_kind", xt_string_from_cstr("gzip"));
  return stream;
}

/* A stream descriptor for each decompressor. The actual decompression is not
 * implemented yet (only stored blocks round-trip through `pipeline`), but the
 * import and the `create*` surface exist so undici and other consumers compile. */
static xt_value xt_zlib_create_kind(const char *kind) {
  xt_value stream = xt_object_new();
  xt_node_set(stream, "_kind", xt_string_from_cstr(kind));
  return stream;
}

xt_value xt_node_create_inflate(int32_t argc, xt_value *argv) { (void)argc; (void)argv; return xt_zlib_create_kind("inflate"); }
xt_value xt_node_create_gunzip(int32_t argc, xt_value *argv) { (void)argc; (void)argv; return xt_zlib_create_kind("gunzip"); }
xt_value xt_node_create_inflate_raw(int32_t argc, xt_value *argv) { (void)argc; (void)argv; return xt_zlib_create_kind("inflateRaw"); }
xt_value xt_node_create_brotli_decompress(int32_t argc, xt_value *argv) { (void)argc; (void)argv; return xt_zlib_create_kind("brotliDecompress"); }
xt_value xt_node_create_zstd_decompress(int32_t argc, xt_value *argv) { (void)argc; (void)argv; return xt_zlib_create_kind("zstdDecompress"); }

/* -- DEFLATE stored blocks (no compression, RFC 1951) ---------------------- */

static void xt_zlib_le16(unsigned char *out, uint16_t value) {
  out[0] = (unsigned char)(value & 0xff);
  out[1] = (unsigned char)((value >> 8) & 0xff);
}

/* -- optional miniz compression backend (installed by the `compression` link
 *    feature's bridge constructor) ------------------------------------------ */

typedef unsigned char *(*xt_zlib_compress_fn)(const unsigned char *data, size_t length, size_t *out_length);
typedef unsigned char *(*xt_zlib_inflate_fn)(const unsigned char *data, size_t length, size_t *out_length);

static xt_zlib_compress_fn xt_zlib_compress_raw_fn = NULL;
static xt_zlib_inflate_fn xt_zlib_inflate_raw_fn = NULL;

void xt_zlib_register_compression(xt_zlib_compress_fn compress_fn, xt_zlib_inflate_fn inflate_fn) {
  xt_zlib_compress_raw_fn = compress_fn;
  xt_zlib_inflate_raw_fn = inflate_fn;
}

/* Wrap malloc'd bytes in a Buffer value. */
static xt_value xt_zlib_buffer_from_bytes(const unsigned char *data, size_t length) {
  xt_value sizeValue = xt_number((double)length);
  xt_value result = xt_buffer_ctor(1, &sizeValue);
  for (size_t i = 0; i < length; i++) xt_node_buffer_set(result, (uint32_t)i, data[i]);
  return result;
}

/* Encode `data` as a raw DEFLATE stream of stored blocks, returned as a Buffer. */
static xt_value xt_zlib_deflate_raw(const unsigned char *data, size_t length) {
  size_t blocks = length / 65535 + 1;
  size_t capacity = length + blocks * 5;
  unsigned char *buffer = (unsigned char *)malloc(capacity);
  if (!buffer) return xt_undefined();
  size_t pos = 0;
  size_t offset = 0;
  do {
    size_t chunk = length - offset;
    if (chunk > 65535) chunk = 65535;
    int final = offset + chunk >= length;
    buffer[pos++] = (unsigned char)(final ? 0x01 : 0x00); /* BFINAL=1, BTYPE=00 */
    xt_zlib_le16(buffer + pos, (uint16_t)chunk);
    pos += 2;
    xt_zlib_le16(buffer + pos, (uint16_t)~chunk);
    pos += 2;
    if (chunk > 0) memcpy(buffer + pos, data + offset, chunk);
    pos += chunk;
    offset += chunk;
  } while (offset < length);

  xt_value result = xt_zlib_buffer_from_bytes(buffer, pos);
  free(buffer);
  return result;
}

xt_value xt_node_deflate_raw_sync(int32_t argc, xt_value *argv) {
  xt_value input = xt_arg_at(argc, argv, 0);
  const unsigned char *data = NULL;
  size_t length = 0;
  unsigned char *owned = NULL;
  if (xt_node_is_buffer(input)) {
    owned = xt_node_buffer_bytes(input, &length);
    data = owned;
  } else {
    xt_value text = xt_to_string(input);
    data = (const unsigned char *)xt_string_data(text);
    length = (size_t)xt_string_length_value(text);
  }

  /* Prefer the miniz backend when the compression feature is linked. */
  if (xt_zlib_compress_raw_fn != NULL) {
    size_t out_length = 0;
    unsigned char *out = xt_zlib_compress_raw_fn(data, length, &out_length);
    free(owned);
    if (out != NULL) {
      xt_value result = xt_zlib_buffer_from_bytes(out, out_length);
      free(out);
      return result;
    }
    return xt_undefined();
  }

  xt_value result = xt_zlib_deflate_raw(data, length);
  free(owned);
  return result;
}

/* -- zstd stubs (no zstd codec linked) ------------------------------------- */

xt_value xt_node_zstd_compress_sync(int32_t argc, xt_value *argv) {
  return argc > 0 ? argv[0] : XT_UNDEFINED;
}

xt_value xt_node_zstd_decompress_sync(int32_t argc, xt_value *argv) {
  return argc > 0 ? argv[0] : XT_UNDEFINED;
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
