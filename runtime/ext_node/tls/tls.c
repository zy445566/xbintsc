/*
 * Node.js `tls` module for xbintsc — importable bindings.
 *
 * The implementation is the `node-tls` link feature (`tls_backend.c`), which
 * needs mbedTLS and is therefore only linked when a program imports this
 * module. This translation unit is always compiled with the node extension so
 * the import resolves either way: without the feature the calls throw a clear
 * runtime error instead of failing at link time.
 */

#include "../node_common.h"
#include "../../xt_node_tls_api.h"

static xt_value xt_tls_unsupported(void) {
  xt_throw(xt_error_from_message(
      "Error: node:tls is not available (the 'node-tls'/'tls' link features are not linked; "
      "run `npm run tls` to build them)"));
  return XT_UNDEFINED;
}

xt_value xt_tls_connect(int32_t argc, xt_value *argv) {
  if (xt_tls_node_api == NULL) return xt_tls_unsupported();
  return xt_tls_node_api->connect(argc, argv);
}

xt_value xt_tls_create_server(int32_t argc, xt_value *argv) {
  if (xt_tls_node_api == NULL) return xt_tls_unsupported();
  return xt_tls_node_api->create_server(argc, argv);
}

xt_value xt_tls_socket_ctor(int32_t argc, xt_value *argv) {
  if (xt_tls_node_api == NULL) return xt_tls_unsupported();
  return xt_tls_node_api->socket_ctor(argc, argv);
}
