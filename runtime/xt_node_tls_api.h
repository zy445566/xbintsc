/*
 * High-level `node:tls` surface provided by the `node-tls` link feature.
 *
 * The always-linked `tls` module keeps the importable bindings and falls back to
 * a clear runtime error when the feature is absent. The feature's backend owns
 * the JavaScript objects (`TLSSocket`, the server wrapper) and registers them
 * here in a constructor, mirroring the `zlib` compression bridge.
 */

#ifndef XT_NODE_TLS_API_H
#define XT_NODE_TLS_API_H

#include "rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xt_node_tls_api {
  /** `tls.connect(options[, host][, callback])`. */
  xt_value (*connect)(int32_t argc, xt_value *argv);
  /** `tls.createServer(options[, secureConnectionListener])`. */
  xt_value (*create_server)(int32_t argc, xt_value *argv);
  /** `new tls.TLSSocket(...)`. */
  xt_value (*socket_ctor)(int32_t argc, xt_value *argv);
} XtNodeTlsApi;

/** The installed backend, or NULL when the `node-tls` feature is not linked. */
extern const XtNodeTlsApi *xt_tls_node_api;

void xt_node_tls_register(const XtNodeTlsApi *api);

#ifdef __cplusplus
}
#endif

#endif /* XT_NODE_TLS_API_H */
