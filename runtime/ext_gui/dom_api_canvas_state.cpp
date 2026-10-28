/*
 * xbintsc GUI engine — the Canvas2D state additions.
 *
 * `globalCompositeOperation`, the four shadow properties, the image-smoothing
 * flags, and the path/clip hit-testing entry points. They live apart from the
 * original style bindings so both translation units stay small.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "canvas.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {

namespace {

/** Parse a trailing fill-rule argument (`"evenodd"`/`"nonzero"`). */
bool fill_rule_arg(xt_value value, bool *even_odd) {
  if (!XT_IS_STRING(value)) return false;
  const std::string rule = to_string_value(value);
  if (rule == "evenodd") {
    *even_odd = true;
    return true;
  }
  if (rule == "nonzero") {
    *even_odd = false;
    return true;
  }
  return false;
}

}  // namespace

METHOD(canvas_global_composite_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->global_composite_operation().c_str()
                                               : "source-over");
}

METHOD(canvas_global_composite_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_global_composite_operation(canvas_string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_shadow_color_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->shadow_color().c_str()
                                               : "rgba(0, 0, 0, 0)");
}

METHOD(canvas_shadow_color_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_shadow_color(canvas_string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_shadow_blur_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_number(canvas != nullptr ? canvas->shadow_blur() : 0.0);
}

METHOD(canvas_shadow_blur_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_shadow_blur((float)canvas_number_arg(argc, argv, 0, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_shadow_offset_x_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_number(canvas != nullptr ? canvas->shadow_offset_x() : 0.0);
}

METHOD(canvas_shadow_offset_x_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_shadow_offset_x((float)canvas_number_arg(argc, argv, 0, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_shadow_offset_y_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_number(canvas != nullptr ? canvas->shadow_offset_y() : 0.0);
}

METHOD(canvas_shadow_offset_y_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_shadow_offset_y((float)canvas_number_arg(argc, argv, 0, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_image_smoothing_enabled_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_bool(canvas == nullptr || canvas->image_smoothing_enabled());
}

METHOD(canvas_image_smoothing_enabled_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) {
    canvas->set_image_smoothing_enabled(value_truthy(xt_arg(argc, argv, 0)));
  }
  return XT_UNDEFINED;
}

METHOD(canvas_image_smoothing_quality_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  return xt_string_from_cstr(canvas != nullptr ? canvas->image_smoothing_quality().c_str()
                                               : "low");
}

METHOD(canvas_image_smoothing_quality_set) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->set_image_smoothing_quality(canvas_string_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(canvas_clip) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr) return XT_UNDEFINED;
  bool even_odd = false;
  bool has_path = false;
  std::vector<xtgui::CanvasSubpath> path;
  for (int32_t i = 0; i < argc; i++) {
    xt_value value = xt_arg(argc, argv, i);
    bool rule = false;
    if (fill_rule_arg(value, &rule)) {
      even_odd = rule;
    } else if (path2d_is(value)) {
      path = path2d_subpaths(value);
      has_path = true;
    }
  }
  if (has_path) {
    canvas->clip_paths(path, even_odd);
  } else {
    canvas->clip(even_odd);
  }
  return XT_UNDEFINED;
}

METHOD(canvas_is_point_in_path) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr || argc == 0) return XT_FALSE;
  int32_t index = 0;
  std::vector<xtgui::CanvasSubpath> path;
  bool has_path = false;
  if (path2d_is(xt_arg(argc, argv, 0))) {
    path = path2d_subpaths(xt_arg(argc, argv, 0));
    has_path = true;
    index = 1;
  }
  const float x = (float)canvas_number_arg(argc, argv, index, 0.0);
  const float y = (float)canvas_number_arg(argc, argv, index + 1, 0.0);
  bool even_odd = false;
  for (int32_t i = index + 2; i < argc; i++) fill_rule_arg(xt_arg(argc, argv, i), &even_odd);
  const bool inside =
      has_path ? canvas->is_point_in_paths(path, x, y, even_odd)
               : canvas->is_point_in_path(x, y, even_odd);
  return xt_bool(inside);
}

METHOD(canvas_is_point_in_stroke) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr || argc == 0) return XT_FALSE;
  int32_t index = 0;
  std::vector<xtgui::CanvasSubpath> path;
  bool has_path = false;
  if (path2d_is(xt_arg(argc, argv, 0))) {
    path = path2d_subpaths(xt_arg(argc, argv, 0));
    has_path = true;
    index = 1;
  }
  const float x = (float)canvas_number_arg(argc, argv, index, 0.0);
  const float y = (float)canvas_number_arg(argc, argv, index + 1, 0.0);
  const bool inside =
      has_path ? canvas->is_point_in_stroke_paths(path, x, y) : canvas->is_point_in_stroke(x, y);
  return xt_bool(inside);
}

}  // namespace domapi

#undef METHOD
