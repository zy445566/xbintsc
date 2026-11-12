/* -- TLSSocket ------------------------------------------------------------ */

static void xt_tls_socket_finish(xt_value socket, int emitEnd) {
  int fd = xt_tls_socket_fd(socket);
  if (fd >= 0) xt_loop_remove(fd);
  xt_tls_conn *conn = xt_tls_conn_of(socket);
  if (conn != NULL) {
    xt_tls_core_api->close(conn);
    xt_tls_set_conn(socket, NULL);
  }
  xt_node_set(socket, "__fd", xt_number(-1));
  if (!xt_truthy(xt_object_get_cstr(socket, "__closed"))) {
    xt_node_set(socket, "__closed", xt_bool(1));
    if (emitEnd) xt_node_emit0(socket, "end");
    xt_node_emit0(socket, "close");
  }
}

static void xt_tls_socket_read(void *userdata, int events) {
  (void)events;
  if (xt_tls_core_api == NULL) return;
  xt_value socket = *(xt_value *)userdata;
  xt_tls_conn *conn = xt_tls_conn_of(socket);
  if (conn == NULL) return;
  unsigned char buffer[65536];
  int count = xt_tls_core_api->read(conn, buffer, sizeof(buffer));
  if (count > 0) {
    const char *encoding = xt_node_encoding(xt_object_get_cstr(socket, "__encoding"));
    xt_node_emit1(socket, "data", xt_node_encode(buffer, (size_t)count, encoding));
    return;
  }
  if (count == 0) {
    xt_tls_socket_finish(socket, 1);
    return;
  }
  const char *message = xt_tls_core_api->error(conn);
  char detail[256];
  snprintf(detail, sizeof(detail), "%s", message != NULL ? message : "TLS read failed");
  xt_tls_socket_finish(socket, 0);
  xt_node_emit1(socket, "error", xt_tls_error_value(detail));
}

static xt_value xt_tls_socket_wrap(xt_tls_conn *conn) {
  xt_value socket = xt_object_new_with_proto(xt_tls_socket_proto());
  int fd = xt_tls_core_api->fd(conn);
  xt_tls_set_conn(socket, conn);
  xt_node_set(socket, "__fd", xt_number((double)fd));
  xt_node_set(socket, "encrypted", xt_bool(1));
  xt_node_set(socket, "authorized", xt_bool(1));
  const char *alpn = xt_tls_core_api->alpn(conn);
  if (alpn != NULL) xt_node_set(socket, "alpnProtocol", xt_string_from_cstr(alpn));
  xt_loop_add(fd, XT_IO_READ, xt_tls_socket_read, xt_tls_box(socket));
  return socket;
}

static xt_value xt_tls_socket_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_tls_conn *conn = xt_tls_conn_of(thisValue);
  if (conn == NULL) {
    xt_value callback = XT_IS_FUNCTION(xt_arg(argc, argv, 1)) ? xt_arg(argc, argv, 1) : xt_arg(argc, argv, 2);
    if (XT_IS_FUNCTION(callback)) {
      xt_value error = xt_tls_error_value("Cannot write after the TLS socket was closed");
      xt_call_with_this(callback, xt_undefined(), 1, &error);
    }
    return xt_bool(0);
  }
  size_t length = 0;
  unsigned char *bytes = xt_tls_bytes(xt_arg(argc, argv, 0), &length);
  int result = xt_tls_core_api->write(conn, bytes, length);
  free(bytes);
  xt_value callback = XT_IS_FUNCTION(xt_arg(argc, argv, 1)) ? xt_arg(argc, argv, 1) : xt_arg(argc, argv, 2);
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return xt_bool(result == 0);
}

static xt_value xt_tls_socket_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) {
    xt_tls_socket_write(thisValue, env, 1, &argv[0]);
  }
  int fd = xt_tls_socket_fd(thisValue);
  if (fd >= 0) {
    /* Half-close: the peer still gets our FIN but we keep reading its reply. */
#if defined(_WIN32)
    shutdown(fd, SD_SEND);
#else
    shutdown(fd, SHUT_WR);
#endif
    xt_node_set(thisValue, "__ending", xt_bool(1));
  }
  return thisValue;
}

static xt_value xt_tls_socket_destroy(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_tls_socket_finish(thisValue, 1);
  return thisValue;
}

static xt_value xt_tls_socket_set_encoding(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_node_set(thisValue, "__encoding", xt_arg(argc, argv, 0));
  return thisValue;
}

static xt_value xt_tls_socket_noop(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_tls_socket_address(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int fd = xt_tls_socket_fd(thisValue);
  if (fd < 0) {
    xt_value local = xt_object_new();
    xt_node_set(local, "address", xt_string_from_cstr("127.0.0.1"));
    xt_node_set(local, "port", xt_number(0));
    xt_node_set(local, "family", xt_string_from_cstr("IPv4"));
    return local;
  }
  struct sockaddr_storage storage;
  xt_tls_backend_socklen_t length = sizeof(storage);
  if (getsockname(fd, (struct sockaddr *)&storage, &length) != 0) return xt_null();
  char host[256];
  char service[32];
  if (getnameinfo((struct sockaddr *)&storage, length, host, sizeof(host), service, sizeof(service),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    return xt_null();
  }
  xt_value local = xt_object_new();
  xt_node_set(local, "address", xt_string_from_cstr(host));
  xt_node_set(local, "port", xt_number((double)atoi(service)));
  xt_node_set(local, "family", xt_string_from_cstr(strchr(host, ':') != NULL ? "IPv6" : "IPv4"));
  return local;
}

static xt_value xt_tls_socket_certificate(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_tls_conn *conn = xt_tls_conn_of(thisValue);
  if (conn == NULL) return xt_null();
  xt_value certificate = xt_object_new();
  xt_node_set(certificate, "subject", xt_string_from_cstr(""));
  xt_node_set(certificate, "issuer", xt_string_from_cstr(""));
  return certificate;
}

static xt_value xt_tls_socket_proto(void) {
  static xt_value proto = 0;
  xt_gc_add_root(&proto);
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "write", (void *)xt_tls_socket_write);
  xt_node_define_method(proto, "end", (void *)xt_tls_socket_end);
  xt_node_define_method(proto, "destroy", (void *)xt_tls_socket_destroy);
  xt_node_define_method(proto, "setEncoding", (void *)xt_tls_socket_set_encoding);
  xt_node_define_method(proto, "pause", (void *)xt_tls_socket_noop);
  xt_node_define_method(proto, "resume", (void *)xt_tls_socket_noop);
  xt_node_define_method(proto, "setTimeout", (void *)xt_tls_socket_noop);
  xt_node_define_method(proto, "address", (void *)xt_tls_socket_address);
  xt_node_define_method(proto, "getPeerCertificate", (void *)xt_tls_socket_certificate);
  return proto;
}
