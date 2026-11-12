/* -- client --------------------------------------------------------------- */

static int xt_tls_parse_identity(xt_tls_setup *setup, const char *cert_pem, const char *key_pem,
                                 char *err, size_t errlen) {
  if (cert_pem != NULL && cert_pem[0] != '\0') {
    int ret = mbedtls_x509_crt_parse(&setup->cert, (const unsigned char *)cert_pem,
                                     strlen(cert_pem) + 1);
    if (ret != 0) {
      snprintf(err, errlen, "cannot parse the client certificate (mbedtls %d)", ret);
      return -1;
    }
    setup->has_cert = 1;
  }
  if (key_pem != NULL && key_pem[0] != '\0') {
    int ret = mbedtls_pk_parse_key(&setup->key, (const unsigned char *)key_pem,
                                   strlen(key_pem) + 1, NULL, 0, mbedtls_ctr_drbg_random,
                                   &setup->ctr_drbg);
    if (ret != 0) {
      snprintf(err, errlen, "cannot parse the client private key (mbedtls %d)", ret);
      return -1;
    }
    setup->has_key = 1;
  }
  if (setup->has_cert != setup->has_key) {
    snprintf(err, errlen, "a client certificate and key must be provided together");
    return -1;
  }
  return 0;
}

static int xt_tls_tcp_connect(const char *host, int port) {
  char service[16];
  snprintf(service, sizeof(service), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *result = NULL;
  if (getaddrinfo(host, service, &hints, &result) != 0) return -1;
  int fd = -1;
  for (struct addrinfo *info = result; info != NULL; info = info->ai_next) {
    int candidate = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (candidate < 0) continue;
    if (connect(candidate, info->ai_addr, (xt_tls_socklen_t)info->ai_addrlen) == 0) {
      fd = candidate;
      break;
    }
    xt_tls_closesocket(candidate);
  }
  freeaddrinfo(result);
  return fd;
}

static xt_tls_conn *xt_tls_client_connect(const char *host, int port, const char *servername,
                                          int verify, const char *ca_pem, const char *cert_pem,
                                          const char *key_pem, const char *alpn, char *err,
                                          size_t errlen) {
  xt_tls_conn *conn = (xt_tls_conn *)calloc(1, sizeof(xt_tls_conn));
  if (conn == NULL) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  conn->fd = -1;
  conn->owns_setup = 1;
  conn->setup = (xt_tls_setup *)calloc(1, sizeof(xt_tls_setup));
  if (conn->setup == NULL) {
    snprintf(err, errlen, "out of memory");
    free(conn);
    return NULL;
  }
  xt_tls_setup *setup = conn->setup;
  mbedtls_ssl_init(&conn->ssl);
  if (xt_tls_setup_crypto(setup, err, errlen) != 0) goto fail;
  if (xt_tls_parse_identity(setup, cert_pem, key_pem, err, errlen) != 0) goto fail;
  if (verify && xt_tls_setup_trust(setup, ca_pem, err, errlen) != 0) goto fail;
  if (mbedtls_ssl_config_defaults(&setup->config, MBEDTLS_SSL_IS_CLIENT,
                                  MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    snprintf(err, errlen, "cannot initialise the TLS client configuration");
    goto fail;
  }
  mbedtls_ssl_conf_rng(&setup->config, mbedtls_ctr_drbg_random, &setup->ctr_drbg);
  if (verify) {
    mbedtls_ssl_conf_authmode(&setup->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&setup->config, &setup->ca, NULL);
  } else {
    mbedtls_ssl_conf_authmode(&setup->config, MBEDTLS_SSL_VERIFY_NONE);
  }
  if (setup->has_cert) mbedtls_ssl_conf_own_cert(&setup->config, &setup->cert, &setup->key);
  xt_tls_setup_alpn(setup, alpn);
  if (setup->alpn_count > 0) mbedtls_ssl_conf_alpn_protocols(&setup->config, setup->alpn_list);
  if (mbedtls_ssl_setup(&conn->ssl, &setup->config) != 0) {
    snprintf(err, errlen, "cannot initialise the TLS session");
    goto fail;
  }
  if (servername != NULL && servername[0] != '\0') mbedtls_ssl_set_hostname(&conn->ssl, servername);
  conn->fd = xt_tls_tcp_connect(host, port);
  if (conn->fd < 0) {
    snprintf(err, errlen, "connect ECONNREFUSED %s:%d", host, port);
    goto fail;
  }
  xt_tls_set_blocking(conn->fd);
  mbedtls_ssl_set_bio(&conn->ssl, conn, xt_tls_send_bio, xt_tls_recv_bio, NULL);
  if (xt_tls_finish_handshake(conn, verify) != 0) {
    snprintf(err, errlen, "%s", conn->error);
    goto fail;
  }
  return conn;

fail:
  xt_tls_conn_close(conn);
  return NULL;
}
