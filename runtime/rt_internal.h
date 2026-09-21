/*
 * xbintsc runtime — private internal header.
 *
 * Shared by the runtime translation units:
 *   xt_alloc.c       heap arena + allocation kinds
 *   xt_values.c      constructors, strings, conversions, arithmetic, comparison
 *   xt_containers.c  objects, arrays, generic member access, boxes
 *   xt_stdlib.c      Array/String methods and Math
 *   xt_builtins.c    Object methods, globals and closures
 *   xt_io.c          exceptions (try/catch) and console output
 *
 * This header declares the internal object layouts and the small set of
 * helpers that cross translation-unit boundaries. Nothing here is part of the
 * public ABI in `rt.h`; generated code only sees the `xt_*` prototypes there.
 *
 * Design notes
 * ------------
 *  * Values are 64-bit NaN-boxed words (`xt_value`). Numbers travel unboxed;
 *    everything else is a tagged pointer into an allocation arena.
 *  * The allocator is a bump arena. It never frees, which keeps the generated
 *    code free of write barriers and makes the first implementation small and
 *    predictable. Replacing it with a precise/conservative collector is an
 *    isolated change behind `xt_alloc` plus root registration.
 *  * Strings are UTF-8. JavaScript measures string length in UTF-16 code
 *    units; `xt_string_length` currently reports code points (see README).
 *  * Objects use a small linear property list. Fine for the language subset
 *    xbintsc targets today; swap for a hash map when the profile demands it.
 */

#ifndef XT_RT_INTERNAL_H
#define XT_RT_INTERNAL_H

#include "rt.h"

#include <stddef.h>
#include <stdint.h>

/* Allocation kinds recorded in `xt_header.kind`. */
#define XT_OBJECT_KIND_STRING 1
#define XT_OBJECT_KIND_OBJECT 2
#define XT_OBJECT_KIND_ARRAY 3
#define XT_OBJECT_KIND_FUNCTION 4
#define XT_OBJECT_KIND_PROMISE 5
#define XT_OBJECT_KIND_MAP 6
#define XT_OBJECT_KIND_SET 7
#define XT_OBJECT_KIND_DATE 8
#define XT_OBJECT_KIND_REGEXP 9
#define XT_OBJECT_KIND_SYMBOL 10
#define XT_OBJECT_KIND_BIGINT 11

/* Common header for every heap object. */
typedef struct xt_header {
  uint8_t kind;
  uint8_t flags;
  uint16_t reserved;
  uint32_t size;
  struct xt_header *gc_next;
} xt_header;

typedef struct {
  xt_header header;
  uint32_t length;
  uint32_t capacity;
  char *data;
} xt_string;

/* Sign-magnitude arbitrary-precision integer; see xt_bigint.c. */
typedef struct {
  xt_header header;
  int32_t sign; /* -1, 0 or 1 */
  uint32_t length;
  uint32_t capacity;
  uint32_t *limbs;
} xt_bigint;

typedef struct {
  xt_string *key;
  xt_value value;
  /* Accessor properties (`get x()` / `set x(v)`) store the functions here;
   * when set, `value` is ignored and property access invokes the getter. */
  xt_value getter;
  xt_value setter;
} xt_property;

typedef struct {
  xt_header header;
  uint32_t count;
  uint32_t capacity;
  xt_property *properties;
  xt_value prototype;
  uint8_t frozen;
  uint8_t is_array_base;
  uint16_t reserved2;
} xt_object;

typedef struct {
  xt_header header;
  uint32_t length;
  uint32_t capacity;
  xt_value *items;
} xt_array;

typedef struct {
  xt_header header;
  xt_code_fn code;
  xt_value *environment;
  int32_t environment_count;
  int32_t arity;
  xt_string *name;
  xt_object *properties;
  xt_value prototype;
} xt_function;

/* Allocation (xt_alloc.c). */
void *xt_alloc(size_t size, int kind);

/* Value / string helpers shared across translation units (xt_values.c). */
xt_string *xt_string_alloc(size_t length);
xt_string *xt_as_string(xt_value value);
int32_t xt_string_length(xt_string *s);
int xt_string_equals(xt_string *a, xt_string *b);
int32_t xt_to_int32(xt_value v);

/* BigInt runtime (xt_bigint.c). */
int xt_is_bigint(xt_value value);
double xt_bigint_to_double_value(xt_value value);
xt_value xt_bigint_to_string_radix(xt_value value, int radix);
xt_value xt_bigint_to_decimal(xt_value value);
xt_value xt_bigint_from_i64(int64_t value);
xt_value xt_bigint_from_double(double value);
xt_value xt_bigint_neg(xt_value value);
xt_value xt_bigint_add(xt_value a, xt_value b);
xt_value xt_bigint_sub(xt_value a, xt_value b);
xt_value xt_bigint_mul(xt_value a, xt_value b);
xt_value xt_bigint_div(xt_value a, xt_value b);
xt_value xt_bigint_mod(xt_value a, xt_value b);
xt_value xt_bigint_pow(xt_value a, xt_value b);
xt_value xt_bigint_bit_and(xt_value a, xt_value b);
xt_value xt_bigint_bit_or(xt_value a, xt_value b);
xt_value xt_bigint_bit_xor(xt_value a, xt_value b);
xt_value xt_bigint_bit_not(xt_value a);
xt_value xt_bigint_shl(xt_value a, xt_value b);
xt_value xt_bigint_shr(xt_value a, xt_value b);
int xt_bigint_compare(xt_value a, xt_value b);
int xt_bigint_compare_double(xt_value a, double b);
xt_value xt_bigint_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);

/* Container helper (xt_containers.c). */
void xt_array_reserve(xt_array *array, uint32_t needed);

/* Object helpers used by the standard library. */
xt_object *xt_as_object(xt_value value);
xt_property *xt_object_find_property(xt_object *obj, xt_string *key);

/* Map / Set / Promise / Date / RegExp live in xt_stdlib.c and xt_promise.c. */
int xt_is_promise(xt_value value);
int xt_is_map(xt_value value);
int xt_is_set(xt_value value);
int xt_is_date(xt_value value);
int xt_is_regexp(xt_value value);
int xt_regexp_find(xt_value regexp, xt_value input, int32_t start, int32_t *matchStart, int32_t *matchEnd);
xt_value xt_regexp_exec(xt_value regexp, xt_value input);
xt_value xt_regexp_replace(xt_value value, xt_value regexp, xt_value replacement);
xt_value xt_regexp_get_property(xt_value regexp, xt_value key);
int32_t xt_map_size(xt_value value);
int32_t xt_set_size(xt_value value);
/* `for...of` iteration: dispatch over arrays, strings, Maps and Sets. */
xt_value xt_iter_length(xt_value value);
xt_value xt_iter_value(xt_value value, xt_value index);

/* -- collection / date / regexp representations --------------------------- */
typedef struct {
  xt_header header;
  xt_value *keys;
  xt_value *values;
  uint32_t count;
  uint32_t capacity;
} xt_map;

typedef struct {
  xt_header header;
  xt_value *values;
  uint32_t count;
  uint32_t capacity;
} xt_set_object;

typedef struct {
  xt_header header;
  double time;
} xt_date;

typedef struct {
  xt_header header;
  xt_value source;
  xt_value flags;
} xt_regexp;

/* Extended method dispatch (xt_stdlib2.c). `*handled` is set when the method
 * was recognised, allowing an intentional `undefined` result to be returned. */
xt_value xt_ext_array_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);
xt_value xt_ext_string_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);
xt_value xt_ext_number_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);
xt_value xt_ext_object_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);
xt_value xt_ext_container_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);
xt_value xt_promise_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);

/* JSON helpers used by Promise / Map immutability checks. */
int xt_value_equals(xt_value a, xt_value b);

/* Standard-library helpers (xt_stdlib.c). */
xt_array *xt_as_array(xt_value value);
xt_value xt_arg_at(int32_t argc, xt_value *argv, int32_t index);

#endif /* XT_RT_INTERNAL_H */
