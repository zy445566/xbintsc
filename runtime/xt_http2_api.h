/*
 * High-level `node:http2` surface provided by the `http2` link feature.
 *
 * Same shape as `xt_node_tls_api.h`: the always-linked `http2` module keeps the
 * importable bindings and forwards to this table, which the nghttp2-backed
 * feature installs in a constructor.
 */

#ifndef XT_HTTP2_API_H
#define XT_HTTP2_API_H

#include "rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xt_http2_api {
  /** `http2.connect(authority[, options][, listener])`. */
  xt_value (*connect)(int32_t argc, xt_value *argv);
  /** `http2.createServer([options][, onRequest])` (cleartext h2c). */
  xt_value (*create_server)(int32_t argc, xt_value *argv);
  /** `http2.createSecureServer(options[, onRequest])` (TLS + ALPN h2). */
  xt_value (*create_secure_server)(int32_t argc, xt_value *argv);
  /** `http2.constants` (frame/error code numbers). */
  xt_value (*constants)(void);
} XtHttp2Api;

/** The installed backend, or NULL when the `http2` feature is not linked. */
extern const XtHttp2Api *xt_http2_api;

void xt_http2_register(const XtHttp2Api *api);

#ifdef __cplusplus
}
#endif

#endif /* XT_HTTP2_API_H */
