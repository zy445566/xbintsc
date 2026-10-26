/*
 * xbintsc GUI engine — the newer half of the Canvas2D DOM surface.
 *
 * `dom_api_canvas.cpp` owns the original path/rect/text/image bindings and the
 * shared context helpers; this unit adds gradients, ellipses, rounded
 * rectangles, line caps/joins, dash patterns and `getTransform`. Keeping them
 * separate keeps both translation units under the repository's file budget.
 */

#include "dom_api_internal.h"

#include <cmath>
#include <string>
#include <vector>

#include "canvas.h"
#include "css.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {

xt_value canvas_gradient_handle(xt_value context, const xtgui::Canvas2D *canvas, uint64_t id) {
  const std::string key = "__xt_gui_grad_" + std::to_string(id);
  xt_value cached = xt_object_get_cstr(context, key.c_str());
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value handle = xt_object_new_with_proto(canvas_gradient_proto());
  if (canvas != nullptr) {
    xt_object_set(handle, xt_string_from_cstr("__xt_gui_canvas"), xt_number((double)canvas->id()));
  }
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_gradient"), xt_number((double)id));
  xt_object_set(context, xt_string_from_cstr(key.c_str()), handle);
  return handle;
}

uint64_t canvas_gradient_id(xt_value handle, const xtgui::Canvas2D *canvas) {
  if (canvas == nullptr || !XT_IS_OBJECT(handle)) return 0;
  xt_value canvasId = xt_object_get_cstr(handle, "__xt_gui_canvas");
  xt_value gradientId = xt_object_get_cstr(handle, "__xt_gui_gradient");
  if (!XT_IS_NUMBER(canvasId) || !XT_IS_NUMBER(gradientId)) return 0;
  if ((uint64_t)xt_to_number(canvasId) != canvas->id()) return 0;
  uint64_t id = (uint64_t)xt_to_number(gradientId);
  return canvas->find_gradient(id) == nullptr ? 0 : id;
}

/* -- gradients ------------------------------------------------------------ */

METHOD(canvas_create_linear_gradient) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr) return XT_NULL;
  xtgui::CanvasGradient *gradient = canvas->create_linear_gradient(
      (float)canvas_number_arg(argc, argv, 0, 0.0), (float)canvas_number_arg(argc, argv, 1, 0.0),
      (float)canvas_number_arg(argc, argv, 2, 0.0), (float)canvas_number_arg(argc, argv, 3, 0.0));
  return canvas_gradient_handle(self, canvas, gradient->id());
}

METHOD(canvas_create_radial_gradient) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr) return XT_NULL;
  xtgui::CanvasGradient *gradient = canvas->create_radial_gradient(
      (float)canvas_number_arg(argc, argv, 0, 0.0), (float)canvas_number_arg(argc, argv, 1, 0.0),
      (float)canvas_number_arg(argc, argv, 2, 0.0), (float)canvas_number_arg(argc, argv, 3, 0.0),
      (float)canvas_number_arg(argc, argv, 4, 0.0), (float)canvas_number_arg(argc, argv, 5, 0.0));
  return canvas_gradient_handle(self, canvas, gradient->id());
}

METHOD(canvas_gradient_add_color_stop) {
  (void)env;
  xt_value canvasId = xt_object_get_cstr(self, "__xt_gui_canvas");
  xt_value gradientId = xt_object_get_cstr(self, "__xt_gui_gradient");
  if (!XT_IS_NUMBER(canvasId) || !XT_IS_NUMBER(gradientId)) return XT_UNDEFINED;
  xtgui::Canvas2D *canvas = xtgui::xt_canvas_by_id((uint64_t)xt_to_number(canvasId));
  if (canvas == nullptr) return XT_UNDEFINED;
  xtgui::CanvasGradient *gradient = canvas->find_gradient((uint64_t)xt_to_number(gradientId));
  if (gradient == nullptr) return XT_UNDEFINED;
  xtgui::Color color = xtgui::xt_css_parse_color(canvas_string_arg(argc, argv, 1));
  if (!color.valid) return XT_UNDEFINED;
  float rgba[4] = {color.r, color.g, color.b, color.a};
  gradient->add_color_stop((float)canvas_number_arg(argc, argv, 0, 0.0), rgba);
  return XT_UNDEFINED;
}

/* -- paths ---------------------------------------------------------------- */

METHOD(canvas_ellipse) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) {
    canvas->ellipse((float)canvas_number_arg(argc, argv, 0, 0.0),
                    (float)canvas_number_arg(argc, argv, 1, 0.0),
                    (float)canvas_number_arg(argc, argv, 2, 0.0),
                    (float)canvas_number_arg(argc, argv, 3, 0.0),
                    (float)canvas_number_arg(argc, argv, 4, 0.0),
                    (float)canvas_number_arg(argc, argv, 5, 0.0),
                    (float)canvas_number_arg(argc, argv, 6, 0.0),
                    value_truthy(xt_arg(argc, argv, 7)));
  }
  return XT_UNDEFINED;
}

METHOD(canvas_round_rect) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) {
    canvas->round_rect((float)canvas_number_arg(argc, argv, 0, 0.0),
                       (float)canvas_number_arg(argc, argv, 1, 0.0),
                       (float)canvas_number_arg(argc, argv, 2, 0.0),
                       (float)canvas_number_arg(argc, argv, 3, 0.0),
                       (float)canvas_number_arg(argc, argv, 4, 0.0));
  }
  return XT_UNDEFINED;
}

/* -- line styles ---------------------------------------------------------- */

METHOD(canvas_line_cap_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->line_cap().c_str() : "butt");
}

METHOD(canvas_line_cap_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_line_cap(canvas_string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_line_join_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->line_join().c_str() : "miter");
}

METHOD(canvas_line_join_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_line_join(canvas_string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_miter_limit_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_number(canvas != nullptr ? canvas->miter_limit() : 10.0);
}

METHOD(canvas_miter_limit_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_miter_limit((float)canvas_number_arg(argc, argv, 0, 10.0));
  return XT_UNDEFINED;
}

METHOD(canvas_line_dash_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  xt_value result = xt_array_new(0, nullptr);
  if (canvas != nullptr) {
    for (float entry : canvas->line_dash()) xt_array_push(result, xt_number(entry));
  }
  return result;
}

METHOD(canvas_line_dash_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr) return XT_UNDEFINED;
  std::vector<float> pattern;
  if (argc > 0) {
    xt_value value = xt_arg(argc, argv, 0);
    if (XT_IS_ARRAY(value)) {
      int32_t size = xt_array_size(value);
      for (int32_t i = 0; i < size; i++) {
        pattern.push_back((float)xt_to_number(xt_array_get(value, xt_number((double)i))));
      }
    }
  }
  canvas->set_line_dash(pattern);
  return XT_UNDEFINED;
}

METHOD(canvas_line_dash_offset_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_number(canvas != nullptr ? canvas->line_dash_offset() : 0.0);
}

METHOD(canvas_line_dash_offset_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) {
    canvas->set_line_dash_offset((float)canvas_number_arg(argc, argv, 0, 0.0));
  }
  return XT_UNDEFINED;
}

/* -- transform ------------------------------------------------------------ */

METHOD(canvas_get_transform) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  const xtgui::CanvasTransform transform =
      canvas != nullptr ? canvas->current_transform() : xtgui::CanvasTransform();
  xt_value result = xt_object_new();
  xt_object_set(result, xt_string_from_cstr("a"), xt_number(transform.a));
  xt_object_set(result, xt_string_from_cstr("b"), xt_number(transform.b));
  xt_object_set(result, xt_string_from_cstr("c"), xt_number(transform.c));
  xt_object_set(result, xt_string_from_cstr("d"), xt_number(transform.d));
  xt_object_set(result, xt_string_from_cstr("e"), xt_number(transform.e));
  xt_object_set(result, xt_string_from_cstr("f"), xt_number(transform.f));
  xt_object_set(result, xt_string_from_cstr("is2D"), xt_bool(true));
  return result;
}

METHOD(node_width_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return xt_number(0);
  if (node->isTag("canvas")) {
    if (xtgui::WebGLContext *webgl = xt_gui_webgl_find(win, node)) {
      return xt_number(webgl->width());
    }
    xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
    if (canvas != nullptr) return xt_number(canvas->width());
  }
  const std::string *attr = node->attr("width");
  float value = 0.0f;
  if (attr != nullptr && xtgui::xt_css_parse_number(*attr, &value)) return xt_number(value);
  /* A canvas with no context still reports the CSS default bitmap size. */
  return xt_number(node->isTag("canvas") ? 300 : 0);
}

METHOD(node_width_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  double value = canvas_number_arg(argc, argv, 0, 0.0);
  if (node->isTag("canvas")) {
    if (xtgui::WebGLContext *webgl = xt_gui_webgl_find(win, node)) {
      webgl->resize((int)value, webgl->height());
      /* A resize replaces the colour texture; the page has to re-upload the
       * image op even when the box size did not change. */
      mark_dirty(win);
      if (win != nullptr && win->document != nullptr) {
        win->document->setCanvasInfo(node, (float)webgl->width(), (float)webgl->height(),
                                     webgl->id());
      }
      return XT_UNDEFINED;
    }
    xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
    if (canvas != nullptr) {
      canvas->resize((int)value, canvas->height());
      if (win != nullptr && win->document != nullptr) {
        win->document->setCanvasInfo(node, (float)canvas->width(), (float)canvas->height(),
                                     canvas->id());
      }
      return XT_UNDEFINED;
    }
    /* No context yet: update the attribute so a later getContext uses it. */
    int height = 150;
    const std::string *height_attr = node->attr("height");
    float parsed = 0.0f;
    if (height_attr != nullptr && xtgui::xt_css_parse_number(*height_attr, &parsed)) {
      height = (int)(parsed + 0.5f);
    }
    set_attribute(win, node, "width", std::to_string((long long)value));
    if (win != nullptr && win->document != nullptr) {
      win->document->setCanvasInfo(node, (float)value, (float)height, 0);
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
    if (xtgui::WebGLContext *webgl = xt_gui_webgl_find(win, node)) {
      return xt_number(webgl->height());
    }
    xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
    if (canvas != nullptr) return xt_number(canvas->height());
  }
  const std::string *attr = node->attr("height");
  float value = 0.0f;
  if (attr != nullptr && xtgui::xt_css_parse_number(*attr, &value)) return xt_number(value);
  return xt_number(node->isTag("canvas") ? 150 : 0);
}

METHOD(node_height_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  double value = canvas_number_arg(argc, argv, 0, 0.0);
  if (node->isTag("canvas")) {
    if (xtgui::WebGLContext *webgl = xt_gui_webgl_find(win, node)) {
      webgl->resize(webgl->width(), (int)value);
      mark_dirty(win);
      if (win != nullptr && win->document != nullptr) {
        win->document->setCanvasInfo(node, (float)webgl->width(), (float)webgl->height(),
                                     webgl->id());
      }
      return XT_UNDEFINED;
    }
    xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
    if (canvas != nullptr) {
      canvas->resize(canvas->width(), (int)value);
      if (win != nullptr && win->document != nullptr) {
        win->document->setCanvasInfo(node, (float)canvas->width(), (float)canvas->height(),
                                     canvas->id());
      }
      return XT_UNDEFINED;
    }
    int width = 300;
    const std::string *width_attr = node->attr("width");
    float parsed = 0.0f;
    if (width_attr != nullptr && xtgui::xt_css_parse_number(*width_attr, &parsed)) {
      width = (int)(parsed + 0.5f);
    }
    set_attribute(win, node, "height", std::to_string((long long)value));
    if (win != nullptr && win->document != nullptr) {
      win->document->setCanvasInfo(node, (float)width, (float)value, 0);
    }
    return XT_UNDEFINED;
  }
  set_attribute(win, node, "height", std::to_string((long long)value));
  return XT_UNDEFINED;
}

/* -- state / style getters and setters ------------------------------------ */


}  // namespace domapi

#undef METHOD