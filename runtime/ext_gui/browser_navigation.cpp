/*
 * xbintsc GUI engine — window navigation and focus.
 *
 * `scrollTo` / `scrollBy` move the page viewport (painting and hit testing
 * subtract the offset), `scrollX`/`scrollY` (and the `page*Offset` aliases)
 * report it, and `focus` / `blur` raise the OS window and toggle
 * `document.hasFocus()` plus the corresponding window events.
 */

#include "gui_engine.h"

#include <cmath>

namespace {

xt_value scroll_axis(xt_value options, const char *name) {
  if (!XT_IS_OBJECT(options)) return XT_UNDEFINED;
  return xt_object_get_cstr(options, name);
}

double coordinate(xt_value value, double fallback) {
  return XT_IS_NUMBER(value) ? xt_to_number(value) : fallback;
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

/** `scrollTo(x, y)` / `scrollTo({ left, top })`. */
METHOD(win_scroll_to) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || argc < 1) return XT_UNDEFINED;
  xt_value options = xt_arg(argc, argv, 0);
  double x = 0;
  double y = 0;
  if (XT_IS_OBJECT(options) && !XT_IS_ARRAY(options)) {
    x = coordinate(scroll_axis(options, "left"), coordinate(scroll_axis(options, "x"), 0));
    y = coordinate(scroll_axis(options, "top"), coordinate(scroll_axis(options, "y"), 0));
  } else {
    x = coordinate(options, 0);
    if (argc >= 2) y = coordinate(xt_arg(argc, argv, 1), 0);
  }
  xt_gui_scroll_to(win, (float)x, (float)y);
  return XT_UNDEFINED;
}

METHOD(win_scroll_by) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || argc < 1) return XT_UNDEFINED;
  xt_value options = xt_arg(argc, argv, 0);
  double dx = 0;
  double dy = 0;
  if (XT_IS_OBJECT(options) && !XT_IS_ARRAY(options)) {
    dx = coordinate(scroll_axis(options, "left"), coordinate(scroll_axis(options, "x"), 0));
    dy = coordinate(scroll_axis(options, "top"), coordinate(scroll_axis(options, "y"), 0));
  } else {
    dx = coordinate(options, 0);
    if (argc >= 2) dy = coordinate(xt_arg(argc, argv, 1), 0);
  }
  xt_gui_scroll_to(win, win->scroll_x + (float)dx, win->scroll_y + (float)dy);
  return XT_UNDEFINED;
}

METHOD(win_scroll_x_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  return xt_number(win != nullptr ? (double)win->scroll_x : 0.0);
}

METHOD(win_scroll_y_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  return xt_number(win != nullptr ? (double)win->scroll_y : 0.0);
}

METHOD(win_focus) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || !win->open) return XT_UNDEFINED;
  if (win->window != nullptr) SDL_RaiseWindow(win->window);
  if (!win->focused) {
    win->focused = 1;
    xt_gui_emit(win, "focus");
  }
  return XT_UNDEFINED;
}

METHOD(win_blur) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || !win->open) return XT_UNDEFINED;
  if (win->focused) {
    win->focused = 0;
    xt_gui_emit(win, "blur");
  }
  return XT_UNDEFINED;
}

#undef METHOD

}  // namespace

void xt_browser_install_window_navigation(xt_value proto) {
  xt_object_set(proto, xt_string_from_cstr("scrollTo"),
                xt_closure_new((void *)win_scroll_to, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("scroll"),
                xt_closure_new((void *)win_scroll_to, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("scrollBy"),
                xt_closure_new((void *)win_scroll_by, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("focus"),
                xt_closure_new((void *)win_focus, 0, nullptr));
  xt_object_set(proto, xt_string_from_cstr("blur"),
                xt_closure_new((void *)win_blur, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("scrollX"),
                          xt_closure_new((void *)win_scroll_x_get, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("scrollY"),
                          xt_closure_new((void *)win_scroll_y_get, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("pageXOffset"),
                          xt_closure_new((void *)win_scroll_x_get, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("pageYOffset"),
                          xt_closure_new((void *)win_scroll_y_get, 0, nullptr));
}
