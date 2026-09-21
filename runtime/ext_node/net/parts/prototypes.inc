/* -- prototypes ----------------------------------------------------------- */

static xt_value xt_net_socket_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "write", (void *)xt_net_socket_write);
  xt_node_define_method(proto, "end", (void *)xt_net_socket_end);
  xt_node_define_method(proto, "destroy", (void *)xt_net_socket_destroy);
  xt_node_define_method(proto, "setEncoding", (void *)xt_net_socket_set_encoding);
  xt_node_define_method(proto, "pause", (void *)xt_net_socket_pause);
  xt_node_define_method(proto, "resume", (void *)xt_net_socket_resume);
  xt_node_define_method(proto, "setNoDelay", (void *)xt_net_socket_noop);
  xt_node_define_method(proto, "setKeepAlive", (void *)xt_net_socket_noop);
  xt_node_define_method(proto, "setTimeout", (void *)xt_net_socket_noop);
  xt_node_define_method(proto, "ref", (void *)xt_net_socket_noop);
  xt_node_define_method(proto, "unref", (void *)xt_net_socket_noop);
  xt_node_define_method(proto, "address", (void *)xt_net_socket_address);
  return proto;
}

static xt_value xt_net_server_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "listen", (void *)xt_net_server_listen);
  xt_node_define_method(proto, "close", (void *)xt_net_server_close);
  xt_node_define_method(proto, "address", (void *)xt_net_server_address);
  xt_node_define_method(proto, "getConnections", (void *)xt_net_server_get_connections);
  xt_node_define_method(proto, "ref", (void *)xt_net_socket_noop);
  xt_node_define_method(proto, "unref", (void *)xt_net_socket_noop);
  return proto;
}
