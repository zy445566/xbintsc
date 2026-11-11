/*
 * xbintsc `compression` link feature — registration bridge.
 *
 * The miniz DEFLATE implementation is only compiled and linked when the
 * `compression` feature is active. This tiny C unit installs the miniz-backed
 * raw-DEFLATE compress/inflate functions into the always-linked `zlib` module
 * before `main` runs; without it `zlib.deflateRawSync` falls back to DEFLATE
 * stored (uncompressed) blocks.
 */

#include "miniz.h"

extern void xt_zlib_register_compression(
    unsigned char *(*compress_fn)(const unsigned char *data, size_t length, size_t *out_length),
    unsigned char *(*inflate_fn)(const unsigned char *data, size_t length, size_t *out_length));

static unsigned char *bridge_compress_raw(const unsigned char *data, size_t length, size_t *out_length) {
  size_t out_len = 0;
  void *out = tdefl_compress_mem_to_heap(data, length, &out_len, TDEFL_DEFAULT_MAX_PROBES);
  *out_length = out_len;
  return (unsigned char *)out;
}

static unsigned char *bridge_inflate_raw(const unsigned char *data, size_t length, size_t *out_length) {
  size_t out_len = 0;
  void *out = tinfl_decompress_mem_to_heap(data, length, &out_len, 0 /* raw deflate */);
  *out_length = out_len;
  return (unsigned char *)out;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_zlib_compression_register(void) {
  xt_zlib_register_compression(bridge_compress_raw, bridge_inflate_raw);
}
#elif defined(_MSC_VER)
void xt_zlib_compression_register(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_zlib_compression_register_ctor)(void) =
    xt_zlib_compression_register;
void xt_zlib_compression_register(void) {
  xt_zlib_register_compression(bridge_compress_raw, bridge_inflate_raw);
}
#endif
