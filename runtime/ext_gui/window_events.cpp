/*
 * xbintsc GUI engine — window-level event listeners.
 *
 * `win.on` / `win.off` keep their legacy behaviour (an event name plus a
 * callback, delivered with the input payload); `win.addEventListener` /
 * `win.removeEventListener` take the Web options bag (`capture`, `once`,
 * `passive`, `signal`). Both register in the window's single listener vector, so
 * a listener added through one spelling can be removed through the other, and
 * `gui_events.cpp` delivers one merged batch.
 */

#include "gui_engine.h"
#include "listener_options.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::string listener_string(xt_value value) {
  const char *data = xt_string_data(xt_to_string(value));
  return data != nullptr ? data : "";
}

void push_listener(XtGuiWindow *win, const std::string &type, xt_value fn, bool once, bool capture,
                   bool passive, xt_value signal) {
  if (win == nullptr || !XT_IS_FUNCTION(fn)) return;
  XtGuiWindowListener listener;
  listener.type = type;
  listener.fn = fn;
  listener.once = once;
  listener.capture = capture;
  listener.passive = passive;
  listener.signal = signal;
  win->window_listeners.push_back(listener);
}

xt_value win_on(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc >= 2) {
    XtGuiWindow *win = xt_gui_window_from_this(self);
    xt_value fn = xt_arg(argc, argv, 1);
    if (win != nullptr && XT_IS_FUNCTION(fn)) {
      push_listener(win, listener_string(xt_arg(argc, argv, 0)), fn, false, false, false,
                    XT_UNDEFINED);
    }
  }
  return self;
}

xt_value win_off(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc >= 2) {
    XtGuiWindow *win = xt_gui_window_from_this(self);
    if (win != nullptr) {
      std::string type = listener_string(xt_arg(argc, argv, 0));
      xt_value fn = xt_arg(argc, argv, 1);
      std::vector<XtGuiWindowListener> &listeners = win->window_listeners;
      listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                     [&](const XtGuiWindowListener &listener) {
                                       return listener.type == type &&
                                              xt_truthy(xt_seq(listener.fn, fn));
                                     }),
                      listeners.end());
    }
  }
  return self;
}

/** Web spelling of `on`: `window.addEventListener(type, fn, options?)`. Window
 * delivery has no capture phase, but `capture` is part of listener identity so
 * `removeEventListener(type, fn, true)` still matches the right entry. */
xt_value win_add_event_listener(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 2) return XT_UNDEFINED;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  std::string type = listener_string(xt_arg(argc, argv, 0));
  xt_value fn = xt_arg(argc, argv, 1);
  XtGuiListenerOptions options = xt_gui_listener_options(xt_arg(argc, argv, 2));
  if (win == nullptr || !XT_IS_FUNCTION(fn) || xt_gui_signal_aborted(options.signal)) {
    return XT_UNDEFINED;
  }
  /* The DOM ignores a second registration of the same (type, fn, capture). */
  for (const XtGuiWindowListener &existing : win->window_listeners) {
    if (existing.type == type && existing.capture == options.capture &&
        xt_truthy(xt_seq(existing.fn, fn))) {
      return XT_UNDEFINED;
    }
  }
  push_listener(win, type, fn, options.once, options.capture, options.passive, options.signal);
  return XT_UNDEFINED;
}

/** Web spelling of `off`: `window.removeEventListener(type, fn, options?)`. */
xt_value win_remove_event_listener(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 2) return XT_UNDEFINED;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr) return XT_UNDEFINED;
  std::string type = listener_string(xt_arg(argc, argv, 0));
  xt_value fn = xt_arg(argc, argv, 1);
  XtGuiListenerOptions options = xt_gui_listener_options(xt_arg(argc, argv, 2));
  std::vector<XtGuiWindowListener> &listeners = win->window_listeners;
  for (auto it = listeners.begin(); it != listeners.end(); ++it) {
    if (it->type == type && it->capture == options.capture && xt_truthy(xt_seq(it->fn, fn))) {
      listeners.erase(it);
      return XT_UNDEFINED;
    }
  }
  return XT_UNDEFINED;
}

}  // namespace

void xt_gui_install_window_events(xt_value proto) {
  xt_object_set(proto, xt_string_from_cstr("on"),
                xt_closure_new((void *)win_on, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("off"),
                xt_closure_new((void *)win_off, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("addEventListener"),
                xt_closure_new((void *)win_add_event_listener, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("removeEventListener"),
                xt_closure_new((void *)win_remove_event_listener, 0, nullptr));
}
