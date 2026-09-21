/*
 * xbintsc runtime — arbitrary-precision integers (BigInt).
 *
 * BigInt values are sign-magnitude integers stored as little-endian base 2^32
 * limb arrays behind the `XT_TAG_BIGINT` NaN-box tag. The implementation is
 * deliberately small and dependency free: it favours clarity over throughput
 * and covers everything the language subset needs (literals, arithmetic,
 * bitwise operators, shifts, comparisons, string conversion and the
 * `BigInt.asIntN` / `BigInt.asUintN` statics).
 *
 * Division uses binary long division (shift/subtract). That is O(bits*limbs)
 * rather than the O(n*m) of Knuth's algorithm D, but keeps the code compact and
 * is more than fast enough for the fixed-width (64-bit) values the compiler
 * itself relies on.
 *
 * The implementation is grouped by functional area under `xt_bigint/` and
 * `#include`d here as a single translation unit, so the limb helpers stay
 * `static` while each file stays focused on one area.
 */

#include "rt_internal.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BI_LIMB_BITS 32
#define BI_MAX_SHIFT 0x01000000u /* 16 Mi bits: guard against runaway shifts */

#include "xt_bigint/helpers.inc"
#include "xt_bigint/small-arithmetic.inc"
#include "xt_bigint/magnitude.inc"
#include "xt_bigint/bits.inc"
#include "xt_bigint/division.inc"
#include "xt_bigint/format.inc"
#include "xt_bigint/conversions.inc"
#include "xt_bigint/compare.inc"
#include "xt_bigint/arithmetic.inc"
#include "xt_bigint/bitwise.inc"
#include "xt_bigint/public.inc"
