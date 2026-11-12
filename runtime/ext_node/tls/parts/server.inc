/* -- tls.createServer ----------------------------------------------------- */

/*
 * Listening is delegated to `net.Server`: `createServer` builds a plain net
 * server, then replaces the handling of each accepted socket with a TLS
 * handshake. Reusing the net server keeps `listen`/`address`/`listening` and
 * the error surface identical to `node:net`.
 */

static xt_value xt_tls_server_connection(xt_value server, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1) return XT_UNDEFINED;
  xt_value socket = argv[0];
  int fd = xt_tls_socket_fd(socket);
  if (fd < 0) return XT_UNDEFINED;

  /* Take the descriptor away from the net socket: its readiness watcher would
   * otherwise read encrypted bytes as plaintext. */
  xt_loop_remove(fd);
  xt_node_set(socket, "__fd", xt_number(-1));
  xt_node_set(socket, "__closed", xt_bool(1));

  xt_tls_server_config *config =
      (xt_tls_server_config *)(uintptr_t)xt_to_number(xt_object_get_cstr(server, "__tlsConfig"));
  if (config == NULL || xt_tls_core_api == NULL) {
    xt_tls_backend_closesocket(fd);
    return XT_UNDEFINED;
  }
  char error[256];
  error[0] = '\0';
  xt_tls_conn *conn = xt_tls_core_api->server_accept(config, fd, error, sizeof(error));
  if (conn == NULL) {
    xt_tls_backend_closesocket(fd);
    xt_node_emit1(server, "tlsClientError", xt_tls_error_value(error));
    return XT_UNDEFINED;
  }
  xt_value secureSocket = xt_tls_socket_wrap(conn);
  xt_node_set(secureSocket, "__secure", xt_bool(1));
  xt_node_emit1(server, "secureConnection", secureSocket);
  return XT_UNDEFINED;
}

static xt_value xt_tls_server_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_tls_server_config *config =
      (xt_tls_server_config *)(uintptr_t)xt_to_number(xt_object_get_cstr(thisValue, "__tlsConfig"));
  if (config != NULL && xt_tls_core_api != NULL) {
    xt_tls_core_api->config_free(config);
    xt_node_set(thisValue, "__tlsConfig", xt_number(0));
  }
  xt_value netClose = xt_object_get_cstr(thisValue, "__netClose");
  if (XT_IS_FUNCTION(netClose)) xt_call_with_this(netClose, thisValue, argc, argv);
  return thisValue;
}

xt_value xt_tls_backend_create_server(int32_t argc, xt_value *argv) {
  if (xt_tls_core_api == NULL) {
    xt_throw(xt_error_from_message("Error: the tls link feature is not linked"));
    return XT_UNDEFINED;
  }
  xt_value options = xt_undefined();
  xt_value listener = xt_undefined();
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_OBJECT(argv[i]) && options == xt_undefined()) options = argv[i];
    else if (XT_IS_FUNCTION(argv[i]) && listener == xt_undefined()) listener = argv[i];
  }

  char *cert = xt_tls_option_text(options, "cert");
  char *key = xt_tls_option_text(options, "key");
  char *ca = xt_tls_option_text(options, "ca");
  char *alpn = xt_tls_alpn_option(options);
  int requestCert = xt_tls_option_flag(options, "requestCert", 0);
  int rejectUnauthorized = xt_tls_option_flag(options, "rejectUnauthorized", 1);
  char error[256];
  error[0] = '\0';
  xt_tls_server_config *config = xt_tls_core_api->server_config(
      cert, key, ca, requestCert, requestCert && rejectUnauthorized ? 1 : 0, alpn, error, sizeof(error));
  free(cert);
  free(key);
  free(ca);
  free(alpn);
  if (config == NULL) {
    xt_throw(xt_tls_error_value(error));
    return XT_UNDEFINED;
  }

  xt_value server = xt_net_static(xt_string_from_cstr("createServer"), 0, NULL);
  xt_node_set(server, "__tlsConfig", xt_number((double)(uintptr_t)config));
  xt_value netClose = xt_object_get_cstr(server, "close");
  xt_node_set(server, "__netClose", netClose);
  xt_node_define_method(server, "close", (void *)xt_tls_server_close);

  /* Attach the handshake handler before any listener: the event emitter flushes
   * buffered events on `on`, so an early connection must not beat it. */
  xt_array_push(xt_node_listeners(server, "connection", 1),
                xt_closure_new((void *)xt_tls_server_connection, 0, NULL));
  if (XT_IS_FUNCTION(listener)) {
    xt_array_push(xt_node_listeners(server, "secureConnection", 1), listener);
  }
  return server;
}
