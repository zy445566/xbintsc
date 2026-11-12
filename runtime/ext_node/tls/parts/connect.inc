/* -- tls.connect ---------------------------------------------------------- */

/*
 * Accepted forms (Node):
 *   connect(options[, callback])
 *   connect(port[, host][, options][, callback])
 * The handshake is synchronous in this runtime: the returned socket is already
 * secure, and the callback runs once before `connect` returns.
 */

static void xt_tls_parse_connect(int32_t argc, xt_value *argv, xt_value *options, int *port,
                                 char **host, xt_value *callback) {
  *options = xt_undefined();
  *port = 0;
  *host = NULL;
  *callback = xt_undefined();
  int index = 0;
  if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    *options = argv[0];
    index = 1;
  } else if (argc > 0 && XT_IS_NUMBER(argv[0])) {
    *port = (int)xt_to_number(argv[0]);
    index = 1;
    if (argc > 1 && XT_IS_STRING(argv[1])) {
      *host = xt_tls_text(argv[1]);
      index = 2;
    }
  }
  for (int32_t i = index; i < argc; i++) {
    if (XT_IS_OBJECT(argv[i]) && *options == xt_undefined()) {
      *options = argv[i];
    } else if (XT_IS_FUNCTION(argv[i])) {
      *callback = argv[i];
    }
  }
}

/*
 * `tls.connect` returns before `secureConnect` fires in Node, so the callback
 * can close over the `const socket = connect(...)` binding. Doing it inline
 * would run the callback while that binding is still uninitialised; a zero
 * delay timer restores the asynchronous ordering without blocking.
 */
static xt_value xt_tls_deferred_connect(xt_value thisValue, xt_value env, int32_t argc,
                                        xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value socket = xt_closure_env(env, 0);
  xt_value callback = xt_closure_env(env, 1);
  xt_node_emit0(socket, "secureConnect");
  xt_node_emit0(socket, "connect");
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, socket, 0, NULL);
  return XT_UNDEFINED;
}

xt_value xt_tls_backend_connect(int32_t argc, xt_value *argv) {
  if (xt_tls_core_api == NULL) {
    xt_throw(xt_error_from_message("Error: the tls link feature is not linked"));
    return XT_UNDEFINED;
  }
  xt_value options;
  int port = 0;
  char *host = NULL;
  xt_value callback;
  xt_tls_parse_connect(argc, argv, &options, &port, &host, &callback);

  if (host == NULL) host = xt_tls_option_text(options, "host");
  if (host == NULL) host = xt_tls_text(xt_string_from_cstr("127.0.0.1"));
  char *servername = xt_tls_option_text(options, "servername");
  char *ca = xt_tls_option_text(options, "ca");
  char *cert = xt_tls_option_text(options, "cert");
  char *key = xt_tls_option_text(options, "key");
  char *alpn = xt_tls_alpn_option(options);
  if (port == 0) {
    xt_value portValue = XT_IS_OBJECT(options) ? xt_object_get_cstr(options, "port") : xt_undefined();
    if (!xt_truthy(xt_is_nullish(portValue))) port = (int)xt_to_number(portValue);
  }
  if (port == 0) port = 443;
  int verify = xt_tls_option_flag(options, "rejectUnauthorized", 1);
  if (servername == NULL) servername = xt_tls_text(xt_string_from_cstr(host));

  char error[256];
  error[0] = '\0';
  xt_tls_conn *conn = xt_tls_core_api->client_connect(host, port, servername, verify, ca, cert, key,
                                                      alpn, error, sizeof(error));
  free(host);
  free(ca);
  free(cert);
  free(key);
  free(alpn);
  if (conn == NULL) {
    free(servername);
    xt_throw(xt_tls_error_value(error));
    return XT_UNDEFINED;
  }

  xt_value socket = xt_tls_socket_wrap(conn);
  xt_node_set(socket, "servername", xt_string_from_cstr(servername != NULL ? servername : ""));
  free(servername);
  xt_node_set(socket, "__secure", xt_bool(1));
  xt_value captures[2] = {socket, callback};
  xt_value deferred = xt_closure_new((void *)xt_tls_deferred_connect, 2, captures);
  xt_value timer[2] = {deferred, xt_number(0)};
  xt_set_timeout(2, timer);
  return socket;
}
