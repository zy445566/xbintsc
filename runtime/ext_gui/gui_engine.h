/*
 * xbintsc GUI engine — internal header.
 *
 * The engine owns one SDL3 window + SDL_GPU device per `createWindow(...)`
 * call. Windows are exposed to TypeScript as runtime objects whose methods live
 * on a shared prototype (see `window.cpp`). The three `xt_gui_*` entry points
 * below use the uniform `(argc, argv)` ABI the extension bindings expect.
 *
 * SDL3 and its dependencies are linked into `gui.a`; only the SDL3 headers
 * (`SDL3/SDL.h`) are needed to build these translation units.
 */
#ifndef XT_GUI_ENGINE_H
#define XT_GUI_ENGINE_H

#include "rt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Uniform `(argc, argv)` entry points bound by `src/extensions/gui`. */
xt_value xt_gui_create_window(int32_t argc, xt_value *argv);
xt_value xt_gui_run(int32_t argc, xt_value *argv);
xt_value xt_gui_quit(int32_t argc, xt_value *argv);

#ifdef __cplusplus
} /* extern "C" */

#include <SDL3/SDL.h>

#include <memory>
#include <string>

#include "document.h"
#include "renderer.h"

/* One live window. `object` is the JavaScript-visible handle; the record is
 * addressed from it through the hidden `__xt_gui_index` property. */
struct XtGuiWindow {
  SDL_Window *window;
  xt_value object;
  int open;
  int ready;
  std::string html;
  std::unique_ptr<xtgui::XtDocument> document;
  XtGuiGeometry geometry;
  XtGuiGeometry text_geometry;
  float background[4];
  int width;
  int height;
};

/** Resolve a window handle (`this`) to its record, or NULL for a foreign value. */
XtGuiWindow *xt_gui_window_from_this(xt_value thisValue);
/** Shared prototype carrying the window methods. */
xt_value xt_gui_window_proto(void);
/** Release GPU claim + destroy the window and mark the record closed. */
void xt_gui_quit_window(XtGuiWindow *win);
/** Deliver `event` (no arguments) to handlers registered with `on`. */
void xt_gui_emit(XtGuiWindow *win, const char *event);
/** Deliver `event` with a single `payload` argument (input events). */
void xt_gui_emit_payload(XtGuiWindow *win, const char *event, xt_value payload);
/** Acquire a swapchain frame for `win` and clear it (placeholder paint). */
void xt_gui_render_window(XtGuiWindow *win);
/** Current logical window size (CSS viewport), in pixels. */
void xt_gui_window_viewport(XtGuiWindow *win, float *width, float *height);
/** CSS-like descriptor of the deepest element at (x, y), or `""` (test/debug). */
std::string xt_gui_hit_test(XtGuiWindow *win, float x, float y);
/** Synthesise a pointer event (`mousemove`/`mousedown`/`mouseup`/`click`). */
void xt_gui_dispatch_pointer(XtGuiWindow *win, const char *type, float x, float y, int button,
                             int clicks);
/** Synthesise a wheel event. */
void xt_gui_dispatch_wheel(XtGuiWindow *win, float x, float y, float delta_x, float delta_y);
/** Synthesise a key event (`keydown`/`keyup`). */
void xt_gui_dispatch_key(XtGuiWindow *win, const char *type, const char *key, const char *code);

#endif /* __cplusplus */

#endif /* XT_GUI_ENGINE_H */
