/*
 * Node.js `dns` module for xbintsc.
 *
 * `lookup(hostname[, options], callback)` resolves synchronously through
 * `getaddrinfo` and calls the callback with the resulting address records
 * (each `{ address, family }`). This is enough for the common
 * `lookup(host, { all: true }, cb)` style used by HTTP clients.
 */

#include "../node_common.h"

#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

xt_value xt_dns_lookup(int32_t argc, xt_value *argv) {
  const char *hostname = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : NULL;
  xt_value cb = xt_arg(argc, argv, argc - 1);

  if (!hostname || hostname[0] == '\0') {
    if (XT_IS_FUNCTION(cb)) {
      xt_value error = xt_error_from_message("Error: getaddrinfo ENOTFOUND");
      xt_closure_call(cb, 1, &error);
    }
    return XT_UNDEFINED;
  }

  struct addrinfo hints;
  struct addrinfo *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(hostname, NULL, &hints, &result) != 0 || result == NULL) {
    if (XT_IS_FUNCTION(cb)) {
      xt_value error = xt_error_from_message("Error: getaddrinfo ENOTFOUND");
      xt_closure_call(cb, 1, &error);
    }
    return XT_UNDEFINED;
  }

  xt_value addresses = xt_array_new(0, NULL);
  for (struct addrinfo *ai = result; ai != NULL; ai = ai->ai_next) {
    char buffer[64] = {0};
    int family;
    if (ai->ai_family == AF_INET) {
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)ai->ai_addr;
      inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer));
      family = 4;
    } else if (ai->ai_family == AF_INET6) {
      struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ai->ai_addr;
      inet_ntop(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer));
      family = 6;
    } else {
      continue;
    }
    xt_value record = xt_object_new();
    xt_node_set(record, "address", xt_string_from_cstr(buffer));
    xt_node_set(record, "family", xt_number((double)family));
    xt_array_push(addresses, record);
  }
  freeaddrinfo(result);

  if (XT_IS_FUNCTION(cb)) {
    xt_value args[2] = { XT_NULL, addresses };
    xt_closure_call(cb, 2, args);
  }
  return XT_UNDEFINED;
}
