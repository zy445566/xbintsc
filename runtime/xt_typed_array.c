/*
 * xbintsc runtime - typed arrays.
 *
 * A TypedArray is a view over an `ArrayBuffer`: its elements live in the
 * buffer's bytes at `byteOffset`, and `length` / `byteLength` describe the
 * window. `subarray` shares that buffer; `slice` copies into a new one.
 * Every write is coerced to the element type, so `new Uint8Array(2)[0] = 300`
 * stores 44 exactly like ECMAScript and a `Float32Array` round-trips through
 * single precision.
 */

#include "rt_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  XT_TA_U8,
  XT_TA_I8,
  XT_TA_U8C,
  XT_TA_U16,
  XT_TA_I16,
  XT_TA_U32,
  XT_TA_I32,
  XT_TA_F32,
  XT_TA_F64,
  XT_TA_KIND_COUNT
};

static const int xt_ta_bytes[XT_TA_KIND_COUNT] = {1, 1, 1, 2, 2, 4, 4, 4, 8};
static const char *const xt_ta_names[XT_TA_KIND_COUNT] = {
    "Uint8Array", "Int8Array", "Uint8ClampedArray", "Uint16Array", "Int16Array",
    "Uint32Array", "Int32Array", "Float32Array", "Float64Array",
};

static xt_value xt_ta_protos[XT_TA_KIND_COUNT];

/* -- forward declarations ------------------------------------------------- */

static xt_value xt_ta_method_fill(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_set(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_slice(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_subarray(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_join(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_to_string(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_index_of(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_last_index_of(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_includes(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_for_each(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_map(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_filter(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_every(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_some(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_find(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_find_index(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_reduce(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_reverse(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_sort(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_copy_within(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_at(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_keys(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_values(xt_value, xt_value, int32_t, xt_value *);
static xt_value xt_ta_method_entries(xt_value, xt_value, int32_t, xt_value *);

/* The implementation is grouped by functional area under `xt_typed_array/`
 * and `#include`d here as a single translation unit, so the internal helpers
 * stay `static` while each file stays focused on one area. */

#include "xt_typed_array/elements.inc"
#include "xt_typed_array/construction.inc"
#include "xt_typed_array/methods.inc"
