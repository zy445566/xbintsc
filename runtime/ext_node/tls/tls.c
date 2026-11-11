/*
 * Node.js `tls` module for xbintsc — a stub (TLS is not implemented).
 */

#include "../node_common.h"

xt_value xt_tls_connect(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:tls is not supported in compiled binaries"));
  return XT_UNDEFINED;
}

xt_value xt_tls_create_server(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:tls is not supported in compiled binaries"));
  return XT_UNDEFINED;
}

xt_value xt_tls_socket_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:tls is not supported in compiled binaries"));
  return XT_UNDEFINED;
}
