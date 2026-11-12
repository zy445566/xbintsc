/* -- server: h2c and TLS+ALPN --------------------------------------------- */

static xt_value xt_h2_start_server(xt_h2_conn *conn, xt_value owner) {
  conn->is_server = 1;
  conn->owner = owner;
  snprintf(conn->scheme, sizeof(conn->scheme), "%s", conn->tls != NULL ? "https" : "http");
  snprintf(conn->authority, sizeof(conn->authority), "%s", "localhost");
  nghttp2_session_callbacks *callbacks = NULL;
  nghttp2_session_callbacks_new(&callbacks);
  xt_h2_install_callbacks(callbacks);
  nghttp2_session_server_new(&conn->session, callbacks, conn);
  nghttp2_session_callbacks_del(callbacks);
  nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, NULL, 0);
  conn->session_value = xt_h2_session_object(conn);
  /* A server session is never returned to JavaScript, so keep it reachable from
   * the server object (`server.__sessions`); otherwise the collector may free
   * it while nghttp2 callbacks still need it. */
  if (XT_IS_OBJECT(owner)) {
    xt_value sessions = xt_object_get_cstr(owner, "__sessions");
    if (!XT_IS_ARRAY(sessions)) {
      sessions = xt_array_new(0, NULL);
      xt_node_set(owner, "__sessions", sessions);
    }
    xt_array_push(sessions, conn->session_value);
  }
  xt_h2_attach(conn, conn->session_value);
  return conn->session_value;
}

static void xt_h2_server_adopt_socket(xt_value socket, int *fd, xt_tls_conn **tls) {
  *fd = (int)xt_to_number(xt_object_get_cstr(socket, "__fd"));
  *tls = NULL;
  if (*fd >= 0) xt_loop_remove(*fd);
}

/* `net` connection: cleartext HTTP/2 with prior knowledge (h2c). */
static xt_value xt_h2_server_connection(xt_value server, xt_value env, int32_t argc,
                                        xt_value *argv) {
  (void)env;
  if (argc < 1) return XT_UNDEFINED;
  xt_value socket = argv[0];
  int fd = -1;
  xt_tls_conn *tls = NULL;
  xt_h2_server_adopt_socket(socket, &fd, &tls);
  if (fd < 0) return XT_UNDEFINED;
  xt_node_set(socket, "__fd", xt_number(-1));
  xt_node_set(socket, "__closed", xt_bool(1));
  xt_h2_conn *conn = (xt_h2_conn *)calloc(1, sizeof(xt_h2_conn));
  if (conn == NULL) {
    xt_h2_closesocket(fd);
    return XT_UNDEFINED;
  }
  conn->fd = fd;
  conn->tls = tls;
  xt_value session = xt_h2_start_server(conn, server);
  if (!conn->closed) xt_node_emit1(server, "session", session);
  return XT_UNDEFINED;
}

/* `tls` secureConnection: HTTP/2 over TLS (ALPN negotiated `h2`). */
static xt_value xt_h2_secure_connection(xt_value server, xt_value env, int32_t argc,
                                        xt_value *argv) {
  (void)env;
  if (argc < 1) return XT_UNDEFINED;
  xt_value socket = argv[0];
  int fd = -1;
  xt_tls_conn *tls = NULL;
  xt_h2_server_adopt_socket(socket, &fd, &tls);
  if (fd < 0) return XT_UNDEFINED;
  tls = (xt_tls_conn *)(uintptr_t)xt_to_number(xt_object_get_cstr(socket, "__conn"));
  xt_node_set(socket, "__conn", xt_number(0));
  xt_node_set(socket, "__fd", xt_number(-1));
  xt_node_set(socket, "__closed", xt_bool(1));
  if (tls == NULL) {
    xt_h2_closesocket(fd);
    return XT_UNDEFINED;
  }
  xt_h2_conn *conn = (xt_h2_conn *)calloc(1, sizeof(xt_h2_conn));
  if (conn == NULL) {
    xt_tls_core_api->close(tls);
    return XT_UNDEFINED;
  }
  conn->fd = fd;
  conn->tls = tls;
  xt_value session = xt_h2_start_server(conn, server);
  if (!conn->closed) xt_node_emit1(server, "session", session);
  return XT_UNDEFINED;
}

static xt_value xt_h2_attach_handler(xt_value server, xt_value handler) {
  if (XT_IS_FUNCTION(handler)) {
    xt_array_push(xt_node_listeners(server, "stream", 1), handler);
  }
  return server;
}

xt_value xt_h2_create_server(int32_t argc, xt_value *argv) {
  xt_value handler = xt_undefined();
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i]) && handler == xt_undefined()) handler = argv[i];
  }
  xt_value server = xt_net_static(xt_string_from_cstr("createServer"), 0, NULL);
  xt_array_push(xt_node_listeners(server, "connection", 1),
                xt_closure_new((void *)xt_h2_server_connection, 0, NULL));
  return xt_h2_attach_handler(server, handler);
}

xt_value xt_h2_create_secure_server(int32_t argc, xt_value *argv) {
  if (xt_tls_node_api == NULL || xt_tls_core_api == NULL) {
    xt_throw(xt_h2_error("createSecureServer requires the node-tls and tls link features"));
    return XT_UNDEFINED;
  }
  xt_value options = xt_object_new();
  xt_value handler = xt_undefined();
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_OBJECT(argv[i])) {
      xt_value keys = xt_object_keys(argv[i]);
      int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
      for (int32_t k = 0; k < count; k++) {
        xt_value key = xt_array_get(keys, xt_number((double)k));
        xt_object_set(options, key, xt_object_get(argv[i], key));
      }
    } else if (XT_IS_FUNCTION(argv[i]) && handler == xt_undefined()) {
      handler = argv[i];
    }
  }
  xt_value protocols = xt_array_new(0, NULL);
  xt_array_push(protocols, xt_string_from_cstr("h2"));
  xt_node_set(options, "ALPNProtocols", protocols);
  xt_value arguments[1] = {options};
  xt_value server = xt_tls_node_api->create_server(1, arguments);
  xt_array_push(xt_node_listeners(server, "secureConnection", 1),
                xt_closure_new((void *)xt_h2_secure_connection, 0, NULL));
  return xt_h2_attach_handler(server, handler);
}

/* -- server response ------------------------------------------------------ */

static void xt_h2_response_submit(xt_value response) {
  if (xt_truthy(xt_object_get_cstr(response, "__submitted"))) return;
  xt_h2_conn *conn = xt_h2_conn_of(response);
  if (conn == NULL || conn->closed) return;
  xt_h2_body *body = xt_h2_body_of(response);
  if (body == NULL) return;
  xt_node_set(response, "__submitted", xt_bool(1));
  body->ended = 1;
  int32_t id = (int32_t)xt_to_number(xt_object_get_cstr(response, "__id"));
  nghttp2_nv nva[XT_H2_MAX_HEADERS];
  char status[16];
  snprintf(status, sizeof(status), "%d",
           (int)xt_to_number(xt_object_get_cstr(response, "__status")));
  size_t count = xt_h2_add_nv(nva, 0, XT_H2_MAX_HEADERS, ":status", status);
  count = xt_h2_add_headers(nva, count, XT_H2_MAX_HEADERS,
                            xt_object_get_cstr(response, "__headers"));
  nghttp2_data_provider2 provider;
  provider.source.ptr = body;
  provider.read_callback = xt_h2_read_body;
  nghttp2_submit_response2(conn->session, id, nva, count, &provider);
  xt_h2_free_nv(nva, count);
  xt_h2_flush(conn);
}

static void xt_h2_merge_headers(xt_value target, xt_value source) {
  if (!XT_IS_OBJECT(source)) return;
  xt_value keys = xt_object_keys(source);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *name = xt_string_data(xt_to_string(key));
    if (name != NULL && name[0] == ':') continue;
    xt_object_set(target, key, xt_object_get(source, key));
  }
}

xt_value xt_h2_response_write_head(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int index = 0;
  if (argc > 0 && XT_IS_NUMBER(argv[0])) {
    xt_node_set(thisValue, "__status", xt_number(xt_to_number(argv[0])));
    index = 1;
  }
  for (int32_t i = index; i < argc; i++) {
    if (XT_IS_OBJECT(argv[i])) {
      xt_value status = xt_object_get_cstr(argv[i], ":status");
      if (!xt_truthy(xt_is_nullish(status))) {
        xt_node_set(thisValue, "__status", xt_number(xt_to_number(status)));
      }
    }
    xt_h2_merge_headers(xt_object_get_cstr(thisValue, "__headers"), argv[i]);
  }
  return thisValue;
}

xt_value xt_h2_response_set_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_object_set(xt_object_get_cstr(thisValue, "__headers"), xt_arg(argc, argv, 0), xt_arg(argc, argv, 1));
  return thisValue;
}

xt_value xt_h2_response_get_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_object_get(xt_object_get_cstr(thisValue, "__headers"), xt_arg(argc, argv, 0));
}

xt_value xt_h2_response_get_headers(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(thisValue, "__headers");
}

xt_value xt_h2_response_has_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_object_get(xt_object_get_cstr(thisValue, "__headers"), xt_arg(argc, argv, 0));
  return xt_bool(!xt_truthy(xt_is_nullish(value)));
}

xt_value xt_h2_response_remove_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value headers = xt_object_get_cstr(thisValue, "__headers");
  xt_value key = xt_arg(argc, argv, 0);
  xt_value kept = xt_object_new();
  xt_value keys = xt_object_keys(headers);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(keys, xt_number((double)i));
    if (!xt_truthy(xt_eq(item, key))) xt_object_set(kept, item, xt_object_get(headers, item));
  }
  xt_node_set(thisValue, "__headers", kept);
  return thisValue;
}

xt_value xt_h2_response_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc > 0) xt_h2_stream_append(thisValue, argv[0]);
  return xt_bool(1);
}

xt_value xt_h2_response_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) xt_h2_stream_append(thisValue, argv[0]);
  xt_h2_response_submit(thisValue);
  return thisValue;
}

xt_value xt_h2_response_flush(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

xt_value xt_h2_response_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_h2_conn *conn = xt_h2_conn_of(thisValue);
  if (conn == NULL || conn->closed) return thisValue;
  int32_t id = (int32_t)xt_to_number(xt_object_get_cstr(thisValue, "__id"));
  nghttp2_submit_rst_stream(conn->session, NGHTTP2_FLAG_NONE, id, NGHTTP2_CANCEL);
  xt_h2_flush(conn);
  return thisValue;
}

xt_value xt_h2_request_respond(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value response = xt_object_get_cstr(thisValue, "__res");
  if (!XT_IS_OBJECT(response)) return thisValue;
  xt_h2_response_write_head(response, env, argc, argv);
  return thisValue;
}

xt_value xt_h2_request_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value response = xt_object_get_cstr(thisValue, "__res");
  if (!XT_IS_OBJECT(response)) return xt_bool(0);
  return xt_h2_response_write(response, env, argc, argv);
}

xt_value xt_h2_request_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value response = xt_object_get_cstr(thisValue, "__res");
  if (!XT_IS_OBJECT(response)) return thisValue;
  xt_h2_response_end(response, env, argc, argv);
  return thisValue;
}

xt_value xt_h2_request_set_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value response = xt_object_get_cstr(thisValue, "__res");
  if (!XT_IS_OBJECT(response)) return thisValue;
  return xt_h2_response_set_header(response, env, argc, argv);
}

xt_value xt_h2_request_get_header(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value response = xt_object_get_cstr(thisValue, "__res");
  if (!XT_IS_OBJECT(response)) return xt_undefined();
  return xt_h2_response_get_header(response, env, argc, argv);
}

xt_value xt_h2_request_get_headers(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value response = xt_object_get_cstr(thisValue, "__res");
  if (!XT_IS_OBJECT(response)) return xt_object_new();
  return xt_h2_response_get_headers(response, env, argc, argv);
}
