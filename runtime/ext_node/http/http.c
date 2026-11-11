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

/* `http.STATUS_CODES`: numeric code (as a string key) -> reason phrase. */
xt_value xt_http_status_codes_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  static const struct {
    const char *code;
    const char *message;
  } codes[] = {
    {"100", "Continue"}, {"101", "Switching Protocols"},
    {"200", "OK"}, {"201", "Created"}, {"202", "Accepted"}, {"204", "No Content"},
    {"206", "Partial Content"},
    {"300", "Multiple Choices"}, {"301", "Moved Permanently"}, {"302", "Found"},
    {"304", "Not Modified"}, {"307", "Temporary Redirect"}, {"308", "Permanent Redirect"},
    {"400", "Bad Request"}, {"401", "Unauthorized"}, {"403", "Forbidden"},
    {"404", "Not Found"}, {"405", "Method Not Allowed"}, {"406", "Not Acceptable"},
    {"408", "Request Timeout"}, {"409", "Conflict"}, {"410", "Gone"},
    {"413", "Payload Too Large"}, {"414", "URI Too Long"}, {"415", "Unsupported Media Type"},
    {"416", "Range Not Satisfiable"}, {"418", "I'm a Teapot"}, {"422", "Unprocessable Entity"},
    {"425", "Too Early"}, {"426", "Upgrade Required"}, {"428", "Precondition Required"},
    {"429", "Too Many Requests"}, {"431", "Request Header Fields Too Large"},
    {"500", "Internal Server Error"}, {"501", "Not Implemented"}, {"502", "Bad Gateway"},
    {"503", "Service Unavailable"}, {"504", "Gateway Timeout"}, {"505", "HTTP Version Not Supported"},
  };
  xt_value object = xt_object_new();
  for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
    xt_node_set(object, codes[i].code, xt_string_from_cstr(codes[i].message));
  }
  return object;
}
