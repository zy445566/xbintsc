/*
 * xbintsc GUI engine — window diagnostics and background painting exposed as
 * test/debug hooks (`setBackground`, `paintList`, `paintCount`).
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
