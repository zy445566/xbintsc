/* -- socket methods ------------------------------------------------------- */

static xt_value xt_net_socket_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int fd = xt_net_fd(thisValue);
  if (fd < 0) {
    xt_value callback = xt_arg(argc, argv, argc > 1 ? 2 : 1);
    xt_value error = xt_string_from_cstr("ERR_SOCKET_CLOSED");
    if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 1, &error);
    return xt_bool(0);
  }
  size_t length = 0;
  unsigned char *bytes = xt_net_bytes(xt_arg(argc, argv, 0), &length);
  xt_net_send_all(fd, bytes, length);
  free(bytes);
  xt_value callback = XT_IS_FUNCTION(xt_arg(argc, argv, 1)) ? xt_arg(argc, argv, 1) : xt_arg(argc, argv, 2);
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return xt_bool(1);
}

static xt_value xt_net_socket_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int fd = xt_net_fd(thisValue);
  if (fd < 0) return thisValue;
  if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) {
    xt_value writeFn = xt_object_get_cstr(thisValue, "write");
    if (XT_IS_FUNCTION(writeFn)) xt_call_with_this(writeFn, thisValue, 1, &argv[0]);
  }
#if defined(_WIN32)
  shutdown(fd, SD_SEND);
#else
  shutdown(fd, SHUT_WR);
#endif
  return thisValue;
}

static xt_value xt_net_socket_destroy(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int fd = xt_net_fd(thisValue);
  if (fd >= 0) {
    xt_loop_remove(fd);
    xt_close_socket(fd);
  }
  xt_node_set(thisValue, "__fd", xt_number(-1));
  if (!xt_truthy(xt_object_get_cstr(thisValue, "__closed"))) {
    xt_node_set(thisValue, "__closed", xt_bool(1));
    xt_node_emit0(thisValue, "close");
  }
  return thisValue;
}

static xt_value xt_net_socket_set_encoding(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_node_set(thisValue, "__encoding", xt_arg(argc, argv, 0));
  return thisValue;
}

static xt_value xt_net_socket_pause(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_net_socket_resume(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_net_socket_noop(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_net_socket_address(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int fd = xt_net_fd(thisValue);
  if (fd < 0) return xt_null();
  struct sockaddr_storage storage;
  xt_socklen_t length = sizeof(storage);
  if (getsockname(fd, (struct sockaddr *)&storage, &length) != 0) return xt_null();
  char host[NI_MAXHOST];
  char service[NI_MAXSERV];
  if (getnameinfo((struct sockaddr *)&storage, length, host, sizeof(host), service, sizeof(service),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0)
    return xt_null();
  xt_value object = xt_object_new();
  xt_node_set(object, "address", xt_string_from_cstr(host));
  xt_node_set(object, "port", xt_number((double)atoi(service)));
  xt_node_set(object, "family", xt_string_from_cstr(strchr(host, ':') ? "IPv6" : "IPv4"));
  return object;
}
