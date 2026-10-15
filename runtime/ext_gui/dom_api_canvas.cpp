/*
 * xbintsc GUI engine — the `<canvas>` DOM bindings.
 *
 * `element.getContext("2d")` returns a context handle whose methods and
 * accessors live on a shared prototype here. The handle only stores the canvas
 * id (looked up in `canvas.h`'s process-wide registry) and a strong reference
 * to the element handle, so the GC keeps the element alive for as long as the
 * script can still draw.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "canvas.h"
#include "css.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {

namespace {

/** The canvas behind a context handle, or null (stale/foreign handle). */
xtgui::Canvas2D *context_canvas(xt_value self) {
  xt_value value = xt_object_get_cstr(self, "__xt_gui_canvas");
  if (!XT_IS_NUMBER(value)) return nullptr;
  double id = xt_to_number(value);
  if (!(id > 0.0)) return nullptr;
  return xtgui::xt_canvas_by_id((uint64_t)id);
}

double number_arg(int32_t argc, xt_value *argv, int32_t index, double fallback) {
  if (index >= argc) return fallback;
  return xt_to_number(xt_arg(argc, argv, index));
}

std::string string_arg(int32_t argc, xt_value *argv, int32_t index) {
  if (index >= argc) return std::string();
  return to_string_value(xt_arg(argc, argv, index));
}

}  // namespace

METHOD(node_get_context) {
  (void)env;
  std::string mode = string_arg(argc, argv, 0);
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement() || !node->isTag("canvas")) return XT_NULL;
  if (mode != "2d") return XT_NULL;
  xtgui::Canvas2D *canvas = xt_gui_canvas_get(win, node);
  if (canvas == nullptr) return XT_NULL;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_ctx");
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value ctx = xt_object_new_with_proto(canvas_proto());
  xt_object_set(ctx, xt_string_from_cstr("__xt_gui_canvas"), xt_number((double)canvas->id()));
  /* Keep the element handle alive (and reachable) from the context. */
  xt_object_set(ctx, xt_string_from_cstr("__xt_gui_node"), self);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_ctx"), ctx);
  return ctx;
}

METHOD(node_width_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return xt_number(0);
  if (node->isTag("canvas")) {
    xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
    if (canvas != nullptr) return xt_number(canvas->width());
  }
  const std::string *attr = node->attr("width");
  float value = 0.0f;
  if (attr != nullptr && xtgui::xt_css_parse_number(*attr, &value)) return xt_number(value);
  return xt_number(0);
}

METHOD(node_width_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  double value = number_arg(argc, argv, 0, 0.0);
  if (node->isTag("canvas")) {
    xtgui::Canvas2D *canvas = xt_gui_canvas_get(win, node);
    if (canvas != nullptr) {
      canvas->resize((int)value, canvas->height());
      if (win != nullptr && win->document != nullptr) {
        win->document->setCanvasInfo(node, (float)canvas->width(), (float)canvas->height(),
                                     canvas->id());
      }
    }
    return XT_UNDEFINED;
  }
  set_attribute(win, node, "width", std::to_string((long long)value));
  return XT_UNDEFINED;
}

METHOD(node_height_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return xt_number(0);
  if (node->isTag("canvas")) {
    xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
    if (canvas != nullptr) return xt_number(canvas->height());
  }
  const std::string *attr = node->attr("height");
  float value = 0.0f;
  if (attr != nullptr && xtgui::xt_css_parse_number(*attr, &value)) return xt_number(value);
  return xt_number(0);
}

METHOD(node_height_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  double value = number_arg(argc, argv, 0, 0.0);
  if (node->isTag("canvas")) {
    xtgui::Canvas2D *canvas = xt_gui_canvas_get(win, node);
    if (canvas != nullptr) {
      canvas->resize(canvas->width(), (int)value);
      if (win != nullptr && win->document != nullptr) {
        win->document->setCanvasInfo(node, (float)canvas->width(), (float)canvas->height(),
                                     canvas->id());
      }
    }
    return XT_UNDEFINED;
  }
  set_attribute(win, node, "height", std::to_string((long long)value));
  return XT_UNDEFINED;
}

/* -- state / style getters and setters ------------------------------------ */

METHOD(canvas_get_canvas) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_gui_node");
}

METHOD(canvas_fill_style_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->fill_style().c_str() : "#000000");
}

METHOD(canvas_fill_style_set) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->set_fill_style(string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_stroke_style_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->stroke_style().c_str() : "#000000");
}

METHOD(canvas_stroke_style_set) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->set_stroke_style(string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_line_width_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  return xt_number(canvas != nullptr ? canvas->line_width() : 1.0);
}

METHOD(canvas_line_width_set) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->set_line_width((float)number_arg(argc, argv, 0, 1.0));
  return XT_UNDEFINED;
}

METHOD(canvas_global_alpha_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  return xt_number(canvas != nullptr ? canvas->global_alpha() : 1.0);
}

METHOD(canvas_global_alpha_set) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->set_global_alpha((float)number_arg(argc, argv, 0, 1.0));
  return XT_UNDEFINED;
}

METHOD(canvas_font_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->font().c_str() : "10px sans-serif");
}

METHOD(canvas_font_set) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->set_font(string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_text_align_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->text_align().c_str() : "start");
}

METHOD(canvas_text_align_set) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->set_text_align(string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_text_baseline_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->text_baseline().c_str() : "alphabetic");
}

METHOD(canvas_text_baseline_set) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->set_text_baseline(string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

/* -- state / transform ---------------------------------------------------- */

METHOD(canvas_save) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->save();
  return XT_UNDEFINED;
}

METHOD(canvas_restore) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->restore();
  return XT_UNDEFINED;
}

METHOD(canvas_translate) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->translate((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_rotate) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->rotate((float)number_arg(argc, argv, 0, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_scale) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->scale((float)number_arg(argc, argv, 0, 1.0), (float)number_arg(argc, argv, 1, 1.0));
  return XT_UNDEFINED;
}

METHOD(canvas_transform) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->transform((float)number_arg(argc, argv, 0, 1.0), (float)number_arg(argc, argv, 1, 0.0),
                      (float)number_arg(argc, argv, 2, 0.0), (float)number_arg(argc, argv, 3, 1.0),
                      (float)number_arg(argc, argv, 4, 0.0), (float)number_arg(argc, argv, 5, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_set_transform) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->set_transform((float)number_arg(argc, argv, 0, 1.0),
                          (float)number_arg(argc, argv, 1, 0.0),
                          (float)number_arg(argc, argv, 2, 0.0),
                          (float)number_arg(argc, argv, 3, 1.0),
                          (float)number_arg(argc, argv, 4, 0.0),
                          (float)number_arg(argc, argv, 5, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_reset_transform) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->reset_transform();
  return XT_UNDEFINED;
}

/* -- paths ---------------------------------------------------------------- */

METHOD(canvas_begin_path) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->begin_path();
  return XT_UNDEFINED;
}

METHOD(canvas_close_path) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->close_path();
  return XT_UNDEFINED;
}

METHOD(canvas_move_to) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->move_to((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_line_to) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->line_to((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_rect) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->rect((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0),
                 (float)number_arg(argc, argv, 2, 0.0), (float)number_arg(argc, argv, 3, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_arc) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) {
    canvas->arc((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0),
                (float)number_arg(argc, argv, 2, 0.0), (float)number_arg(argc, argv, 3, 0.0),
                (float)number_arg(argc, argv, 4, 0.0), value_truthy(xt_arg(argc, argv, 5)));
  }
  return XT_UNDEFINED;
}

METHOD(canvas_quadratic_curve_to) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->quadratic_curve_to((float)number_arg(argc, argv, 0, 0.0),
                               (float)number_arg(argc, argv, 1, 0.0),
                               (float)number_arg(argc, argv, 2, 0.0),
                               (float)number_arg(argc, argv, 3, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_bezier_curve_to) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->bezier_curve_to((float)number_arg(argc, argv, 0, 0.0),
                            (float)number_arg(argc, argv, 1, 0.0),
                            (float)number_arg(argc, argv, 2, 0.0),
                            (float)number_arg(argc, argv, 3, 0.0),
                            (float)number_arg(argc, argv, 4, 0.0),
                            (float)number_arg(argc, argv, 5, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_fill) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->fill();
  return XT_UNDEFINED;
}

METHOD(canvas_stroke) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr) canvas->stroke();
  return XT_UNDEFINED;
}

METHOD(canvas_clear_rect) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->clear_rect((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0),
                       (float)number_arg(argc, argv, 2, 0.0), (float)number_arg(argc, argv, 3, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_fill_rect) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->fill_rect((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0),
                      (float)number_arg(argc, argv, 2, 0.0), (float)number_arg(argc, argv, 3, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_stroke_rect) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->stroke_rect((float)number_arg(argc, argv, 0, 0.0), (float)number_arg(argc, argv, 1, 0.0),
                        (float)number_arg(argc, argv, 2, 0.0),
                        (float)number_arg(argc, argv, 3, 0.0));
  return XT_UNDEFINED;
}

/* -- text ----------------------------------------------------------------- */

METHOD(canvas_measure_text) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  xt_value metrics = xt_object_new();
  xt_object_set(metrics, xt_string_from_cstr("width"),
                xt_number(canvas != nullptr ? canvas->measure_text(string_arg(argc, argv, 0)) : 0.0));
  return metrics;
}

METHOD(canvas_fill_text) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->fill_text(string_arg(argc, argv, 0), (float)number_arg(argc, argv, 1, 0.0),
                      (float)number_arg(argc, argv, 2, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_stroke_text) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas != nullptr)
    canvas->stroke_text(string_arg(argc, argv, 0), (float)number_arg(argc, argv, 1, 0.0),
                        (float)number_arg(argc, argv, 2, 0.0));
  return XT_UNDEFINED;
}

/* -- images --------------------------------------------------------------- */

METHOD(canvas_draw_image) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  if (canvas == nullptr || argc < 3) return XT_UNDEFINED;
  XtGuiWindow *win = nullptr;
  xtgui::Node *source = resolve_node(xt_arg(argc, argv, 0), &win);
  const xtgui::Image *image = nullptr;
  if (source != nullptr && source->isElement() && source->isTag("img")) {
    const std::string *src = source->attr("src");
    if (src != nullptr) image = xtgui::xt_image_load(*src);
  } else if (source != nullptr) {
    xtgui::Canvas2D *other = xt_gui_canvas_find(win, source);
    if (other != nullptr) image = &other->image();
  }
  if (image == nullptr || image->width <= 0 || image->height <= 0) return XT_UNDEFINED;
  float dx = (float)number_arg(argc, argv, 1, 0.0);
  float dy = (float)number_arg(argc, argv, 2, 0.0);
  float dw = (float)image->width;
  float dh = (float)image->height;
  float sx = 0.0f;
  float sy = 0.0f;
  float sw = (float)image->width;
  float sh = (float)image->height;
  if (argc >= 5) {
    dw = (float)number_arg(argc, argv, 3, (double)dw);
    dh = (float)number_arg(argc, argv, 4, (double)dh);
  }
  if (argc >= 9) {
    sx = (float)number_arg(argc, argv, 5, 0.0);
    sy = (float)number_arg(argc, argv, 6, 0.0);
    sw = (float)number_arg(argc, argv, 7, (double)sw);
    sh = (float)number_arg(argc, argv, 8, (double)sh);
  }
  canvas->draw_image(*image, dx, dy, dw, dh, sx, sy, sw, sh);
  return XT_UNDEFINED;
}

METHOD(canvas_get_image_data) {
  (void)env;
  xtgui::Canvas2D *canvas = context_canvas(self);
  xt_value result = xt_object_new();
  if (canvas == nullptr) return result;
  int x = (int)number_arg(argc, argv, 0, 0.0);
  int y = (int)number_arg(argc, argv, 1, 0.0);
  int width = (int)number_arg(argc, argv, 2, 0.0);
  int height = (int)number_arg(argc, argv, 3, 0.0);
  std::vector<unsigned char> pixels;
  if (!canvas->get_image_data(x, y, width, height, &pixels)) {
    width = 0;
    height = 0;
  }
  xt_value data = xt_array_new(0, nullptr);
  for (unsigned char byte : pixels) xt_array_push(data, xt_number((double)byte));
  xt_object_set(result, xt_string_from_cstr("width"), xt_number((double)width));
  xt_object_set(result, xt_string_from_cstr("height"), xt_number((double)height));
  xt_object_set(result, xt_string_from_cstr("data"), data);
  return result;
}

}  // namespace domapi

#undef METHOD
