/*
 * xbintsc runtime - built-in constructors as first-class values.
 *
 * `Array`, `Object`, `Map`, the Error family, typed arrays and the other
 * globals were previously only recognised by name in the code generator. This
 * unit turns them into ordinary function values:
 *
 *     const A = Array;
 *     const a = new A(1, 2, 3);
 *     A.isArray(a);              // statics travel with the value
 *     a instanceof A;            // prototype chain, not the name
 *
 * Every constructor is cached (and registered as a GC root), has metadata
 * (`name` / `length`), a `prototype` object and a `builtin_name` that the
 * property path uses to resolve statics.
 */

#include "rt_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- constructor table ---------------------------------------------------- */

enum {
  XT_PROTO_OBJECT,
  XT_PROTO_ARRAY,
  XT_PROTO_FUNCTION,
  XT_PROTO_PLAIN,
  XT_PROTO_ERROR,
  XT_PROTO_TYPED
};

typedef struct {
  const char *name;
  int32_t arity;
  int32_t prototype;
} xt_ctor_spec;

#define XT_CTOR_SPECS(X)                                                       \
  X(Array, 1, XT_PROTO_ARRAY)                                              \
  X(Object, 1, XT_PROTO_OBJECT)                                            \
  X(Function, 1, XT_PROTO_FUNCTION)                                        \
  X(Map, 0, XT_PROTO_PLAIN)                                                \
  X(Set, 0, XT_PROTO_PLAIN)                                                \
  X(Date, 7, XT_PROTO_PLAIN)                                               \
  X(RegExp, 2, XT_PROTO_PLAIN)                                             \
  X(Promise, 1, XT_PROTO_PLAIN)                                            \
  X(Uint8Array, 3, XT_PROTO_TYPED)                                        \
  X(Int8Array, 3, XT_PROTO_TYPED)                                         \
  X(Uint8ClampedArray, 3, XT_PROTO_TYPED)                                 \
  X(Uint16Array, 3, XT_PROTO_TYPED)                                       \
  X(Int16Array, 3, XT_PROTO_TYPED)                                        \
  X(Uint32Array, 3, XT_PROTO_TYPED)                                       \
  X(Int32Array, 3, XT_PROTO_TYPED)                                        \
  X(Float32Array, 3, XT_PROTO_TYPED)                                      \
  X(Float64Array, 3, XT_PROTO_TYPED)                                      \
  X(Error, 1, XT_PROTO_ERROR)                                             \
  X(TypeError, 1, XT_PROTO_ERROR)                                         \
  X(RangeError, 1, XT_PROTO_ERROR)                                        \
  X(SyntaxError, 1, XT_PROTO_ERROR)                                       \
  X(ReferenceError, 1, XT_PROTO_ERROR)                                    \
  X(EvalError, 1, XT_PROTO_ERROR)                                         \
  X(URIError, 1, XT_PROTO_ERROR)                                          \
  X(AggregateError, 2, XT_PROTO_ERROR)                                    \
  X(Number, 1, XT_PROTO_PLAIN)                                            \
  X(String, 1, XT_PROTO_PLAIN)                                            \
  X(Boolean, 1, XT_PROTO_PLAIN)                                           \
  X(BigInt, 1, XT_PROTO_PLAIN)                                            \
  X(Symbol, 0, XT_PROTO_PLAIN)

#define XT_CTOR_ENUM(name, arity, proto) XT_CTOR_##name,
enum { XT_CTOR_SPECS(XT_CTOR_ENUM) XT_CTOR_COUNT };
#undef XT_CTOR_ENUM

#define XT_CTOR_ENTRY(name, arity, proto) {#name, arity, proto},
static const xt_ctor_spec XT_CTOR_TABLE[XT_CTOR_COUNT] = {XT_CTOR_SPECS(XT_CTOR_ENTRY)};
#undef XT_CTOR_ENTRY

static xt_value xt_ctor_cache[XT_CTOR_COUNT];
static xt_value xt_ctor_plain_protos[XT_CTOR_COUNT];
static int xt_ctor_cache_ready = 0;

static void xt_ctor_cache_init(void) {
  if (xt_ctor_cache_ready) return;
  xt_ctor_cache_ready = 1;
  xt_gc_add_root_range(xt_ctor_cache, XT_CTOR_COUNT);
  xt_gc_add_root_range(xt_ctor_plain_protos, XT_CTOR_COUNT);
  for (int i = 0; i < XT_CTOR_COUNT; i++) {
    xt_ctor_cache[i] = XT_UNDEFINED;
    xt_ctor_plain_protos[i] = XT_UNDEFINED;
  }
}

static int xt_ctor_index(const char *name) {
  if (!name) return -1;
  for (int i = 0; i < XT_CTOR_COUNT; i++) {
    if (strcmp(XT_CTOR_TABLE[i].name, name) == 0) return i;
  }
  return -1;
}

/* -- construction --------------------------------------------------------- */

static int xt_ctor_is_typed_array(const char *name) {
  int index = xt_ctor_index(name);
  return index >= XT_CTOR_Uint8Array && index <= XT_CTOR_Float64Array;
}

static int xt_ctor_is_error(const char *name) {
  static const char *const names[] = {
      "Error",     "TypeError", "RangeError",  "SyntaxError",
      "ReferenceError", "EvalError", "URIError", "AggregateError",
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    if (strcmp(name, names[i]) == 0) return 1;
  }
  return 0;
}

xt_value xt_array_ctor(int32_t argc, xt_value *argv) {
  /* `Array(3)` allocates a length-3 array; any other argument list becomes the
   * elements. */
  if (argc == 1 && XT_IS_NUMBER(argv[0])) {
    double length = xt_to_number(argv[0]);
    if (!(length >= 0) || floor(length) != length || length > 4294967295.0) {
      xt_throw(xt_string_from_cstr("RangeError: Invalid array length"));
      return XT_UNDEFINED;
    }
    xt_value array = xt_array_new(0, NULL);
    if (length > 0) xt_array_set(array, xt_number(length - 1), XT_UNDEFINED);
    return array;
  }
  return xt_array_new(argc, argv);
}

xt_value xt_object_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_object_new();
  xt_value value = argv[0];
  if (value == XT_UNDEFINED || value == XT_NULL) return xt_object_new();
  /* `Object(value)` returns the object unchanged. Primitives are not boxed in
   * this runtime, so they pass through like `new Object(primitive)` did. */
  return value;
}

/* `Function()` / `new Function()`: source evaluation is not supported, so the
 * constructor gives an empty function (declared functions get their metadata
 * from the codegen; this body simply does nothing). */
static xt_value xt_function_ctor_body(xt_value thisValue, xt_value env, int32_t argc,
                                      xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

static xt_value xt_builtin_ctor_call(const char *name, int32_t argc, xt_value *argv) {
  xt_value nameValue = xt_string_from_cstr(name);
  if (strcmp(name, "Array") == 0) return xt_array_ctor(argc, argv);
  if (strcmp(name, "Object") == 0) return xt_object_ctor(argc, argv);
  if (strcmp(name, "Function") == 0) {
    return xt_closure_new((void *)xt_function_ctor_body, 0, NULL);
  }
  if (strcmp(name, "Map") == 0) return xt_map_ctor(argc, argv);
  if (strcmp(name, "Set") == 0) return xt_set_ctor(argc, argv);
  if (strcmp(name, "Date") == 0) return xt_date_ctor(argc, argv);
  if (strcmp(name, "RegExp") == 0) return xt_regexp_ctor(argc, argv);
  if (strcmp(name, "Promise") == 0) return xt_promise_ctor(argc, argv);
  if (strcmp(name, "Number") == 0) return xt_number_ctor(argc, argv);
  if (strcmp(name, "String") == 0) return xt_string_ctor(argc, argv);
  if (strcmp(name, "Boolean") == 0) return xt_boolean_ctor(argc, argv);
  if (strcmp(name, "BigInt") == 0) return xt_bigint_ctor(argc, argv);
  if (strcmp(name, "Symbol") == 0) return xt_symbol(argc, argv);
  if (xt_ctor_is_typed_array(name)) return xt_typed_array_ctor_by_name(nameValue, argc, argv);
  if (xt_ctor_is_error(name)) return xt_error_ctor_by_name(nameValue, argc, argv);
  return XT_UNDEFINED;
}

/* The closure body of every first-class constructor. The name travels in
 * environment slot 0. */
static xt_value xt_builtin_ctor_invoke(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)thisValue;
  xt_value nameValue = xt_closure_env(env, 0);
  const char *name = xt_string_data(nameValue);
  if (!name) return XT_UNDEFINED;
  return xt_builtin_ctor_call(name, argc, argv);
}

/* -- prototypes ----------------------------------------------------------- */

static xt_value xt_ctor_prototype(int index, xt_value nameValue) {
  const xt_ctor_spec *spec = &XT_CTOR_TABLE[index];
  switch (spec->prototype) {
    case XT_PROTO_OBJECT:
      return xt_object_prototype();
    case XT_PROTO_ARRAY:
      return xt_array_prototype();
    case XT_PROTO_FUNCTION:
      return xt_function_prototype();
    case XT_PROTO_ERROR:
      return xt_error_prototype(nameValue);
    case XT_PROTO_TYPED:
      return xt_typed_array_prototype(nameValue);
    case XT_PROTO_PLAIN:
    default:
      if (xt_ctor_plain_protos[index] == XT_UNDEFINED) {
        xt_ctor_plain_protos[index] = xt_object_new();
      }
      return xt_ctor_plain_protos[index];
  }
}

xt_value xt_builtin_constructor(xt_value nameValue) {
  if (!XT_IS_STRING(nameValue)) return XT_UNDEFINED;
  xt_ctor_cache_init();
  const char *name = xt_string_data(nameValue);
  int index = xt_ctor_index(name);
  if (index < 0) return XT_UNDEFINED;
  if (xt_ctor_cache[index] != XT_UNDEFINED) return xt_ctor_cache[index];

  /* The Error family already has a first-class constructor factory that keeps
   * one closure per prototype (`__ctor`), so reuse it to keep
   * `err.constructor === Error` stable no matter which side is evaluated
   * first. */
  if (xt_ctor_is_error(name)) {
    xt_value errorCtor = xt_error_constructor(nameValue);
    if (errorCtor != XT_UNDEFINED) {
      xt_ctor_cache[index] = errorCtor;
      return errorCtor;
    }
  }

  xt_value env[1] = {nameValue};
  xt_value closure = xt_closure_new((void *)xt_builtin_ctor_invoke, 1, env);
  xt_function *function = (xt_function *)XT_GET_PTR(closure);
  function->builtin_name = XT_CTOR_TABLE[index].name;
  xt_function_set_metadata(closure, nameValue, XT_CTOR_TABLE[index].arity);
  /* Cache before resolving the prototype: `Array.prototype` and
   * `Object.prototype` install their `constructor` property by asking for the
   * constructor again. */
  xt_ctor_cache[index] = closure;

  xt_value proto = xt_ctor_prototype(index, nameValue);
  if (proto != XT_UNDEFINED) {
    xt_function_set_prototype(closure, proto);
    /* `Array.prototype` and `Object.prototype` install their own constructor
     * while resolving the prototype above; every other prototype gets it here. */
    int32_t kind = XT_CTOR_TABLE[index].prototype;
    if (kind != XT_PROTO_ARRAY && kind != XT_PROTO_OBJECT) {
      xt_object_set(proto, xt_string_from_cstr("constructor"), closure);
    }
  }
  return closure;
}

/* -- statics -------------------------------------------------------------- */

static const char *const XT_ARRAY_STATICS[] = {"isArray", "of", "from"};
static const char *const XT_OBJECT_STATICS[] = {
    "keys", "values", "entries", "assign", "getOwnPropertyNames",
    "getOwnPropertySymbols", "groupBy", "freeze", "isFrozen", "fromEntries",
    "getPrototypeOf", "setPrototypeOf", "hasOwn", "is", "create",
};
static const char *const XT_NUMBER_STATICS[] = {
    "isInteger", "isSafeInteger", "isFinite", "isNaN", "parseFloat", "parseInt",
};
static const char *const XT_NUMBER_CONSTANTS[] = {
    "MAX_SAFE_INTEGER", "MIN_SAFE_INTEGER", "MAX_VALUE", "MIN_VALUE", "EPSILON",
    "POSITIVE_INFINITY", "NEGATIVE_INFINITY", "NaN",
};
static const char *const XT_STRING_STATICS[] = {"fromCharCode", "fromCodePoint", "raw"};
static const char *const XT_BIGINT_STATICS[] = {"asIntN", "asUintN"};
static const char *const XT_SYMBOL_STATICS[] = {"for", "keyFor"};
static const char *const XT_DATE_STATICS[] = {"now", "parse", "UTC"};
static const char *const XT_PROMISE_STATICS[] = {
    "resolve", "reject", "all", "allSettled", "race", "any",
};
static const char *const XT_TYPED_ARRAY_STATICS[] = {"from", "of"};

static int xt_name_in(const char *name, const char *const *names, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(name, names[i]) == 0) return 1;
  }
  return 0;
}

#define XT_NAMES_COUNT(names) (sizeof(names) / sizeof((names)[0]))

static int xt_ctor_has_static(const char *namespace, const char *method) {
  if (strcmp(namespace, "Array") == 0)
    return xt_name_in(method, XT_ARRAY_STATICS, XT_NAMES_COUNT(XT_ARRAY_STATICS));
  if (strcmp(namespace, "Object") == 0)
    return xt_name_in(method, XT_OBJECT_STATICS, XT_NAMES_COUNT(XT_OBJECT_STATICS));
  if (strcmp(namespace, "Number") == 0)
    return xt_name_in(method, XT_NUMBER_STATICS, XT_NAMES_COUNT(XT_NUMBER_STATICS));
  if (strcmp(namespace, "String") == 0)
    return xt_name_in(method, XT_STRING_STATICS, XT_NAMES_COUNT(XT_STRING_STATICS));
  if (strcmp(namespace, "BigInt") == 0)
    return xt_name_in(method, XT_BIGINT_STATICS, XT_NAMES_COUNT(XT_BIGINT_STATICS));
  if (strcmp(namespace, "Symbol") == 0)
    return xt_name_in(method, XT_SYMBOL_STATICS, XT_NAMES_COUNT(XT_SYMBOL_STATICS));
  if (strcmp(namespace, "Date") == 0)
    return xt_name_in(method, XT_DATE_STATICS, XT_NAMES_COUNT(XT_DATE_STATICS));
  if (strcmp(namespace, "Promise") == 0)
    return xt_name_in(method, XT_PROMISE_STATICS, XT_NAMES_COUNT(XT_PROMISE_STATICS));
  if (xt_ctor_is_typed_array(namespace))
    return xt_name_in(method, XT_TYPED_ARRAY_STATICS, XT_NAMES_COUNT(XT_TYPED_ARRAY_STATICS));
  return 0;
}

static xt_value xt_builtin_static_call(const char *namespace, const char *method, int32_t argc,
                                       xt_value *argv) {
  xt_value nameValue = xt_string_from_cstr(namespace);
  xt_value methodValue = xt_string_from_cstr(method);
  if (strcmp(namespace, "Array") == 0) return xt_array_static(methodValue, argc, argv);
  if (strcmp(namespace, "Object") == 0) return xt_object_static(methodValue, argc, argv);
  if (strcmp(namespace, "Number") == 0) return xt_number_static(methodValue, argc, argv);
  if (strcmp(namespace, "String") == 0) return xt_string_static(methodValue, argc, argv);
  if (strcmp(namespace, "BigInt") == 0) return xt_bigint_static(methodValue, argc, argv);
  if (strcmp(namespace, "Symbol") == 0) return xt_symbol_static(methodValue, argc, argv);
  if (strcmp(namespace, "Date") == 0) return xt_date_static(methodValue, argc, argv);
  if (strcmp(namespace, "Promise") == 0) return xt_promise_static(methodValue, argc, argv);
  if (xt_ctor_is_typed_array(namespace))
    return xt_typed_array_static_by_name(nameValue, methodValue, argc, argv);
  return XT_UNDEFINED;
}

static xt_value xt_builtin_static_invoke(xt_value thisValue, xt_value env, int32_t argc,
                                         xt_value *argv) {
  (void)thisValue;
  const char *namespace = xt_string_data(xt_closure_env(env, 0));
  const char *method = xt_string_data(xt_closure_env(env, 1));
  if (!namespace || !method) return XT_UNDEFINED;
  return xt_builtin_static_call(namespace, method, argc, argv);
}

xt_value xt_builtin_static_value(xt_value ctor, xt_value key) {
  if (!XT_IS_FUNCTION(ctor) || !XT_IS_STRING(key)) return XT_UNDEFINED;
  xt_function *function = (xt_function *)XT_GET_PTR(ctor);
  const char *namespace = function->builtin_name;
  if (!namespace) return XT_UNDEFINED;
  const char *method = xt_string_data(key);
  if (!method) return XT_UNDEFINED;

  /* Values that are not functions. */
  if (strcmp(namespace, "Symbol") == 0) {
    xt_value wellKnown = xt_symbol_get(key);
    if (wellKnown != XT_UNDEFINED) return wellKnown;
  }
  if (strcmp(namespace, "Number") == 0 &&
      xt_name_in(method, XT_NUMBER_CONSTANTS, XT_NAMES_COUNT(XT_NUMBER_CONSTANTS))) {
    return xt_number_static(key, 0, NULL);
  }
  if (xt_ctor_is_typed_array(namespace) && strcmp(method, "BYTES_PER_ELEMENT") == 0) {
    xt_value nameValue = xt_string_from_cstr(namespace);
    xt_value proto = xt_typed_array_prototype(nameValue);
    return xt_object_get(proto, key);
  }

  if (!xt_ctor_has_static(namespace, method)) return XT_UNDEFINED;
  if (xt_truthy(xt_object_has_own(ctor, key))) return xt_object_get(ctor, key);

  xt_value env[2] = {xt_string_from_cstr(namespace), key};
  xt_value closure = xt_closure_new((void *)xt_builtin_static_invoke, 2, env);
  xt_function_set_metadata(closure, key, 0);
  xt_object_set(ctor, key, closure);
  return closure;
}

/* -- prototype lookup for runtime kinds ----------------------------------- */

xt_value xt_builtin_prototype_for_kind(int32_t kind) {
  const char *name = NULL;
  switch (kind) {
    case XT_OBJECT_KIND_MAP: name = "Map"; break;
    case XT_OBJECT_KIND_SET: name = "Set"; break;
    case XT_OBJECT_KIND_DATE: name = "Date"; break;
    case XT_OBJECT_KIND_REGEXP: name = "RegExp"; break;
    case XT_OBJECT_KIND_PROMISE: name = "Promise"; break;
    case XT_OBJECT_KIND_SYMBOL: name = "Symbol"; break;
    default: return XT_UNDEFINED;
  }
  xt_value constructor = xt_builtin_constructor(xt_string_from_cstr(name));
  return xt_function_get_prototype(constructor);
}
