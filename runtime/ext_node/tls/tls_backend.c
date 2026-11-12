/*
 * xbintsc `node-tls` link feature — the JavaScript `node:tls` surface.
 *
 * Compiled only when a program imports `node:tls` (its `requires` also pulls
 * in the mbedTLS `tls` transport). The implementation is split by area under
 * `tls/parts/` and `#include`d here as one translation unit so the helpers stay
 * `static`, following the `net`/`http` modules.
 *
 * `TLSSocket` is an EventEmitter over an `xt_tls_conn`; client connects and
 * server accepts run the blocking mbedTLS handshake (the runtime has a
 * cooperative, single-threaded reactor), then a readiness watcher decrypts
 * records and emits `data`. `createServer` reuses `net.Server` for listening
 * and replaces its raw `connection` handling with a handshake that emits
 * `secureConnection`.
 */

#include "../node_common.h"
#include "../../xt_node_tls_api.h"
#include "../../xt_tls_api.h"

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int xt_tls_backend_socklen_t;
#define xt_tls_backend_closesocket closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef socklen_t xt_tls_backend_socklen_t;
#define xt_tls_backend_closesocket close
#endif

/* Provided by net.c (linked with the node extension). */
extern xt_value xt_net_static(xt_value name, int32_t argc, xt_value *argv);

/* Byte helpers exported by buffer.c. */
int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

static xt_value xt_tls_socket_proto(void);

#include "parts/helpers.inc"
#include "parts/socket.inc"
#include "parts/connect.inc"
#include "parts/server.inc"
#include "parts/api.inc"
