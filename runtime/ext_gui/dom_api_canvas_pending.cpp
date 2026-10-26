/*
 * xbintsc GUI engine — 2D contexts for pending canvas handles.
 *
 * The gui extension hands out element handles before `loadHTML`; a module may
 * call `getContext("2d")` in that window. Keep the bridge here so
 * `dom_api_canvas.cpp` stays focused (and under the per-file line budget).
 */
#include <memory>
#include <string>

#include "canvas.h"
#include "dom_api_internal.h"
#include "gui_engine.h"
#include "webgl.h"

namespace {

bool is_pending_canvas(xt_value self) {
  xt_value pending = xt_object_get_cstr(self, "__xt_gui_pending");
  return XT_IS_NUMBER(pending);
}

}  // namespace

namespace domapi {

xt_value xt_canvas_pending_context(xt_value self, XtGuiWindow *win) {
  if (!is_pending_canvas(self) || win == nullptr) return XT_NULL;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_ctx");
  if (XT_IS_OBJECT(cached)) return cached;
  auto created = std::make_unique<xtgui::Canvas2D>(300, 150);
  xtgui::Canvas2D *raw = created.get();
  win->pending_canvases[raw->id()] = std::move(created);
  xt_value ctx = xt_object_new_with_proto(canvas_proto());
  xt_object_set(ctx, xt_string_from_cstr("__xt_gui_canvas"), xt_number((double)raw->id()));
  xt_object_set(ctx, xt_string_from_cstr("__xt_gui_node"), self);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_ctx"), ctx);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_pending_canvas"), xt_number((double)raw->id()));
  xt_gui_track_pending_canvas(win, self);
  return ctx;
}

xt_value xt_webgl_pending_context(xt_value self, XtGuiWindow *win, bool webgl2) {
  if (!is_pending_canvas(self) || win == nullptr) return XT_NULL;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_gl");
  if (XT_IS_OBJECT(cached)) {
    bool cachedWebgl2 = xt_truthy(xt_object_get_cstr(cached, "__xt_gl_webgl2")) != 0;
    return cachedWebgl2 == webgl2 ? cached : XT_NULL;
  }
  xtgui::WebGLContext *context = xt_gui_webgl_pending_create(win, webgl2, 300, 150);
  if (context == nullptr) return XT_NULL;
  xt_object_set(self, xt_string_from_cstr("__xt_gui_pending_webgl"),
                xt_number((double)context->id()));
  xt_gui_track_pending_canvas(win, self);
  return webgl_context_handle(self, context);
}

}  // namespace domapi
