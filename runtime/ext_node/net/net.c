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

/* Winsock reports socket errors through WSAGetLastError, not errno. */
#if defined(_WIN32)
#define xt_net_would_block() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#define xt_net_would_block() (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

/* Byte access helpers exported by the Buffer module. */
int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

static xt_value xt_net_socket_proto(void);
static xt_value xt_net_server_proto(void);

/*
 * The implementation is grouped by functional area under `net/parts/` and
 * `#include`d here as a single translation unit so the section helpers stay
 * `static` while each file stays focused on one area.
 */

#include "parts/platform.inc"
#include "parts/read-loop.inc"
#include "parts/socket-methods.inc"
#include "parts/server.inc"
#include "parts/construction.inc"
#include "parts/static-api.inc"
#include "parts/prototypes.inc"
