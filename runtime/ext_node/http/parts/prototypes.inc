/* -- prototypes ----------------------------------------------------------- */

static xt_value xt_http_server_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "listen", (void *)xt_http_server_listen);
  xt_node_define_method(proto, "close", (void *)xt_http_server_close);
  xt_node_define_method(proto, "address", (void *)xt_http_server_address);
  return proto;
}

static xt_value xt_http_response_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "writeHead", (void *)xt_http_response_write_head);
  xt_node_define_method(proto, "setHeader", (void *)xt_http_response_set_header);
  xt_node_define_method(proto, "getHeader", (void *)xt_http_response_get_header);
  xt_node_define_method(proto, "removeHeader", (void *)xt_http_response_remove_header);
  xt_node_define_method(proto, "getHeaders", (void *)xt_http_response_get_headers);
  xt_node_define_method(proto, "write", (void *)xt_http_response_write);
  xt_node_define_method(proto, "end", (void *)xt_http_response_end);
  return proto;
}

static xt_value xt_http_request_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "setEncoding", (void *)xt_http_noop);
  return proto;
}

static xt_value xt_http_client_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "write", (void *)xt_http_client_write);
  xt_node_define_method(proto, "end", (void *)xt_http_client_end);
  xt_node_define_method(proto, "setHeader", (void *)xt_http_client_set_header);
  xt_node_define_method(proto, "abort", (void *)xt_http_client_abort);
  return proto;
}

static xt_value xt_http_client_response_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "setEncoding", (void *)xt_http_noop);
  return proto;
}
