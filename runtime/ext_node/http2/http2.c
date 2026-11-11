/*
 * Node.js `http2` module for xbintsc — a stub (HTTP/2 is not implemented).
 */

#include "../node_common.h"

xt_value xt_http2_connect(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:http2 is not supported in compiled binaries"));
  return XT_UNDEFINED;
}

xt_value xt_http2_create_server(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:http2 is not supported in compiled binaries"));
  return XT_UNDEFINED;
}

xt_value xt_http2_create_secure_server(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:http2 is not supported in compiled binaries"));
  return XT_UNDEFINED;
}
