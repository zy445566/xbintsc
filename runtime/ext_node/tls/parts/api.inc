/* -- API table and registration ------------------------------------------- */

xt_value xt_tls_backend_socket_ctor(int32_t argc, xt_value *argv) {
  xt_value socket = xt_object_new_with_proto(xt_tls_socket_proto());
  xt_node_set(socket, "__conn", xt_number(0));
  xt_node_set(socket, "__fd", xt_number(-1));
  xt_node_set(socket, "encrypted", xt_bool(1));
  xt_node_set(socket, "authorized", xt_bool(0));
  if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    xt_value conn = xt_object_get_cstr(argv[0], "__conn");
    xt_value fd = xt_object_get_cstr(argv[0], "__fd");
    if (XT_IS_NUMBER(conn) && XT_IS_NUMBER(fd)) {
      xt_node_set(socket, "__conn", conn);
      xt_node_set(socket, "__fd", fd);
      xt_node_set(argv[0], "__fd", xt_number(-1));
      int descriptor = (int)xt_to_number(fd);
      if (descriptor >= 0) xt_loop_add(descriptor, XT_IO_READ, xt_tls_socket_read, xt_tls_box(socket));
    }
  }
  return socket;
}

static const XtNodeTlsApi kNodeTlsApi = {
    xt_tls_backend_connect,
    xt_tls_backend_create_server,
    xt_tls_backend_socket_ctor,
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_node_tls_boot(void) {
  xt_node_tls_register(&kNodeTlsApi);
}
#elif defined(_MSC_VER)
void xt_node_tls_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_node_tls_boot_ctor)(void) = xt_node_tls_boot;
void xt_node_tls_boot(void) { xt_node_tls_register(&kNodeTlsApi); }
#endif
