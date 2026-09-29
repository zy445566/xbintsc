/*
 * xbintsc runtime — the global `fetch` API.
 *
 * This is a small, dependency-free HTTP/1.1 client exposed as the platform
 * `fetch` global. The runtime has no event loop (promises are drained
 * synchronously by `await`), so the request is performed with blocking sockets
 * and the returned promise is already settled. TLS is not implemented, so only
 * `http://` URLs are accepted; `https://` rejects with a `TypeError`.
 *
 * The implementation is grouped by concern under `xt_fetch/` and `#include`d
 * here as a single translation unit, following the other runtime modules.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int xt_fetch_socklen_t;
#define xt_fetch_close_socket closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef socklen_t xt_fetch_socklen_t;
#define xt_fetch_close_socket close
#endif

#include "xt_fetch/buffer.inc"
#include "xt_fetch/url.inc"
#include "xt_fetch/socket.inc"
#include "xt_fetch/headers.inc"
#include "xt_fetch/response.inc"
#include "xt_fetch/request.inc"
