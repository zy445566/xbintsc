/*
 * Node.js `stream/web` module for xbintsc.
 *
 * The WHATWG streams themselves live in the core runtime (`xt_streams.c`) and
 * are available as browser globals too. This unit only carries the two pieces
 * Node's subpath needs beyond that surface: the internal `ReadableStreamTee`
 * export (deliberately `undefined`, so undici falls back to `body.stream.tee()`)
 * and the compression streams that the compiled runtime does not implement.
 */

#include "../node_common.h"

xt_value xt_stream_web_tee(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

static xt_value xt_stream_web_missing(const char *name) {
  char message[160];
  snprintf(message, sizeof(message), "Error: %s is not supported in compiled binaries", name);
  xt_throw(xt_error_from_message(message));
  return XT_UNDEFINED;
}

xt_value xt_compression_stream_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_stream_web_missing("CompressionStream");
}

xt_value xt_decompression_stream_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_stream_web_missing("DecompressionStream");
}
