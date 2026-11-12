/*
 * Low-level TLS transport used by the core `fetch` implementation and by the
 * Node `tls` module.
 *
 * The mbedTLS backend is an optional *link feature*: its archive is only linked
 * when a program may use TLS, so this table is the only thing the always-linked
 * runtime knows about it. `xt_protocol_registry.c` owns the (initially NULL)
 * table; the feature's bridge installs the implementation in a constructor
 * before `main` runs. When the table is NULL, `https://` requests reject and
 * `node:tls` reports that TLS is unavailable, instead of failing at link time.
 */

#ifndef XT_TLS_API_H
#define XT_TLS_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One established TLS connection (client or accepted server side). */
typedef struct xt_tls_conn xt_tls_conn;

/** Server credentials and verification policy, reusable across connections. */
typedef struct xt_tls_server_config xt_tls_server_config;

typedef struct xt_tls_api {
  /**
   * Connect a TCP socket and perform the TLS handshake, blocking until both
   * complete. `verify` enables certificate verification; `ca_pem` replaces the
   * system trust store when non-NULL. `cert_pem`/`key_pem` present a client
   * certificate. `alpn` is a comma-separated protocol list. Returns NULL on
   * failure and writes a human-readable reason into `err`.
   */
  xt_tls_conn *(*client_connect)(const char *host, int port, const char *servername, int verify,
                                 const char *ca_pem, const char *cert_pem, const char *key_pem,
                                 const char *alpn, char *err, size_t errlen);
  /** Build the server credentials/verification policy from PEM strings. */
  xt_tls_server_config *(*server_config)(const char *cert_pem, const char *key_pem,
                                         const char *ca_pem, int request_cert, int verify,
                                         const char *alpn, char *err, size_t errlen);
  /** Wrap an accepted socket (blocking handshake); NULL on failure. */
  xt_tls_conn *(*server_accept)(xt_tls_server_config *config, int fd, char *err, size_t errlen);
  /** Decrypted read: positive byte count, 0 on orderly EOF, negative on error. */
  int (*read)(xt_tls_conn *conn, unsigned char *buffer, size_t capacity);
  /** Encrypt and write all bytes; returns 0 on success. */
  int (*write)(xt_tls_conn *conn, const unsigned char *data, size_t length);
  /** Send close_notify (best effort) without closing the descriptor. */
  void (*shutdown)(xt_tls_conn *conn);
  /** Send close_notify, close the descriptor and free `conn`. */
  void (*close)(xt_tls_conn *conn);
  /** Free `conn` without touching the descriptor (already closed). */
  void (*conn_free)(xt_tls_conn *conn);
  void (*config_free)(xt_tls_server_config *config);
  /** Negotiated ALPN protocol, or NULL. */
  const char *(*alpn)(xt_tls_conn *conn);
  /** Last error message for `conn`, or NULL. */
  const char *(*error)(xt_tls_conn *conn);
  /** The underlying socket descriptor. */
  int (*fd)(xt_tls_conn *conn);
} XtTlsApi;

/** The installed backend, or NULL when the `tls` link feature is not linked. */
extern const XtTlsApi *xt_tls_core_api;

void xt_tls_core_register(const XtTlsApi *api);

#ifdef __cplusplus
}
#endif

#endif /* XT_TLS_API_H */
