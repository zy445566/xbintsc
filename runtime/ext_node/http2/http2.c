/*
 * Node.js `http2` module for xbintsc — importable bindings.
 *
 * The implementation is the `http2` link feature (`http2_backend.c`, nghttp2).
 * This translation unit is always compiled with the node extension so the
 * import resolves either way; without the feature the calls throw a clear
 * runtime error instead of failing at link time.
 */

#include "../node_common.h"
#include "../../xt_http2_api.h"

static xt_value xt_http2_unsupported(void) {
  xt_throw(xt_error_from_message(
      "Error: node:http2 is not available (the 'http2' link feature is not linked; "
      "run `npm run tls` to build it)"));
  return XT_UNDEFINED;
}

xt_value xt_http2_connect(int32_t argc, xt_value *argv) {
  if (xt_http2_api == NULL) return xt_http2_unsupported();
  return xt_http2_api->connect(argc, argv);
}

xt_value xt_http2_create_server(int32_t argc, xt_value *argv) {
  if (xt_http2_api == NULL) return xt_http2_unsupported();
  return xt_http2_api->create_server(argc, argv);
}

xt_value xt_http2_create_secure_server(int32_t argc, xt_value *argv) {
  if (xt_http2_api == NULL) return xt_http2_unsupported();
  return xt_http2_api->create_secure_server(argc, argv);
}

xt_value xt_http2_constants(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (xt_http2_api == NULL || xt_http2_api->constants == NULL) return xt_object_new();
  return xt_http2_api->constants();
}
