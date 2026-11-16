/*
 * xbintsc runtime — `AbortController` / `AbortSignal`.
 *
 * A signal is an ordinary object carrying its state under `__`-prefixed own
 * properties (aborted flag, reason, listener array, timeout handle); the
 * public surface lives on the prototype, so `signal instanceof AbortSignal`
 * holds and methods are shared. `aborted` / `reason` / `onabort` are accessors
 * over that state.
 *
 * Covered: `abort(reason?)`, `addEventListener("abort", fn[, { once }])`,
 * `removeEventListener`, `onabort`, `throwIfAborted()`, and the statics
 * `AbortSignal.abort()`, `AbortSignal.timeout(ms)`, `AbortSignal.any([...])`.
 */

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

/* Prototypes are built once and kept alive for the life of the program. */
static xt_value xt_web_abort_signal_proto_value = XT_UNDEFINED;
static xt_value xt_web_abort_controller_proto_value = XT_UNDEFINED;

/* Method bodies, defined below and installed on the prototypes. */
static xt_value xt_web_signal_add_listener(xt_value thisValue, xt_value env, int32_t argc,
                                           xt_value *argv);
static xt_value xt_web_signal_remove_listener(xt_value thisValue, xt_value env, int32_t argc,
                                              xt_value *argv);
static xt_value xt_web_signal_throw_if_aborted(xt_value thisValue, xt_value env, int32_t argc,
                                               xt_value *argv);
static xt_value xt_web_controller_abort(xt_value thisValue, xt_value env, int32_t argc,
                                        xt_value *argv);
static xt_value xt_web_timeout_fire(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
static xt_value xt_web_chain_fire(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);

static void xt_web_abort_define(xt_value object, const char *name, xt_code_fn fn,
                                int32_t arity) {
  xt_value key = xt_string_from_cstr(name);
  xt_value closure = xt_closure_new((void *)fn, 0, NULL);
  xt_function_set_metadata(closure, key, arity);
  xt_object_set(object, key, closure);
}

static void xt_web_abort_define_accessor(xt_value object, const char *name, xt_code_fn getter,
                                         xt_code_fn setter) {
  xt_value key = xt_string_from_cstr(name);
  xt_value getterValue = xt_closure_new((void *)getter, 0, NULL);
  xt_function_set_metadata(getterValue, key, 0);
  xt_object_define_getter(object, key, getterValue);
  if (!setter) return;
  xt_value setterValue = xt_closure_new((void *)setter, 0, NULL);
  xt_function_set_metadata(setterValue, key, 1);
  xt_object_define_setter(object, key, setterValue);
}

/* -- state ---------------------------------------------------------------- */

static int xt_web_signal_is_aborted(xt_value signal) {
  return xt_truthy(xt_object_get_cstr(signal, "__aborted"));
}

static xt_value xt_web_signal_listeners(xt_value signal) {
  xt_value listeners = xt_object_get_cstr(signal, "__listeners");
  if (!XT_IS_ARRAY(listeners)) {
    listeners = xt_array_new(0, NULL);
    xt_object_set(signal, xt_string_from_cstr("__listeners"), listeners);
  }
  return listeners;
}

static int32_t xt_web_count(xt_value array) {
  if (!XT_IS_ARRAY(array)) return 0;
  return (int32_t)xt_to_number(xt_array_length(array));
}

/* `<name>Error` with a message, shaped like the platform's DOMException
 * (`error.name` is what programs branch on). */
static xt_value xt_web_named_error(const char *name, const char *message) {
  xt_value arguments[1];
  arguments[0] = xt_string_from_cstr(message);
  xt_value error = xt_error_ctor_by_name(xt_string_from_cstr("Error"), 1, arguments);
  if (XT_IS_OBJECT(error)) {
    xt_object_set(error, xt_string_from_cstr("name"), xt_string_from_cstr(name));
  }
  return error;
}

static xt_value xt_web_abort_reason_default(void) {
  return xt_web_named_error("AbortError", "This operation was aborted");
}

static xt_value xt_web_timeout_reason(void) {
  return xt_web_named_error("TimeoutError", "The operation was aborted due to timeout");
}

/* -- aborting ------------------------------------------------------------- */

/* Fire the `abort` event: `onabort` first, then every registered listener in
 * registration order; `{ once: true }` entries are dropped after the call. */
static void xt_web_signal_fire(xt_value signal) {
  xt_value event = xt_object_new();
  xt_object_set(event, xt_string_from_cstr("type"), xt_string_from_cstr("abort"));
  xt_object_set(event, xt_string_from_cstr("target"), signal);
  xt_object_set(event, xt_string_from_cstr("currentTarget"), signal);

  xt_value handler = xt_object_get_cstr(signal, "__onabort");
  if (XT_IS_FUNCTION(handler)) xt_call_with_this(handler, signal, 1, &event);

  xt_value listeners = xt_web_signal_listeners(signal);
  int32_t count = xt_web_count(listeners);
  if (count == 0) return;
  /* Snapshot first: a listener may add or remove listeners. */
  xt_value snapshot = xt_array_new(0, NULL);
  for (int32_t index = 0; index < count; index++) {
    xt_array_push(snapshot, xt_array_get(listeners, xt_number((double)index)));
  }
  int32_t snapshotCount = xt_web_count(snapshot);
  for (int32_t index = 0; index < snapshotCount; index++) {
    xt_value entry = xt_array_get(snapshot, xt_number((double)index));
    if (!XT_IS_OBJECT(entry)) continue;
    xt_value fn = xt_object_get_cstr(entry, "fn");
    if (XT_IS_FUNCTION(fn)) xt_call_with_this(fn, signal, 1, &event);
  }
  /* Drop every `once` entry that fired. */
  int32_t remaining = 0;
  for (int32_t index = 0; index < count; index++) {
    xt_value entry = xt_array_get(listeners, xt_number((double)index));
    if (XT_IS_OBJECT(entry) && xt_truthy(xt_object_get_cstr(entry, "once"))) continue;
    if (remaining != index) {
      xt_array_set(listeners, xt_number((double)remaining), entry);
    }
    remaining++;
  }
  xt_as_array(listeners)->length = remaining;
}

static void xt_web_signal_abort(xt_value signal, xt_value reason) {
  if (xt_web_signal_is_aborted(signal)) return;
  xt_object_set(signal, xt_string_from_cstr("__aborted"), XT_TRUE);
  xt_object_set(signal, xt_string_from_cstr("__reason"), reason);
  xt_web_signal_fire(signal);
}

/* A signal that is not aborted yet, chained onto the signal prototype. */
static xt_value xt_web_signal_new(void) {
  xt_value signal = xt_object_new();
  xt_object_set_prototype(signal, xt_web_abort_signal_proto());
  xt_object_set(signal, xt_string_from_cstr("__aborted"), XT_FALSE);
  xt_object_set(signal, xt_string_from_cstr("__reason"), XT_UNDEFINED);
  xt_object_set(signal, xt_string_from_cstr("__listeners"), xt_array_new(0, NULL));
  return signal;
}

/* -- AbortSignal methods -------------------------------------------------- */

static xt_value xt_web_signal_add_listener(xt_value thisValue, xt_value env, int32_t argc,
                                           xt_value *argv) {
  (void)env;
  xt_value type = xt_arg(argc, argv, 0);
  xt_value listener = xt_arg(argc, argv, 1);
  xt_value options = xt_arg(argc, argv, 2);
  const char *name = XT_IS_STRING(type) ? xt_string_data(type) : NULL;
  if (!name || strcmp(name, "abort") != 0) return XT_UNDEFINED;
  if (!XT_IS_FUNCTION(listener)) return XT_UNDEFINED;
  /* Registering after the signal aborted never fires (same as the platform). */
  if (xt_web_signal_is_aborted(thisValue)) return XT_UNDEFINED;
  int once = 0;
  if (XT_IS_OBJECT(options)) once = xt_truthy(xt_object_get_cstr(options, "once"));
  xt_value entry = xt_object_new();
  xt_object_set(entry, xt_string_from_cstr("fn"), listener);
  xt_object_set(entry, xt_string_from_cstr("once"), once ? XT_TRUE : XT_FALSE);
  xt_array_push(xt_web_signal_listeners(thisValue), entry);
  return XT_UNDEFINED;
}

static xt_value xt_web_signal_remove_listener(xt_value thisValue, xt_value env, int32_t argc,
                                              xt_value *argv) {
  (void)env;
  xt_value type = xt_arg(argc, argv, 0);
  xt_value listener = xt_arg(argc, argv, 1);
  const char *name = XT_IS_STRING(type) ? xt_string_data(type) : NULL;
  if (!name || strcmp(name, "abort") != 0 || !XT_IS_FUNCTION(listener)) return XT_UNDEFINED;
  xt_value listeners = xt_web_signal_listeners(thisValue);
  int32_t count = xt_web_count(listeners);
  int32_t remaining = 0;
  for (int32_t index = 0; index < count; index++) {
    xt_value entry = xt_array_get(listeners, xt_number((double)index));
    xt_value fn = XT_IS_OBJECT(entry) ? xt_object_get_cstr(entry, "fn") : XT_UNDEFINED;
    if (fn == listener) continue;
    if (remaining != index) xt_array_set(listeners, xt_number((double)remaining), entry);
    remaining++;
  }
  xt_as_array(listeners)->length = remaining;
  return XT_UNDEFINED;
}

static xt_value xt_web_signal_throw_if_aborted(xt_value thisValue, xt_value env, int32_t argc,
                                               xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  if (!xt_web_signal_is_aborted(thisValue)) return XT_UNDEFINED;
  xt_throw(xt_object_get_cstr(thisValue, "__reason"));
  return XT_UNDEFINED;
}

static xt_value xt_web_signal_get_aborted(xt_value thisValue, xt_value env, int32_t argc,
                                          xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_web_signal_is_aborted(thisValue) ? XT_TRUE : XT_FALSE;
}

static xt_value xt_web_signal_get_reason(xt_value thisValue, xt_value env, int32_t argc,
                                         xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(thisValue, "__reason");
}

static xt_value xt_web_signal_get_onabort(xt_value thisValue, xt_value env, int32_t argc,
                                          xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(thisValue, "__onabort");
}

static xt_value xt_web_signal_set_onabort(xt_value thisValue, xt_value env, int32_t argc,
                                          xt_value *argv) {
  (void)env;
  xt_value handler = xt_arg(argc, argv, 0);
  if (!XT_IS_FUNCTION(handler) && handler != XT_NULL && handler != XT_UNDEFINED) return XT_UNDEFINED;
  xt_object_set(thisValue, xt_string_from_cstr("__onabort"),
                XT_IS_FUNCTION(handler) ? handler : XT_UNDEFINED);
  return XT_UNDEFINED;
}

/* -- AbortController methods ---------------------------------------------- */

static xt_value xt_web_controller_abort(xt_value thisValue, xt_value env, int32_t argc,
                                        xt_value *argv) {
  (void)env;
  xt_value signal = xt_object_get_cstr(thisValue, "signal");
  if (!XT_IS_OBJECT(signal)) return XT_UNDEFINED;
  xt_value reason = xt_arg(argc, argv, 0);
  if (reason == XT_UNDEFINED) reason = xt_web_abort_reason_default();
  xt_web_signal_abort(signal, reason);
  return XT_UNDEFINED;
}

/* -- prototypes ----------------------------------------------------------- */

xt_value xt_web_abort_signal_proto(void) {
  if (xt_web_abort_signal_proto_value != XT_UNDEFINED) return xt_web_abort_signal_proto_value;
  xt_value proto = xt_object_new();
  xt_web_abort_signal_proto_value = proto;
  xt_gc_add_root(&xt_web_abort_signal_proto_value);
  xt_web_abort_define(proto, "addEventListener", xt_web_signal_add_listener, 2);
  xt_web_abort_define(proto, "removeEventListener", xt_web_signal_remove_listener, 2);
  xt_web_abort_define(proto, "throwIfAborted", xt_web_signal_throw_if_aborted, 0);
  xt_web_abort_define_accessor(proto, "aborted", xt_web_signal_get_aborted, NULL);
  xt_web_abort_define_accessor(proto, "reason", xt_web_signal_get_reason, NULL);
  xt_web_abort_define_accessor(proto, "onabort", xt_web_signal_get_onabort,
                               xt_web_signal_set_onabort);
  return proto;
}

xt_value xt_web_abort_controller_proto(void) {
  if (xt_web_abort_controller_proto_value != XT_UNDEFINED) {
    return xt_web_abort_controller_proto_value;
  }
  xt_value proto = xt_object_new();
  xt_web_abort_controller_proto_value = proto;
  xt_gc_add_root(&xt_web_abort_controller_proto_value);
  xt_web_abort_define(proto, "abort", xt_web_controller_abort, 1);
  return proto;
}

/* -- constructors --------------------------------------------------------- */

xt_value xt_abort_signal_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  /* `new AbortSignal()` is illegal in the platform API. */
  xt_throw(xt_error_from_message("TypeError: Illegal constructor"));
  return XT_UNDEFINED;
}

xt_value xt_abort_controller_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value controller = xt_object_new();
  xt_object_set_prototype(controller, xt_web_abort_controller_proto());
  xt_object_set(controller, xt_string_from_cstr("signal"), xt_web_signal_new());
  return controller;
}

/* -- statics -------------------------------------------------------------- */

static xt_value xt_web_signal_aborted(int32_t argc, xt_value *argv) {
  xt_value signal = xt_web_signal_new();
  xt_value reason = xt_arg(argc, argv, 0);
  if (reason == XT_UNDEFINED) reason = xt_web_abort_reason_default();
  xt_web_signal_abort(signal, reason);
  return signal;
}

static xt_value xt_web_signal_timeout(int32_t argc, xt_value *argv) {
  double milliseconds = xt_to_number(xt_arg(argc, argv, 0));
  if (!(milliseconds >= 0)) milliseconds = 0;
  xt_value signal = xt_web_signal_new();
  xt_value environment[1] = {signal};
  xt_value callback = xt_closure_new((void *)xt_web_timeout_fire, 1, environment);
  xt_value arguments[2] = {callback, xt_number(milliseconds)};
  xt_value handle = xt_set_timeout(2, arguments);
  xt_object_set(signal, xt_string_from_cstr("__timer"), handle);
  return signal;
}

static xt_value xt_web_timeout_fire(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value signal = xt_closure_env(env, 0);
  xt_web_signal_abort(signal, xt_web_timeout_reason());
  return XT_UNDEFINED;
}

/* `AbortSignal.any(signals)`: abort with the first input's reason. */
static xt_value xt_web_signal_any(int32_t argc, xt_value *argv) {
  xt_value inputs = xt_arg(argc, argv, 0);
  xt_value combined = xt_web_signal_new();
  if (!XT_IS_ARRAY(inputs)) return combined;
  int32_t count = xt_web_count(inputs);
  for (int32_t index = 0; index < count; index++) {
    xt_value input = xt_array_get(inputs, xt_number((double)index));
    if (!XT_IS_OBJECT(input)) continue;
    if (xt_web_signal_is_aborted(input)) {
      xt_web_signal_abort(combined, xt_object_get_cstr(input, "__reason"));
      return combined;
    }
    xt_value environment[2] = {combined, input};
    xt_value closure = xt_closure_new((void *)xt_web_chain_fire, 2, environment);
    xt_value entry = xt_object_new();
    xt_object_set(entry, xt_string_from_cstr("fn"), closure);
    xt_object_set(entry, xt_string_from_cstr("once"), XT_TRUE);
    xt_array_push(xt_web_signal_listeners(input), entry);
  }
  return combined;
}

static xt_value xt_web_chain_fire(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value combined = xt_closure_env(env, 0);
  xt_value input = xt_closure_env(env, 1);
  xt_web_signal_abort(combined, xt_object_get_cstr(input, "__reason"));
  return XT_UNDEFINED;
}

/* `AbortSignal.<method>(...)`: the code generator's static dispatcher. */
xt_value xt_abort_signal_static(xt_value method, int32_t argc, xt_value *argv) {
  const char *name = XT_IS_STRING(method) ? xt_string_data(method) : NULL;
  if (!name) return XT_UNDEFINED;
  if (strcmp(name, "abort") == 0) return xt_web_signal_aborted(argc, argv);
  if (strcmp(name, "timeout") == 0) return xt_web_signal_timeout(argc, argv);
  if (strcmp(name, "any") == 0) return xt_web_signal_any(argc, argv);
  return XT_UNDEFINED;
}
