/* -- server --------------------------------------------------------------- */

static void xt_net_server_accept(void *userdata, int events) {
  (void)events;
  xt_value server = *(xt_value *)userdata;
  int listenFd = xt_net_fd(server);
  if (listenFd < 0) return;
  for (;;) {
    struct sockaddr_storage storage;
    xt_socklen_t length = sizeof(storage);
    int fd = accept(listenFd, (struct sockaddr *)&storage, &length);
    if (fd < 0) break;
    xt_value socket = xt_net_socket_from_fd(fd, (struct sockaddr *)&storage, length);
    xt_node_emit1(server, "connection", socket);
  }
}

/* Parse the many listen() signatures into a port + host. */
static void xt_net_parse_listen(int32_t argc, xt_value *argv, int *port, const char **host, xt_value *callback) {
  *port = 0;
  *host = "0.0.0.0";
  *callback = XT_UNDEFINED;
  int index = 0;
  if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    xt_value portValue = xt_object_get_cstr(argv[0], "port");
    if (!xt_truthy(xt_is_nullish(portValue))) *port = (int)xt_to_number(portValue);
    xt_value hostValue = xt_object_get_cstr(argv[0], "host");
    if (XT_IS_STRING(hostValue)) *host = xt_string_data(hostValue);
    index = 1;
  } else if (argc > 0 && XT_IS_NUMBER(argv[0])) {
    *port = (int)xt_to_number(argv[0]);
    index = 1;
    if (argc > 1 && XT_IS_STRING(argv[1])) {
      *host = xt_string_data(argv[1]);
      index = 2;
    }
  }
  for (int32_t i = index; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) *callback = argv[i];
  }
}

static xt_value xt_net_server_listen(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int port = 0;
  const char *host = "0.0.0.0";
  xt_value callback = XT_UNDEFINED;
  xt_net_parse_listen(argc, argv, &port, &host, &callback);

  char service[16];
  snprintf(service, sizeof(service), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  struct addrinfo *result = NULL;
  if (getaddrinfo(host, service, &hints, &result) != 0) {
    if (XT_IS_FUNCTION(callback)) {
      xt_value error = xt_string_from_cstr("EADDRNOTAVAIL");
      xt_call_with_this(callback, xt_undefined(), 1, &error);
    }
    return thisValue;
  }

  int listenFd = -1;
  for (struct addrinfo *info = result; info; info = info->ai_next) {
    int fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (fd < 0) continue;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    if (bind(fd, info->ai_addr, info->ai_addrlen) == 0 && listen(fd, 511) == 0) {
      listenFd = fd;
      break;
    }
    xt_close_socket(fd);
  }
  freeaddrinfo(result);

  if (listenFd < 0) {
    if (XT_IS_FUNCTION(callback)) {
      xt_value error = xt_string_from_cstr("EADDRINUSE");
      xt_call_with_this(callback, xt_undefined(), 1, &error);
    }
    return thisValue;
  }

  xt_net_set_nonblocking(listenFd);
  xt_node_set(thisValue, "__fd", xt_number((double)listenFd));
  xt_node_set(thisValue, "__listening", xt_bool(1));
  xt_loop_add(listenFd, XT_IO_READ, xt_net_server_accept, xt_net_box(thisValue));

  xt_node_emit0(thisValue, "listening");
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return thisValue;
}

static xt_value xt_net_server_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int fd = xt_net_fd(thisValue);
  if (fd >= 0) {
    xt_loop_remove(fd);
    xt_close_socket(fd);
  }
  xt_node_set(thisValue, "__fd", xt_number(-1));
  xt_node_set(thisValue, "__listening", xt_bool(0));
  xt_value callback = XT_IS_FUNCTION(xt_arg(argc, argv, 0)) ? xt_arg(argc, argv, 0) : XT_UNDEFINED;
  xt_node_emit0(thisValue, "close");
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return thisValue;
}

static xt_value xt_net_server_address(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  return xt_net_socket_address(thisValue, env, argc, argv);
}

static xt_value xt_net_server_get_connections(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  xt_value callback = xt_arg(argc, argv, 0);
  if (XT_IS_FUNCTION(callback)) {
    xt_value count = xt_number(0);
    xt_value args[2];
    args[0] = xt_null();
    args[1] = count;
    xt_call_with_this(callback, xt_undefined(), 2, args);
  }
  return xt_undefined();
}
