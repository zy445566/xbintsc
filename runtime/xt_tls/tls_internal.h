/*
 * Shared state for the mbedTLS-backed TLS backend (`tls_core.c`).
 *
 * One `xt_tls_setup` owns the configuration and credentials; a
 * `xt_tls_conn` owns one handshake/stream. Servers build the setup once and
 * borrow it for every accepted connection, clients own one per connection.
 */

#ifndef XT_TLS_INTERNAL_H
#define XT_TLS_INTERNAL_H

#include <stddef.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

/** How many ALPN protocols a single configuration accepts. */
#define XT_TLS_MAX_ALPN 8
#define XT_TLS_ALPN_MAX 64

typedef struct xt_tls_setup {
  mbedtls_ssl_config config;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_x509_crt ca;
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;
  char alpn[XT_TLS_MAX_ALPN][XT_TLS_ALPN_MAX];
  const char *alpn_list[XT_TLS_MAX_ALPN + 1];
  int alpn_count;
  int has_ca;
  int has_cert;
  int has_key;
  int initialized;
} xt_tls_setup;

struct xt_tls_conn {
  int fd;
  mbedtls_ssl_context ssl;
  /* Borrowed for accepted connections, owned for clients. */
  xt_tls_setup *setup;
  int owns_setup;
  char error[256];
  char alpn[XT_TLS_ALPN_MAX];
};

struct xt_tls_server_config {
  xt_tls_setup setup;
  int verify;
};

int xt_tls_platform_init(void);

/** Parse a comma-separated ALPN list into `setup` (empty list clears it). */
void xt_tls_setup_alpn(xt_tls_setup *setup, const char *list);

/** Load `ca_pem` or, when NULL, the host trust store. */
int xt_tls_setup_trust(xt_tls_setup *setup, const char *ca_pem, char *err, size_t errlen);

void xt_tls_setup_free(xt_tls_setup *setup);

int xt_tls_send_bio(void *ctx, const unsigned char *buf, size_t len);
int xt_tls_recv_bio(void *ctx, unsigned char *buf, size_t len);

/** Write a formatted error into `conn->error`. */
void xt_tls_conn_fail(xt_tls_conn *conn, const char *message, int code);
void xt_tls_conn_free(xt_tls_conn *conn);

#endif /* XT_TLS_INTERNAL_H */
