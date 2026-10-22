/*
 * xbintsc runtime — builtins.
 *
 * `Object.*` statics and the free functions (`parseInt`, `isNaN`, `in`, ...),
 * along with the closure representation and calling convention.
 */

#include "rt_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Object methods and global functions                                       */
/* ------------------------------------------------------------------------- */

xt_value xt_object_values(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  xt_value keys = xt_object_keys(value);
  xt_array *array = xt_as_array(keys);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) xt_array_push(result, xt_get(value, array->items[i]));
  return result;
}

xt_value xt_object_entries(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  xt_value keys = xt_object_keys(value);
  xt_array *array = xt_as_array(keys);
  if (!array) return result;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value pair = xt_array_new(0, NULL);
    xt_array_push(pair, array->items[i]);
    xt_array_push(pair, xt_get(value, array->items[i]));
    xt_array_push(result, pair);
  }
  return result;
}

xt_value xt_object_assign(int32_t argc, xt_value *argv) {
  if (argc <= 0) return xt_undefined();
  xt_value target = argv[0];
  if (!XT_IS_OBJECT(target)) return target;
  for (int32_t i = 1; i < argc; i++) {
    if (!XT_IS_OBJECT(argv[i]) && !XT_IS_ARRAY(argv[i]) && !XT_IS_STRING(argv[i])) continue;
    /* Arrays and strings contribute their index properties, exactly like
     * `{ ..."ab" }` / `Object.assign({}, [1, 2])` in ECMAScript. */
    xt_value properties = xt_object_rest(argv[i], 0, NULL);
    xt_object *source = (xt_object *)XT_GET_PTR(properties);
    for (uint32_t j = 0; j < source->count; j++) {
      xt_object_set(target, source->properties[j].key, source->properties[j].value);
    }
  }
  return target;
}

/* Copy every own enumerable property of `source` onto `target` (`{...source}`). */
xt_value xt_object_spread(xt_value target, xt_value source) {
  xt_value args[2];
  args[0] = target;
  args[1] = source;
  return xt_object_assign(2, args);
}

/* True when `key` is one of the explicitly destructured properties. */
static int xt_object_rest_excludes(xt_value key, int32_t argc, xt_value *argv) {
  for (int32_t i = 0; i < argc; i++) {
    if (xt_property_key_equals(key, argv[i])) return 1;
  }
  return 0;
}

static void xt_object_rest_copy(xt_value target, xt_value key, xt_value value, int32_t argc, xt_value *argv) {
  if (xt_object_rest_excludes(key, argc, argv)) return;
  xt_object_set(target, key, value);
}

/*
 * `const { a, ...rest } = source` (and the assignment / parameter forms).
 *
 * Copies the *own enumerable* properties that the pattern did not name, in
 * insertion order. Arrays and strings contribute their index properties, like
 * ECMAScript; accessor properties are read through their getter, with the
 * source as the receiver.
 */
xt_value xt_object_rest(xt_value source, int32_t argc, xt_value *argv) {
  if (source == XT_UNDEFINED || source == XT_NULL) {
    xt_throw(xt_error_from_message("TypeError: Cannot destructure null or undefined"));
    return XT_UNDEFINED;
  }
  xt_value result = xt_object_new();

  if (XT_IS_ARRAY(source)) {
    xt_array *array = (xt_array *)XT_GET_PTR(source);
    for (uint32_t i = 0; i < array->length; i++) {
      char buffer[16];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_object_rest_copy(result, xt_string_from_cstr(buffer), array->items[i], argc, argv);
    }
    return result;
  }

  if (XT_IS_STRING(source)) {
    xt_string *string = (xt_string *)XT_GET_PTR(source);
    uint32_t units = xt_string_utf16_length(string);
    for (uint32_t i = 0; i < units; i++) {
      char buffer[16];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_object_rest_copy(result, xt_string_from_cstr(buffer), xt_string_unit_value(string, (int32_t)i), argc, argv);
    }
    return result;
  }

  if (!XT_IS_OBJECT(source) && !XT_IS_FUNCTION(source)) return result;
  xt_object *object = xt_as_object(source);
  if (!object) return result;
  if (object->header.kind != XT_OBJECT_KIND_OBJECT && object->header.kind != XT_OBJECT_KIND_ERROR) {
    /* Map/Set/Date/RegExp/Promise have no own enumerable string properties. */
    return result;
  }
  for (uint32_t i = 0; i < object->count; i++) {
    xt_property *property = &object->properties[i];
    if (xt_object_rest_excludes(property->key, argc, argv)) continue;
    xt_value value = property->value;
    if (property->getter != XT_UNDEFINED && XT_IS_FUNCTION(property->getter)) {
      value = xt_call_with_this(property->getter, source, 0, NULL);
    }
    xt_object_set(result, property->key, value);
  }
  return result;
}

xt_value xt_parse_int(int32_t argc, xt_value *argv) {
  xt_string *s = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
  const char *p = xt_string_bytes(s);
  /* JavaScript `parseInt` semantics: skip leading whitespace, accept an
     optional sign, pick the radix (auto-detecting a `0x` prefix), read the
     longest run of valid digits and accumulate into a double. Accumulating as
     a double (instead of `strtol` into a `long`) keeps large literals exact on
     every platform -- Windows `long` is only 32 bits. */
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') p++;
  int sign = 1;
  if (*p == '+') {
    p++;
  } else if (*p == '-') {
    sign = -1;
    p++;
  }
  int radix = argc >= 2 ? xt_to_int32(argv[1]) : 0;
  if (radix != 0) {
    if (radix < 2 || radix > 36) return xt_number(NAN);
  } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    radix = 16;
  } else {
    radix = 10;
  }
  if (radix == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
  double value = 0;
  int digits = 0;
  for (;;) {
    unsigned char c = (unsigned char)*p;
    int digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
    else break;
    if (digit >= radix) break;
    value = value * radix + digit;
    p++;
    digits++;
  }
  if (digits == 0) return xt_number(NAN);
  return xt_number(sign * value);
}

xt_value xt_parse_float(int32_t argc, xt_value *argv) {
  xt_string *s = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
  const char *bytes = xt_string_bytes(s);
  char *end = NULL;
  double value = strtod(bytes, &end);
  if (end == bytes) return xt_number(NAN);
  return xt_number(value);
}

xt_value xt_is_nan(int32_t argc, xt_value *argv) {
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  return xt_bool(value != value);
}

xt_value xt_is_finite(int32_t argc, xt_value *argv) {
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  return xt_bool(!isnan(value) && !isinf(value));
}

/* ------------------------------------------------------------------------- */
/* URI encoding / decoding (ECMAScript 19.2.6)                               */
/* ------------------------------------------------------------------------- */

static _Noreturn void xt_throw_uri_error(const char *message) {
  char buffer[96];
  snprintf(buffer, sizeof(buffer), "URIError: %s", message);
  xt_throw(xt_error_from_message(buffer));
  abort();
}

static int xt_uri_is_kept(const char *allowed, unsigned char c) {
  return c < 0x80 && c != 0 && strchr(allowed, (int)c) != NULL;
}

static xt_value xt_uri_encode(xt_value input, const char *allowed) {
  xt_string *s = xt_as_string(xt_to_string(input));
  const char *bytes = xt_string_bytes(s);
  static const char hex[] = "0123456789ABCDEF";
  char *out = (char *)malloc((size_t)s->length * 3 + 1);
  if (!out) {
    fprintf(stderr, "xbintsc: out of memory\n");
    abort();
  }
  size_t written = 0;
  for (uint32_t i = 0; i < s->length; i++) {
    unsigned char c = (unsigned char)bytes[i];
    if (xt_uri_is_kept(allowed, c)) {
      out[written++] = (char)c;
    } else {
      out[written++] = '%';
      out[written++] = hex[(c >> 4) & 0xF];
      out[written++] = hex[c & 0xF];
    }
  }
  xt_value result = xt_string_new(out, written);
  free(out);
  return result;
}

static int xt_hex_value(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

/* Decode percent-escapes as UTF-8. When `component` is false (`decodeURI`),
 * escapes for reserved characters are copied through unchanged. */
static xt_value xt_uri_decode(xt_value input, int component) {
  xt_string *s = xt_as_string(xt_to_string(input));
  const char *bytes = xt_string_bytes(s);
  char *out = (char *)malloc((size_t)s->length + 1);
  if (!out) {
    fprintf(stderr, "xbintsc: out of memory\n");
    abort();
  }
  size_t written = 0;
  uint32_t i = 0;
  while (i < s->length) {
    unsigned char c = (unsigned char)bytes[i];
    if (c != '%') {
      out[written++] = (char)c;
      i++;
      continue;
    }
    if (i + 2 >= s->length) xt_throw_uri_error("URI malformed");
    int h1 = xt_hex_value((unsigned char)bytes[i + 1]);
    int h2 = xt_hex_value((unsigned char)bytes[i + 2]);
    if (h1 < 0 || h2 < 0) xt_throw_uri_error("URI malformed");
    unsigned char b = (unsigned char)((h1 << 4) | h2);
    if (b < 0x80) {
      if (!component && xt_uri_is_kept(";/?:@&=+$,#", b)) {
        out[written++] = bytes[i];
        out[written++] = bytes[i + 1];
        out[written++] = bytes[i + 2];
      } else {
        out[written++] = (char)b;
      }
      i += 3;
      continue;
    }
    int extra;
    if (b >= 0xC2 && b <= 0xDF) extra = 1;
    else if (b >= 0xE0 && b <= 0xEF) extra = 2;
    else if (b >= 0xF0 && b <= 0xF4) extra = 3;
    else xt_throw_uri_error("URI malformed");
    char seq[4];
    seq[0] = (char)b;
    for (int k = 1; k <= extra; k++) {
      uint32_t pos = i + (uint32_t)k * 3;
      if (pos + 2 >= s->length) xt_throw_uri_error("URI malformed");
      if (bytes[pos] != '%') xt_throw_uri_error("URI malformed");
      int c1 = xt_hex_value((unsigned char)bytes[pos + 1]);
      int c2 = xt_hex_value((unsigned char)bytes[pos + 2]);
      if (c1 < 0 || c2 < 0) xt_throw_uri_error("URI malformed");
      unsigned char cb = (unsigned char)((c1 << 4) | c2);
      if ((cb & 0xC0) != 0x80) xt_throw_uri_error("URI malformed");
      seq[k] = (char)cb;
    }
    uint32_t codePoint;
    if (extra == 1) codePoint = ((uint32_t)(b & 0x1F) << 6) | ((uint32_t)seq[1] & 0x3F);
    else if (extra == 2)
      codePoint = ((uint32_t)(b & 0x0F) << 12) | (((uint32_t)seq[1] & 0x3F) << 6) | ((uint32_t)seq[2] & 0x3F);
    else
      codePoint = ((uint32_t)(b & 0x07) << 18) | (((uint32_t)seq[1] & 0x3F) << 12) |
                  (((uint32_t)seq[2] & 0x3F) << 6) | ((uint32_t)seq[3] & 0x3F);
    uint32_t minimum = extra == 1 ? 0x80u : (extra == 2 ? 0x800u : 0x10000u);
    if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
      xt_throw_uri_error("URI malformed");
    for (int k = 0; k <= extra; k++) out[written++] = seq[k];
    i += (uint32_t)(extra + 1) * 3;
  }
  xt_value result = xt_string_new(out, written);
  free(out);
  return result;
}

xt_value xt_encode_uri_component(int32_t argc, xt_value *argv) {
  return xt_uri_encode(xt_arg_at(argc, argv, 0),
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'()");
}

xt_value xt_encode_uri(int32_t argc, xt_value *argv) {
  return xt_uri_encode(xt_arg_at(argc, argv, 0),
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'();,/?:@&=+$#");
}

xt_value xt_decode_uri_component(int32_t argc, xt_value *argv) {
  return xt_uri_decode(xt_arg_at(argc, argv, 0), /* component */ 1);
}

xt_value xt_decode_uri(int32_t argc, xt_value *argv) {
  return xt_uri_decode(xt_arg_at(argc, argv, 0), /* component */ 0);
}

xt_value xt_number_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_number(0);
  return xt_number(xt_to_number(argv[0]));
}

xt_value xt_string_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_string_from_cstr("");
  if (xt_is_symbol(argv[0])) return xt_symbol_to_string(argv[0]);
  return xt_to_string(argv[0]);
}

xt_value xt_boolean_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return XT_FALSE;
  return xt_bool(xt_truthy(argv[0]));
}

/*
 * Builtin function values. Global constructors like `Boolean` are usually
 * called directly, but JavaScript also allows passing them around
 * (`arr.filter(Boolean)`). These trampolines adapt the uniform `(argc, argv)`
 * builtin ABI to the closure ABI so the codegen can wrap them with
 * `xt_closure_new`.
 */
#define XT_BUILTIN_TRAMPOLINE(name, target)                            \
  xt_value name(xt_value thisValue, xt_value env, int32_t argc,        \
                xt_value *argv) {                                      \
    (void)thisValue;                                                   \
    (void)env;                                                         \
    return target(argc, argv);                                         \
  }

XT_BUILTIN_TRAMPOLINE(xt_builtin_value_boolean, xt_boolean_ctor)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_number, xt_number_ctor)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_bigint, xt_bigint_ctor)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_string, xt_string_ctor)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_symbol, xt_symbol)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_parseInt, xt_parse_int)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_parseFloat, xt_parse_float)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_isNaN, xt_is_nan)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_isFinite, xt_is_finite)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_encodeURIComponent, xt_encode_uri_component)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_encodeURI, xt_encode_uri)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_decodeURIComponent, xt_decode_uri_component)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_decodeURI, xt_decode_uri)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_setTimeout, xt_set_timeout)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_clearTimeout, xt_clear_timeout)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_setInterval, xt_set_interval)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_clearInterval, xt_clear_interval)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_setImmediate, xt_set_immediate)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_clearImmediate, xt_clear_immediate)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_queueMicrotask, xt_queue_microtask)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_structuredClone, xt_structured_clone)
XT_BUILTIN_TRAMPOLINE(xt_builtin_value_fetch, xt_fetch)

xt_value xt_in(xt_value key, xt_value value) {
  if (XT_IS_OBJECT(value)) return xt_object_has(value, key);
  if (XT_IS_ARRAY(value)) {
    if (xt_is_symbol(key)) {
      xt_array *array = (xt_array *)XT_GET_PTR(value);
      if (!XT_IS_OBJECT(array->extra)) return XT_FALSE;
      return xt_object_has_own(array->extra, key);
    }
    int32_t index = xt_to_int32(key);
    return xt_bool(index >= 0 && (uint32_t)index < xt_as_array(value)->length);
  }
  if (XT_IS_STRING(value)) {
    if (xt_is_symbol(key)) return XT_FALSE;
    xt_string *s = xt_as_string(value);
    xt_string *k = xt_as_string(xt_to_string(key));
    if (k->length == 6 && memcmp(xt_string_bytes(k), "length", 6) == 0) return XT_TRUE;
    int32_t index = xt_to_int32(key);
    return xt_bool(index >= 0 && (uint32_t)index < xt_string_utf16_length(s));
  }
  return XT_FALSE;
}

xt_value xt_delete(xt_value value, xt_value key) {
  if (XT_IS_OBJECT(value)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(value);
    xt_value k = xt_to_property_key(key);
    for (uint32_t i = 0; i < obj->count; i++) {
      if (xt_property_key_equals(obj->properties[i].key, k)) {
        for (uint32_t j = i + 1; j < obj->count; j++) obj->properties[j - 1] = obj->properties[j];
        obj->count--;
        xt_object_shape_changed(obj);
        break;
      }
    }
    return XT_TRUE;
  }
  if (XT_IS_ARRAY(value)) {
    if (!xt_is_symbol(key)) xt_array_set(value, key, XT_UNDEFINED);
    return XT_TRUE;
  }
  return XT_TRUE;
}

xt_value xt_rest_args(int32_t argc, xt_value *argv, int32_t start) {
  if (start < 0) start = 0;
  int32_t count = argc - start;
  if (count < 0) count = 0;
  return xt_array_new(count, argv ? argv + start : NULL);
}


/* ------------------------------------------------------------------------- */
/* Functions                                                                 */
/* ------------------------------------------------------------------------- */

xt_value xt_arg(int32_t argc, xt_value *argv, int32_t index) {
  if (index < 0 || index >= argc) return XT_UNDEFINED;
  return argv[index];
}

xt_value xt_closure_new(void *fn, int32_t env_count, xt_value *env) {
  xt_function *function = (xt_function *)xt_alloc(sizeof(xt_function), XT_OBJECT_KIND_FUNCTION);
  function->code = (xt_code_fn)fn;
  function->environment_count = env_count;
  function->arity = -1;
  function->name = NULL;
  function->properties = NULL;
  function->environment = NULL;
  function->prototype = XT_UNDEFINED;
  function->is_generator = 0;
  function->is_async = 0;
  function->builtin_name = NULL;
  if (env_count > 0) {
    function->environment = (xt_value *)malloc(sizeof(xt_value) * (size_t)env_count);
    for (int32_t i = 0; i < env_count; i++) function->environment[i] = env[i];
  }
  return XT_FROM_PTR(XT_TAG_FUNCTION, function);
}

xt_value xt_closure_call(xt_value value, int32_t argc, xt_value *argv) {
  return xt_call_with_this(value, XT_UNDEFINED, argc, argv);
}

xt_value xt_call_with_this(xt_value value, xt_value thisValue, int32_t argc, xt_value *argv) {
  if (!XT_IS_FUNCTION(value)) {
    xt_throw(xt_error_from_message("TypeError: value is not a function"));
    return XT_UNDEFINED;
  }
  xt_function *function = (xt_function *)XT_GET_PTR(value);
  /* Calling a generator function returns a suspended generator instead of
   * running the body. */
  if (function->is_generator) return xt_generator_new(value, thisValue, argc, argv);
  /* An `async` function runs its body on a coroutine and returns a promise,
   * which may already be settled or may resolve after an event-loop turn. */
  if (function->is_async) return xt_async_call(value, thisValue, argc, argv);
  /* The closure value itself is threaded through as `env` so the callee can
   * read its captures with xt_closure_env(env, i). */
  return function->code(thisValue, value, argc, argv);
}

xt_value xt_this(void) { return XT_UNDEFINED; }

xt_value xt_function_set_prototype(xt_value value, xt_value proto) {
  if (XT_IS_FUNCTION(value)) ((xt_function *)XT_GET_PTR(value))->prototype = proto;
  return value;
}

xt_value xt_function_get_prototype(xt_value value) {
  if (XT_IS_FUNCTION(value)) return ((xt_function *)XT_GET_PTR(value))->prototype;
  return XT_UNDEFINED;
}

xt_value xt_new(xt_value ctor, int32_t argc, xt_value *argv) {
  if (!XT_IS_FUNCTION(ctor)) {
    xt_throw(xt_error_from_message("TypeError: constructor is not a function"));
    return XT_UNDEFINED;
  }
  xt_function *function = (xt_function *)XT_GET_PTR(ctor);
  xt_value proto = function->prototype;
  xt_value instance = xt_object_new_with_proto(XT_IS_OBJECT(proto) ? proto : XT_UNDEFINED);
  xt_value result = function->code(instance, ctor, argc, argv);
  if (XT_IS_OBJECT(result) || XT_IS_ARRAY(result) || XT_IS_FUNCTION(result)) return result;
  return instance;
}

xt_value xt_instance_of(xt_value value, xt_value ctor) {
  if (!XT_IS_FUNCTION(ctor)) return XT_FALSE;
  xt_value proto = ((xt_function *)XT_GET_PTR(ctor))->prototype;
  if (XT_IS_OBJECT(proto) || XT_IS_ARRAY(proto) || XT_IS_FUNCTION(proto)) {
    /* Walk the value's [[Prototype]] chain. `xt_object_get_prototype` knows the
     * layout of every kind (arrays carry a prototype field; Map/Set/Date/... use
     * their built-in prototype), so this is safe for containers too. */
    xt_value cursor = xt_object_get_prototype(value);
    while (cursor != XT_UNDEFINED && cursor != XT_NULL) {
      if (cursor == proto) return XT_TRUE;
      cursor = xt_object_get_prototype(cursor);
    }
  }
  return XT_FALSE;
}

/*
 * `value instanceof Array` (and the rest of the built-in constructors).
 *
 * The codegen recognises the direct spelling and asks this helper instead of
 * materialising the constructor value; an alias (`const A = Array`) goes
 * through the ordinary prototype chain in `xt_instance_of`. Ids match
 * `BUILTIN_INSTANCEOF` in `src/codegen/generator/tables.ts`; 16..24 map onto
 * the runtime's `XT_TA_*` order (Uint8Array first).
 */
enum {
  XT_INSTANCE_ARRAY = 0,
  XT_INSTANCE_OBJECT = 1,
  XT_INSTANCE_FUNCTION = 2,
  XT_INSTANCE_MAP = 3,
  XT_INSTANCE_SET = 4,
  XT_INSTANCE_DATE = 5,
  XT_INSTANCE_REGEXP = 6,
  XT_INSTANCE_PROMISE = 7,
  XT_INSTANCE_TYPED_ARRAY_FIRST = 16,
  XT_INSTANCE_TYPED_ARRAY_LAST = 24
};

static int xt_value_has_kind(xt_value value, uint8_t kind) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == kind;
}

xt_value xt_instance_of_builtin(xt_value value, int32_t id) {
  switch (id) {
    case XT_INSTANCE_ARRAY:
      return xt_bool(XT_IS_ARRAY(value));
    case XT_INSTANCE_OBJECT:
      return xt_bool(XT_IS_OBJECT(value) || XT_IS_ARRAY(value) || XT_IS_FUNCTION(value));
    case XT_INSTANCE_FUNCTION:
      return xt_bool(XT_IS_FUNCTION(value));
    case XT_INSTANCE_MAP:
      return xt_bool(xt_value_has_kind(value, XT_OBJECT_KIND_MAP));
    case XT_INSTANCE_SET:
      return xt_bool(xt_value_has_kind(value, XT_OBJECT_KIND_SET));
    case XT_INSTANCE_DATE:
      return xt_bool(xt_value_has_kind(value, XT_OBJECT_KIND_DATE));
    case XT_INSTANCE_REGEXP:
      return xt_bool(xt_value_has_kind(value, XT_OBJECT_KIND_REGEXP));
    case XT_INSTANCE_PROMISE:
      return xt_bool(xt_value_has_kind(value, XT_OBJECT_KIND_PROMISE));
    default:
      if (id >= XT_INSTANCE_TYPED_ARRAY_FIRST && id <= XT_INSTANCE_TYPED_ARRAY_LAST) {
        return xt_bool(xt_typed_array_kind_of(value) == id - XT_INSTANCE_TYPED_ARRAY_FIRST);
      }
      return XT_FALSE;
  }
}

xt_value xt_closure_env(xt_value value, int32_t index) {
  if (!XT_IS_FUNCTION(value)) return XT_UNDEFINED;
  xt_function *function = (xt_function *)XT_GET_PTR(value);
  if (index < 0 || index >= function->environment_count) return XT_UNDEFINED;
  return function->environment[index];
}

int32_t xt_closure_arity(xt_value value) {
  if (!XT_IS_FUNCTION(value)) return -1;
  return ((xt_function *)XT_GET_PTR(value))->arity;
}

xt_value xt_function_set_metadata(xt_value value, xt_value name, int32_t arity) {
  if (XT_IS_FUNCTION(value)) {
    xt_function *function = (xt_function *)XT_GET_PTR(value);
    function->arity = arity;
    function->name = XT_IS_STRING(name) ? xt_as_string(name) : NULL;
  }
  return value;
}

