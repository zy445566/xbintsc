/*
 * xbintsc GUI engine — `Path2D`.
 *
 * A path is stored on the runtime object itself as an array of subpaths, each a
 * flat number list `[x0, y0, x1, y1, …]` (user space). Keeping the geometry in
 * the JS heap means the collector owns it: a page that builds a path per frame
 * does not grow a native registry. `ctx.fill(path)` / `ctx.stroke(path)` read it
 * back with `path2d_subpaths` and draw it under the current transform.
 */

#include "dom_api_internal.h"

#include <cmath>
#include <string>
#include <vector>

#include "canvas.h"

namespace domapi {

namespace {

xt_value g_path2d_proto = XT_UNDEFINED;
constexpr float kPi = 3.14159265358979323846f;

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

xt_value path_data(xt_value self, bool create) {
  xt_value data = xt_object_get_cstr(self, "__xt_gui_path_data");
  if (XT_IS_ARRAY(data) || !create) return data;
  data = xt_array_new(0, nullptr);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_path_data"), data);
  return data;
}

xt_value current_subpath(xt_value self, bool create) {
  xt_value data = path_data(self, create);
  if (!XT_IS_ARRAY(data)) return XT_UNDEFINED;
  int32_t count = (int32_t)xt_to_number(xt_array_length(data));
  if (count > 0) return xt_array_get(data, xt_number((double)(count - 1)));
  if (!create) return XT_UNDEFINED;
  xt_value subpath = xt_array_new(0, nullptr);
  xt_array_push(data, subpath);
  return subpath;
}

void push_point(xt_value subpath, float x, float y) {
  xt_array_push(subpath, xt_number(x));
  xt_array_push(subpath, xt_number(y));
}

float number_arg(int32_t argc, xt_value *argv, int32_t index, double fallback) {
  if (index >= argc) return (float)fallback;
  return (float)xt_to_number(xt_arg(argc, argv, index));
}

bool last_point(xt_value subpath, float *x, float *y) {
  int32_t count = (int32_t)xt_to_number(xt_array_length(subpath));
  if (count < 2) return false;
  *x = (float)xt_to_number(xt_array_get(subpath, xt_number((double)(count - 2))));
  *y = (float)xt_to_number(xt_array_get(subpath, xt_number((double)(count - 1))));
  return true;
}

/** Append a flattened polyline, starting a new subpath when none is open. */
void append_points(xt_value self, const std::vector<float> &flat) {
  if (flat.size() < 2) return;
  xt_value subpath = current_subpath(self, true);
  float lx = 0.0f, ly = 0.0f;
  size_t start = 0;
  if (last_point(subpath, &lx, &ly)) {
    if (lx != flat[0] || ly != flat[1]) push_point(subpath, flat[0], flat[1]);
    start = 2;
  }
  for (size_t i = start; i + 1 < flat.size(); i += 2) push_point(subpath, flat[i], flat[i + 1]);
}

void flatten_arc(std::vector<float> *out, float cx, float cy, float rx, float ry, float rotation,
                 float start, float sweep) {
  if (sweep == 0.0f) return;
  int segments = (int)std::ceil(std::fabs(sweep) / (kPi / 32.0f));
  if (segments < 1) segments = 1;
  if (segments > 2048) segments = 2048;
  const float cosRotation = std::cos(rotation);
  const float sinRotation = std::sin(rotation);
  for (int i = 0; i <= segments; i++) {
    float angle = start + sweep * ((float)i / (float)segments);
    float px = rx * std::cos(angle);
    float py = ry * std::sin(angle);
    out->push_back(cx + px * cosRotation - py * sinRotation);
    out->push_back(cy + px * sinRotation + py * cosRotation);
  }
}

void normalize_sweep(float *sweep, bool counterclockwise) {
  if (counterclockwise) {
    while (*sweep > 0.0f) *sweep -= 2.0f * kPi;
    while (*sweep < -2.0f * kPi) *sweep += 2.0f * kPi;
  } else {
    while (*sweep < 0.0f) *sweep += 2.0f * kPi;
    while (*sweep > 2.0f * kPi) *sweep -= 2.0f * kPi;
  }
}

METHOD(path2d_move_to) {
  (void)env;
  xt_value data = path_data(self, true);
  xt_value subpath = xt_array_new(0, nullptr);
  push_point(subpath, number_arg(argc, argv, 0, 0.0), number_arg(argc, argv, 1, 0.0));
  xt_array_push(data, subpath);
  return XT_UNDEFINED;
}

METHOD(path2d_line_to) {
  (void)env;
  xt_value subpath = current_subpath(self, true);
  push_point(subpath, number_arg(argc, argv, 0, 0.0), number_arg(argc, argv, 1, 0.0));
  return XT_UNDEFINED;
}

METHOD(path2d_close_path) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value subpath = current_subpath(self, false);
  if (!XT_IS_ARRAY(subpath)) return XT_UNDEFINED;
  int32_t count = (int32_t)xt_to_number(xt_array_length(subpath));
  if (count < 4) return XT_UNDEFINED;
  float firstX = (float)xt_to_number(xt_array_get(subpath, xt_number(0)));
  float firstY = (float)xt_to_number(xt_array_get(subpath, xt_number(1)));
  float lx = 0.0f, ly = 0.0f;
  if (last_point(subpath, &lx, &ly) && (lx != firstX || ly != firstY)) {
    push_point(subpath, firstX, firstY);
  }
  return XT_UNDEFINED;
}

METHOD(path2d_rect) {
  (void)env;
  float x = number_arg(argc, argv, 0, 0.0);
  float y = number_arg(argc, argv, 1, 0.0);
  float w = number_arg(argc, argv, 2, 0.0);
  float h = number_arg(argc, argv, 3, 0.0);
  xt_value data = path_data(self, true);
  xt_value subpath = xt_array_new(0, nullptr);
  push_point(subpath, x, y);
  push_point(subpath, x + w, y);
  push_point(subpath, x + w, y + h);
  push_point(subpath, x, y + h);
  push_point(subpath, x, y);
  xt_array_push(data, subpath);
  return XT_UNDEFINED;
}

METHOD(path2d_round_rect) {
  (void)env;
  float x = number_arg(argc, argv, 0, 0.0);
  float y = number_arg(argc, argv, 1, 0.0);
  float w = number_arg(argc, argv, 2, 0.0);
  float h = number_arg(argc, argv, 3, 0.0);
  float radius = number_arg(argc, argv, 4, 0.0);
  if (w < 0.0f) {
    x += w;
    w = -w;
  }
  if (h < 0.0f) {
    y += h;
    h = -h;
  }
  float maxRadius = std::min(w, h) * 0.5f;
  if (radius < 0.0f) radius = 0.0f;
  if (radius > maxRadius) radius = maxRadius;
  xt_value data = path_data(self, true);
  xt_value subpath = xt_array_new(0, nullptr);
  std::vector<float> points;
  flatten_arc(&points, x + w - radius, y + radius, radius, radius, 0.0f, -kPi * 0.5f, kPi * 0.5f);
  flatten_arc(&points, x + w - radius, y + h - radius, radius, radius, 0.0f, 0.0f, kPi * 0.5f);
  flatten_arc(&points, x + radius, y + h - radius, radius, radius, 0.0f, kPi * 0.5f, kPi * 0.5f);
  flatten_arc(&points, x + radius, y + radius, radius, radius, 0.0f, kPi, kPi * 0.5f);
  push_point(subpath, x + radius, y);
  for (size_t i = 0; i + 1 < points.size(); i += 2) push_point(subpath, points[i], points[i + 1]);
  push_point(subpath, x + radius, y);
  xt_array_push(data, subpath);
  return XT_UNDEFINED;
}

METHOD(path2d_arc) {
  (void)env;
  float sweep = number_arg(argc, argv, 4, 0.0) - number_arg(argc, argv, 3, 0.0);
  normalize_sweep(&sweep, value_truthy(xt_arg(argc, argv, 5)));
  std::vector<float> points;
  flatten_arc(&points, number_arg(argc, argv, 0, 0.0), number_arg(argc, argv, 1, 0.0),
              number_arg(argc, argv, 2, 0.0), number_arg(argc, argv, 2, 0.0), 0.0f,
              number_arg(argc, argv, 3, 0.0), sweep);
  append_points(self, points);
  return XT_UNDEFINED;
}

METHOD(path2d_ellipse) {
  (void)env;
  float sweep = number_arg(argc, argv, 6, 0.0) - number_arg(argc, argv, 5, 0.0);
  normalize_sweep(&sweep, value_truthy(xt_arg(argc, argv, 7)));
  std::vector<float> points;
  flatten_arc(&points, number_arg(argc, argv, 0, 0.0), number_arg(argc, argv, 1, 0.0),
              number_arg(argc, argv, 2, 0.0), number_arg(argc, argv, 3, 0.0),
              number_arg(argc, argv, 4, 0.0), number_arg(argc, argv, 5, 0.0), sweep);
  append_points(self, points);
  return XT_UNDEFINED;
}

METHOD(path2d_quadratic_curve_to) {
  (void)env;
  xt_value subpath = current_subpath(self, true);
  float lx = 0.0f, ly = 0.0f;
  if (!last_point(subpath, &lx, &ly)) {
    lx = number_arg(argc, argv, 0, 0.0);
    ly = number_arg(argc, argv, 1, 0.0);
    push_point(subpath, lx, ly);
  }
  float cpx = number_arg(argc, argv, 0, 0.0);
  float cpy = number_arg(argc, argv, 1, 0.0);
  float tx = number_arg(argc, argv, 2, 0.0);
  float ty = number_arg(argc, argv, 3, 0.0);
  for (int i = 1; i <= 16; i++) {
    float t = (float)i / 16.0f;
    float a = 1.0f - t;
    push_point(subpath, a * a * lx + 2.0f * a * t * cpx + t * t * tx,
               a * a * ly + 2.0f * a * t * cpy + t * t * ty);
  }
  return XT_UNDEFINED;
}

METHOD(path2d_bezier_curve_to) {
  (void)env;
  xt_value subpath = current_subpath(self, true);
  float lx = 0.0f, ly = 0.0f;
  if (!last_point(subpath, &lx, &ly)) {
    lx = number_arg(argc, argv, 0, 0.0);
    ly = number_arg(argc, argv, 1, 0.0);
    push_point(subpath, lx, ly);
  }
  float c1x = number_arg(argc, argv, 0, 0.0);
  float c1y = number_arg(argc, argv, 1, 0.0);
  float c2x = number_arg(argc, argv, 2, 0.0);
  float c2y = number_arg(argc, argv, 3, 0.0);
  float tx = number_arg(argc, argv, 4, 0.0);
  float ty = number_arg(argc, argv, 5, 0.0);
  for (int i = 1; i <= 24; i++) {
    float t = (float)i / 24.0f;
    float u = 1.0f - t;
    push_point(subpath,
               u * u * u * lx + 3.0f * u * u * t * c1x + 3.0f * u * t * t * c2x + t * t * t * tx,
               u * u * u * ly + 3.0f * u * u * t * c1y + 3.0f * u * t * t * c2y + t * t * t * ty);
  }
  return XT_UNDEFINED;
}

/** `path.addPath(other, matrix?)` copies another path, optionally transformed. */
METHOD(path2d_add_path) {
  (void)env;
  std::vector<xtgui::CanvasSubpath> source = path2d_subpaths(xt_arg(argc, argv, 0));
  if (source.empty()) return XT_UNDEFINED;
  xtgui::CanvasTransform transform;
  if (argc >= 2 && XT_IS_OBJECT(xt_arg(argc, argv, 1))) {
    xt_value matrix = xt_arg(argc, argv, 1);
    static const char *const names[6] = {"a", "b", "c", "d", "e", "f"};
    float values[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 6; i++) {
      xt_value member = xt_object_get_cstr(matrix, names[i]);
      if (XT_IS_NUMBER(member)) values[i] = (float)xt_to_number(member);
    }
    transform = xtgui::CanvasTransform{values[0], values[1], values[2],
                                       values[3], values[4], values[5]};
  }
  xt_value data = path_data(self, true);
  for (const xtgui::CanvasSubpath &subpath : source) {
    xt_value copy = xt_array_new(0, nullptr);
    for (const xtgui::CanvasPoint &point : subpath) {
      float x = point.x, y = point.y;
      transform.apply(point.x, point.y, &x, &y);
      push_point(copy, x, y);
    }
    xt_array_push(data, copy);
  }
  return XT_UNDEFINED;
}

#undef METHOD

}  // namespace

bool path2d_is(xt_value value) { return XT_IS_OBJECT(value) && value_truthy(xt_object_get_cstr(value, "__xt_gui_path2d")); }

std::vector<xtgui::CanvasSubpath> path2d_subpaths(xt_value value) {
  std::vector<xtgui::CanvasSubpath> result;
  if (!path2d_is(value)) return result;
  xt_value data = xt_object_get_cstr(value, "__xt_gui_path_data");
  if (!XT_IS_ARRAY(data)) return result;
  int32_t count = (int32_t)xt_to_number(xt_array_length(data));
  for (int32_t i = 0; i < count; i++) {
    xt_value item = xt_array_get(data, xt_number((double)i));
    if (!XT_IS_ARRAY(item)) continue;
    xtgui::CanvasSubpath subpath;
    int32_t numbers = (int32_t)xt_to_number(xt_array_length(item));
    for (int32_t j = 0; j + 1 < numbers; j += 2) {
      subpath.push_back(xtgui::CanvasPoint{
          (float)xt_to_number(xt_array_get(item, xt_number((double)j))),
          (float)xt_to_number(xt_array_get(item, xt_number((double)(j + 1))))});
    }
    if (!subpath.empty()) result.push_back(std::move(subpath));
  }
  return result;
}

xt_value path2d_proto() {
  if (XT_IS_OBJECT(g_path2d_proto)) return g_path2d_proto;
  g_path2d_proto = xt_object_new();
  xt_gc_add_root(&g_path2d_proto);
  struct Entry {
    const char *name;
    void *fn;
  };
  const Entry methods[] = {
      {"moveTo", (void *)path2d_move_to},
      {"lineTo", (void *)path2d_line_to},
      {"closePath", (void *)path2d_close_path},
      {"rect", (void *)path2d_rect},
      {"roundRect", (void *)path2d_round_rect},
      {"arc", (void *)path2d_arc},
      {"ellipse", (void *)path2d_ellipse},
      {"quadraticCurveTo", (void *)path2d_quadratic_curve_to},
      {"bezierCurveTo", (void *)path2d_bezier_curve_to},
      {"addPath", (void *)path2d_add_path},
  };
  for (const Entry &method : methods) {
    xt_object_set(g_path2d_proto, xt_string_from_cstr(method.name),
                  xt_closure_new(method.fn, 0, nullptr));
  }
  return g_path2d_proto;
}

/** `new Path2D()` and `new Path2D(otherPath)`; a string (SVG path data) is not
 * parsed, so it yields an empty path instead of silently drawing nonsense. */
xt_value path2d_create(xt_value source) {
  xt_value path = xt_object_new_with_proto(path2d_proto());
  xt_object_set(path, xt_string_from_cstr("__xt_gui_path2d"), XT_TRUE);
  xt_object_set(path, xt_string_from_cstr("__xt_gui_path_data"), xt_array_new(0, nullptr));
  if (path2d_is(source)) {
    xt_value data = xt_object_get_cstr(source, "__xt_gui_path_data");
    xt_value copy = xt_array_new(0, nullptr);
    int32_t count = XT_IS_ARRAY(data) ? (int32_t)xt_to_number(xt_array_length(data)) : 0;
    for (int32_t i = 0; i < count; i++) xt_array_push(copy, xt_array_get(data, xt_number((double)i)));
    xt_object_set(path, xt_string_from_cstr("__xt_gui_path_data"), copy);
  }
  return path;
}

}  // namespace domapi
