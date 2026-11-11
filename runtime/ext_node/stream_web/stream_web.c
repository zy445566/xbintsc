/*
 * Node.js `stream/web` module for xbintsc.
 *
 * WHATWG streams are not implemented. `ReadableStreamTee` is exported as
 * `undefined` so the `ReadableStreamTee?.(...) ?? body.stream.tee()` pattern in
 * undici falls back to the body's own `tee`; the stream constructors throw.
 */

#include "../node_common.h"

xt_value xt_stream_web_tee(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

xt_value xt_stream_web_unsupported_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:stream/web is not supported in compiled binaries"));
  return XT_UNDEFINED;
}
