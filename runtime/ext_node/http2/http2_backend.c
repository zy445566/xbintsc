/*
 * xbintsc `http2` link feature — nghttp2-backed `node:http2`.
 *
 * Compiled only when a program imports `node:http2`; its `requires` pulls in
 * `tls` (mbedTLS) and `node-tls` (the TLSSocket/server wrapper). The
 * implementation is split by role under `http2/parts/` and `#include`d here as
 * one translation unit so the helpers stay `static`.
 *
 * A session owns one socket (plain h2c for `createServer`/`http://`, TLS with
 * ALPN `h2` for `createSecureServer`/`https://`) and one `nghttp2_session`.
 * The reactor's readiness callback feeds bytes into
 * `nghttp2_session_mem_recv2` and flushes `nghttp2_session_mem_send2` output;
 * nghttp2's callbacks turn frames into EventEmitter events on the JavaScript
 * stream objects.
 */

#include "../node_common.h"
#include "../../xt_http2_api.h"
#include "../../xt_node_tls_api.h"
#include "../../xt_tls_api.h"

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int xt_h2_socklen_t;
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef socklen_t xt_h2_socklen_t;
#endif

/* Provided by net.c (linked with the node extension). */
extern xt_value xt_net_static(xt_value name, int32_t argc, xt_value *argv);

/* Byte helpers exported by buffer.c. */
int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

#include "parts/common.inc"
#include "parts/streams.inc"
#include "parts/events.inc"
#include "parts/client.inc"
#include "parts/server.inc"
#include "parts/api.inc"
