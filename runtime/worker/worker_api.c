/*
 * xbintsc runtime — shared JavaScript surface for the worker APIs.
 *
 * One worker object serves both platforms:
 *
 *   Node `worker_threads`            Web Worker
 *   -------------------------------  --------------------------------------
 *   worker.on("message", fn)         worker.onmessage = fn
 *   worker.postMessage(value)        worker.postMessage(value)
 *   worker.terminate(): Promise      worker.terminate()
 *   worker.threadId                  worker.addEventListener("message", fn)
 *
 * Node listeners receive the raw value; `onmessage` / `addEventListener`
 * listeners receive a `MessageEvent`-shaped object (`{ type, data, target }`).
 * Both kinds are stored in the same emitter, which is why a program may mix
 * them. The scope object (`self` in a worker, `parentPort` in Node) is the same
 * shape, so a worker may answer through either convention.
 */

#include "worker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- object helpers ------------------------------------------------------- */

static xt_value xt_wrk_str(const char *text) { return xt_string_from_cstr(text); }

static void xt_wrk_put(xt_value object, const char *key, xt_value value) {
  xt_object_set(object, xt_wrk_str(key), value);
}

static xt_value xt_wrk_fetch(xt_value object, const char *key) {
  return xt_object_get_cstr(object, key);
}

static const char *xt_wrk_text(xt_value value) {
  return XT_IS_STRING(value) ? xt_string_data(value) : NULL;
}

static void xt_wrk_method(xt_value object, const char *name, void *fn) {
  xt_wrk_put(object, name, xt_closure_new(fn, 0, NULL));
}

static void xt_wrk_accessor(xt_value object, const char *name, void *getter, void *setter) {
  xt_object_define_getter(object, xt_wrk_str(name), xt_closure_new(getter, 0, NULL));
  if (setter) xt_object_define_setter(object, xt_wrk_str(name), xt_closure_new(setter, 0, NULL));
}

/* The link pointer travels in a hidden property. Its tag bits are 0x0000 (no
 * value tag), so the collector treats it as a number and skips it. */
static xt_wrk_link *xt_wrk_owner(xt_value object) {
  return (xt_wrk_link *)(uintptr_t)xt_wrk_fetch(object, "__xt_link");
}

static void xt_wrk_attach(xt_value object, xt_wrk_link *link) {
  xt_object_set(object, xt_wrk_str("__xt_link"), (xt_value)(uintptr_t)link);
}

/* -- EventEmitter methods ------------------------------------------------- */

static xt_value xt_wrk_on_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_wrk_text(xt_arg_at(argc, argv, 0));
  if (event) xt_wrk_on(self, event, xt_arg_at(argc, argv, 1), 0);
  return self;
}

static xt_value xt_wrk_once_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_wrk_text(xt_arg_at(argc, argv, 0));
  if (event) xt_wrk_on(self, event, xt_arg_at(argc, argv, 1), 1);
  return self;
}

static xt_value xt_wrk_off_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_wrk_text(xt_arg_at(argc, argv, 0));
  if (event) xt_wrk_off(self, event, xt_arg_at(argc, argv, 1));
  return self;
}

static xt_value xt_wrk_remove_all_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_wrk_text(xt_arg_at(argc, argv, 0));
  if (event) {
    xt_wrk_remove_all(self, event);
  } else {
    xt_value names = xt_object_keys(xt_wrk_fetch(self, "__xt_events"));
    xt_array *list = xt_as_array(names);
    if (list) {
      for (uint32_t i = 0; i < list->length; i++) {
        const char *name = xt_wrk_text(list->items[i]);
        if (name) xt_wrk_remove_all(self, name);
      }
    }
  }
  return self;
}

static xt_value xt_wrk_emit_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *event = xt_wrk_text(xt_arg_at(argc, argv, 0));
  if (!event) return XT_FALSE;
  xt_wrk_emit(self, event, argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : NULL);
  return XT_TRUE;
}

void xt_wrk_install_emitter(xt_value object) {
  xt_wrk_method(object, "on", (void *)xt_wrk_on_method);
  xt_wrk_method(object, "addListener", (void *)xt_wrk_on_method);
  xt_wrk_method(object, "once", (void *)xt_wrk_once_method);
  xt_wrk_method(object, "off", (void *)xt_wrk_off_method);
  xt_wrk_method(object, "removeListener", (void *)xt_wrk_off_method);
  xt_wrk_method(object, "removeAllListeners", (void *)xt_wrk_remove_all_method);
  xt_wrk_method(object, "emit", (void *)xt_wrk_emit_method);
}

/* -- MessageEvent wrappers ------------------------------------------------ */

static xt_value xt_wrk_event_object(const char *type, xt_value target) {
  xt_value event = xt_object_new();
  xt_wrk_put(event, "type", xt_wrk_str(type));
  xt_wrk_put(event, "target", target);
  xt_wrk_put(event, "currentTarget", target);
  return event;
}

static xt_value xt_wrk_message_call(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  xt_value handler = xt_closure_env(env, 0);
  xt_value event = xt_wrk_event_object("message", self);
  xt_wrk_put(event, "data", xt_arg_at(argc, argv, 0));
  return xt_call_with_this(handler, self, 1, &event);
}

static xt_value xt_wrk_error_call(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  xt_value handler = xt_closure_env(env, 0);
  xt_value error = xt_arg_at(argc, argv, 0);
  xt_value event = xt_wrk_event_object("error", self);
  xt_wrk_put(event, "error", error);
  xt_wrk_put(event, "message", xt_to_string(error));
  return xt_call_with_this(handler, self, 1, &event);
}

/* A wrapper is installed instead of the handler so the browser listener sees a
 * MessageEvent while a Node listener on the same object still sees the value. */
static void xt_wrk_listen_dom(xt_value self, const char *type, xt_value handler) {
  if (!XT_IS_FUNCTION(handler) || !type) return;
  void *entry = strcmp(type, "message") == 0 ? (void *)xt_wrk_message_call : (void *)xt_wrk_error_call;
  xt_value environment[1] = {handler};
  xt_value wrapper = xt_closure_new(entry, 1, environment);
  xt_wrk_put(wrapper, "__xt_original", handler);
  xt_wrk_on(self, type, wrapper, 0);
}

static xt_value xt_wrk_add_listener(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *type = xt_wrk_text(xt_arg_at(argc, argv, 0));
  xt_value handler = xt_arg_at(argc, argv, 1);
  if (type && XT_IS_FUNCTION(handler)) {
    if (strcmp(type, "message") == 0 || strcmp(type, "error") == 0 ||
        strcmp(type, "messageerror") == 0) {
      xt_wrk_listen_dom(self, type, handler);
    } else {
      xt_wrk_on(self, type, handler, 0);
    }
  }
  return XT_UNDEFINED;
}

static xt_value xt_wrk_remove_listener(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *type = xt_wrk_text(xt_arg_at(argc, argv, 0));
  if (type) xt_wrk_off(self, type, xt_arg_at(argc, argv, 1));
  return XT_UNDEFINED;
}

static xt_value xt_wrk_dispatch(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value event = xt_arg_at(argc, argv, 0);
  const char *type = XT_IS_OBJECT(event) ? xt_wrk_text(xt_wrk_fetch(event, "type")) : NULL;
  if (type) xt_wrk_emit(self, type, 1, &event);
  return XT_TRUE;
}

/* `onmessage = fn`: an accessor rather than a data property, because assigning
 * it must register (or replace) a listener. */
static xt_value xt_wrk_on_accessor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_wrk_fetch(self, "__xt_on_message");
}

static xt_value xt_wrk_on_setter(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value handler = xt_arg_at(argc, argv, 0);
  xt_value previous = xt_wrk_fetch(self, "__xt_on_message");
  if (XT_IS_FUNCTION(previous)) xt_wrk_off(self, "message", previous);
  xt_wrk_put(self, "__xt_on_message", handler);
  if (XT_IS_FUNCTION(handler)) xt_wrk_listen_dom(self, "message", handler);
  return handler;
}

static xt_value xt_wrk_onerror_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_wrk_fetch(self, "__xt_on_error");
}

static xt_value xt_wrk_onerror_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value handler = xt_arg_at(argc, argv, 0);
  xt_value previous = xt_wrk_fetch(self, "__xt_on_error");
  if (XT_IS_FUNCTION(previous)) xt_wrk_off(self, "error", previous);
  xt_wrk_put(self, "__xt_on_error", handler);
  if (XT_IS_FUNCTION(handler)) xt_wrk_listen_dom(self, "error", handler);
  return handler;
}

void xt_wrk_install_dom_events(xt_value object) {
  xt_wrk_method(object, "addEventListener", (void *)xt_wrk_add_listener);
  xt_wrk_method(object, "removeEventListener", (void *)xt_wrk_remove_listener);
  xt_wrk_method(object, "dispatchEvent", (void *)xt_wrk_dispatch);
  xt_wrk_accessor(object, "onmessage", (void *)xt_wrk_on_accessor, (void *)xt_wrk_on_setter);
  xt_wrk_accessor(object, "onerror", (void *)xt_wrk_onerror_get, (void *)xt_wrk_onerror_set);
  xt_wrk_accessor(object, "onmessageerror", (void *)xt_wrk_onerror_get,
                  (void *)xt_wrk_onerror_set);
}

/* -- worker object -------------------------------------------------------- */

static xt_value xt_wrk_worker_post(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_wrk_link *link = xt_wrk_owner(self);
  xt_wrk_post(link, xt_arg_at(argc, argv, 0), argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : NULL);
  return XT_UNDEFINED;
}

static xt_value xt_wrk_worker_terminate(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_wrk_link *link = xt_wrk_owner(self);
  if (!link) return XT_UNDEFINED;
  return xt_wrk_terminate_promise(link);
}

static xt_value xt_wrk_worker_ref(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_wrk_ref(xt_wrk_owner(self));
  return self;
}

static xt_value xt_wrk_worker_unref(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_wrk_unref(xt_wrk_owner(self));
  return self;
}

static void xt_wrk_install_worker_object(xt_value object) {
  xt_wrk_put(object, "__xt_worker", XT_TRUE);
  xt_wrk_install_emitter(object);
  xt_wrk_install_dom_events(object);
  xt_wrk_method(object, "postMessage", (void *)xt_wrk_worker_post);
  xt_wrk_method(object, "terminate", (void *)xt_wrk_worker_terminate);
  xt_wrk_method(object, "ref", (void *)xt_wrk_worker_ref);
  xt_wrk_method(object, "unref", (void *)xt_wrk_worker_unref);
}

xt_value xt_wrk_new_worker(int32_t argc, xt_value *argv, int web_mode) {
  xt_value script = xt_arg_at(argc, argv, 0);
  xt_wrk_require_self_script(script);
  xt_value options = xt_arg_at(argc, argv, 1);
  xt_value data = XT_UNDEFINED;
  const char *name = "";
  if (XT_IS_OBJECT(options)) {
    data = xt_wrk_fetch(options, "workerData");
    xt_value name_value = xt_wrk_fetch(options, "name");
    if (XT_IS_STRING(name_value)) name = xt_string_data(name_value);
  }
  xt_value object = xt_object_new();
  xt_wrk_install_worker_object(object);
  char *error = NULL;
  xt_wrk_link *link = xt_wrk_spawn(xt_wrk_program_path(), data, name, web_mode, object, &error);
  if (!link) {
    char message[512];
    snprintf(message, sizeof(message), "Error: %s", error ? error : "failed to start worker");
    free(error);
    xt_throw(xt_error_from_message(message));
    return XT_UNDEFINED;
  }
  xt_wrk_attach(object, link);
  xt_wrk_put(object, "threadId", xt_number(xt_wrk_thread_id(link)));
  return object;
}

/* -- scope object (`self` in a worker, `parentPort` in Node) --------------- */

static xt_value xt_wrk_scope_post_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_wrk_scope_post(xt_arg_at(argc, argv, 0));
  (void)self;
  return XT_UNDEFINED;
}

static xt_value xt_wrk_scope_close_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  (void)self;
  xt_wrk_scope_close();
  return XT_UNDEFINED;
}

static xt_value xt_wrk_scope_start_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  (void)self;
  xt_wrk_scope_listen();
  return XT_UNDEFINED;
}

static void xt_wrk_install_scope_object(xt_value object) {
  if (xt_truthy(xt_wrk_fetch(object, "__xt_scope_api"))) return;
  xt_wrk_put(object, "__xt_scope_api", XT_TRUE);
  xt_wrk_install_emitter(object);
  xt_wrk_install_dom_events(object);
  xt_wrk_method(object, "postMessage", (void *)xt_wrk_scope_post_method);
  xt_wrk_method(object, "close", (void *)xt_wrk_scope_close_method);
  xt_wrk_method(object, "start", (void *)xt_wrk_scope_start_method);
  xt_wrk_method(object, "ref", (void *)xt_wrk_worker_ref);
  xt_wrk_method(object, "unref", (void *)xt_wrk_worker_unref);
  xt_wrk_put(object, "name", xt_wrk_str(xt_wrk_scope_name()));
}

xt_value xt_wrk_scope_api_object(void) {
  xt_value object = xt_wrk_scope_object();
  xt_wrk_install_scope_object(object);
  return object;
}

/* -- Web Worker globals --------------------------------------------------- */

/* A global value is read as a function value; `new Worker(...)` then goes
 * through the ordinary `xt_new`. Cache the constructor so `Worker` keeps its
 * identity across reads. */
static xt_value xt_webworker_worker_value = XT_UNDEFINED;

static xt_value xt_webworker_worker_ctor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return xt_wrk_new_worker(argc, argv, 1);
}

xt_value xt_webworker_worker(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (!XT_IS_FUNCTION(xt_webworker_worker_value)) {
    xt_webworker_worker_value = xt_closure_new((void *)xt_webworker_worker_ctor, 0, NULL);
    xt_gc_add_root(&xt_webworker_worker_value);
  }
  return xt_webworker_worker_value;
}

xt_value xt_webworker_worker_new(int32_t argc, xt_value *argv) {
  return xt_wrk_new_worker(argc, argv, 1);
}

xt_value xt_webworker_self(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_wrk_scope_api_object();
}

xt_value xt_webworker_is_main_thread(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_bool(xt_wrk_is_main_thread());
}

xt_value xt_webworker_post_message(int32_t argc, xt_value *argv) {
  xt_wrk_scope_post(xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

/* -- node:worker_threads exports ------------------------------------------ */

xt_value xt_wt_worker(int32_t argc, xt_value *argv) { return xt_wrk_new_worker(argc, argv, 0); }

xt_value xt_wt_is_main_thread(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_bool(xt_wrk_is_main_thread());
}

xt_value xt_wt_worker_data(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (xt_wrk_is_main_thread()) return XT_NULL;
  return xt_wrk_worker_data();
}

xt_value xt_wt_parent_port(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (xt_wrk_is_main_thread()) return XT_NULL;
  return xt_wrk_scope_api_object();
}

xt_value xt_wt_mark_as_uncloneable(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}
