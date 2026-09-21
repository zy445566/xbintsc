/*
 * Node.js `Buffer` for xbintsc.
 *
 * xbintsc has no dedicated binary value type, so a Buffer is represented as a
 * plain object: indexed properties hold the individual bytes and a `length`
 * property records the size. Instance methods come from a shared prototype of
 * native closures, added by `xt_buffer_static` (`Buffer.from`, `Buffer.alloc`,
 * ...) and the `Buffer` constructor.
 */

#include "../node_common.h"

#include <math.h>

static xt_value xt_buffer_proto(void);

/*
 * The implementation is grouped by functional area under `buffer/parts/` and
 * `#include`d here as a single translation unit so the helpers stay `static`
 * while each file stays focused on one area.
 */

#include "parts/byte-access.inc"
#include "parts/construction.inc"
#include "parts/static-api.inc"
#include "parts/methods.inc"
#include "parts/search.inc"
#include "parts/integer-io.inc"
#include "parts/prototype.inc"
