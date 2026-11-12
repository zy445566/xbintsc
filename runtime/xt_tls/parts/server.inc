/* -- server --------------------------------------------------------------- */

static int xt_tls_server_identity(xt_tls_setup *setup, const char *cert_pem, const char *key_pem,
                                  char *err, size_t errlen) {
  if (cert_pem == NULL || cert_pem[0] == '\0' || key_pem == NULL || key_pem[0] == '\0') {
    snprintf(err, errlen, "a TLS server requires both a certificate and a private key");
    return -1;
  }
  int ret = mbedtls_x509_crt_parse(&setup->cert, (const unsigned char *)cert_pem,
                                   strlen(cert_pem) + 1);
  if (ret != 0) {
    snprintf(err, errlen, "cannot parse the server certificate (mbedtls %d)", ret);
    return -1;
  }
  setup->has_cert = 1;
  ret = mbedtls_pk_parse_key(&setup->key, (const unsigned char *)key_pem, strlen(key_pem) + 1,
                             NULL, 0, mbedtls_ctr_drbg_random, &setup->ctr_drbg);
  if (ret != 0) {
    snprintf(err, errlen, "cannot parse the server private key (mbedtls %d)", ret);
    return -1;
  }
  setup->has_key = 1;
  return 0;
}

static xt_tls_server_config *xt_tls_server_config_new(const char *cert_pem, const char *key_pem,
                                                      const char *ca_pem, int request_cert,
                                                      int verify, const char *alpn, char *err,
                                                      size_t errlen) {
  xt_tls_server_config *config = (xt_tls_server_config *)calloc(1, sizeof(xt_tls_server_config));
  if (config == NULL) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  config->verify = verify;
  if (xt_tls_setup_crypto(&config->setup, err, errlen) != 0) goto fail;
  if (xt_tls_server_identity(&config->setup, cert_pem, key_pem, err, errlen) != 0) goto fail;
  if (mbedtls_ssl_config_defaults(&config->setup.config, MBEDTLS_SSL_IS_SERVER,
                                  MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    snprintf(err, errlen, "cannot initialise the TLS server configuration");
    goto fail;
  }
  mbedtls_ssl_conf_rng(&config->setup.config, mbedtls_ctr_drbg_random, &config->setup.ctr_drbg);
  mbedtls_ssl_conf_own_cert(&config->setup.config, &config->setup.cert, &config->setup.key);
  if (request_cert) {
    if (ca_pem == NULL || ca_pem[0] == '\0') {
      if (xt_tls_setup_trust(&config->setup, NULL, err, errlen) != 0) goto fail;
    } else if (xt_tls_setup_trust(&config->setup, ca_pem, err, errlen) != 0) {
      goto fail;
    }
    mbedtls_ssl_conf_ca_chain(&config->setup.config, &config->setup.ca, NULL);
    mbedtls_ssl_conf_authmode(&config->setup.config, MBEDTLS_SSL_VERIFY_REQUIRED);
  } else {
    mbedtls_ssl_conf_authmode(&config->setup.config, MBEDTLS_SSL_VERIFY_NONE);
  }
  xt_tls_setup_alpn(&config->setup, alpn);
  if (config->setup.alpn_count > 0) {
    mbedtls_ssl_conf_alpn_protocols(&config->setup.config, config->setup.alpn_list);
  }
  return config;

fail:
  xt_tls_setup_free(&config->setup);
  free(config);
  return NULL;
}

static void xt_tls_server_config_free(xt_tls_server_config *config) {
  if (config == NULL) return;
  xt_tls_setup_free(&config->setup);
  free(config);
}

static xt_tls_conn *xt_tls_server_accept(xt_tls_server_config *config, int fd, char *err,
                                         size_t errlen) {
  if (config == NULL) {
    snprintf(err, errlen, "the TLS server has no credentials");
    return NULL;
  }
  xt_tls_conn *conn = (xt_tls_conn *)calloc(1, sizeof(xt_tls_conn));
  if (conn == NULL) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  conn->fd = fd;
  conn->owns_setup = 0;
  conn->setup = &config->setup;
  mbedtls_ssl_init(&conn->ssl);
  xt_tls_set_blocking(fd);
  if (mbedtls_ssl_setup(&conn->ssl, &config->setup.config) != 0) {
    snprintf(err, errlen, "cannot initialise the TLS server session");
    xt_tls_conn_free(conn);
    return NULL;
  }
  mbedtls_ssl_set_bio(&conn->ssl, conn, xt_tls_send_bio, xt_tls_recv_bio, NULL);
  if (xt_tls_finish_handshake(conn, config->verify) != 0) {
    snprintf(err, errlen, "%s", conn->error);
    xt_tls_conn_free(conn);
    return NULL;
  }
  return conn;
}
