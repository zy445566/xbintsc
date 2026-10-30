/*
 * xbintsc GUI engine — optional WebGL context/API table.
 *
 * The WebGL DOM bindings (`webgl_*.cpp`, `dom_api_webgl*.cpp`, `gui_webgl.cpp`)
 * are a large part of the engine. A page that never calls
 * `getContext("webgl…")` should not link them, so the core reaches them through
 * this table instead of direct references. The table is installed by the
 * `webgl` link feature's bridge; without it, `getContext("webgl")` answers null
 * and the WebGL host types are undefined, while the 2D canvas works normally.
 */
#ifndef XT_GUI_WEBGL_API_H
#define XT_GUI_WEBGL_API_H

#include <cstdint>

#include "rt.h"

struct XtGuiWindow;

struct XtWebglApi {
  /** Create/find the context of a canvas; `node` is a `xtgui::Node*`. */
  void *(*get)(XtGuiWindow *win, const void *node, bool webgl2);
  /** The existing context of a canvas, or null (no side effects). */
  void *(*find)(XtGuiWindow *win, const void *node);
  /** Drop a canvas's context (called when its handle is forgotten). */
  void (*forget)(XtGuiWindow *win, const void *node);
  /** Release every context of a window. */
  void (*shutdown)(XtGuiWindow *win);
  /** Submit every live context's queued commands; true when any exists. */
  bool (*submit_all)();
  /** Width, height and render id of a context. */
  void (*info)(void *context, float *width, float *height, uint64_t *id);
  /** Resize a context (the `canvas.width`/`height` setters). */
  void (*resize)(void *context, int width, int height);
  /** Read RGBA pixels (GL order) from a context, for `createImageBitmap`. */
  bool (*read_pixels)(void *context, int x, int y, int width, int height, unsigned char *out);
  /** Wrap a context in a DOM handle; `self` is the canvas element handle. */
  xt_value (*handle)(xt_value self, void *context);
  /** The WebGL 1/2 context prototype. */
  xt_value (*proto)(bool webgl2);
  /** A WebGL object prototype (`buffer`, `texture`, `shader`, …). */
  xt_value (*object_proto)(const char *kind);
  /** The colour texture of a context by render id, for the paint pipeline. */
  void *(*color_texture)(uint64_t id);
};

/** Install the table. Called by the `webgl` feature bridge before `main`. */
void xt_webgl_api_install(const XtWebglApi *api);

/** The installed table, or null when the `webgl` feature was not linked. */
const XtWebglApi *xt_webgl_api();

#endif /* XT_GUI_WEBGL_API_H */
