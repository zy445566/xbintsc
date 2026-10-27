/*
 * xbintsc GUI engine — the `addEventListener` options bag.
 *
 * Element and window listeners accept the standard third argument:
 * `capture` / `once` / `passive` (a boolean third argument is the legacy
 * `capture` spelling) and `signal` (an `AbortSignal`; an aborted signal means
 * the listener must not run again). Both delivery sites parse it here so the
 * two paths cannot drift apart.
 */
#ifndef XT_GUI_LISTENER_OPTIONS_H
#define XT_GUI_LISTENER_OPTIONS_H

#include "gui_engine.h"

struct XtGuiListenerOptions {
  bool capture = false;
  bool once = false;
  bool passive = false;
  xt_value signal = XT_UNDEFINED;
};

/** True when `signal` is an already-aborted `AbortSignal`. */
inline bool xt_gui_signal_aborted(xt_value signal) {
  if (!XT_IS_OBJECT(signal)) return false;
  xt_value aborted = xt_object_get_cstr(signal, "aborted");
  return XT_IS_UNDEFINED(aborted) ? false : (xt_truthy(aborted) != 0);
}

inline XtGuiListenerOptions xt_gui_listener_options(xt_value options) {
  XtGuiListenerOptions parsed;
  if (XT_IS_OBJECT(options)) {
    parsed.capture = xt_truthy(xt_object_get_cstr(options, "capture")) != 0;
    parsed.once = xt_truthy(xt_object_get_cstr(options, "once")) != 0;
    parsed.passive = xt_truthy(xt_object_get_cstr(options, "passive")) != 0;
    xt_value signal = xt_object_get_cstr(options, "signal");
    if (XT_IS_OBJECT(signal)) parsed.signal = signal;
  } else if (XT_IS_BOOL(options)) {
    parsed.capture = xt_truthy(options) != 0;
  }
  return parsed;
}

#endif /* XT_GUI_LISTENER_OPTIONS_H */
