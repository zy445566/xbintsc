/*
 * xbintsc runtime — containers.
 *
 * Objects (linear property lists) and arrays (dynamic vectors), plus the
 * generic `xt_get`/`xt_set` member access used by the codegen and the boxing
 * helpers used for captured `let` bindings.
 *
 * The implementation is grouped by functional area under `xt_containers/` and
 * `#include`d here as a single translation unit, so the internal helpers stay
 * `static` while each file stays focused on one area.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xt_containers/objects.inc"
#include "xt_containers/arrays.inc"
#include "xt_containers/access.inc"
#include "xt_containers/boxes.inc"
