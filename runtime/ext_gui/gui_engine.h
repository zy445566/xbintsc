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

/* One live window. `object` is the JavaScript-visible handle; the record is
 * addressed from it through the hidden `__xt_gui_index` property. */
struct XtGuiWindow {
  SDL_Window *window;
  xt_value object;
  int open;
  int ready;
  std::string html;
  std::unique_ptr<xtgui::XtDocument> document;
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
/** Acquire a swapchain frame for `win` and clear it (placeholder paint). */
void xt_gui_render_window(XtGuiWindow *win);
/** Current logical window size (CSS viewport), in pixels. */
void xt_gui_window_viewport(XtGuiWindow *win, float *width, float *height);

#endif /* __cplusplus */

#endif /* XT_GUI_ENGINE_H */
