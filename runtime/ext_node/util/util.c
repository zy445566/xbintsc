/*
 * Node.js `util` module for xbintsc.
 *
 * Implements the pieces of `util` that are useful without a real event loop:
 * `format` (printf-style placeholders), `inspect` (a recursive pretty printer),
 * `isDeepStrictEqual`, `inherits`, `deprecate`, `promisify` and the small set
 * of `isX` type predicates.
 *
 * `promisify` builds a real promise through `xt_promise_ctor`: the returned
 * function calls the wrapped function with a trailing `(err, value)` callback
 * that resolves or rejects the promise.
 */

#include "../node_common.h"

#include <stdio.h>

int xt_node_is_buffer(xt_value value);

/* -- growable string buffer ---------------------------------------------- */

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} util_buf;

static void util_buf_init(util_buf *buf) {
  buf->capacity = 64;
  buf->length = 0;
  buf->data = (char *)malloc(buf->capacity);
  if (buf->data) buf->data[0] = '\0';
}

static void util_buf_reserve(util_buf *buf, size_t extra) {
  if (!buf->data) return;
  if (buf->length + extra + 1 <= buf->capacity) return;
  while (buf->length + extra + 1 > buf->capacity) buf->capacity *= 2;
  buf->data = (char *)realloc(buf->data, buf->capacity);
}

static void util_buf_putc(util_buf *buf, char c) {
  util_buf_reserve(buf, 1);
  if (!buf->data) return;
  buf->data[buf->length++] = c;
  buf->data[buf->length] = '\0';
}

static void util_buf_puts(util_buf *buf, const char *text, size_t length) {
  if (!text) return;
  util_buf_reserve(buf, length);
  if (!buf->data) return;
  memcpy(buf->data + buf->length, text, length);
  buf->length += length;
  buf->data[buf->length] = '\0';
}

static void util_buf_put_cstr(util_buf *buf, const char *text) {
  if (text) util_buf_puts(buf, text, strlen(text));
}

static void util_buf_put_value(util_buf *buf, xt_value value) {
  xt_value text = xt_to_string(value);
  const char *data = xt_string_data(text);
  int32_t length = xt_string_length_value(text);
  if (data) util_buf_puts(buf, data, (size_t)length);
}

static xt_value util_buf_finish(util_buf *buf) {
  xt_value result = buf->data ? xt_string_new(buf->data, buf->length) : xt_string_from_cstr("");
  free(buf->data);
  buf->data = NULL;
  return result;
}

/* -- inspect -------------------------------------------------------------- */

static void util_inspect_into(util_buf *buf, xt_value value, int depth);

static void util_inspect_string(util_buf *buf, xt_value value) {
  const char *data = xt_string_data(value);
  int32_t length = xt_string_length_value(value);
  util_buf_putc(buf, '\'');
  for (int32_t i = 0; i < length; i++) {
    char c = data[i];
    if (c == '\'') util_buf_put_cstr(buf, "\\'");
    else if (c == '\\') util_buf_put_cstr(buf, "\\\\");
    else if (c == '\n') util_buf_put_cstr(buf, "\\n");
    else if (c == '\r') util_buf_put_cstr(buf, "\\r");
    else if (c == '\t') util_buf_put_cstr(buf, "\\t");
    else util_buf_putc(buf, c);
  }
  util_buf_putc(buf, '\'');
}

static void util_inspect_into(util_buf *buf, xt_value value, int depth) {
  if (value == XT_UNDEFINED) {
    util_buf_put_cstr(buf, "undefined");
    return;
  }
  if (value == XT_NULL) {
    util_buf_put_cstr(buf, "null");
    return;
  }
  if (value == XT_TRUE) {
    util_buf_put_cstr(buf, "true");
    return;
  }
  if (value == XT_FALSE) {
    util_buf_put_cstr(buf, "false");
    return;
  }
  if (XT_IS_NUMBER(value)) {
    util_buf_put_value(buf, value);
    return;
  }
  if (XT_IS_BIGINT(value)) {
    util_buf_put_value(buf, value);
    util_buf_putc(buf, 'n');
    return;
  }
  if (XT_IS_STRING(value)) {
    util_inspect_string(buf, value);
    return;
  }
  if (XT_IS_FUNCTION(value)) {
    util_buf_put_cstr(buf, "[Function]");
    return;
  }
  if (xt_is_date(value)) {
    util_buf_put_value(buf, xt_to_string(value));
    return;
  }
  if (xt_is_regexp(value)) {
    util_buf_put_value(buf, xt_to_string(value));
    return;
  }
  if (depth >= 2) {
    if (XT_IS_ARRAY(value)) util_buf_put_cstr(buf, "[Array]");
    else util_buf_put_cstr(buf, "[Object]");
    return;
  }
  if (XT_IS_ARRAY(value)) {
    int32_t count = (int32_t)xt_to_number(xt_array_length(value));
    util_buf_put_cstr(buf, "[ ");
    for (int32_t i = 0; i < count; i++) {
      if (i > 0) util_buf_put_cstr(buf, ", ");
      util_inspect_into(buf, xt_array_get(value, xt_number((double)i)), depth + 1);
    }
    if (count > 0) util_buf_putc(buf, ' ');
    util_buf_putc(buf, ']');
    return;
  }
  if (XT_IS_OBJECT(value)) {
    xt_value keys = xt_object_keys(value);
    int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
    util_buf_put_cstr(buf, "{ ");
    for (int32_t i = 0; i < count; i++) {
      if (i > 0) util_buf_put_cstr(buf, ", ");
      xt_value key = xt_array_get(keys, xt_number((double)i));
      util_buf_put_value(buf, key);
      util_buf_put_cstr(buf, ": ");
      util_inspect_into(buf, xt_object_get(value, key), depth + 1);
    }
    if (count > 0) util_buf_putc(buf, ' ');
    util_buf_putc(buf, '}');
    return;
  }
  util_buf_put_value(buf, value);
}

static xt_value util_inspect_value(xt_value value) {
  util_buf buf;
  util_buf_init(&buf);
  util_inspect_into(&buf, value, 0);
  return util_buf_finish(&buf);
}

xt_value xt_util_inspect(int32_t argc, xt_value *argv) {
  return util_inspect_value(xt_arg(argc, argv, 0));
}

/* -- format --------------------------------------------------------------- */

/* Convert one `%s`/`%d`/... argument for `format`. */
static void util_format_arg(util_buf *buf, char specifier, xt_value value) {
  switch (specifier) {
    case 's':
      if (XT_IS_STRING(value)) {
        const char *data = xt_string_data(value);
        util_buf_puts(buf, data, (size_t)xt_string_length_value(value));
      } else if (XT_IS_OBJECT(value) || XT_IS_ARRAY(value)) {
        util_inspect_into(buf, value, 0);
      } else {
        util_buf_put_value(buf, value);
      }
      break;
    case 'd':
    case 'i': {
      if (XT_IS_BIGINT(value)) {
        util_buf_put_value(buf, value);
      } else {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)xt_to_number(value));
        util_buf_put_cstr(buf, buffer);
      }
      break;
    }
    case 'f':
      if (XT_IS_BIGINT(value)) {
        util_buf_put_value(buf, value);
      } else {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%f", xt_to_number(value));
        util_buf_put_cstr(buf, buffer);
      }
      break;
    case 'j':
      util_buf_put_value(buf, xt_json_stringify(1, &value));
      break;
    case 'o':
    case 'O':
      util_inspect_into(buf, value, 0);
      break;
    default:
      util_buf_put_value(buf, value);
      break;
  }
}

xt_value xt_util_format(int32_t argc, xt_value *argv) {
  util_buf buf;
  util_buf_init(&buf);
  if (argc == 0) return util_buf_finish(&buf);

  xt_value first = argv[0];
  if (!XT_IS_STRING(first)) {
    for (int32_t i = 0; i < argc; i++) {
      if (i > 0) util_buf_putc(&buf, ' ');
      util_inspect_into(&buf, argv[i], 0);
    }
    return util_buf_finish(&buf);
  }

  const char *text = xt_string_data(first);
  int32_t length = xt_string_length_value(first);
  int32_t next = 1;
  for (int32_t i = 0; i < length; i++) {
    char c = text[i];
    if (c != '%' || i + 1 >= length) {
      util_buf_putc(&buf, c);
      continue;
    }
    char specifier = text[++i];
    if (specifier == '%') {
      util_buf_putc(&buf, '%');
      continue;
    }
    if (specifier == 'c') continue; /* CSS directive: ignored */
    if (next < argc) {
      util_format_arg(&buf, specifier, argv[next++]);
    } else {
      util_buf_putc(&buf, '%');
      util_buf_putc(&buf, specifier);
    }
  }
  for (int32_t i = next; i < argc; i++) {
    util_buf_putc(&buf, ' ');
    util_inspect_into(&buf, argv[i], 0);
  }
  return util_buf_finish(&buf);
}

xt_value xt_util_format_with_options(int32_t argc, xt_value *argv) {
  /* The inspect options are accepted and ignored; only the format string and
     its arguments are used. */
  if (argc <= 1) return xt_string_from_cstr("");
  return xt_util_format(argc - 1, argv + 1);
}

/* -- isDeepStrictEqual ---------------------------------------------------- */

static int util_deep_equal(xt_value a, xt_value b, int depth) {
  if (depth > 32) return 1;
  if (a == b) return 1;
  /* NaN === NaN under `isDeepStrictEqual`. */
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    double x = xt_to_number(a), y = xt_to_number(b);
    return (x == y) || (x != x && y != y);
  }
  if (XT_IS_ARRAY(a) && XT_IS_ARRAY(b)) {
    int32_t na = (int32_t)xt_to_number(xt_array_length(a));
    int32_t nb = (int32_t)xt_to_number(xt_array_length(b));
    if (na != nb) return 0;
    for (int32_t i = 0; i < na; i++) {
      if (!util_deep_equal(xt_array_get(a, xt_number((double)i)), xt_array_get(b, xt_number((double)i)), depth + 1))
        return 0;
    }
    return 1;
  }
  if (XT_IS_OBJECT(a) && XT_IS_OBJECT(b)) {
    xt_value keysA = xt_object_keys(a);
    xt_value keysB = xt_object_keys(b);
    int32_t na = (int32_t)xt_to_number(xt_array_length(keysA));
    int32_t nb = (int32_t)xt_to_number(xt_array_length(keysB));
    if (na != nb) return 0;
    for (int32_t i = 0; i < na; i++) {
      xt_value key = xt_array_get(keysA, xt_number((double)i));
      if (!xt_truthy(xt_object_has_own(b, key))) return 0;
      if (!util_deep_equal(xt_object_get(a, key), xt_object_get(b, key), depth + 1)) return 0;
    }
    return 1;
  }
  return 0;
}

xt_value xt_util_is_deep_strict_equal(int32_t argc, xt_value *argv) {
  return xt_bool(util_deep_equal(xt_arg(argc, argv, 0), xt_arg(argc, argv, 1), 0));
}

/* -- inherits ------------------------------------------------------------- */

xt_value xt_util_inherits(int32_t argc, xt_value *argv) {
  xt_value ctor = xt_arg(argc, argv, 0);
  xt_value superCtor = xt_arg(argc, argv, 1);
  if (XT_IS_FUNCTION(ctor) && XT_IS_FUNCTION(superCtor)) {
    xt_value superProto = xt_function_get_prototype(superCtor);
    xt_value proto = xt_object_new_with_proto(superProto);
    xt_object_set(proto, xt_string_from_cstr("constructor"), ctor);
    xt_function_set_prototype(ctor, proto);
    xt_node_set(ctor, "super_", superCtor);
  }
  return xt_undefined();
}

/* -- deprecate ------------------------------------------------------------ */

xt_value xt_util_deprecate(int32_t argc, xt_value *argv) {
  /* No process event loop to emit warnings into: hand back the function. */
  return xt_arg(argc, argv, 0);
}

/* -- promisify ------------------------------------------------------------ */

static xt_value util_promisify_callback(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value resolve = xt_closure_env(env, 0);
  xt_value reject = xt_closure_env(env, 1);
  xt_value error = xt_arg(argc, argv, 0);
  if (!xt_truthy(xt_is_nullish(error))) {
    if (XT_IS_FUNCTION(reject)) xt_closure_call(reject, 1, &error);
    return xt_undefined();
  }
  xt_value value = argc > 1 ? argv[1] : xt_undefined();
  if (XT_IS_FUNCTION(resolve)) xt_closure_call(resolve, 1, &value);
  return xt_undefined();
}

static xt_value util_promisify_executor(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value fn = xt_closure_env(env, 0);
  xt_value args = xt_closure_env(env, 1);
  xt_value resolve = xt_arg(argc, argv, 0);
  xt_value reject = xt_arg(argc, argv, 1);
  int32_t count = (int32_t)xt_to_number(xt_array_length(args));
  xt_value *callArgs = (xt_value *)malloc(sizeof(xt_value) * (size_t)(count + 1));
  if (!callArgs) return xt_undefined();
  for (int32_t i = 0; i < count; i++) callArgs[i] = xt_array_get(args, xt_number((double)i));
  xt_value callbackEnv[2] = {resolve, reject};
  callArgs[count] = xt_closure_new((void *)util_promisify_callback, 2, callbackEnv);
  if (XT_IS_FUNCTION(fn)) xt_closure_call(fn, count + 1, callArgs);
  free(callArgs);
  return xt_undefined();
}

static xt_value util_promisify_wrapper(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value fn = xt_closure_env(env, 0);
  xt_value args = xt_array_new(argc, argv);
  xt_value executorEnv[2] = {fn, args};
  xt_value executor = xt_closure_new((void *)util_promisify_executor, 2, executorEnv);
  xt_value ctorArgs[1] = {executor};
  return xt_promise_ctor(1, ctorArgs);
}

xt_value xt_util_promisify(int32_t argc, xt_value *argv) {
  xt_value fn = xt_arg(argc, argv, 0);
  xt_value env[1] = {fn};
  return xt_closure_new((void *)util_promisify_wrapper, 1, env);
}

/* -- type predicates ------------------------------------------------------ */

xt_value xt_util_is_string(int32_t argc, xt_value *argv) { return xt_bool(XT_IS_STRING(xt_arg(argc, argv, 0))); }
xt_value xt_util_is_number(int32_t argc, xt_value *argv) { return xt_bool(XT_IS_NUMBER(xt_arg(argc, argv, 0))); }
xt_value xt_util_is_boolean(int32_t argc, xt_value *argv) {
  xt_value value = xt_arg(argc, argv, 0);
  return xt_bool(value == XT_TRUE || value == XT_FALSE);
}
xt_value xt_util_is_undefined(int32_t argc, xt_value *argv) { return xt_bool(xt_arg(argc, argv, 0) == XT_UNDEFINED); }
xt_value xt_util_is_null(int32_t argc, xt_value *argv) { return xt_bool(xt_arg(argc, argv, 0) == XT_NULL); }
xt_value xt_util_is_function(int32_t argc, xt_value *argv) { return xt_bool(XT_IS_FUNCTION(xt_arg(argc, argv, 0))); }
xt_value xt_util_is_array(int32_t argc, xt_value *argv) { return xt_bool(XT_IS_ARRAY(xt_arg(argc, argv, 0))); }
xt_value xt_util_is_object(int32_t argc, xt_value *argv) {
  xt_value value = xt_arg(argc, argv, 0);
  return xt_bool(XT_IS_OBJECT(value) && !XT_IS_ARRAY(value));
}
xt_value xt_util_is_buffer(int32_t argc, xt_value *argv) { return xt_bool(xt_node_is_buffer(xt_arg(argc, argv, 0))); }
xt_value xt_util_is_date(int32_t argc, xt_value *argv) { return xt_bool(xt_is_date(xt_arg(argc, argv, 0))); }
xt_value xt_util_is_regexp(int32_t argc, xt_value *argv) { return xt_bool(xt_is_regexp(xt_arg(argc, argv, 0))); }
xt_value xt_util_is_promise(int32_t argc, xt_value *argv) { return xt_bool(xt_is_promise(xt_arg(argc, argv, 0))); }

/* Best-effort `Error` detection: error objects carry own `name`/`message`. */
xt_value xt_util_is_error(int32_t argc, xt_value *argv) {
  xt_value value = xt_arg(argc, argv, 0);
  if (!XT_IS_OBJECT(value)) return xt_bool(0);
  return xt_bool(xt_truthy(xt_object_has_own(value, xt_string_from_cstr("message"))) &&
                 xt_truthy(xt_object_has_own(value, xt_string_from_cstr("name"))));
}

/* -- namespace dispatcher ------------------------------------------------- */

static const struct {
  const char *name;
  xt_value (*fn)(int32_t, xt_value *);
} util_functions[] = {
    {"format", xt_util_format},
    {"formatWithOptions", xt_util_format_with_options},
    {"inspect", xt_util_inspect},
    {"isDeepStrictEqual", xt_util_is_deep_strict_equal},
    {"inherits", xt_util_inherits},
    {"deprecate", xt_util_deprecate},
    {"promisify", xt_util_promisify},
    {"isString", xt_util_is_string},
    {"isNumber", xt_util_is_number},
    {"isBoolean", xt_util_is_boolean},
    {"isUndefined", xt_util_is_undefined},
    {"isNull", xt_util_is_null},
    {"isFunction", xt_util_is_function},
    {"isArray", xt_util_is_array},
    {"isObject", xt_util_is_object},
    {"isBuffer", xt_util_is_buffer},
    {"isDate", xt_util_is_date},
    {"isRegExp", xt_util_is_regexp},
    {"isPromise", xt_util_is_promise},
    {"isError", xt_util_is_error},
};

xt_value xt_util_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  for (size_t i = 0; i < sizeof(util_functions) / sizeof(util_functions[0]); i++) {
    if (strcmp(fn, util_functions[i].name) == 0) return util_functions[i].fn(argc, argv);
  }
  return xt_undefined();
}
