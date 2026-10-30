/*
 * xbintsc GUI engine — `OffscreenCanvas`.
 *
 * An offscreen canvas is a `Canvas2D` bitmap with no DOM element behind it. It
 * is created either by `new OffscreenCanvas(w, h)` or by transferring a live
 * `<canvas>` element (`transferControlToOffscreen`), in which case the element
 * keeps its bitmap and only the context ownership moves.
 *
 * Ownership stays with the window (`offscreen_canvases`), like every other
 * canvas; the process-wide id table makes the bitmap reachable from
 * `drawImage`/`createPattern`/`texImage2D` without a DOM node.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "canvas.h"
#include "gui_engine.h"
#include "image_bitmap.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {

namespace {

xtgui::Canvas2D *offscreen_canvas(xt_value self) {
  xt_value id = xt_object_get_cstr(self, "__xt_gui_canvas");
  if (!XT_IS_NUMBER(id)) return nullptr;
  return xtgui::xt_canvas_by_id((uint64_t)xt_to_number(id));
}

xt_value make_offscreen(xtgui::Canvas2D *canvas, xt_value window) {
  xt_value handle = xt_object_new_with_proto(offscreen_canvas_proto());
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_canvas"), xt_number((double)canvas->id()));
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_window"), window);
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_offscreen"), XT_TRUE);
  /* `ctx.canvas` of an offscreen context returns the offscreen object. */
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_node"), handle);
  return handle;
}

xt_value image_bitmap_handle(const xtgui::Image &image) {
  const uint64_t id = xtgui::xt_image_bitmap_add(image);
  xt_value name = xt_string_from_cstr("ImageBitmap");
  xt_value constructor = xt_browser_type(1, &name);
  xt_value prototype = xt_object_get_cstr(constructor, "prototype");
  xt_value bitmap = XT_IS_OBJECT(prototype) ? xt_object_new_with_proto(prototype)
                                            : xt_object_new();
  xt_object_set(bitmap, xt_string_from_cstr("width"), xt_number(image.width));
  xt_object_set(bitmap, xt_string_from_cstr("height"), xt_number(image.height));
  xt_object_set(bitmap, xt_string_from_cstr("__xt_bitmap"), xt_number((double)id));
  return bitmap;
}

}  // namespace

xt_value offscreen_canvas_proto() {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  struct Entry {
    const char *name;
    void *getter;
    void *setter;
  };
  const Entry properties[] = {
      {"width", (void *)offscreen_width_get, (void *)offscreen_width_set},
      {"height", (void *)offscreen_height_get, (void *)offscreen_height_set},
  };
  for (const Entry &property : properties) {
    xt_object_define_getter(proto, xt_string_from_cstr(property.name),
                            xt_closure_new(property.getter, 0, nullptr));
    xt_object_define_setter(proto, xt_string_from_cstr(property.name),
                            xt_closure_new(property.setter, 0, nullptr));
  }
  struct Method {
    const char *name;
    void *fn;
  };
  const Method methods[] = {
      {"getContext", (void *)offscreen_get_context},
      {"transferToImageBitmap", (void *)offscreen_transfer_to_image_bitmap},
      {"convertToBlob", (void *)offscreen_convert_to_blob},
  };
  for (const Method &method : methods) {
    xt_object_set(proto, xt_string_from_cstr(method.name),
                  xt_closure_new(method.fn, 0, nullptr));
  }
  return proto;
}

xt_value offscreen_canvas_create(int32_t argc, xt_value *argv) {
  xt_value window = xt_gui_ensure_window();
  XtGuiWindow *win = xt_gui_window_from_this(window);
  if (win == nullptr) return XT_NULL;
  int width = argc >= 1 ? (int)xt_to_number(xt_arg(argc, argv, 0)) : 300;
  int height = argc >= 2 ? (int)xt_to_number(xt_arg(argc, argv, 1)) : 150;
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  auto canvas = std::make_unique<xtgui::Canvas2D>(width, height);
  xtgui::Canvas2D *raw = canvas.get();
  win->offscreen_canvases[raw->id()] = std::move(canvas);
  return make_offscreen(raw, window);
}

METHOD(offscreen_width_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = offscreen_canvas(self);
  return xt_number(canvas != nullptr ? canvas->width() : 0);
}

METHOD(offscreen_width_set) {
  (void)env;
  xtgui::Canvas2D *canvas = offscreen_canvas(self);
  if (canvas != nullptr) canvas->resize((int)canvas_number_arg(argc, argv, 0, 0.0), canvas->height());
  return XT_UNDEFINED;
}

METHOD(offscreen_height_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = offscreen_canvas(self);
  return xt_number(canvas != nullptr ? canvas->height() : 0);
}

METHOD(offscreen_height_set) {
  (void)env;
  xtgui::Canvas2D *canvas = offscreen_canvas(self);
  if (canvas != nullptr) canvas->resize(canvas->width(), (int)canvas_number_arg(argc, argv, 0, 0.0));
  return XT_UNDEFINED;
}

METHOD(offscreen_get_context) {
  (void)env;
  xtgui::Canvas2D *canvas = offscreen_canvas(self);
  if (canvas == nullptr) return XT_NULL;
  const std::string mode = canvas_string_arg(argc, argv, 0);
  /* The GPU context needs a DOM node for its texture table; the 2D bitmap does
   * not, so only "2d" resolves here. */
  if (mode != "2d") return XT_NULL;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_ctx");
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value attributes = xt_arg(argc, argv, 1);
  bool alpha = true;
  if (XT_IS_OBJECT(attributes)) {
    xt_value value = xt_object_get_cstr(attributes, "alpha");
    if (XT_IS_BOOL(value)) alpha = value_truthy(value);
  }
  canvas->set_alpha(alpha);
  xt_value context = xt_object_new_with_proto(canvas_proto());
  xt_object_set(context, xt_string_from_cstr("__xt_gui_canvas"), xt_number((double)canvas->id()));
  xt_object_set(context, xt_string_from_cstr("__xt_gui_node"), self);
  xt_object_set(context, xt_string_from_cstr("__xt_gui_ctx_attributes"),
                canvas_context_attributes(attributes, alpha));
  xt_object_set(self, xt_string_from_cstr("__xt_gui_ctx"), context);
  return context;
}

METHOD(offscreen_transfer_to_image_bitmap) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = offscreen_canvas(self);
  if (canvas == nullptr || canvas->image().width <= 0) return XT_NULL;
  return image_bitmap_handle(canvas->image());
}

METHOD(offscreen_convert_to_blob) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = offscreen_canvas(self);
  if (canvas == nullptr) return xt_promise_reject(xt_string_from_cstr("InvalidStateError"));
  std::vector<unsigned char> png;
  if (!canvas->encode_png(&png)) return xt_promise_reject(xt_string_from_cstr("InvalidStateError"));
  xt_value data = xt_array_new(0, nullptr);
  for (unsigned char byte : png) xt_array_push(data, xt_number((double)byte));
  xt_value blob = xt_object_new();
  xt_object_set(blob, xt_string_from_cstr("type"), xt_string_from_cstr("image/png"));
  xt_object_set(blob, xt_string_from_cstr("size"), xt_number((double)png.size()));
  xt_object_set(blob, xt_string_from_cstr("__xt_gui_blob_data"), data);
  return xt_promise_resolve(blob);
}

METHOD(canvas_transfer_control_to_offscreen) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value existing = xt_object_get_cstr(self, "__xt_gui_offscreen");
  if (XT_IS_OBJECT(existing)) return existing;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (win == nullptr || node == nullptr || !node->isElement() || !node->isTag("canvas")) {
    return XT_NULL;
  }
  if (const XtWebglApi *webgl = xt_webgl_api()) {
    if (webgl->find(win, node) != nullptr) return XT_NULL;
  }
  xtgui::Canvas2D *canvas = xt_gui_canvas_get(win, node);
  if (canvas == nullptr) return XT_NULL;
  xt_value handle = make_offscreen(canvas, win->object);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_offscreen"), handle);
  return handle;
}

}  // namespace domapi

#undef METHOD
