/*
 * xbintsc runtime — standard library.
 *
 * Array and String prototype methods, method dispatch (`xt_call_method`) and
 * the `Math` namespace.
 */

#include "rt_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The implementation is grouped by functional area under `xt_stdlib/` and
 * `#include`d here as a single translation unit so the helpers stay `static`
 * while each file stays focused on one area.
 */

#include "xt_stdlib/helpers.inc"
#include "xt_stdlib/arrays.inc"
#include "xt_stdlib/strings.inc"
#include "xt_stdlib/dispatch.inc"
#include "xt_stdlib/math.inc"
