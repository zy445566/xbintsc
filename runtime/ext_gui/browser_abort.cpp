/*
 * xbintsc GUI engine — `AbortController` / `AbortSignal`.
 *
 * The pair exists so `addEventListener(..., { signal })` can actually remove a
 * listener, as the DOM specifies. The engine has no Web-IDL machinery, so both
 * objects are plain runtime objects with a shared hidden state:
 *
 *   controller.__xt_gui_abort_state   the state object
 *   controller.__xt_gui_abort_signal  the signal handed out by `.signal`
 *   signal.__xt_gui_abort_state       the same state object
 *
 * The state holds `aborted`, `reason` and the 'abort' listener list. Every value
 * lives in a runtime object reachable from the handle the page kept, so the
 * mark-sweep collector sees the whole graph.
 */

#include "dom_api_internal.h"
#include "listener_options.h"

namespace domapi {

xt_value abort_signal_proto();
xt_value abort_controller_proto();

namespace {

xt_value new_state() {
  xt_value state = xt_object_new();
  xt_object_set(state, xt_string_from_cstr("aborted"), XT_FALSE);
  xt_object_set(state, xt_string_from_cstr("reason"), XT_UNDEFINED);
  xt_object_set(state, xt_string_from_cstr("listeners"), xt_array_new(0, nullptr));
  return state;
}

xt_value state_of(xt_value self) { return xt_object_get_cstr(self, "__xt_gui_abort_state"); }

xt_value signal_of(xt_value self) { return xt_object_get_cstr(self, "__xt_gui_abort_signal"); }

bool state_aborted(xt_value state) {
  return XT_IS_OBJECT(state) && value_truthy(xt_object_get_cstr(state, "aborted"));
}

xt_value abort_signal_from_state(xt_value state, xt_value controller) {
  xt_value signal = xt_object_new_with_proto(abort_signal_proto());
  xt_object_set(signal, xt_string_from_cstr("__xt_gui_abort_state"), state);
  if (!XT_IS_UNDEFINED(controller)) {
    xt_object_set(signal, xt_string_from_cstr("__xt_gui_abort_controller"), controller);
  }
  return signal;
}

/** Call `fn` (or `{ handleEvent }`) with an `{ type, target }` event. */
void call_abort_listener(xt_value fn, xt_value signal) {
  xt_value event = xt_object_new_with_proto(event_proto());
  xt_object_set(event, xt_string_from_cstr("type"), xt_string_from_cstr("abort"));
  xt_object_set(event, xt_string_from_cstr("target"), signal);
  xt_object_set(event, xt_string_from_cstr("currentTarget"), signal);
  xt_object_set(event, xt_string_from_cstr("defaultPrevented"), XT_FALSE);
  if (XT_IS_FUNCTION(fn)) {
    xt_value args[1] = {event};
    xt_call_with_this(fn, signal, 1, args);
    return;
  }
  if (XT_IS_OBJECT(fn)) {
    xt_value handler = xt_object_get_cstr(fn, "handleEvent");
    if (XT_IS_FUNCTION(handler)) {
      xt_value args[1] = {event};
      xt_call_with_this(handler, fn, 1, args);
    }
  }
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(signal_aborted_get) {
  (void)env;
  (void)argc;
  (void)argv;
  return state_aborted(state_of(self)) ? XT_TRUE : XT_FALSE;
}

METHOD(signal_reason_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = state_of(self);
  if (!XT_IS_OBJECT(state)) return XT_UNDEFINED;
  return xt_object_get_cstr(state, "reason");
}

METHOD(signal_onabort_get) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_gui_onabort");
}

METHOD(signal_onabort_set) {
  (void)env;
  if (argc >= 1) xt_object_set(self, xt_string_from_cstr("__xt_gui_onabort"), xt_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

/** `signal.addEventListener("abort", fn, options?)` — only 'abort' exists. */
METHOD(signal_add_listener) {
  (void)env;
  xt_value state = state_of(self);
  if (!XT_IS_OBJECT(state) || argc < 2) return XT_UNDEFINED;
  std::string type = to_string_value(xt_arg(argc, argv, 0));
  if (type != "abort") return XT_UNDEFINED;
  xt_value fn = xt_arg(argc, argv, 1);
  if (!XT_IS_FUNCTION(fn) && !XT_IS_OBJECT(fn)) return XT_UNDEFINED;
  XtGuiListenerOptions options = xt_gui_listener_options(xt_arg(argc, argv, 2));
  xt_value entry = xt_object_new();
  xt_object_set(entry, xt_string_from_cstr("fn"), fn);
  xt_object_set(entry, xt_string_from_cstr("once"), options.once ? XT_TRUE : XT_FALSE);
  xt_array_push(xt_object_get_cstr(state, "listeners"), entry);
  return XT_UNDEFINED;
}

METHOD(signal_remove_listener) {
  (void)env;
  xt_value state = state_of(self);
  if (!XT_IS_OBJECT(state) || argc < 2) return XT_UNDEFINED;
  xt_value listeners = xt_object_get_cstr(state, "listeners");
  if (!XT_IS_ARRAY(listeners)) return XT_UNDEFINED;
  xt_value fn = xt_arg(argc, argv, 1);
  xt_value kept = xt_array_new(0, nullptr);
  int32_t count = (int32_t)xt_to_number(xt_array_length(listeners));
  for (int32_t i = 0; i < count; i++) {
    xt_value entry = xt_array_get(listeners, xt_number((double)i));
    if (!xt_truthy(xt_seq(xt_object_get_cstr(entry, "fn"), fn))) xt_array_push(kept, entry);
  }
  xt_object_set(state, xt_string_from_cstr("listeners"), kept);
  return XT_UNDEFINED;
}

/** `signal.throwIfAborted()`: raise the abort reason (an `Error` by default). */
METHOD(signal_throw_if_aborted) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = state_of(self);
  if (!state_aborted(state)) return XT_UNDEFINED;
  xt_value reason = xt_object_get_cstr(state, "reason");
  if (XT_IS_UNDEFINED(reason) || reason == XT_NULL) {
    reason = xt_error_ctor_by_name(xt_string_from_cstr("Error"), 0, nullptr);
  }
  xt_throw(reason);
  return XT_UNDEFINED;
}

METHOD(controller_signal_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value signal = signal_of(self);
  if (XT_IS_OBJECT(signal)) return signal;
  xt_value state = state_of(self);
  if (!XT_IS_OBJECT(state)) {
    state = new_state();
    xt_object_set(self, xt_string_from_cstr("__xt_gui_abort_state"), state);
  }
  signal = abort_signal_from_state(state, self);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_abort_signal"), signal);
  return signal;
}

/** `controller.abort(reason)`: flip the state and dispatch 'abort' once. */
METHOD(controller_abort) {
  (void)env;
  xt_value state = state_of(self);
  if (!XT_IS_OBJECT(state) || state_aborted(state)) return XT_UNDEFINED;
  xt_value reason = argc >= 1 ? xt_arg(argc, argv, 0) : XT_UNDEFINED;
  if (XT_IS_UNDEFINED(reason)) {
    xt_value message = xt_string_from_cstr("This operation was aborted");
    reason = xt_error_ctor_by_name(xt_string_from_cstr("Error"), 1, &message);
  }
  xt_object_set(state, xt_string_from_cstr("aborted"), XT_TRUE);
  xt_object_set(state, xt_string_from_cstr("reason"), reason);
  xt_value signal = signal_of(self);
  if (!XT_IS_OBJECT(signal)) signal = controller_signal_get(self, XT_UNDEFINED, 0, nullptr);
  xt_value onabort = xt_object_get_cstr(signal, "__xt_gui_onabort");
  if (XT_IS_FUNCTION(onabort)) call_abort_listener(onabort, signal);
  xt_value listeners = xt_object_get_cstr(state, "listeners");
  int32_t count = XT_IS_ARRAY(listeners) ? (int32_t)xt_to_number(xt_array_length(listeners)) : 0;
  xt_value snapshot = xt_array_new(0, nullptr);
  for (int32_t i = 0; i < count; i++) {
    xt_array_push(snapshot, xt_array_get(listeners, xt_number((double)i)));
  }
  xt_object_set(state, xt_string_from_cstr("listeners"), xt_array_new(0, nullptr));
  for (int32_t i = 0; i < count; i++) {
    xt_value entry = xt_array_get(snapshot, xt_number((double)i));
    call_abort_listener(xt_object_get_cstr(entry, "fn"), signal);
  }
  return XT_UNDEFINED;
}

#undef METHOD

}  // namespace

xt_value abort_signal_proto() {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("aborted"),
                          xt_closure_new((void *)signal_aborted_get, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("reason"),
                          xt_closure_new((void *)signal_reason_get, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("onabort"),
                          xt_closure_new((void *)signal_onabort_get, 0, nullptr));
  xt_object_define_setter(proto, xt_string_from_cstr("onabort"),
                          xt_closure_new((void *)signal_onabort_set, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("addEventListener"),
                xt_closure_new((void *)signal_add_listener, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("removeEventListener"),
                xt_closure_new((void *)signal_remove_listener, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("throwIfAborted"),
                xt_closure_new((void *)signal_throw_if_aborted, 0, nullptr));
  return proto;
}

xt_value abort_controller_proto() {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("signal"),
                          xt_closure_new((void *)controller_signal_get, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("abort"),
                xt_closure_new((void *)controller_abort, 0, nullptr));
  return proto;
}

xt_value abort_controller_new() {
  xt_value controller = xt_object_new_with_proto(abort_controller_proto());
  xt_object_set(controller, xt_string_from_cstr("__xt_gui_abort_state"), new_state());
  return controller;
}

xt_value abort_signal_new(bool aborted) {
  xt_value state = new_state();
  if (aborted) xt_object_set(state, xt_string_from_cstr("aborted"), XT_TRUE);
  return abort_signal_from_state(state, XT_UNDEFINED);
}

}  // namespace domapi
