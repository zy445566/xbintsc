/*
 * xbintsc runtime — extended standard library.
 *
 * Additional Array/String/Number/Object methods and statics, JSON, and basic
 * Map / Set / Date / RegExp implementations.
 *
 * The implementation is grouped by functional area under `xt_stdlib2/` and
 * `#include`d here as a single translation unit, so the section helpers stay
 * `static` while each file stays focused on one area.
 */

#include "rt_internal.h"

#include <ctype.h>
#include <math.h>
#if defined(_WIN32)
#include "xt_regex.h"
#else
#include <regex.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xt_stdlib2/helpers.inc"
#include "xt_stdlib2/arrays.inc"
#include "xt_stdlib2/strings.inc"
#include "xt_stdlib2/numbers.inc"
#include "xt_stdlib2/objects.inc"
#include "xt_stdlib2/array-statics.inc"
#include "xt_stdlib2/number-string-statics.inc"
#include "xt_stdlib2/json.inc"
#include "xt_stdlib2/map.inc"
#include "xt_stdlib2/set.inc"
#include "xt_stdlib2/date.inc"
#include "xt_stdlib2/regexp.inc"
#include "xt_stdlib2/error.inc"
#include "xt_stdlib2/regexp-match.inc"
#include "xt_stdlib2/container.inc"
