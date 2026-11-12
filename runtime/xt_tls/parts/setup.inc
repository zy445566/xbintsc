/* -- configuration, credentials and trust store ---------------------------- */

/*
 * mbedTLS ships no platform trust-store loader. Windows exposes its roots
 * through the crypto API; the Unix hosts use the conventional CA bundle paths
 * (macOS ships `/etc/ssl/cert.pem`, the BSDs use the same file, and Linux
 * distributions use one of the two bundle locations). A caller-supplied `ca`
 * (Node's `tls.connect({ ca })`) bypasses the platform store entirely.
 */

#if defined(_WIN32)
#include <wincrypt.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int xt_tls_platform_init(void) { return 0; }

void xt_tls_setup_alpn(xt_tls_setup *setup, const char *list) {
  setup->alpn_count = 0;
  setup->alpn_list[0] = NULL;
  if (list == NULL) return;
  const char *cursor = list;
  while (*cursor != '\0' && setup->alpn_count < XT_TLS_MAX_ALPN) {
    const char *comma = strchr(cursor, ',');
    size_t length = comma ? (size_t)(comma - cursor) : strlen(cursor);
    if (length > 0 && length < XT_TLS_ALPN_MAX) {
      memcpy(setup->alpn[setup->alpn_count], cursor, length);
      setup->alpn[setup->alpn_count][length] = '\0';
      setup->alpn_list[setup->alpn_count] = setup->alpn[setup->alpn_count];
      setup->alpn_count++;
    }
    if (!comma) break;
    cursor = comma + 1;
  }
  setup->alpn_list[setup->alpn_count] = NULL;
}

#if defined(_WIN32)
static int xt_tls_load_system_roots(mbedtls_x509_crt *chain) {
  HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
  if (store == NULL) return -1;
  int loaded = 0;
  PCCERT_CONTEXT certificate = NULL;
  while ((certificate = CertEnumCertificatesInStore(store, certificate)) != NULL) {
    if (mbedtls_x509_crt_parse_der(chain, certificate->pbCertEncoded,
                                   certificate->cbCertEncoded) == 0) {
      loaded++;
    }
  }
  CertCloseStore(store, 0);
  return loaded > 0 ? 0 : -1;
}
#else
static int xt_tls_load_system_roots(mbedtls_x509_crt *chain) {
  static const char *bundles[] = {
      "/etc/ssl/certs/ca-certificates.crt", /* Debian/Ubuntu/Alpine */
      "/etc/pki/tls/certs/ca-bundle.crt",   /* Fedora/RHEL */
      "/etc/ssl/cert.pem",                  /* macOS/BSD */
      "/etc/ssl/ca-bundle.pem",
      NULL,
  };
  for (size_t i = 0; bundles[i] != NULL; i++) {
    if (access(bundles[i], R_OK) != 0) continue;
    if (mbedtls_x509_crt_parse_file(chain, bundles[i]) == 0) return 0;
  }
  /* Last resort: every hashed certificate in the directory. */
  return mbedtls_x509_crt_parse_path(chain, "/etc/ssl/certs");
}
#endif

int xt_tls_setup_trust(xt_tls_setup *setup, const char *ca_pem, char *err, size_t errlen) {
  int loaded = 0;
  if (ca_pem != NULL && ca_pem[0] != '\0') {
    if (mbedtls_x509_crt_parse(&setup->ca, (const unsigned char *)ca_pem, strlen(ca_pem) + 1) != 0) {
      snprintf(err, errlen, "cannot parse the supplied CA certificates");
      return -1;
    }
    loaded = 1;
  } else if (xt_tls_load_system_roots(&setup->ca) == 0) {
    loaded = 1;
  }
  /* Node-compatible escape hatch for a private CA (and the e2e tests): the
   * named PEM bundle is trusted in addition to the platform roots. */
  const char *extra = getenv("NODE_EXTRA_CA_CERTS");
  if (extra != NULL && extra[0] != '\0') {
    if (mbedtls_x509_crt_parse_file(&setup->ca, extra) != 0) {
      snprintf(err, errlen, "cannot read NODE_EXTRA_CA_CERTS (%s)", extra);
      return -1;
    }
    loaded = 1;
  }
  if (!loaded) {
    snprintf(err, errlen, "no CA certificates are available");
    return -1;
  }
  setup->has_ca = 1;
  return 0;
}

static int xt_tls_setup_crypto(xt_tls_setup *setup, char *err, size_t errlen) {
  mbedtls_ssl_config_init(&setup->config);
  mbedtls_entropy_init(&setup->entropy);
  mbedtls_ctr_drbg_init(&setup->ctr_drbg);
  mbedtls_x509_crt_init(&setup->ca);
  mbedtls_x509_crt_init(&setup->cert);
  mbedtls_pk_init(&setup->key);
  setup->has_ca = setup->has_cert = setup->has_key = 0;
  setup->alpn_count = 0;
  setup->alpn_list[0] = NULL;
  static const char pers[] = "xbintsc-tls";
  int ret = mbedtls_ctr_drbg_seed(&setup->ctr_drbg, mbedtls_entropy_func, &setup->entropy,
                                  (const unsigned char *)pers, sizeof(pers) - 1);
  if (ret != 0) {
    snprintf(err, errlen, "cannot seed the TLS RNG (mbedtls %d)", ret);
    return -1;
  }
  setup->initialized = 1;
  return 0;
}

void xt_tls_setup_free(xt_tls_setup *setup) {
  if (setup == NULL) return;
  mbedtls_ssl_config_free(&setup->config);
  mbedtls_ctr_drbg_free(&setup->ctr_drbg);
  mbedtls_entropy_free(&setup->entropy);
  mbedtls_x509_crt_free(&setup->ca);
  mbedtls_x509_crt_free(&setup->cert);
  mbedtls_pk_free(&setup->key);
  setup->initialized = 0;
}

void xt_tls_conn_fail(xt_tls_conn *conn, const char *message, int code) {
  char detail[128];
  if (code != 0) {
    mbedtls_strerror(code, detail, sizeof(detail));
    snprintf(conn->error, sizeof(conn->error), "%s (mbedtls: %s)", message, detail);
  } else {
    snprintf(conn->error, sizeof(conn->error), "%s", message);
  }
}
