/*
 * Node.js `events` module for xbintsc.
 *
 * Provides a standalone `EventEmitter` class backed by the shared emitter in
 * `../node_common.h` (listeners live in an `__xt_events` property and events
 * emitted before a listener exists are buffered until one attaches). On top of
 * the base surface this module adds Node's richer API: `once` with real
 * "remove after firing" semantics, `prependListener` / `prependOnceListener`,
 * `listeners` / `rawListeners`, `listenerCount`, `eventNames`,
 * `removeAllListeners` and the max-listener bookkeeping.
 *
 * `EventEmitter` is exposed both as a global constructor and as a named export
 * of `import { EventEmitter } from "events"`.
 */

#include "../node_common.h"

static xt_value xt_events_proto(void);

xt_value xt_event_emitter_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_object_new_with_proto(xt_events_proto());
}

/* -- listener queries ----------------------------------------------------- */

static xt_value xt_events_method_listeners(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  xt_value result = xt_array_new(0, NULL);
  if (!event) return result;
  xt_value array = xt_node_listeners(thisValue, event, 0);
  if (XT_IS_ARRAY(array)) {
    int32_t count = (int32_t)xt_to_number(xt_array_length(array));
    for (int32_t i = 0; i < count; i++) xt_array_push(result, xt_array_get(array, xt_number((double)i)));
  }
  return result;
}

static xt_value xt_events_method_listener_count(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  if (!event) return xt_number(0);
  xt_value array = xt_node_listeners(thisValue, event, 0);
  if (!XT_IS_ARRAY(array)) return xt_number(0);
  return xt_number(xt_to_number(xt_array_length(array)));
}

static xt_value xt_events_method_event_names(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value result = xt_array_new(0, NULL);
  xt_value map = xt_node_event_map(thisValue, 0);
  if (!XT_IS_OBJECT(map)) return result;
  xt_value keys = xt_object_keys(map);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *event = xt_string_data(xt_to_string(key));
    xt_value listeners = xt_node_listeners(thisValue, event, 0);
    if (XT_IS_ARRAY(listeners) && xt_to_number(xt_array_length(listeners)) > 0) xt_array_push(result, key);
  }
  return result;
}

static xt_value xt_events_method_remove_all(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value event = xt_arg(argc, argv, 0);
  if (argc == 0 || xt_truthy(xt_is_nullish(event))) {
    xt_node_set(thisValue, "__xt_events", xt_object_new());
    xt_node_set(thisValue, "__xt_pending", xt_object_new());
    return thisValue;
  }
  const char *name = xt_node_cstr(event);
  if (name) {
    xt_value map = xt_node_event_map(thisValue, 1);
    xt_object_set(map, xt_string_from_cstr(name), xt_array_new(0, NULL));
  }
  return thisValue;
}

/* -- max listeners -------------------------------------------------------- */

static xt_value xt_events_method_set_max(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_node_set(thisValue, "__max_listeners", xt_arg(argc, argv, 0));
  return thisValue;
}

static xt_value xt_events_method_get_max(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value value = xt_object_get_cstr(thisValue, "__max_listeners");
  if (xt_truthy(xt_is_nullish(value))) return xt_number(10);
  return value;
}

/* -- prepend -------------------------------------------------------------- */

static void xt_events_prepend(xt_value target, const char *event, xt_value fn) {
  xt_value existing = xt_node_listeners(target, event, 1);
  xt_value rebuilt = xt_array_new(1, &fn);
  int32_t count = (int32_t)xt_to_number(xt_array_length(existing));
  for (int32_t i = 0; i < count; i++) xt_array_push(rebuilt, xt_array_get(existing, xt_number((double)i)));
  xt_value map = xt_node_event_map(target, 1);
  xt_object_set(map, xt_string_from_cstr(event), rebuilt);
}

static xt_value xt_events_method_prepend(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  xt_value fn = xt_arg(argc, argv, 1);
  if (event && XT_IS_FUNCTION(fn)) {
    xt_events_prepend(thisValue, event, fn);
    xt_node_flush(thisValue, event);
  }
  return thisValue;
}

/* -- once ----------------------------------------------------------------- */

/* Drop the once-wrapper carrying `token` from `event`'s listeners. */
static void xt_events_remove_token(xt_value target, const char *event, xt_value token) {
  xt_value array = xt_node_listeners(target, event, 0);
  if (!XT_IS_ARRAY(array)) return;
  xt_value kept = xt_array_new(0, NULL);
  int32_t count = (int32_t)xt_to_number(xt_array_length(array));
  for (int32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(array, xt_number((double)i));
    if (xt_node_get(item, "__xt_once_token") == token) continue;
    xt_array_push(kept, item);
  }
  xt_value map = xt_node_event_map(target, 1);
  xt_object_set(map, xt_string_from_cstr(event), kept);
}

static xt_value xt_events_once_callback(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value target = xt_closure_env(env, 0);
  xt_value original = xt_closure_env(env, 1);
  xt_value eventValue = xt_closure_env(env, 2);
  xt_value token = xt_closure_env(env, 3);
  const char *event = xt_string_data(xt_to_string(eventValue));
  if (event) xt_events_remove_token(target, event, token);
  if (XT_IS_FUNCTION(original)) return xt_call_with_this(original, target, argc, argv);
  return xt_undefined();
}

/* Register `fn` so it fires at most once; `prepend` controls its position.
   The wrapper identifies itself for removal through a shared marker object,
   since a closure cannot hold a reference to itself. */
static xt_value xt_events_once_impl(xt_value target, const char *event, xt_value fn, int prepend) {
  xt_value eventValue = xt_string_from_cstr(event);
  xt_value token = xt_object_new();
  xt_value env[4] = {target, fn, eventValue, token};
  xt_value wrapper = xt_closure_new((void *)xt_events_once_callback, 4, env);
  xt_node_set(wrapper, "__xt_once_token", token);
  if (prepend) {
    xt_events_prepend(target, event, wrapper);
  } else {
    xt_value array = xt_node_listeners(target, event, 1);
    xt_array_push(array, wrapper);
  }
  xt_node_flush(target, event);
  return target;
}

static xt_value xt_events_method_once(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  xt_value fn = xt_arg(argc, argv, 1);
  if (event && XT_IS_FUNCTION(fn)) xt_events_once_impl(thisValue, event, fn, 0);
  return thisValue;
}

static xt_value xt_events_method_prepend_once(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  xt_value fn = xt_arg(argc, argv, 1);
  if (event && XT_IS_FUNCTION(fn)) xt_events_once_impl(thisValue, event, fn, 1);
  return thisValue;
}

/* -- statics -------------------------------------------------------------- */

/* `events.once(emitter, name)` returns a Promise that resolves on the next
   emission of `name`. */
static xt_value xt_events_once_promise_resolve(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value resolve = xt_closure_env(env, 0);
  xt_value value = xt_arg(argc, argv, 0);
  if (XT_IS_FUNCTION(resolve)) xt_closure_call(resolve, 1, &value);
  return xt_undefined();
}

static xt_value xt_events_once_promise_executor(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value emitter = xt_closure_env(env, 0);
  xt_value event = xt_closure_env(env, 1);
  xt_value resolve = xt_arg(argc, argv, 0);
  xt_value resolveEnv[1] = {resolve};
  xt_value listener = xt_closure_new((void *)xt_events_once_promise_resolve, 1, resolveEnv);
  const char *name = xt_string_data(xt_to_string(event));
  if (name) xt_events_once_impl(emitter, name, listener, 0);
  return xt_undefined();
}

/* `EventEmitter.listenerCount(emitter, event)` etc. */
xt_value xt_events_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "listenerCount") == 0) {
    xt_value emitter = xt_arg(argc, argv, 0);
    const char *event = xt_node_cstr(xt_arg(argc, argv, 1));
    if (!event) return xt_number(0);
    xt_value array = xt_node_listeners(emitter, event, 0);
    if (!XT_IS_ARRAY(array)) return xt_number(0);
    return xt_number(xt_to_number(xt_array_length(array)));
  }
  if (strcmp(fn, "getEventListeners") == 0) {
    xt_value emitter = xt_arg(argc, argv, 0);
    xt_value result = xt_array_new(0, NULL);
    const char *event = xt_node_cstr(xt_arg(argc, argv, 1));
    if (!event) return result;
    xt_value array = xt_node_listeners(emitter, event, 0);
    if (XT_IS_ARRAY(array)) {
      int32_t count = (int32_t)xt_to_number(xt_array_length(array));
      for (int32_t i = 0; i < count; i++) xt_array_push(result, xt_array_get(array, xt_number((double)i)));
    }
    return result;
  }
  if (strcmp(fn, "getMaxListeners") == 0) {
    xt_value emitter = xt_arg(argc, argv, 0);
    xt_value value = xt_object_get_cstr(emitter, "__max_listeners");
    if (xt_truthy(xt_is_nullish(value))) return xt_number(10);
    return value;
  }
  if (strcmp(fn, "setMaxListeners") == 0) {
    xt_value emitter = xt_arg(argc, argv, 0);
    xt_node_set(emitter, "__max_listeners", xt_arg(argc, argv, 1));
    return emitter;
  }
  if (strcmp(fn, "once") == 0) {
    xt_value emitter = xt_arg(argc, argv, 0);
    xt_value event = xt_arg(argc, argv, 1);
    xt_value env[2] = {emitter, event};
    xt_value executor = xt_closure_new((void *)xt_events_once_promise_executor, 2, env);
    xt_value ctorArgs[1] = {executor};
    return xt_promise_ctor(1, ctorArgs);
  }
  if (strcmp(fn, "addAbortListener") == 0) {
    xt_value signal = xt_arg(argc, argv, 0);
    xt_value listener = xt_arg(argc, argv, 1);
    if (XT_IS_OBJECT(signal)) {
      xt_value array = xt_node_listeners(signal, "abort", 1);
      if (XT_IS_FUNCTION(listener)) xt_array_push(array, listener);
    }
    return XT_UNDEFINED;
  }
  return xt_undefined();
}

/* -- prototype ------------------------------------------------------------ */

static xt_value xt_events_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  /* Override the default emitter surface with the fuller `events` behaviour. */
  xt_node_define_method(proto, "once", (void *)xt_events_method_once);
  xt_node_define_method(proto, "prependListener", (void *)xt_events_method_prepend);
  xt_node_define_method(proto, "prependOnceListener", (void *)xt_events_method_prepend_once);
  xt_node_define_method(proto, "listeners", (void *)xt_events_method_listeners);
  xt_node_define_method(proto, "rawListeners", (void *)xt_events_method_listeners);
  xt_node_define_method(proto, "listenerCount", (void *)xt_events_method_listener_count);
  xt_node_define_method(proto, "eventNames", (void *)xt_events_method_event_names);
  xt_node_define_method(proto, "removeAllListeners", (void *)xt_events_method_remove_all);
  xt_node_define_method(proto, "setMaxListeners", (void *)xt_events_method_set_max);
  xt_node_define_method(proto, "getMaxListeners", (void *)xt_events_method_get_max);
  return proto;
}
