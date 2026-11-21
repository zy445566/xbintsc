/*
 * xbintsc GUI engine — window diagnostics and background painting exposed as
 * test/debug hooks (`setBackground`, `paintList`, `paintCount`, `sendEvent`).
 *
 * `sendEvent` synthesises the same input payloads a real SDL event produces, so
 * the whole hit-test/listener path is testable without a mouse. The SDL event
 * switch lives in `gui_input.cpp`; the single-window record and its prototype
 * stay in `window.cpp`.
 */

#include "gui_internal.h"

#include "paint.h"

#include <string>

xt_value xt_gui_window_set_background(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win != NULL && argc >= 1) {
    xt_value text = xt_to_string(xt_arg(argc, argv, 0));
    const char *data = xt_string_data(text);
    if (data != nullptr) {
      xtgui::Color color = xtgui::xt_css_parse_color(data);
      if (color.valid) {
        win->background[0] = color.r;
        win->background[1] = color.g;
        win->background[2] = color.b;
        win->background[3] = color.a;
        win->present_dirty = 1;
      }
    }
    xt_object_set(self, xt_string_from_cstr("__xt_gui_background"), text);
  }
  return XT_UNDEFINED;
}

/** Diagnostic/test hook: the display list built from the current layout. */
xt_value xt_gui_window_paint_list(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return xt_string_from_cstr("");
  xt_gui_flush_dom(win);
  xtgui::DisplayList list;
  xtgui::xt_paint_build(win->document->layout().root(), list);
  std::string text = list.dump();
  return xt_string_from_cstr(text.c_str());
}

/** Diagnostic/test hook: number of shapes in the display list. */
xt_value xt_gui_window_paint_count(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return xt_number(0);
  xt_gui_flush_dom(win);
  xtgui::DisplayList list;
  xtgui::xt_paint_build(win->document->layout().root(), list);
  return xt_number((double)list.rects.size());
}

static double xt_gui_number_or(xt_value value, double fallback) {
  return XT_IS_NUMBER(value) ? xt_to_number(value) : fallback;
}

/** Test/debug hook: synthesise an input event and deliver it to handlers.
 * `win.sendEvent("click", { x, y, button, clicks })`,
 * `win.sendEvent("wheel", { x, y, deltaX, deltaY })`,
 * `win.sendEvent("mousemove", { x, y, movementX, movementY })` or
 * `win.sendEvent("keydown", { key, code })`. A key event may instead name the
 * SDL `scancode`/`keycode` plus modifiers to exercise a real-keyboard path. */
xt_value xt_gui_window_send_event(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || argc < 1) return XT_FALSE;
  const char *type = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
  if (type == nullptr) return XT_FALSE;
  xt_value options = xt_arg(argc, argv, 1);
  double x = 0;
  double y = 0;
  double movement_x = 0;
  double movement_y = 0;
  if (XT_IS_OBJECT(options)) {
    x = xt_gui_number_or(xt_object_get_cstr(options, "x"), 0);
    y = xt_gui_number_or(xt_object_get_cstr(options, "y"), 0);
    movement_x = xt_gui_number_or(xt_object_get_cstr(options, "movementX"), 0);
    movement_y = xt_gui_number_or(xt_object_get_cstr(options, "movementY"), 0);
  }
  std::string name(type);
  if (name == "textInput") {
    std::string text;
    if (XT_IS_OBJECT(options)) {
      xt_value value = xt_object_get_cstr(options, "text");
      if (XT_IS_STRING(value)) text = std::string(xt_string_data(value));
    }
    xt_gui_text_input(win, text.c_str());
    return XT_TRUE;
  }
  if (name == "compositionstart" || name == "compositionupdate" || name == "compositionend") {
    std::string data;
    if (XT_IS_OBJECT(options)) {
      xt_value value = xt_object_get_cstr(options, "data");
      if (XT_IS_STRING(value)) data = std::string(xt_string_data(value));
    }
    xt_gui_composition_event(win, name.c_str(), data.c_str());
    return XT_TRUE;
  }
  if (name == "wheel") {
    double delta_x = 0;
    double delta_y = 0;
    if (XT_IS_OBJECT(options)) {
      delta_x = xt_gui_number_or(xt_object_get_cstr(options, "deltaX"), 0);
      delta_y = xt_gui_number_or(xt_object_get_cstr(options, "deltaY"), 0);
    }
    xt_gui_dispatch_wheel(win, (float)x, (float)y, (float)delta_x, (float)delta_y);
    return XT_TRUE;
  }
  if (name == "keydown" || name == "keyup") {
    auto flag = [options](const char *key) { return xt_truthy(xt_object_get_cstr(options, key)); };
    if (XT_IS_OBJECT(options) && XT_IS_NUMBER(xt_object_get_cstr(options, "scancode"))) {
      int scancode = (int)xt_gui_number_or(xt_object_get_cstr(options, "scancode"), 0);
      int keycode = (int)xt_gui_number_or(xt_object_get_cstr(options, "keycode"), 0);
      xt_gui_dispatch_physical_key(win, type, scancode, keycode, flag("repeat"), flag("ctrl"),
                                   flag("shift"), flag("alt"), flag("meta"), flag("caps"));
      return XT_TRUE;
    }
    const char *key = "";
    const char *code = "";
    if (XT_IS_OBJECT(options)) {
      xt_value key_value = xt_object_get_cstr(options, "key");
      xt_value code_value = xt_object_get_cstr(options, "code");
      if (XT_IS_STRING(key_value)) key = xt_string_data(key_value);
      if (XT_IS_STRING(code_value)) code = xt_string_data(code_value);
    }
    xt_gui_dispatch_key_with_modifiers(win, type, key, code, flag("repeat"), flag("ctrl"),
                                       flag("shift"), flag("alt"), flag("meta"));
    return XT_TRUE;
  }
  int button = -1;
  int clicks = 0;
  if (XT_IS_OBJECT(options)) {
    button = (int)xt_gui_number_or(xt_object_get_cstr(options, "button"), -1);
    clicks = (int)xt_gui_number_or(xt_object_get_cstr(options, "clicks"), 0);
  }
  xt_gui_dispatch_pointer(win, type, (float)x, (float)y, button, clicks, (float)movement_x,
                          (float)movement_y);
  return XT_TRUE;
}
