/*
 * Node.js `http` for xbintsc.
 *
 * The server side wraps a `net` server: each accepted socket accumulates bytes
 * until a full request (headers plus any Content-Length body) is available, at
 * which point the `request` listener is invoked with a `req`/`res` pair. The
 * client side wraps a `net` socket, writes a simple HTTP/1.1 request and
 * parses the buffered response once the connection closes.
 */

#include "../node_common.h"

#include <string.h>

/* Provided by net.c (linked whenever the node extension is present). */
extern xt_value xt_net_static(xt_value name, int32_t argc, xt_value *argv);

/* Byte helpers exported by buffer.c. */
int xt_node_is_buffer(xt_value value);
unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength);

static xt_value xt_http_client_finish(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);

/*
 * The implementation is grouped by functional area under `http/parts/` and
 * `#include`d here as a single translation unit so the section helpers stay
 * `static` while each file stays focused on one area.
 */

#include "parts/helpers.inc"
#include "parts/string-builder.inc"
#include "parts/response.inc"
#include "parts/server.inc"
#include "parts/client.inc"
#include "parts/prototypes.inc"
