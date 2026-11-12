/* -- client: connect and request ------------------------------------------ */

static int xt_h2_tcp_connect(const char *host, int port) {
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
    if (connect(candidate, info->ai_addr, (xt_h2_socklen_t)info->ai_addrlen) == 0) {
      fd = candidate;
      break;
    }
    xt_h2_closesocket(candidate);
  }
  freeaddrinfo(result);
  return fd;
}

static void xt_h2_install_callbacks(nghttp2_session_callbacks *callbacks) {
  nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, xt_h2_on_begin_headers);
  nghttp2_session_callbacks_set_on_header_callback(callbacks, xt_h2_on_header);
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, xt_h2_on_frame_recv);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, xt_h2_on_data_chunk_recv);
  nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, xt_h2_on_stream_close);
}

static xt_value xt_h2_session_request(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_h2_conn *conn = xt_h2_conn_of(thisValue);
  if (conn == NULL || conn->closed) {
    xt_throw(xt_h2_error("the HTTP/2 session is closed"));
    return XT_UNDEFINED;
  }
  xt_value headers = xt_arg(argc, argv, 0);
  if (!XT_IS_OBJECT(headers)) {
    xt_throw(xt_h2_error("http2 request headers must be an object"));
    return XT_UNDEFINED;
  }
  const char *method = xt_string_data(xt_object_get_cstr(headers, ":method"));
  const char *path = xt_string_data(xt_object_get_cstr(headers, ":path"));
  const char *scheme = xt_string_data(xt_object_get_cstr(headers, ":scheme"));
  const char *authority = xt_string_data(xt_object_get_cstr(headers, ":authority"));
  if (method == NULL) method = "GET";
  if (path == NULL) path = "/";
  if (scheme == NULL) scheme = conn->scheme;
  if (authority == NULL) authority = conn->authority;

  nghttp2_nv nva[XT_H2_MAX_HEADERS];
  size_t count = 0;
  count = xt_h2_add_nv(nva, count, XT_H2_MAX_HEADERS, ":method", method);
  count = xt_h2_add_nv(nva, count, XT_H2_MAX_HEADERS, ":path", path);
  count = xt_h2_add_nv(nva, count, XT_H2_MAX_HEADERS, ":scheme", scheme);
  count = xt_h2_add_nv(nva, count, XT_H2_MAX_HEADERS, ":authority", authority);
  count = xt_h2_add_headers(nva, count, XT_H2_MAX_HEADERS, headers);

  xt_h2_body *body = xt_h2_body_new();
  nghttp2_data_provider2 provider;
  provider.source.ptr = body;
  provider.read_callback = xt_h2_read_body;
  int32_t id = nghttp2_submit_request2(conn->session, NULL, nva, count, &provider, NULL);
  xt_h2_free_nv(nva, count);
  if (id < 0) {
    xt_h2_body_free(body);
    xt_throw(xt_h2_error("cannot submit the HTTP/2 request"));
    return XT_UNDEFINED;
  }
  xt_value stream = xt_h2_new_stream(conn, id, body);
  xt_h2_flush(conn);
  return stream;
}

xt_value xt_h2_session_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_h2_conn *conn = xt_h2_conn_of(thisValue);
  if (conn == NULL || conn->closed) return thisValue;
  nghttp2_session_terminate_session(conn->session, NGHTTP2_NO_ERROR);
  xt_h2_flush(conn);
  xt_h2_finish(conn, NULL);
  return thisValue;
}

xt_value xt_h2_stream_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc > 0) xt_h2_stream_append(thisValue, argv[0]);
  xt_h2_conn *conn = xt_h2_conn_of(thisValue);
  if (conn == NULL || conn->closed) return xt_bool(0);
  int32_t id = (int32_t)xt_to_number(xt_object_get_cstr(thisValue, "__id"));
  nghttp2_session_resume_data(conn->session, id);
  xt_h2_flush(conn);
  return xt_bool(1);
}

xt_value xt_h2_stream_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) xt_h2_stream_append(thisValue, argv[0]);
  xt_h2_body *body = xt_h2_body_of(thisValue);
  if (body != NULL) body->ended = 1;
  xt_h2_conn *conn = xt_h2_conn_of(thisValue);
  if (conn == NULL || conn->closed) return thisValue;
  int32_t id = (int32_t)xt_to_number(xt_object_get_cstr(thisValue, "__id"));
  nghttp2_session_resume_data(conn->session, id);
  xt_h2_flush(conn);
  return thisValue;
}

xt_value xt_h2_stream_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
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

xt_value xt_h2_stream_set_encoding(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_node_set(thisValue, "__encoding", xt_arg(argc, argv, 0));
  return thisValue;
}

/* `http2.connect(authority[, options][, listener])` */
xt_value xt_h2_connect(int32_t argc, xt_value *argv) {
  const char *authority = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
  if (authority == NULL) {
    xt_throw(xt_h2_error("http2.connect requires an authority URL"));
    return XT_UNDEFINED;
  }
  char scheme[8];
  char host[256];
  char authorityCopy[512];
  int port = 0;
  int secure = xt_h2_parse_authority(authority, scheme, sizeof(scheme), host, sizeof(host), &port,
                                     authorityCopy, sizeof(authorityCopy));
  if (secure < 0) {
    xt_throw(xt_h2_error("http2.connect requires an http:// or https:// URL"));
    return XT_UNDEFINED;
  }

  xt_value options = xt_undefined();
  xt_value listener = xt_undefined();
  for (int32_t i = 1; i < argc; i++) {
    if (XT_IS_OBJECT(argv[i]) && options == xt_undefined()) options = argv[i];
    else if (XT_IS_FUNCTION(argv[i])) listener = argv[i];
  }

  xt_h2_conn *conn = (xt_h2_conn *)calloc(1, sizeof(xt_h2_conn));
  if (conn == NULL) {
    xt_throw(xt_h2_error("out of memory"));
    return XT_UNDEFINED;
  }
  conn->fd = -1;
  snprintf(conn->scheme, sizeof(conn->scheme), "%s", scheme);
  snprintf(conn->authority, sizeof(conn->authority), "%s", authorityCopy);

  if (secure) {
    if (xt_tls_core_api == NULL) {
      free(conn);
      xt_throw(xt_h2_error("https requires the tls link feature"));
      return XT_UNDEFINED;
    }
    char error[256];
    error[0] = '\0';
    int verify = 1;
    if (XT_IS_OBJECT(options)) {
      xt_value reject = xt_object_get_cstr(options, "rejectUnauthorized");
      if (!xt_truthy(xt_is_nullish(reject))) verify = xt_truthy(reject) ? 1 : 0;
    }
    xt_value caValue = XT_IS_OBJECT(options) ? xt_object_get_cstr(options, "ca") : xt_undefined();
    const char *ca = XT_IS_STRING(caValue) ? xt_string_data(caValue) : NULL;
    conn->tls = xt_tls_core_api->client_connect(host, port, host, verify, ca, NULL, NULL, "h2",
                                                error, sizeof(error));
    if (conn->tls == NULL) {
      free(conn);
      xt_throw(xt_h2_error(error));
      return XT_UNDEFINED;
    }
    conn->fd = xt_tls_core_api->fd(conn->tls);
  } else {
    conn->fd = xt_h2_tcp_connect(host, port);
    if (conn->fd < 0) {
      free(conn);
      xt_throw(xt_h2_error("http2.connect could not reach the server"));
      return XT_UNDEFINED;
    }
  }

  nghttp2_session_callbacks *callbacks = NULL;
  nghttp2_session_callbacks_new(&callbacks);
  xt_h2_install_callbacks(callbacks);
  nghttp2_session_client_new(&conn->session, callbacks, conn);
  nghttp2_session_callbacks_del(callbacks);
  nghttp2_submit_settings(conn->session, NGHTTP2_FLAG_NONE, NULL, 0);

  conn->is_server = 0;
  conn->session_value = xt_h2_session_object(conn);
  xt_value session = conn->session_value;
  xt_h2_attach(conn, session);
  if (XT_IS_FUNCTION(listener)) {
    xt_array_push(xt_node_listeners(session, "connect", 1), listener);
  }
  xt_node_emit0(session, "connect");
  return session;
}
