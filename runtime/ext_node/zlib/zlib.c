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

xt_value xt_node_create_gzip(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value stream = xt_object_new();
  xt_node_set(stream, "_kind", xt_string_from_cstr("gzip"));
  return stream;
}
