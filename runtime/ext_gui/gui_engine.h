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
#include <unordered_map>
#include <vector>

#include "document.h"
#include "renderer.h"

/** One element-level event listener registered through `addEventListener`. */
struct XtGuiNodeListener {
  std::string type;
  xt_value fn;
  bool once;
  bool capture;
};

/* One live window. `object` is the JavaScript-visible handle; the record is
 * addressed from it through the hidden `__xt_gui_index` property. */
struct XtGuiWindow {
  SDL_Window *window;
  xt_value object;
  int open;
  double last_frame_ms;
  int ready;
  std::string html;
  std::unique_ptr<xtgui::XtDocument> document;
  XtGuiGeometry geometry;
  XtGuiGeometry text_geometry;
  XtGuiGeometry image_geometry;
  float background[4];
  int width;
  int height;

  /* -- DOM object model (element handles + events) ----------------------- */
  /** `document` handle for this window (lazily created). */
  xt_value document_object = XT_UNDEFINED;
  /** Bumped on every `loadHTML`; stale handles no-op. */
  double doc_generation = 1.0;
  /** Node -> table index / cached handle, so identity is stable. */
  std::vector<xtgui::Node *> node_order;
  std::unordered_map<const xtgui::Node *, int> node_index;
  std::unordered_map<const xtgui::Node *, xt_value> node_handles;
  /** Element-level listeners, keyed by node. */
  std::unordered_map<const xtgui::Node *, std::vector<XtGuiNodeListener>> node_listeners;
  /** Set by a DOM mutation; consumed by the frame loop before repainting. */
  int struct_dirty = 0;
};

/** Resolve a window handle (`this`) to its record, or NULL for a foreign value. */
XtGuiWindow *xt_gui_window_from_this(xt_value thisValue);
/** The `document` handle for `win` (created on first use). */
xt_value xt_gui_document_handle(XtGuiWindow *win);
/** Node handle for `node` (created and cached on first use). */
xt_value xt_gui_node_handle(XtGuiWindow *win, const xtgui::Node *node);
/** Drop every cached handle/listener and bump the document generation. */
void xt_gui_handles_reset(XtGuiWindow *win);
/** Deliver `type` to element listeners along the propagation path. */
void xt_gui_emit_dom_event(XtGuiWindow *win, const char *type, const xtgui::Node *target,
                           xt_value legacy_payload);
/** Synthesise a DOM event whose target is `target` (used by element.click()). */
void xt_gui_dispatch_to_node(XtGuiWindow *win, const xtgui::Node *target, const char *type);
/** Apply any pending DOM mutation (restyle/relayout) before a synchronous read. */
void xt_gui_flush_dom(XtGuiWindow *win);
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
