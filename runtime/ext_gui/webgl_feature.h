/*
 * xbintsc GUI engine — WebGL context lifetime, implemented by the optional
 * `webgl` feature.
 *
 * These functions are only referenced by `webgl_feature.cpp`, which installs
 * them in `XtWebglApi`; no core translation unit calls them directly. The header
 * is therefore only included by the feature's own translation units.
 */
#ifndef XT_GUI_WEBGL_FEATURE_H
#define XT_GUI_WEBGL_FEATURE_H

#include "gui_engine.h"
#include "webgl.h"

/** The WebGL context of a `<canvas>`, creating it on first use. `webgl2`
 * selects the WebGL 2 API/prototype; a canvas keeps the first context kind it
 * asked for. */
xtgui::WebGLContext *xt_gui_webgl_get(XtGuiWindow *win, const xtgui::Node *node, bool webgl2);
/** The existing WebGL context of `node`, or null (no side effects). */
xtgui::WebGLContext *xt_gui_webgl_find(XtGuiWindow *win, const xtgui::Node *node);
/** Drop a node's WebGL context (called when its handle is forgotten). */
void xt_gui_webgl_forget(XtGuiWindow *win, const xtgui::Node *node);
/** Release every WebGL context of `win`. */
void xt_gui_webgl_shutdown(XtGuiWindow *win);
/** Submit every live context's queued commands; true when any exists. */
bool xt_gui_webgl_submit_all();

#endif /* XT_GUI_WEBGL_FEATURE_H */
