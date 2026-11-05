/*
 * xbintsc GUI engine — window-level event listeners (`window.on` /
 * `window.addEventListener`) live here so the input/rendering unit stays focused.
 */

#include "gui_internal.h"
#include "listener_options.h"

#include <vector>

/* -- window events -------------------------------------------------------- */

void xt_gui_emit(XtGuiWindow *win, const char *event) {
  xt_gui_emit_payload(win, event, XT_UNDEFINED);
}

/** Drop one window listener (by identity: type + fn + capture). */
static void remove_window_listener(XtGuiWindow *win, const XtGuiWindowListener &listener) {
  std::vector<XtGuiWindowListener> &listeners = win->window_listeners;
  for (auto it = listeners.begin(); it != listeners.end(); ++it) {
    if (it->type == listener.type && it->capture == listener.capture &&
        xt_truthy(xt_seq(it->fn, listener.fn))) {
      listeners.erase(it);
      return;
    }
  }
}

void xt_gui_emit_payload(XtGuiWindow *win, const char *event, xt_value payload) {
  if (win == NULL || !XT_IS_OBJECT(win->object)) return;
  xt_value args[1];
  int32_t count = 0;
  if (!XT_IS_UNDEFINED(payload)) {
    args[0] = payload;
    count = 1;
  }
  /* Snapshot the list: a handler may register or remove listeners while it
   * runs, and the DOM runs the batch as it stood when dispatch started. */
  std::vector<XtGuiWindowListener> snapshot = win->window_listeners;
  for (const XtGuiWindowListener &listener : snapshot) {
    if (listener.type != event) continue;
    /* `{ signal }` removes the listener at abort time (checked here, so no
     * callback plumbing is needed inside the signal). */
    if (xt_gui_signal_aborted(listener.signal)) {
      remove_window_listener(win, listener);
      continue;
    }
    if (listener.once) remove_window_listener(win, listener);
    if (XT_IS_FUNCTION(listener.fn)) xt_gui_invoke(listener.fn, win->object, count, args);
  }
}
