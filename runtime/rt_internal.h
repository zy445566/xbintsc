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
 *  * Strings are stored as UTF-8 (with lone surrogate halves kept as WTF-8) but
 *    expose JavaScript's UTF-16 view: `length`, indexing, slicing, searching
 *    and splitting all count UTF-16 code units. `xt_string_length` is the raw
 *    byte length (what extensions and I/O need), while
 *    `xt_string_utf16_length` is the JavaScript `.length`. Concatenation is
 *    lazy: a `+` between two non-empty strings builds an immutable cons node
 *    (`flat == NULL`) and the bytes are materialised once, on first
 *    observation, by `xt_string_bytes`. This keeps `s += x` in a loop linear
 *    instead of copying the accumulated prefix every iteration.
 *  * Objects use a small linear property list. Fine for the language subset
 *    xbintsc targets today; swap for a hash map when the profile demands it.
 */

#ifndef XT_RT_INTERNAL_H
#define XT_RT_INTERNAL_H

#include "rt.h"

#include <setjmp.h>
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
#define XT_OBJECT_KIND_ERROR 12
#define XT_OBJECT_KIND_GENERATOR 13
#define XT_OBJECT_KIND_ITERATOR 14
#define XT_OBJECT_KIND_ASYNC 15
#define XT_OBJECT_KIND_ASYNC_ITERATOR 16

/* Common header for every heap object. `flags` bit 0 is the collector's mark
 * bit; it is cleared on every surviving object at the end of a sweep. */
typedef struct xt_header {
  uint8_t kind;
  uint8_t flags;
  uint16_t reserved;
  uint32_t size;
  struct xt_header *gc_next;
} xt_header;

#define XT_GC_MARK 0x01u

/* A string is either *flat* (`flat` points at `capacity` bytes, the last one
 * NUL) or a *cons* node (`flat == NULL`, `left`/`right` name the operands).
 * `length` is the raw UTF-8 byte length; `utf16_length` (cached, 0 means
 * "not computed yet" and is also the length of the empty string) is the same
 * text measured in UTF-16 code units, which is what JavaScript observes.
 * `code_point_count` is the number of Unicode scalar values (used by
 * `for...of` and `Array.from`). `xt_string_bytes` turns a cons node into a flat
 * one in place; treat the result as immutable and never write through a pointer
 * obtained from it. */
typedef struct xt_string {
  xt_header header;
  uint32_t length;
  uint32_t utf16_length;
  uint32_t code_point_count;
  uint32_t capacity;
  char *flat;
  struct xt_string *left;
  struct xt_string *right;
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
  /* String or Symbol value (see `xt_to_property_key`). */
  xt_value key;
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
  /* Captured `Error.stack` for XT_OBJECT_KIND_ERROR, XT_UNDEFINED otherwise.
   * Deliberately not a property: `Object.keys`, `for...in` and JSON.stringify
   * must not see it, while `error.stack` returns it through `xt_get`. */
  xt_value error_stack;
} xt_object;

typedef struct {
  xt_header header;
  uint32_t length;
  uint32_t capacity;
  xt_value *items;
  /* Named (non-index) properties such as `raw` on template strings or
   * `index`/`input` on RegExp match results, stored in an `xt_object`. */
  xt_value extra;
  /* The array's [[Prototype]] (`Array.prototype` for ordinary arrays). Arrays
   * are a distinct allocation kind, so this field is what lets
   * `Object.getPrototypeOf([])` and `x instanceof <Array alias>` work. */
  xt_value prototype;
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
  /* Set on generator function objects: calling the closure does not run the
   * body, it creates a suspended generator (see xt_generator.c). */
  uint8_t is_generator;
  /* Set on `async` function objects: calling the closure starts a coroutine
   * (see xt_async.c) and immediately returns the promise it resolves. */
  uint8_t is_async;
  /* Set on `async function*` objects. `is_generator` is also set, but the call
   * path checks this flag first so the generator is created in async mode. */
  uint8_t is_async_generator;
  /* Non-NULL for the first-class value of a built-in constructor ("Array",
   * "Map", ...). It lets property access resolve the constructor's statics
   * (`const A = Array; A.isArray(x)`). Points at a static string. */
  const char *builtin_name;
} xt_function;

/* Allocation and garbage collection (xt_alloc.c).
 *
 * The collector is a non-moving mark-sweep over the linked list of every
 * allocation. Roots are (a) explicitly registered value slots, (b) root
 * providers registered by runtime subsystems and extensions, and (c) a
 * conservative scan of the active C stack (plus a setjmp buffer for the
 * callee-saved registers). Heap objects are traced precisely for their own
 * value fields, with a conservative scan of their payload for the raw pointer
 * fields that older code still stores directly. */
void *xt_alloc(size_t size, int kind);

/* Record the current stack as the base of the root set. Call once, as early as
 * possible, before any allocation (generated `main` does this). */
void xt_gc_init(void);
/* Record the high bound of the main stack. The generated `main` passes the
 * address of one of its own locals, which lives above every frame its callees
 * ever use, so the conservative scan reaches spilled roots. Without this the
 * bound would be taken from inside the runtime and would cut off the top of
 * the caller's frame. Safe to call before `xt_gc_init`. */
void xt_gc_set_stack_base(void *base);
/* Enable automatic collection once the heap crosses the threshold. Programs
 * that never arm the collector still get `xt_gc_collect()` on demand. */
void xt_gc_arm(void);
/* Run a full collection now. Re-entrant calls are ignored. */
void xt_gc_collect(void);
/* Override the automatic trigger threshold (bytes of live heap). */
void xt_gc_set_threshold(size_t bytes);

/* Explicit roots: the address of a slot that always holds a live value (a
 * module global, a runtime prototype cache, ...). */
void xt_gc_add_root(xt_value *slot);
void xt_gc_add_root_range(xt_value *base, size_t count);
/* Subsystems whose live set is not reachable through value slots (the event
 * loop's timers, the promise microtask queue, ...) register a provider that
 * marks its reachable objects. Providers run during every collection. */
void xt_gc_register_root_provider(void (*provider)(void));

/* Track the active C stack so the conservative scan has an upper bound. The
 * initial region is installed by `xt_gc_init`; each generator pushes its own
 * private stack while it runs. Before switching to a deeper region the caller
 * records where its own stack was suspended, so every active region is scanned
 * and suspended callers stay reachable. */
void xt_gc_push_stack(uintptr_t low, uintptr_t high);
/* Mark the currently-executing region as suspended at `sp` (its frames above
 * `sp` stay live while a deeper generator region runs). */
void xt_gc_suspend(uintptr_t sp);
void xt_gc_pop_stack(void);

/* Marking primitives used by root providers and per-kind trace hooks. */
void xt_gc_mark_value(xt_value value);
void xt_gc_mark_header(xt_header *header);
/* Conservatively mark every pointer-sized word in `[base, base + bytes)`. */
void xt_gc_scan_region(const void *base, size_t bytes);

/* Kind-specific hooks for allocations with separately allocated sub-objects.
 * Implemented by the translation unit that owns the representation. */
void xt_promise_gc_trace(xt_header *header);
void xt_promise_gc_release(xt_header *header);
void xt_generator_gc_trace(xt_header *header);
void xt_generator_gc_release(xt_header *header);
void xt_async_gc_trace(xt_header *header);
void xt_async_gc_release(xt_header *header);
void xt_async_iterator_gc_trace(xt_header *header);

/* Stackful coroutines (xt_coroutine.c). A coroutine owns a private C stack and
 * switches to it with `ucontext`/fibers; `enter` runs until the body calls
 * `leave`. Exception frames and GC stack regions are swapped alongside the C
 * stack. Generators and async functions own one each. */
typedef struct xt_coroutine xt_coroutine;
xt_coroutine *xt_coroutine_create(void (*entry)(void *), void *arg);
void xt_coroutine_enter(xt_coroutine *co);
void xt_coroutine_leave(xt_coroutine *co);
/* The coroutine whose body is currently executing, or NULL on the main stack. */
xt_coroutine *xt_coroutine_current(void);
int xt_coroutine_is_current(xt_coroutine *co);
void xt_coroutine_gc_trace(xt_coroutine *co);
void xt_coroutine_release(xt_coroutine *co);

/* Value / string helpers shared across translation units (xt_values.c). */
xt_string *xt_string_alloc(size_t length);
xt_string *xt_as_string(xt_value value);
int32_t xt_string_length(xt_string *s);
int xt_string_equals(xt_string *a, xt_string *b);
/* Contiguous UTF-8 bytes of a string, flattening a cons node in place on first
 * use. The result is NUL-terminated at `s->length`; never write through it. */
const char *xt_string_bytes(xt_string *s);

/* UTF-8 <-> UTF-16 helpers (xt_values/utf16.inc). Decoding accepts WTF-8, so
 * lone surrogate halves produced by indexing survive a round trip. */
uint32_t xt_utf8_decode(const char *data, size_t length, size_t *width);
size_t xt_utf8_encode(uint32_t code, char *out);
uint32_t xt_utf8_utf16_length(const char *data, size_t length);
void xt_utf8_measure(const char *data, size_t length, uint32_t *units, uint32_t *points);
/* Combine an adjacent WTF-8 high/low surrogate pair into one four-byte
 * sequence, in place. Returns the resulting byte length. */
size_t xt_utf8_normalize_surrogates(char *data, size_t length);

/* Publish a manually filled buffer (`xt_string_alloc` + `xt_string_mutable`):
 * normalises surrogate pairs, records the byte length and caches the UTF-16 and
 * code-point counts. */
void xt_string_finish(xt_string *s);
uint32_t xt_string_utf16_length(xt_string *s);
uint32_t xt_string_code_point_count(xt_string *s);
/* UTF-16 code unit at `index`, or -1 when out of range. */
int32_t xt_string_code_unit(xt_string *s, int32_t index);
/* Unicode code point beginning at UTF-16 index `index` (combining a surrogate
 * pair), or -1 when out of range. */
int32_t xt_string_code_point_at(xt_string *s, int32_t index);
/* A one-code-unit string (a lone surrogate when `index` points into a pair). */
xt_value xt_string_unit_value(xt_string *s, int32_t index);
xt_value xt_string_from_code_point(uint32_t code);
xt_value xt_string_from_units(const uint16_t *units, size_t count);
/* Copy UTF-16 range [start, end), splitting a surrogate pair when needed. */
xt_value xt_string_slice_units(xt_string *s, int32_t start, int32_t end);
/* Byte offset / UTF-16 index conversions; both clamp to the string bounds. */
int32_t xt_string_byte_offset(xt_string *s, int32_t unit_index);
int32_t xt_string_unit_offset(xt_string *s, int32_t byte_offset);
/* True when `unit_index` sits on a code-point boundary (not between the two
 * halves of a surrogate pair). */
int xt_string_is_boundary(xt_string *s, int32_t unit_index);
/* The `index`-th Unicode code point (for `for...of` and `Array.from`). */
xt_value xt_string_code_point_value(xt_string *s, int32_t index);
/* Mutable bytes of a string just created with `xt_string_alloc` (never a cons
 * node). Used by the constructors that fill a buffer before publishing it. */
char *xt_string_mutable(xt_string *s);
int32_t xt_to_int32(xt_value v);

/* ECMAScript number formatting (xt_values.c). */
void xt_number_to_js_string(double d, char *buffer, size_t size);
void xt_number_to_exponential(double d, int precision, char *buffer, size_t size);
void xt_number_to_precision(double d, int precision, char *buffer, size_t size);
void xt_number_to_fixed(double d, int precision, char *buffer, size_t size);

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
xt_property *xt_object_find_property(xt_object *obj, xt_value key);

/* Property keys are either strings or symbols. */
xt_value xt_to_property_key(xt_value key);
int xt_property_key_equals(xt_value a, xt_value b);

/* Symbols (xt_symbol.c). */
int xt_is_symbol(xt_value value);
xt_value xt_symbol_new(xt_value description);
xt_value xt_symbol_description(xt_value value);
xt_value xt_symbol_to_string(xt_value value);
xt_value xt_symbol_well_known(const char *name);
xt_value xt_object_own_property_symbols(xt_value value);

/* Map / Set / Promise / Date / RegExp live in xt_stdlib.c and xt_promise.c. */
int xt_is_promise(xt_value value);
int xt_is_map(xt_value value);
int xt_is_set(xt_value value);
int xt_is_date(xt_value value);
int xt_is_regexp(xt_value value);
int xt_regexp_find(xt_value regexp, xt_value input, int32_t start, int32_t *matchStart, int32_t *matchEnd);
xt_value xt_regexp_exec(xt_value regexp, xt_value input);
xt_value xt_regexp_match(xt_value input, xt_value regexp);
xt_value xt_regexp_replace(xt_value value, xt_value regexp, xt_value replacement);
xt_value xt_regexp_split(xt_value value, xt_value regexp, xt_value limit);
xt_value xt_regexp_get_property(xt_value regexp, xt_value key);
int32_t xt_map_size(xt_value value);
int32_t xt_set_size(xt_value value);
/* `for...of` iteration: dispatch over arrays, strings, Maps and Sets. */
xt_value xt_iter_length(xt_value value);
xt_value xt_iter_value(xt_value value, xt_value index);
/* Per-step iteration check. Generators pull the next value here; the index is
 * ignored because the value is cached until `xt_iter_value` reads it. */
xt_value xt_iter_has(xt_value value, xt_value index);
/* Resolve the iteration protocol once: returns `value` for the built-in index
 * protocols, or a wrapper around `value[Symbol.iterator]()` otherwise. */
xt_value xt_iter_open(xt_value value);

/* Monotonic clock in fractional milliseconds (xt_loop.c), for `performance.now`. */
double xt_monotonic_ms(void);

/* Stackful generators (xt_generator.c). */
int xt_is_generator(xt_value value);
xt_value xt_generator_new(xt_value function, xt_value thisValue, int32_t argc, xt_value *argv);
xt_value xt_generator_next(xt_value generator, xt_value sent);
xt_value xt_generator_return(xt_value generator, xt_value value);
xt_value xt_generator_throw(xt_value generator, xt_value error);
xt_value xt_yield(xt_value value);
xt_value xt_yield_star(xt_value delegate);
int xt_generator_has_next(xt_value generator);
xt_value xt_generator_iter_value(xt_value generator);
/* Async-generator support. `xt_generator_await` handles an `await` inside an
 * async generator and returns 1; it returns 0 when no async generator is
 * running (ordinary/async code handles the await itself). */
int xt_generator_await(xt_value value, xt_value *result);
void xt_generator_wake(void *generator, xt_value value, int rejected);
int xt_is_async_generator(xt_value value);
/* `[Symbol.asyncIterator]()` for a generator object. */
xt_value xt_async_iterator_method(void);
/* `for await...of`: resolve the async iteration protocol once, then pull one
 * step per call. Both return a promise for the step object. */
xt_value xt_async_iter_open(xt_value value);
xt_value xt_async_iter_next(xt_value iterator);

/* Async coroutines (xt_async.c). Calling an `async` function runs its body on
 * a coroutine that suspends at a pending `await` and resumes when the awaited
 * promise settles, so the event loop can make progress in between. */
#define XT_PROMISE_PENDING 0
#define XT_PROMISE_FULFILLED 1
#define XT_PROMISE_REJECTED 2
xt_value xt_async_call(xt_value function, xt_value thisValue, int32_t argc, xt_value *argv);
/* Resume `async` with the settlement of the promise it awaited. Called by the
 * microtask runner when the promise settles. */
void xt_async_wake(void *async, xt_value value, int rejected);
/* Create a pending promise / settle an existing one (used by xt_async.c). */
xt_value xt_promise_create(void);
void xt_promise_fulfill(xt_value promise, xt_value value);
void xt_promise_reject_value(xt_value promise, xt_value value);
/* `-1` when `value` is not a promise, otherwise PENDING / FULFILLED / REJECTED. */
int xt_promise_state_of(xt_value value);
xt_value xt_promise_value_of(xt_value value);
/* Register a coroutine to resume when `promise` settles (it must be pending). */
void xt_promise_add_async_waiter(xt_value promise, void *async);
/* Queue a microtask that resumes `async` with an already settled value. */
void xt_promise_queue_async_wake(void *async, xt_value settled, int rejected);

/* Exception-frame plumbing shared with the generator runtime. The struct is
 * public so a coroutine can `_setjmp` its own boundary directly (a `setjmp`
 * must run in the frame it returns to, never in a helper). */
typedef struct xt_try_frame {
  jmp_buf buf;
  struct xt_try_frame *prev;
  xt_value exception;
  /* Shadow-stack depth when the protected region was entered; restored when
   * the region is left (normally or through a throw) so frames from unwound
   * calls do not leak into later stack traces. */
  int32_t stack_depth;
} xt_try_frame;
/* Save a try frame with clang's `_setjmp` built-in.
 *
 * The generated IR passes the frame explicitly because it is already IR and
 * clang will not rewrite it. This C helper instead calls `_setjmp(buf)` and lets
 * clang supply the frame: on Windows x64 it injects the caller's frame address
 * (`@llvm.frameaddress(0)`) into `_JUMP_BUFFER.Frame` so `longjmp` can feed it
 * to `RtlUnwind`; on Windows ARM64, where the UCRT has no `_setjmp`, it lowers
 * the call to `_setjmpex` with the entry stack pointer (`@llvm.sponentry`). On
 * Linux/macOS `_setjmp` takes only the buffer and pairs with `longjmp`.
 * `_setjmp` is used rather than the exported `setjmp` symbol, whose Windows ABI
 * is an incompatible two-argument routine. */
#define xt_try_setjmp(framePtr) _setjmp(((xt_try_frame *)(framePtr))->buf)
void *xt_try_mark(void);
void xt_try_restore(void *mark);

/* -- shadow call stack and diagnostics (xt_io/exception.inc) --------------- */
/* Generated code pushes a frame on entry and pops it before returning, so an
 * Error can capture a JavaScript-level stack without any debug metadata. Frame
 * strings live in the module's static data and are never freed. */
void xt_stack_push(const char *name, const char *file, int32_t line);
void xt_stack_pop(void);
int32_t xt_stack_depth(void);
void xt_stack_set_line(int32_t line);
/* Restore the depth saved by a try frame after unwinding inner calls. */
void xt_stack_trim(int32_t depth);
/* Format `Name: message` plus the current frames as an `Error.stack` string. */
xt_value xt_stack_capture(const char *name, const char *message);
/* The captured stack of an error object, or XT_UNDEFINED. */
xt_value xt_error_stack(xt_value error);
/* Best-effort stack dump on a fatal native signal / unhandled exception. */
void xt_install_crash_handler(void);

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

/* Error family helpers (xt_stdlib2.c). */
xt_value xt_error_to_string(xt_value error);
/* Turn an internal "TypeError: message" / "RangeError: message" string into a
 * real Error-family value so `catch` observes an Error, not a string. */
xt_value xt_error_from_message(const char *message);
xt_value xt_aggregate_error_ctor(int32_t argc, xt_value *argv);
xt_value xt_ext_container_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);
xt_value xt_promise_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled);

/* JSON helpers used by Promise / Map immutability checks. */
int xt_value_equals(xt_value a, xt_value b);

/* Standard-library helpers (xt_stdlib.c). */
xt_array *xt_as_array(xt_value value);
xt_value xt_arg_at(int32_t argc, xt_value *argv, int32_t index);

/* Typed arrays (xt_typed_array.c). */
int xt_typed_array_kind_of(xt_value value);
double xt_typed_array_coerce(int kind, double value);
/* Buffer-backed element access; the array must be a typed-array value. */
xt_value xt_typed_array_read(xt_value array, uint32_t index);
void xt_typed_array_write(xt_value array, uint32_t index, xt_value value);
xt_value xt_typed_array_buffer_of(xt_value array);
uint32_t xt_typed_array_byte_offset_of(xt_value array);
const char *xt_typed_array_name_of(int kind);

/* ArrayBuffer helpers (xt_array_buffer.c). */
int xt_is_array_buffer(xt_value value);
uint32_t xt_array_buffer_byte_length(xt_value value);
uint8_t xt_array_buffer_get_byte(xt_value buffer, uint32_t index);
void xt_array_buffer_set_byte(xt_value buffer, uint32_t index, uint8_t value);
xt_value xt_array_buffer_ctor(int32_t argc, xt_value *argv);

#endif /* XT_RT_INTERNAL_H */
