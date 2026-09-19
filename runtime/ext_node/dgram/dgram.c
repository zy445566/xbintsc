/*
 * Node.js `dgram` for xbintsc.
 *
 * UDP sockets driven by the core event loop. `createSocket(...)` returns an
 * EventEmitter with `bind`, `send`, `close` and `address`; inbound datagrams
 * are delivered to `message` listeners together with an `rinfo` object.
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
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef socklen_t xt_socklen_t;
#define xt_close_socket close
#endif

int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

static xt_value xt_dgram_socket_proto(void);

static void xt_dgram_set_nonblocking(int fd) {
#if defined(_WIN32)
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static xt_value *xt_dgram_box(xt_value value) {
  xt_value *handle = (xt_value *)malloc(sizeof(xt_value));
  *handle = value;
  return handle;
}

static int xt_dgram_fd(xt_value socket) { return (int)xt_to_number(xt_object_get_cstr(socket, "__fd")); }

static unsigned char *xt_dgram_bytes(xt_value data, size_t *outLength) {
  if (xt_node_is_buffer(data)) return xt_node_buffer_bytes(data, outLength);
  const char *text = xt_string_data(xt_to_string(data));
  size_t length = text ? strlen(text) : 0;
  unsigned char *bytes = (unsigned char *)malloc(length + 1);
  if (length > 0 && text) memcpy(bytes, text, length);
  bytes[length] = '\0';
  *outLength = length;
  return bytes;
}

static void xt_dgram_read(void *userdata, int events) {
  (void)events;
  xt_value socket = *(xt_value *)userdata;
  int fd = xt_dgram_fd(socket);
  if (fd < 0) return;

  unsigned char buffer[65536];
  struct sockaddr_storage storage;
  xt_socklen_t length = sizeof(storage);
#if defined(_WIN32)
  int count = recvfrom(fd, (char *)buffer, (int)sizeof(buffer), 0, (struct sockaddr *)&storage, &length);
#else
  ssize_t count = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&storage, &length);
#endif
  if (count < 0) return;

  char host[NI_MAXHOST];
  char service[NI_MAXSERV];
  if (getnameinfo((struct sockaddr *)&storage, length, host, sizeof(host), service, sizeof(service),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0)
    return;

  xt_value rinfo = xt_object_new();
  xt_node_set(rinfo, "address", xt_string_from_cstr(host));
  xt_node_set(rinfo, "port", xt_number((double)atoi(service)));
  xt_node_set(rinfo, "family", xt_string_from_cstr(strchr(host, ':') ? "IPv6" : "IPv4"));
  xt_node_set(rinfo, "size", xt_number((double)count));

  const char *encoding = xt_node_encoding(xt_object_get_cstr(socket, "__encoding"));
  xt_value chunk = xt_node_encode(buffer, (size_t)count, encoding);
  xt_node_emit(socket, "message", 2, (xt_value[]){chunk, rinfo});
}

/* bind([port][, address][, callback]) | bind(options[, callback]) */
static xt_value xt_dgram_bind(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int port = 0;
  const char *address = NULL;
  xt_value callback = XT_UNDEFINED;
  int index = 0;
  if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    xt_value portValue = xt_object_get_cstr(argv[0], "port");
    if (!xt_truthy(xt_is_nullish(portValue))) port = (int)xt_to_number(portValue);
    xt_value addressValue = xt_object_get_cstr(argv[0], "address");
    if (XT_IS_STRING(addressValue)) address = xt_string_data(addressValue);
    index = 1;
  } else if (argc > 0 && XT_IS_NUMBER(argv[0])) {
    port = (int)xt_to_number(argv[0]);
    index = 1;
    if (argc > 1 && XT_IS_STRING(argv[1])) {
      address = xt_string_data(argv[1]);
      index = 2;
    }
  }
  for (int32_t i = index; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) callback = argv[i];
  }

  const char *type = xt_string_data(xt_to_string(xt_object_get_cstr(thisValue, "__type")));
  int family = (type && strcmp(type, "udp6") == 0) ? AF_INET6 : AF_INET;
  if (address && strchr(address, ':')) family = AF_INET6;

  int fd = socket(family, SOCK_DGRAM, 0);
  if (fd < 0) return thisValue;
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

  char service[16];
  snprintf(service, sizeof(service), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  struct addrinfo *result = NULL;
  int bound = -1;
  if (getaddrinfo(address, service, &hints, &result) == 0) {
    for (struct addrinfo *info = result; info; info = info->ai_next) {
      if (bind(fd, info->ai_addr, info->ai_addrlen) == 0) {
        bound = 0;
        break;
      }
    }
    freeaddrinfo(result);
  }
  if (bound != 0) {
    xt_close_socket(fd);
    xt_value error = xt_string_from_cstr("EADDRINUSE");
    xt_node_emit1(thisValue, "error", error);
    if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 1, &error);
    return thisValue;
  }

  xt_dgram_set_nonblocking(fd);
  xt_node_set(thisValue, "__fd", xt_number((double)fd));
  xt_node_set(thisValue, "__bound", xt_bool(1));
  xt_loop_add(fd, XT_IO_READ, xt_dgram_read, xt_dgram_box(thisValue));

  xt_node_emit0(thisValue, "listening");
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return thisValue;
}

static int xt_dgram_resolve(const char *host, int port, struct sockaddr_storage *storage, xt_socklen_t *length) {
  char service[16];
  snprintf(service, sizeof(service), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  struct addrinfo *result = NULL;
  if (getaddrinfo(host, service, &hints, &result) != 0) return -1;
  int status = -1;
  if (result) {
    memcpy(storage, result->ai_addr, result->ai_addrlen);
    *length = (xt_socklen_t)result->ai_addrlen;
    status = 0;
  }
  freeaddrinfo(result);
  return status;
}

/* send(msg[, offset, length], port[, address][, callback]) */
static xt_value xt_dgram_send(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  int fd = xt_dgram_fd(thisValue);
  /* Node auto-binds an unbound socket on the first send. */
  if (fd < 0) {
    xt_dgram_bind(thisValue, env, 0, NULL);
    fd = xt_dgram_fd(thisValue);
  }
  size_t length = 0;
  unsigned char *bytes = xt_dgram_bytes(xt_arg(argc, argv, 0), &length);

  int port = 0;
  const char *address = "127.0.0.1";
  xt_value callback = XT_UNDEFINED;
  /* The final numeric argument is the destination port; any earlier numbers
   * are the optional `offset`/`length` (currently unused, the whole message is
   * sent). The first string is the address and the first function a callback. */
  for (int32_t i = 1; i < argc; i++) {
    if (XT_IS_NUMBER(argv[i])) port = (int)xt_to_number(argv[i]);
    else if (XT_IS_STRING(argv[i]) && strcmp(address, "127.0.0.1") == 0) address = xt_string_data(argv[i]);
    else if (XT_IS_FUNCTION(argv[i])) callback = argv[i];
  }

  xt_value error = XT_UNDEFINED;
  if (fd < 0) {
    error = xt_string_from_cstr("ERR_SOCKET_DGRAM_NOT_RUNNING");
  } else {
    struct sockaddr_storage storage;
    xt_socklen_t storageLength = 0;
    if (xt_dgram_resolve(address, port, &storage, &storageLength) != 0) {
      error = xt_string_from_cstr("EAI_FAIL");
    } else if (sendto(fd, (const char *)bytes, length, 0, (struct sockaddr *)&storage, storageLength) < 0) {
      error = xt_string_from_cstr("EIO");
    }
  }
  free(bytes);

  if (!xt_truthy(xt_is_nullish(error))) {
    xt_node_emit1(thisValue, "error", error);
    if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 1, &error);
  } else if (XT_IS_FUNCTION(callback)) {
    xt_call_with_this(callback, xt_undefined(), 0, NULL);
  }
  return thisValue;
}

static xt_value xt_dgram_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int fd = xt_dgram_fd(thisValue);
  if (fd >= 0) {
    xt_loop_remove(fd);
    xt_close_socket(fd);
  }
  xt_node_set(thisValue, "__fd", xt_number(-1));
  xt_value callback = XT_IS_FUNCTION(xt_arg(argc, argv, 0)) ? xt_arg(argc, argv, 0) : XT_UNDEFINED;
  xt_node_emit0(thisValue, "close");
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return thisValue;
}

static xt_value xt_dgram_address(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  int fd = xt_dgram_fd(thisValue);
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

static xt_value xt_dgram_set_broadcast(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int fd = xt_dgram_fd(thisValue);
  int value = argc > 0 ? (int)xt_to_number(argv[0]) : 1;
  if (fd >= 0) setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&value, sizeof(value));
  return thisValue;
}

static xt_value xt_dgram_set_ttl(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int fd = xt_dgram_fd(thisValue);
  int value = argc > 0 ? (int)xt_to_number(argv[0]) : 1;
  if (fd >= 0) setsockopt(fd, IPPROTO_IP, IP_TTL, (const char *)&value, sizeof(value));
  return thisValue;
}

static xt_value xt_dgram_noop(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

xt_value xt_node_dgram_create_socket(int32_t argc, xt_value *argv) {
  const char *type = "udp4";
  xt_value callback = XT_UNDEFINED;
  if (argc > 0 && XT_IS_STRING(argv[0])) type = xt_string_data(argv[0]);
  else if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    xt_value typeValue = xt_object_get_cstr(argv[0], "type");
    if (XT_IS_STRING(typeValue)) type = xt_string_data(typeValue);
  }
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) callback = argv[i];
  }

  xt_value socket = xt_object_new_with_proto(xt_dgram_socket_proto());
  xt_node_set(socket, "__fd", xt_number(-1));
  xt_node_set(socket, "__type", xt_string_from_cstr(type));
  if (XT_IS_FUNCTION(callback)) xt_array_push(xt_node_listeners(socket, "message", 1), callback);
  return socket;
}

xt_value xt_dgram_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "createSocket") == 0) return xt_node_dgram_create_socket(argc, argv);
  return xt_undefined();
}

static xt_value xt_dgram_socket_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "bind", (void *)xt_dgram_bind);
  xt_node_define_method(proto, "send", (void *)xt_dgram_send);
  xt_node_define_method(proto, "close", (void *)xt_dgram_close);
  xt_node_define_method(proto, "address", (void *)xt_dgram_address);
  xt_node_define_method(proto, "setBroadcast", (void *)xt_dgram_set_broadcast);
  xt_node_define_method(proto, "setTTL", (void *)xt_dgram_set_ttl);
  xt_node_define_method(proto, "setMulticastTTL", (void *)xt_dgram_set_ttl);
  xt_node_define_method(proto, "setMulticastLoopback", (void *)xt_dgram_set_broadcast);
  xt_node_define_method(proto, "setRecvBufferSize", (void *)xt_dgram_noop);
  xt_node_define_method(proto, "setSendBufferSize", (void *)xt_dgram_noop);
  xt_node_define_method(proto, "addMembership", (void *)xt_dgram_noop);
  xt_node_define_method(proto, "dropMembership", (void *)xt_dgram_noop);
  xt_node_define_method(proto, "ref", (void *)xt_dgram_noop);
  xt_node_define_method(proto, "unref", (void *)xt_dgram_noop);
  return proto;
}
