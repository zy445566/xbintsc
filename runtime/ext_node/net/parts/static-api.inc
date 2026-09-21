/* -- connect / static API ------------------------------------------------- */

static int xt_net_connect_fd(const char *host, int port) {
  char service[16];
  snprintf(service, sizeof(service), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *result = NULL;
  if (getaddrinfo(host, service, &hints, &result) != 0) return -1;
  int fd = -1;
  for (struct addrinfo *info = result; info; info = info->ai_next) {
    int candidate = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (candidate < 0) continue;
    if (connect(candidate, info->ai_addr, info->ai_addrlen) == 0) {
      fd = candidate;
      break;
    }
    xt_close_socket(candidate);
  }
  freeaddrinfo(result);
  return fd;
}

xt_value xt_node_net_connect(int32_t argc, xt_value *argv) {
  int port = 0;
  const char *host = "127.0.0.1";
  xt_value callback = XT_UNDEFINED;
  int index = 0;
  if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    xt_value portValue = xt_object_get_cstr(argv[0], "port");
    if (!xt_truthy(xt_is_nullish(portValue))) port = (int)xt_to_number(portValue);
    xt_value hostValue = xt_object_get_cstr(argv[0], "host");
    if (XT_IS_STRING(hostValue)) host = xt_string_data(hostValue);
    index = 1;
  } else if (argc > 0 && XT_IS_NUMBER(argv[0])) {
    port = (int)xt_to_number(argv[0]);
    index = 1;
    if (argc > 1 && XT_IS_STRING(argv[1])) {
      host = xt_string_data(argv[1]);
      index = 2;
    }
  }
  for (int32_t i = index; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) callback = argv[i];
  }

  int fd = xt_net_connect_fd(host, port);
  if (fd < 0) {
    xt_value socket = xt_net_socket_ctor(0, NULL);
    xt_value error = xt_string_from_cstr("ECONNREFUSED");
    if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 1, &error);
    else xt_node_emit1(socket, "error", error);
    return socket;
  }

  struct sockaddr_storage storage;
  xt_socklen_t length = sizeof(storage);
  getpeername(fd, (struct sockaddr *)&storage, &length);
  xt_value socket = xt_net_socket_from_fd(fd, (struct sockaddr *)&storage, length);
  xt_node_set(socket, "__connecting", xt_bool(0));
  xt_node_emit0(socket, "connect");
  xt_node_emit0(socket, "ready");
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return socket;
}

xt_value xt_net_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "createServer") == 0) {
    xt_value server = xt_net_server_ctor(0, NULL);
    for (int32_t i = 0; i < argc; i++) {
      if (XT_IS_FUNCTION(argv[i])) {
        xt_array_push(xt_node_listeners(server, "connection", 1), argv[i]);
        break;
      }
    }
    return server;
  }
  if (strcmp(fn, "connect") == 0 || strcmp(fn, "createConnection") == 0) return xt_node_net_connect(argc, argv);
  if (strcmp(fn, "isIP") == 0 || strcmp(fn, "isIPv4") == 0 || strcmp(fn, "isIPv6") == 0) {
    const char *input = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
    if (!input) return xt_number(0);
    if (strcmp(fn, "isIPv4") == 0) return xt_bool(strchr(input, '.') && !strchr(input, ':'));
    if (strcmp(fn, "isIPv6") == 0) return xt_bool(strchr(input, ':') != NULL);
    if (strchr(input, ':')) return xt_number(6);
    if (strchr(input, '.')) return xt_number(4);
    return xt_number(0);
  }
  return xt_undefined();
}
