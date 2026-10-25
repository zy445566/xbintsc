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
  return ctx;
}

}  // namespace domapi
