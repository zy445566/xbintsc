/* -- socket read loop ----------------------------------------------------- */

static void xt_net_socket_read(void *userdata, int events) {
  (void)events;
  xt_value socket = *(xt_value *)userdata;
  int fd = xt_net_fd(socket);
  if (fd < 0) return;

  unsigned char buffer[65536];
#if defined(_WIN32)
  int count = recv(fd, (char *)buffer, (int)sizeof(buffer), 0);
#else
  ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
#endif
  if (count > 0) {
    const char *encoding = xt_node_encoding(xt_object_get_cstr(socket, "__encoding"));
    xt_value chunk = xt_node_encode(buffer, (size_t)count, encoding);
    xt_node_emit1(socket, "data", chunk);
    return;
  }
  if (count == 0) {
    xt_loop_remove(fd);
    xt_close_socket(fd);
    xt_node_set(socket, "__fd", xt_number(-1));
    xt_node_set(socket, "__closed", xt_bool(1));
    xt_node_emit0(socket, "end");
    xt_node_emit0(socket, "close");
    return;
  }
  if (xt_net_would_block()) return;
  xt_loop_remove(fd);
  xt_close_socket(fd);
  xt_node_set(socket, "__fd", xt_number(-1));
  xt_node_set(socket, "__closed", xt_bool(1));
  xt_value error = xt_string_from_cstr("ECONNRESET");
  xt_node_emit1(socket, "error", error);
  xt_node_emit0(socket, "close");
}

static xt_value xt_net_socket_from_fd(int fd, const struct sockaddr *peer, xt_socklen_t peerLength) {
  xt_value socket = xt_object_new_with_proto(xt_net_socket_proto());
  xt_node_set(socket, "__fd", xt_number((double)fd));
  xt_node_set(socket, "__connected", xt_bool(1));
  xt_node_set(socket, "__readable", xt_array_new(0, NULL));

  char host[NI_MAXHOST];
  char service[NI_MAXSERV];
  if (peer && getnameinfo(peer, peerLength, host, sizeof(host), service, sizeof(service),
                          NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
    xt_node_set(socket, "remoteAddress", xt_string_from_cstr(host));
    xt_node_set(socket, "remotePort", xt_number((double)atoi(service)));
    xt_node_set(socket, "remoteFamily", xt_string_from_cstr(strchr(host, ':') ? "IPv6" : "IPv4"));
  }

  xt_net_set_nonblocking(fd);
  xt_loop_add(fd, XT_IO_READ, xt_net_socket_read, xt_net_box(socket));
  return socket;
}
