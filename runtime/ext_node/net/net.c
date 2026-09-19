/*
 * Node.js `net` for xbintsc.
 *
 * TCP client and server support built directly on the core event loop
 * (`xt_loop_add`). `net.connect` performs a conventional blocking connect and
 * then registers the socket for readiness notifications, while
 * `net.createServer(...).listen(...)` registers the listening descriptor and
 * accepts connections as they arrive. Sockets are EventEmitters that expose
 * `write`/`end`/`destroy` and emit `data`, `end`, `close`, `connect`.
 */

#include "../node_common.h"

#include <errno.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int xt_socklen_t;
#define xt_close_socket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef socklen_t xt_socklen_t;
#define xt_close_socket close
#endif

/* Byte access helpers exported by the Buffer module. */
int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

static xt_value xt_net_socket_proto(void);
static xt_value xt_net_server_proto(void);

/* -- platform helpers ----------------------------------------------------- */

static void xt_net_set_nonblocking(int fd) {
#if defined(_WIN32)
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static xt_value *xt_net_box(xt_value value) {
  xt_value *handle = (xt_value *)malloc(sizeof(xt_value));
  *handle = value;
  return handle;
}

static int xt_net_fd(xt_value object) {
  return (int)xt_to_number(xt_object_get_cstr(object, "__fd"));
}

static unsigned char *xt_net_bytes(xt_value data, size_t *outLength) {
  if (xt_node_is_buffer(data)) return xt_node_buffer_bytes(data, outLength);
  xt_value text = xt_to_string(data);
  const char *string = xt_string_data(text);
  size_t length = string ? (size_t)xt_string_length(xt_as_string(text)) : 0;
  unsigned char *bytes = (unsigned char *)malloc(length + 1);
  if (length > 0 && string) memcpy(bytes, string, length);
  bytes[length] = '\0';
  *outLength = length;
  return bytes;
}

static int xt_net_send_all(int fd, const unsigned char *data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
#if defined(_WIN32)
    int written = send(fd, (const char *)data + sent, (int)(length - sent), 0);
#else
    ssize_t written = send(fd, data + sent, length - sent, 0);
#endif
    if (written > 0) {
      sent += (size_t)written;
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      fd_set writeSet;
      FD_ZERO(&writeSet);
      FD_SET(fd, &writeSet);
      struct timeval timeout;
      timeout.tv_sec = 2;
      timeout.tv_usec = 0;
      if (select(fd + 1, NULL, &writeSet, NULL, &timeout) <= 0) return -1;
      continue;
    }
    return -1;
  }
  return 0;
}

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
  if (errno == EAGAIN || errno == EWOULDBLOCK) return;
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
