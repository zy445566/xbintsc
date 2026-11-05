/*
 * xbintsc GUI engine — page-level error reporting.
 *
 * A callback the engine invokes (an element listener, a window listener, a
 * timer, an animation frame, a media event) can throw. Without a boundary that
 * exception reaches `xt_throw` with no `try` frame, prints to stderr and calls
 * `exit(1)`; in a GUI program the only visible effect is the window vanishing.
 * `xt_gui_invoke` runs such callbacks under a `try` frame and hands the value
 * to `xt_gui_report_uncaught`, which mirrors the browser surfaces:
 *
 *   - `window.onerror(message, source, line, column, error)`
 *   - `window.addEventListener("error", handler)` (an ErrorEvent-like object)
 *   - `window.onunhandledrejection` / the `unhandledrejection` event
 *
 * Reporting never terminates the program, so one bad handler cannot take the
 * page (or the other listeners) down. A throw inside the reporter itself is
 * only logged, which also breaks the recursion an error listener could cause.
 */

#include "dom_api_internal.h"
#include "gui_engine.h"

#include <setjmp.h>
#include <cstdio>
#include <string>

namespace {

/** Set while a report (and the handlers it runs) is in progress. */
bool g_reporting = false;

std::string to_std_string(xt_value value) {
  const char *data = xt_string_data(xt_to_string(value));
  return data != nullptr ? std::string(data) : std::string();
}

std::string error_message(xt_value error) {
  if (XT_IS_OBJECT(error)) {
    xt_value message = xt_object_get_cstr(error, "message");
    if (XT_IS_STRING(message)) {
      const char *data = xt_string_data(message);
      if (data != nullptr) return std::string(data);
    }
  }
  return to_std_string(error);
}

xt_value set_field(xt_value object, const char *key, xt_value value) {
  xt_object_set(object, xt_string_from_cstr(key), value);
  return object;
}

/** Common Event fields; `event_proto` supplies `preventDefault()`. */
xt_value make_event(const char *type, XtGuiWindow *win) {
  xt_value event = xt_object_new_with_proto(domapi::event_proto());
  set_field(event, "type", xt_string_from_cstr(type));
  set_field(event, "target", win->object);
  set_field(event, "currentTarget", win->object);
  set_field(event, "defaultPrevented", XT_FALSE);
  set_field(event, "bubbles", XT_FALSE);
  set_field(event, "cancelable", XT_TRUE);
  set_field(event, "isTrusted", XT_TRUE);
  set_field(event, "eventPhase", xt_number(2));
  set_field(event, "timeStamp", xt_number(0));
  return event;
}

void log_uncaught(const std::string &message) {
  std::fprintf(stderr, "Uncaught %s\n", message.c_str());
}

}  // namespace

extern "C" void xt_gui_install_error_handlers(void) {
  xt_set_uncaught_exception_handler(xt_gui_report_uncaught);
  xt_set_unhandled_rejection_handler(xt_gui_unhandled_rejection);
}

extern "C" void xt_gui_report_uncaught(xt_value error) {
  std::string message = error_message(error);
  if (g_reporting) {
    log_uncaught(message);
    return;
  }
  XtGuiWindow *win = xt_gui_primary_window();
  if (win == nullptr) {
    log_uncaught(message);
    return;
  }
  g_reporting = true;

  xt_value event = make_event("error", win);
  set_field(event, "message", xt_string_from_cstr(message.c_str()));
  set_field(event, "filename", xt_string_from_cstr(""));
  set_field(event, "lineno", xt_number(0));
  set_field(event, "colno", xt_number(0));
  set_field(event, "error", error);

  /* The classic `onerror` signature, then every `error` listener. Either can
   * call the event's `preventDefault()` (or `onerror` can return true) to
   * suppress the default log line. */
  int prevented = 0;
  xt_value handler = xt_object_get_cstr(win->object, "onerror");
  if (XT_IS_FUNCTION(handler)) {
    xt_value args[5] = {
        xt_string_from_cstr(message.c_str()),
        xt_string_from_cstr(""),
        xt_number(0),
        xt_number(0),
        error,
    };
    prevented = xt_truthy(xt_call_with_this(handler, win->object, 5, args));
  }
  xt_gui_emit_payload(win, "error", event);
  if (xt_truthy(xt_object_get_cstr(event, "defaultPrevented"))) prevented = 1;
  if (!prevented) log_uncaught(message);

  g_reporting = false;
}

extern "C" int xt_gui_unhandled_rejection(xt_value reason, xt_value promise) {
  XtGuiWindow *win = xt_gui_primary_window();
  if (win == nullptr) return 0;
  std::string message = error_message(reason);
  if (g_reporting) {
    std::fprintf(stderr, "Unhandled promise rejection\n%s\n", message.c_str());
    return 1;
  }
  g_reporting = true;

  xt_value event = make_event("unhandledrejection", win);
  set_field(event, "reason", reason);
  set_field(event, "promise", promise);
  set_field(event, "message", xt_string_from_cstr(message.c_str()));

  xt_value handler = xt_object_get_cstr(win->object, "onunhandledrejection");
  if (XT_IS_FUNCTION(handler)) {
    xt_value args[1] = {event};
    xt_call_with_this(handler, win->object, 1, args);
  }
  xt_gui_emit_payload(win, "unhandledrejection", event);
  if (!xt_truthy(xt_object_get_cstr(event, "defaultPrevented"))) {
    std::fprintf(stderr, "Unhandled promise rejection\n%s\n", message.c_str());
  }

  g_reporting = false;
  /* The page owns the rejection now; never turn it into a fatal process exit. */
  return 1;
}

extern "C" int xt_gui_invoke(xt_value fn, xt_value thisValue, int32_t argc, xt_value *argv) {
  if (!XT_IS_FUNCTION(fn)) return 1;
  void *frame = xt_try_enter();
  /* `xt_try_frame` starts with the setjmp buffer; the longjmp in `xt_throw`
   * returns here and the C++ frames between the callback and this boundary
   * hold no destructible state. */
  if (_setjmp(*(jmp_buf *)frame) == 0) {
    xt_call_with_this(fn, thisValue, argc, argv);
    xt_try_leave(frame);
    return 1;
  }
  xt_value error = xt_try_exception(frame);
  xt_try_leave(frame);
  xt_gui_report_uncaught(error);
  return 0;
}
