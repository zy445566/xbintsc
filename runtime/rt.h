/*
 * xbtsc runtime ABI.
 *
 * The runtime is the boundary between generated LLVM IR and the operating
 * system. Every JavaScript value that flows through generated code is an
 * `xt_value` (a 64-bit NaN-boxed word). The compiler emits calls into the
 * functions declared here; linking `libxtrt.a` (or the object file) provides
 * their implementations.
 *
 * Keeping the ABI in a single header lets the compiler, the runtime and any
 * extension module (node/bun shims, ...) agree on representation without
 * sharing implementation details.
 */
#ifndef XBTSC_RUNTIME_H
#define XBTSC_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t xt_value;

/* Tag layout (top 16 bits). Values whose tag bits are not 0xFFF8.. are
 * doubles, so ordinary numbers travel unboxed. */
#define XT_TAG_MASK 0xFFFF000000000000ULL
#define XT_NUMBER_MASK 0xFFF8000000000000ULL

#define XT_TAG_UNDEFINED 0xFFF8000000000000ULL
#define XT_TAG_NULL 0xFFF9000000000000ULL
#define XT_TAG_FALSE 0xFFFA000000000000ULL
#define XT_TAG_TRUE 0xFFFB000000000000ULL
#define XT_TAG_STRING 0xFFFC000000000000ULL
#define XT_TAG_OBJECT 0xFFFD000000000000ULL
#define XT_TAG_ARRAY 0xFFFE000000000000ULL
#define XT_TAG_FUNCTION 0xFFFF000000000000ULL

#define XT_PAYLOAD_MASK 0x0000FFFFFFFFFFFFULL

#define XT_IS_NUMBER(v) (((v) & XT_NUMBER_MASK) != XT_NUMBER_MASK)
#define XT_IS_TAGGED(v, tag) (((v) & XT_TAG_MASK) == (tag))
#define XT_IS_UNDEFINED(v) ((v) == XT_TAG_UNDEFINED)
#define XT_IS_NULL(v) ((v) == XT_TAG_NULL)
#define XT_IS_BOOL(v) (((v) & ~0x1ULL) == XT_TAG_FALSE)
#define XT_IS_STRING(v) XT_IS_TAGGED(v, XT_TAG_STRING)
#define XT_IS_OBJECT(v) XT_IS_TAGGED(v, XT_TAG_OBJECT)
#define XT_IS_ARRAY(v) XT_IS_TAGGED(v, XT_TAG_ARRAY)
#define XT_IS_FUNCTION(v) XT_IS_TAGGED(v, XT_TAG_FUNCTION)
#define XT_IS_HEAP(v) (((v) & XT_NUMBER_MASK) == XT_NUMBER_MASK)

#define XT_GET_PTR(v) ((void *)(uintptr_t)((v) & XT_PAYLOAD_MASK))
#define XT_FROM_PTR(tag, p) ((xt_value)((tag) | ((uintptr_t)(p) & XT_PAYLOAD_MASK)))

#define XT_UNDEFINED XT_TAG_UNDEFINED
#define XT_NULL XT_TAG_NULL
#define XT_FALSE XT_TAG_FALSE
#define XT_TRUE XT_TAG_TRUE

/* Double <-> value helpers. */
static inline xt_value xt_from_double(double d) {
  xt_value v;
  __builtin_memcpy(&v, &d, sizeof(v));
  /* Canonicalise NaNs so they never collide with the tag space. */
  if (!XT_IS_NUMBER(v)) {
    double nan = 0.0 / 0.0;
    __builtin_memcpy(&v, &nan, sizeof(v));
  }
  return v;
}

static inline double xt_to_double(xt_value v) {
  double d;
  __builtin_memcpy(&d, &v, sizeof(d));
  return d;
}

/* -- constructors -------------------------------------------------------- */
xt_value xt_undefined(void);
xt_value xt_null(void);
xt_value xt_bool(int b);
xt_value xt_number(double d);
xt_value xt_string_new(const char *data, size_t len);
xt_value xt_string_from_cstr(const char *data);
/** Raw UTF-8 bytes of a string value (NULL for non-strings); not NUL-safe. */
const char *xt_string_data(xt_value value);
int32_t xt_string_length_value(xt_value value);

/* -- function calling convention ----------------------------------------- */
/* Every compiled function has the signature:
 *     xt_value fn(xt_value env, int32_t argc, xt_value *argv);
 * `env` carries captured values for closures (XT_UNDEFINED when there are
 * none) and `argv` carries the actual arguments. */
typedef xt_value (*xt_code_fn)(xt_value env, int32_t argc, xt_value *argv);

xt_value xt_arg(int32_t argc, xt_value *argv, int32_t index);
xt_value xt_closure_new(void *fn, int32_t env_count, xt_value *env);
xt_value xt_closure_call(xt_value fn, int32_t argc, xt_value *argv);
xt_value xt_closure_env(xt_value fn, int32_t index);
int32_t xt_closure_arity(xt_value fn);

/* -- conversions ---------------------------------------------------------- */
int xt_truthy(xt_value v);
double xt_to_number(xt_value v);
xt_value xt_to_string(xt_value v);
xt_value xt_typeof(xt_value v);

/* -- arithmetic ----------------------------------------------------------- */
xt_value xt_add(xt_value a, xt_value b);
xt_value xt_sub(xt_value a, xt_value b);
xt_value xt_mul(xt_value a, xt_value b);
xt_value xt_div(xt_value a, xt_value b);
xt_value xt_mod(xt_value a, xt_value b);
xt_value xt_pow(xt_value a, xt_value b);
xt_value xt_neg(xt_value a);
xt_value xt_pos(xt_value a);
xt_value xt_bit_and(xt_value a, xt_value b);
xt_value xt_bit_or(xt_value a, xt_value b);
xt_value xt_bit_xor(xt_value a, xt_value b);
xt_value xt_bit_not(xt_value a);
xt_value xt_shl(xt_value a, xt_value b);
xt_value xt_shr(xt_value a, xt_value b);
xt_value xt_ushr(xt_value a, xt_value b);

/* -- comparison (returns a boolean value) --------------------------------- */
xt_value xt_lt(xt_value a, xt_value b);
xt_value xt_le(xt_value a, xt_value b);
xt_value xt_gt(xt_value a, xt_value b);
xt_value xt_ge(xt_value a, xt_value b);
xt_value xt_eq(xt_value a, xt_value b);
xt_value xt_ne(xt_value a, xt_value b);
xt_value xt_seq(xt_value a, xt_value b);
xt_value xt_sne(xt_value a, xt_value b);
xt_value xt_not(xt_value a);

/* -- objects -------------------------------------------------------------- */
xt_value xt_object_new(void);
xt_value xt_object_get(xt_value obj, xt_value key);
xt_value xt_object_get_cstr(xt_value obj, const char *key);
xt_value xt_object_set(xt_value obj, xt_value key, xt_value value);
xt_value xt_object_has(xt_value obj, xt_value key);
xt_value xt_object_keys(xt_value obj);

/* -- arrays --------------------------------------------------------------- */
xt_value xt_array_new(int32_t count, xt_value *items);
xt_value xt_array_get(xt_value arr, xt_value index);
xt_value xt_array_set(xt_value arr, xt_value index, xt_value value);
xt_value xt_array_push(xt_value arr, xt_value value);
xt_value xt_array_length(xt_value arr);
xt_value xt_array_spread(xt_value target, xt_value source);

/* -- generic member access ------------------------------------------------ */
xt_value xt_get(xt_value target, xt_value key);
xt_value xt_set(xt_value target, xt_value key, xt_value value);

/* -- boxes (used for captured variables) ---------------------------------- */
xt_value xt_box_new(xt_value value);
xt_value xt_box_get(xt_value box);
xt_value xt_box_set(xt_value box, xt_value value);
xt_value xt_is_nullish(xt_value value);

/* -- exceptions / output -------------------------------------------------- */
void xt_throw(xt_value v);
void xt_console_log(int32_t argc, xt_value *argv);
void xt_print(xt_value v);
void xt_println(xt_value v);

/* -- diagnostics ---------------------------------------------------------- */
size_t xt_heap_bytes(void);
size_t xt_heap_allocations(void);

#ifdef __cplusplus
}
#endif

#endif /* XBTSC_RUNTIME_H */
