/*
 * xbintsc runtime — values.
 *
 * Constructors, the string representation, JS conversions, arithmetic and
 * comparison operators. All of these operate on the NaN-boxed `xt_value`.
 *
 * The implementation is grouped by functional area under `xt_values/` and
 * `#include`d here as a single translation unit, so the internal helpers stay
 * `static` while each file stays focused on one area.
 */

#include "rt_internal.h"

#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xt_values/constructors.inc"
#include "xt_values/strings.inc"
#include "xt_values/number-format.inc"
#include "xt_values/conversions.inc"
#include "xt_values/arithmetic.inc"
#include "xt_values/comparison.inc"
