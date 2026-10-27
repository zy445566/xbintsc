/*
 * xbintsc GUI engine — the Canvas2D path and fill/stroke bindings.
 *
 * `beginPath`/`moveTo`/… build the context's current path; `fill`/`stroke`
 * paint it (or a `Path2D` supplied as the argument), with the optional
 * `"evenodd"` fill rule honoured.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "canvas.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
/* -- paths ---------------------------------------------------------------- */

namespace {

/** One argument of the fill/stroke family: a `Path2D` handle or a fill rule. */
bool canvas_fill_rule(xt_value value, bool *even_odd) {
  if (!XT_IS_STRING(value)) return false;
  std::string rule = to_string_value(value);
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

/** Collect the `Path2D` arguments of `fill`/`stroke` (the DOM accepts one path
 * plus an optional fill rule, in either order). */
std::vector<std::vector<xtgui::CanvasSubpath>> canvas_path_args(int32_t argc, xt_value *argv,
                                                               bool *even_odd, bool *has_path) {
  std::vector<std::vector<xtgui::CanvasSubpath>> paths;
  *even_odd = false;
  *has_path = false;
  for (int32_t i = 0; i < argc; i++) {
    xt_value value = xt_arg(argc, argv, i);
    bool rule = false;
    if (canvas_fill_rule(value, &rule)) {
      *even_odd = rule;
      continue;
    }
    if (path2d_is(value)) {
      paths.push_back(path2d_subpaths(value));
      *has_path = true;
    }
  }
  return paths;
}

}  // namespace

METHOD(canvas_begin_path) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->begin_path();
  return XT_UNDEFINED;
}

METHOD(canvas_close_path) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) canvas->close_path();
  return XT_UNDEFINED;
}

METHOD(canvas_move_to) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr)
    canvas->move_to((float)canvas_number_arg(argc, argv, 0, 0.0), (float)canvas_number_arg(argc, argv, 1, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_line_to) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr)
    canvas->line_to((float)canvas_number_arg(argc, argv, 0, 0.0), (float)canvas_number_arg(argc, argv, 1, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_rect) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr)
    canvas->rect((float)canvas_number_arg(argc, argv, 0, 0.0), (float)canvas_number_arg(argc, argv, 1, 0.0),
                 (float)canvas_number_arg(argc, argv, 2, 0.0), (float)canvas_number_arg(argc, argv, 3, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_arc) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) {
    canvas->arc((float)canvas_number_arg(argc, argv, 0, 0.0), (float)canvas_number_arg(argc, argv, 1, 0.0),
                (float)canvas_number_arg(argc, argv, 2, 0.0), (float)canvas_number_arg(argc, argv, 3, 0.0),
                (float)canvas_number_arg(argc, argv, 4, 0.0), value_truthy(xt_arg(argc, argv, 5)));
  }
  return XT_UNDEFINED;
}

METHOD(canvas_quadratic_curve_to) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr)
    canvas->quadratic_curve_to((float)canvas_number_arg(argc, argv, 0, 0.0),
                               (float)canvas_number_arg(argc, argv, 1, 0.0),
                               (float)canvas_number_arg(argc, argv, 2, 0.0),
                               (float)canvas_number_arg(argc, argv, 3, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_bezier_curve_to) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr)
    canvas->bezier_curve_to((float)canvas_number_arg(argc, argv, 0, 0.0),
                            (float)canvas_number_arg(argc, argv, 1, 0.0),
                            (float)canvas_number_arg(argc, argv, 2, 0.0),
                            (float)canvas_number_arg(argc, argv, 3, 0.0),
                            (float)canvas_number_arg(argc, argv, 4, 0.0),
                            (float)canvas_number_arg(argc, argv, 5, 0.0));
  return XT_UNDEFINED;
}

METHOD(canvas_fill) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr) return XT_UNDEFINED;
  bool even_odd = false;
  bool has_path = false;
  std::vector<std::vector<xtgui::CanvasSubpath>> paths =
      canvas_path_args(argc, argv, &even_odd, &has_path);
  if (has_path) {
    for (const std::vector<xtgui::CanvasSubpath> &path : paths) {
      canvas->fill_paths(path, even_odd);
    }
  } else {
    canvas->fill(even_odd);
  }
  return XT_UNDEFINED;
}

METHOD(canvas_stroke) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr) return XT_UNDEFINED;
  bool even_odd = false;
  bool has_path = false;
  std::vector<std::vector<xtgui::CanvasSubpath>> paths =
      canvas_path_args(argc, argv, &even_odd, &has_path);
  if (has_path) {
    for (const std::vector<xtgui::CanvasSubpath> &path : paths) {
      canvas->stroke_paths(path);
    }
  } else {
    canvas->stroke();
  }
  return XT_UNDEFINED;
}


#undef METHOD

}  // namespace domapi
