/* -- construction --------------------------------------------------------- */

xt_value xt_net_socket_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value socket = xt_object_new_with_proto(xt_net_socket_proto());
  xt_node_set(socket, "__fd", xt_number(-1));
  xt_node_set(socket, "__readable", xt_array_new(0, NULL));
  return socket;
}

xt_value xt_net_server_ctor(int32_t argc, xt_value *argv) {
  xt_value server = xt_object_new_with_proto(xt_net_server_proto());
  xt_node_set(server, "__fd", xt_number(-1));
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) {
      xt_array_push(xt_node_listeners(server, "connection", 1), argv[i]);
      break;
    }
  }
  return server;
}
