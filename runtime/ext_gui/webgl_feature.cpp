/*
 * xbintsc GUI engine — `webgl` link feature implementation.
 *
 * This translation unit is the only core-side place that references the WebGL
 * context and DOM bindings. It is pulled out of `gui.a` only when
 * `webgl_bridge.c` (the feature's C registration object) is linked, and installs
 * `XtWebglApi` for the rest of the engine. Without it the table is null, so a
 * page that never asks for WebGL does not link any `webgl_*`/`dom_api_webgl*`
 * member.
 */

#include "webgl_feature.h"

#include "webgl_api.h"
#include "dom_api_internal.h"

namespace {

void *get(XtGuiWindow *win, const void *node, bool webgl2) {
  return xt_gui_webgl_get(win, static_cast<const xtgui::Node *>(node), webgl2);
}

void *find(XtGuiWindow *win, const void *node) {
  return xt_gui_webgl_find(win, static_cast<const xtgui::Node *>(node));
}

void forget(XtGuiWindow *win, const void *node) {
  xt_gui_webgl_forget(win, static_cast<const xtgui::Node *>(node));
}

void shutdown(XtGuiWindow *win) { xt_gui_webgl_shutdown(win); }

bool submit_all() { return xt_gui_webgl_submit_all(); }

void info(void *context, float *width, float *height, uint64_t *id) {
  auto *webgl = static_cast<xtgui::WebGLContext *>(context);
  *width = (float)webgl->width();
  *height = (float)webgl->height();
  *id = webgl->id();
}

void resize(void *context, int width, int height) {
  static_cast<xtgui::WebGLContext *>(context)->resize(width, height);
}

bool read_pixels(void *context, int x, int y, int width, int height, unsigned char *out) {
  return static_cast<xtgui::WebGLContext *>(context)->read_pixels(x, y, width, height, out);
}

xt_value handle(xt_value self, void *context) {
  return domapi::webgl_context_handle(self, static_cast<xtgui::WebGLContext *>(context));
}

xt_value proto(bool webgl2) {
  return webgl2 ? domapi::webgl2_proto() : domapi::webgl_proto();
}

xt_value object_proto(const char *kind) { return domapi::webgl_object_proto(kind); }

void *color_texture(uint64_t id) {
  xtgui::WebGLContext *context = xtgui::xt_webgl_by_id(id);
  return context != nullptr ? context->color_texture() : nullptr;
}

const XtWebglApi kWebglApi = {
    get,
    find,
    forget,
    shutdown,
    submit_all,
    info,
    resize,
    read_pixels,
    handle,
    proto,
    object_proto,
    color_texture,
};

}  // namespace

extern "C" void xt_webgl_feature_register(void) { xt_webgl_api_install(&kWebglApi); }
