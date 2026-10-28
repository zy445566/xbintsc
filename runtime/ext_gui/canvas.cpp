/*
 * xbintsc GUI engine — the Canvas2D object model.
 *
 * This unit owns the drawing state, the current path and the draw entry
 * points; the actual pixel work is delegated to `canvas_raster.cpp`. Canvases
 * are registered in a process-wide id table so the GPU renderer can find the
 * bitmap behind a display-list entry without knowing about the DOM.
 */

#include "canvas.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "canvas_internal.h"
#include "perf.h"

namespace xtgui {

namespace {

std::atomic<uint64_t> g_next_canvas_id{1};

/** Process-wide id -> canvas table. The engine is single-threaded; the table
 * lives for the process while the objects are owned by the window.
 *
 * Deliberately leaked: `g_windows` is a global array, so the canvases it owns
 * are destroyed *after* this function-local static would have been, and
 * `~Canvas2D` has to remove itself from the table. Ordering that destruction by
 * hand is not worth it — the table is one map for the whole process. */
std::unordered_map<uint64_t, Canvas2D *> &canvas_registry() {
  static std::unordered_map<uint64_t, Canvas2D *> *registry =
      new std::unordered_map<uint64_t, Canvas2D *>();
  return *registry;
}

CanvasPoint lerp(const CanvasPoint &a, const CanvasPoint &b, float t) {
  return CanvasPoint{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

Canvas2D* xt_canvas_by_id(uint64_t id) {
  auto found = canvas_registry().find(id);
  return found == canvas_registry().end() ? nullptr : found->second;
}

float CanvasTransform::scale() const {
  float determinant = std::fabs(a * d - b * c);
  return std::sqrt(determinant);
}

CanvasTransform CanvasTransform::inverted() const {
  const float determinant = a * d - b * c;
  if (std::fabs(determinant) < 1e-9f) return CanvasTransform();
  const float inv = 1.0f / determinant;
  CanvasTransform result;
  result.a = d * inv;
  result.b = -b * inv;
  result.c = -c * inv;
  result.d = a * inv;
  result.e = (c * f - d * e) * inv;
  result.f = (b * e - a * f) * inv;
  return result;
}

Canvas2D::Canvas2D(int width, int height) : id_(g_next_canvas_id.fetch_add(1)) {
  resize(width, height);
  canvas_registry()[id_] = this;
}

Canvas2D::~Canvas2D() {
  std::unordered_map<uint64_t, Canvas2D *> &registry = canvas_registry();
  auto found = registry.find(id_);
  if (found != registry.end() && found->second == this) registry.erase(found);
}

void Canvas2D::resize(int width, int height) {
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  image_.width = width;
  image_.height = height;
  image_.pixels.assign((size_t)width * (size_t)height * 4, 0);
  /* The DOM resets the rendering context when the bitmap is resized; the clip
   * region in particular has device-space dimensions and cannot survive. */
  state_.clip.reset();
  shadow_layer_.pixels.clear();
  if (!alpha_) {
    /* `{ alpha: false }`: the canvas has an opaque black backdrop, so cleared
     * pixels are black rather than transparent. */
    for (size_t i = 3; i < image_.pixels.size(); i += 4) image_.pixels[i] = 255;
  }
  mark_all_dirty();
  generation_++;
}

void Canvas2D::set_alpha(bool alpha) {
  if (alpha_ == alpha) return;
  alpha_ = alpha;
  resize(image_.width, image_.height);
}

void Canvas2D::save() { stack_.push_back(state_); }

void Canvas2D::restore() {
  if (stack_.empty()) return;
  state_ = stack_.back();
  stack_.pop_back();
}

void Canvas2D::translate(float x, float y) {
  transform(1.0f, 0.0f, 0.0f, 1.0f, x, y);
}

void Canvas2D::rotate(float radians) {
  float c = std::cos(radians);
  float s = std::sin(radians);
  transform(c, s, -s, c, 0.0f, 0.0f);
}

void Canvas2D::scale(float sx, float sy) { transform(sx, 0.0f, 0.0f, sy, 0.0f, 0.0f); }

void Canvas2D::transform(float a, float b, float c, float d, float e, float f) {
  /* Post-multiply: the new matrix applies *after* the current one, so
   * `translate(x, y); rotate(a)` rotates around the translated origin (the
   * canvas rules) instead of rotating the translation itself. */
  const CanvasTransform &m = state_.transform;
  CanvasTransform next;
  next.a = m.a * a + m.c * b;
  next.b = m.b * a + m.d * b;
  next.c = m.a * c + m.c * d;
  next.d = m.b * c + m.d * d;
  next.e = m.a * e + m.c * f + m.e;
  next.f = m.b * e + m.d * f + m.f;
  state_.transform = next;
}

void Canvas2D::set_transform(float a, float b, float c, float d, float e, float f) {
  state_.transform = CanvasTransform{a, b, c, d, e, f};
}

void Canvas2D::begin_path() { path_.clear(); }

void Canvas2D::move_to(float x, float y) { path_.push_back(CanvasSubpath{CanvasPoint{x, y}}); }

void Canvas2D::line_to(float x, float y) {
  if (path_.empty()) path_.push_back(CanvasSubpath{});
  path_.back().push_back(CanvasPoint{x, y});
}

void Canvas2D::close_path() {
  if (path_.empty() || path_.back().size() < 2) return;
  CanvasPoint first = path_.back().front();
  CanvasPoint last = path_.back().back();
  if (first.x != last.x || first.y != last.y) path_.back().push_back(first);
}

void Canvas2D::rect(float x, float y, float width, float height) {
  path_.push_back(CanvasSubpath{CanvasPoint{x, y}, CanvasPoint{x + width, y},
                                CanvasPoint{x + width, y + height}, CanvasPoint{x, y + height},
                                CanvasPoint{x, y}});
}

void Canvas2D::flatten_arc(float cx, float cy, float radius, float start, float end, bool ccw,
                           CanvasSubpath *out) const {
  float sweep = end - start;
  if (ccw) {
    while (sweep > 0.0f) sweep -= 2.0f * 3.14159265358979323846f;
    while (sweep < -2.0f * 3.14159265358979323846f) sweep += 2.0f * 3.14159265358979323846f;
  } else {
    while (sweep < 0.0f) sweep += 2.0f * 3.14159265358979323846f;
    while (sweep > 2.0f * 3.14159265358979323846f) sweep -= 2.0f * 3.14159265358979323846f;
  }
  int segments = (int)std::ceil(std::fabs(sweep) / (3.14159265358979323846f / 32.0f));
  if (segments < 1) segments = 1;
  if (segments > 2048) segments = 2048;
  for (int i = 0; i <= segments; i++) {
    float angle = start + sweep * ((float)i / (float)segments);
    out->push_back(CanvasPoint{cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
  }
}

void Canvas2D::append_path_points(const CanvasSubpath &points) {
  if (points.empty()) return;
  if (path_.empty() || path_.back().empty()) {
    path_.push_back(CanvasSubpath{points.front()});
  } else {
    CanvasPoint last = path_.back().back();
    if (last.x != points.front().x || last.y != points.front().y) {
      path_.back().push_back(points.front());
    }
  }
  for (size_t i = 1; i < points.size(); i++) path_.back().push_back(points[i]);
}

void Canvas2D::arc(float cx, float cy, float radius, float start, float end, bool ccw) {
  CanvasSubpath arcPoints;
  flatten_arc(cx, cy, radius, start, end, ccw, &arcPoints);
  append_path_points(arcPoints);
}

void Canvas2D::ellipse(float cx, float cy, float rx, float ry, float rotation, float start,
                       float end, bool counterclockwise) {
  float sweep = end - start;
  if (counterclockwise) {
    while (sweep > 0.0f) sweep -= 2.0f * kPi;
    while (sweep < -2.0f * kPi) sweep += 2.0f * kPi;
  } else {
    while (sweep < 0.0f) sweep += 2.0f * kPi;
    while (sweep > 2.0f * kPi) sweep -= 2.0f * kPi;
  }
  int segments = (int)std::ceil(std::fabs(sweep) / (kPi / 32.0f));
  if (segments < 1) segments = 1;
  if (segments > 2048) segments = 2048;
  const float cosRotation = std::cos(rotation);
  const float sinRotation = std::sin(rotation);
  CanvasSubpath points;
  points.reserve((size_t)segments + 1);
  for (int i = 0; i <= segments; i++) {
    float angle = start + sweep * ((float)i / (float)segments);
    float px = rx * std::cos(angle);
    float py = ry * std::sin(angle);
    points.push_back(CanvasPoint{cx + px * cosRotation - py * sinRotation,
                                 cy + px * sinRotation + py * cosRotation});
  }
  append_path_points(points);
}

void Canvas2D::round_rect(float x, float y, float width, float height, float radius_tl,
                          float radius_tr, float radius_br, float radius_bl) {
  if (width < 0.0f) {
    x += width;
    width = -width;
  }
  if (height < 0.0f) {
    y += height;
    height = -height;
  }
  const float maxRadius = std::min(width, height) * 0.5f;
  float radii[4] = {radius_tl, radius_tr, radius_br, radius_bl};
  for (float &radius : radii) {
    if (radius < 0.0f) radius = 0.0f;
    if (radius > maxRadius) radius = maxRadius;
  }
  const float tl = radii[0];
  const float tr = radii[1];
  const float br = radii[2];
  const float bl = radii[3];
  move_to(x + tl, y);
  line_to(x + width - tr, y);
  quadratic_curve_to(x + width, y, x + width, y + tr);
  line_to(x + width, y + height - br);
  quadratic_curve_to(x + width, y + height, x + width - br, y + height);
  line_to(x + bl, y + height);
  quadratic_curve_to(x, y + height, x, y + height - bl);
  line_to(x, y + tl);
  quadratic_curve_to(x, y, x + tl, y);
  close_path();
}

void Canvas2D::quadratic_curve_to(float cpx, float cpy, float x, float y) {
  if (path_.empty() || path_.back().empty()) move_to(cpx, cpy);
  CanvasPoint start = path_.back().back();
  CanvasPoint control{cpx, cpy};
  CanvasPoint target{x, y};
  const int segments = 16;
  for (int i = 1; i <= segments; i++) {
    float t = (float)i / (float)segments;
    CanvasPoint a = lerp(start, control, t);
    CanvasPoint b = lerp(control, target, t);
    CanvasPoint point = lerp(a, b, t);
    path_.back().push_back(point);
  }
}

void Canvas2D::bezier_curve_to(float c1x, float c1y, float c2x, float c2y, float x, float y) {
  if (path_.empty() || path_.back().empty()) move_to(c1x, c1y);
  CanvasPoint p0 = path_.back().back();
  CanvasPoint p1{c1x, c1y};
  CanvasPoint p2{c2x, c2y};
  CanvasPoint p3{x, y};
  const int segments = 24;
  for (int i = 1; i <= segments; i++) {
    float t = (float)i / (float)segments;
    float u = 1.0f - t;
    CanvasPoint point{u * u * u * p0.x + 3.0f * u * u * t * p1.x + 3.0f * u * t * t * p2.x + t * t * t * p3.x,
                      u * u * u * p0.y + 3.0f * u * u * t * p1.y + 3.0f * u * t * t * p2.y + t * t * t * p3.y};
    path_.back().push_back(point);
  }
}

void Canvas2D::to_device(float x, float y, float *outX, float *outY) const {
  state_.transform.apply(x, y, outX, outY);
}

void Canvas2D::mark_dirty(float x0, float y0, float x1, float y1) {
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  const int ix0 = std::max(0, (int)std::floor(x0));
  const int iy0 = std::max(0, (int)std::floor(y0));
  const int ix1 = std::min(image_.width, (int)std::ceil(x1));
  const int iy1 = std::min(image_.height, (int)std::ceil(y1));
  if (ix1 <= ix0 || iy1 <= iy0) return;
  if (dirty_.empty()) {
    dirty_ = CanvasDirtyRect{ix0, iy0, ix1, iy1};
    return;
  }
  dirty_.x0 = std::min(dirty_.x0, ix0);
  dirty_.y0 = std::min(dirty_.y0, iy0);
  dirty_.x1 = std::max(dirty_.x1, ix1);
  dirty_.y1 = std::max(dirty_.y1, iy1);
}

void Canvas2D::mark_path_dirty(const std::vector<CanvasSubpath> &subpaths, float expand) {
  bool have = false;
  float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
  for (const CanvasSubpath &subpath : subpaths) {
    for (const CanvasPoint &point : subpath) {
      if (!have) {
        minX = maxX = point.x;
        minY = maxY = point.y;
        have = true;
      } else {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
      }
    }
  }
  if (!have) return;
  mark_dirty(minX - expand, minY - expand, maxX + expand, maxY + expand);
}

void Canvas2D::mark_all_dirty() {
  dirty_ = CanvasDirtyRect{0, 0, image_.width, image_.height};
}

float Canvas2D::stroke_extent() const {
  float scale = state_.transform.scale();
  if (!(scale > 0.0f)) scale = 1.0f;
  float half = state_.line_width * scale * 0.5f;
  if (!(half > 0.0f)) half = 0.5f;
  return half * std::max(1.0f, state_.miter_limit) + 1.0f;
}

std::vector<CanvasSubpath> Canvas2D::device_subpaths() const {
  return device_subpaths_of(path_);
}

std::vector<CanvasSubpath> Canvas2D::device_subpaths_of(
    const std::vector<CanvasSubpath> &paths) const {
  std::vector<CanvasSubpath> result;
  result.reserve(paths.size());
  for (const CanvasSubpath &subpath : paths) {
    CanvasSubpath device;
    device.reserve(subpath.size());
    for (const CanvasPoint &point : subpath) {
      CanvasPoint mapped;
      to_device(point.x, point.y, &mapped.x, &mapped.y);
      device.push_back(mapped);
    }
    result.push_back(std::move(device));
  }
  return result;
}

CanvasPaint Canvas2D::make_paint(uint64_t gradient_id, uint64_t pattern_id,
                                 const float color[4]) const {
  CanvasPaint paint;
  paint.color = color;
  paint.gradient = gradient_id == 0 ? nullptr : find_gradient(gradient_id);
  paint.pattern = pattern_id == 0 ? nullptr : find_pattern(pattern_id);
  paint.inverse = state_.transform.inverted();
  paint.composite = state_.global_composite;
  return paint;
}

CanvasPaint Canvas2D::fill_paint() const {
  return make_paint(state_.fill_gradient, state_.fill_pattern, state_.fill_color);
}

CanvasPaint Canvas2D::stroke_paint() const {
  return make_paint(state_.stroke_gradient, state_.stroke_pattern, state_.stroke_color);
}

void Canvas2D::fill(bool even_odd) {
  if (begin_shadow_layer()) {
    fill(even_odd);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  std::vector<CanvasSubpath> subpaths = device_subpaths();
  std::vector<CanvasSubpath> loops;
  for (CanvasSubpath &subpath : subpaths) {
    if (subpath.size() < 3) continue;
    loops.push_back(std::move(subpath));
  }
  if (loops.empty()) return;
  mark_path_dirty(loops, 0.0f);
  canvas_detail::fill_loops(image_.pixels, image_.width, image_.height, loops, fill_paint(),
                            state_.global_alpha, even_odd, clip_mask());
  generation_++;
}

void Canvas2D::fill_paths(const std::vector<CanvasSubpath> &paths, bool even_odd) {
  if (begin_shadow_layer()) {
    fill_paths(paths, even_odd);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  std::vector<CanvasSubpath> subpaths = device_subpaths_of(paths);
  std::vector<CanvasSubpath> loops;
  for (CanvasSubpath &subpath : subpaths) {
    if (subpath.size() < 3) continue;
    loops.push_back(std::move(subpath));
  }
  if (loops.empty()) return;
  mark_path_dirty(loops, 0.0f);
  canvas_detail::fill_loops(image_.pixels, image_.width, image_.height, loops, fill_paint(),
                            state_.global_alpha, even_odd, clip_mask());
  generation_++;
}

void Canvas2D::stroke() {
  if (begin_shadow_layer()) {
    stroke();
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  std::vector<CanvasSubpath> subpaths = device_subpaths();
  mark_path_dirty(subpaths, stroke_extent());
  const CanvasPaint paint = stroke_paint();
  for (const CanvasSubpath &subpath : subpaths) {
    if (subpath.size() < 2) continue;
    bool closed = subpath.size() >= 3 && subpath.front().x == subpath.back().x &&
                  subpath.front().y == subpath.back().y;
    stroke_subpath(subpath, closed, paint);
  }
  generation_++;
}

void Canvas2D::stroke_paths(const std::vector<CanvasSubpath> &paths) {
  if (begin_shadow_layer()) {
    stroke_paths(paths);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  std::vector<CanvasSubpath> subpaths = device_subpaths_of(paths);
  mark_path_dirty(subpaths, stroke_extent());
  const CanvasPaint paint = stroke_paint();
  for (const CanvasSubpath &subpath : subpaths) {
    if (subpath.size() < 2) continue;
    bool closed = subpath.size() >= 3 && subpath.front().x == subpath.back().x &&
                  subpath.front().y == subpath.back().y;
    stroke_subpath(subpath, closed, paint);
  }
  generation_++;
}

void Canvas2D::clear_rect(float x, float y, float width, float height) {
  g_perf_frame.canvasDraws += 1;
  float x0, y0, x1, y1;
  to_device(x, y, &x0, &y0);
  to_device(x + width, y + height, &x1, &y1);
  mark_dirty(x0, y0, x1, y1);
  canvas_detail::clear_rect(image_.pixels, image_.width, image_.height, x0, y0, x1, y1,
                            clip_mask());
  generation_++;
}

void Canvas2D::fill_rect(float x, float y, float width, float height) {
  if (begin_shadow_layer()) {
    fill_rect(x, y, width, height);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  if (state_.transform.axis_aligned()) {
    float x0, y0, x1, y1;
    to_device(x, y, &x0, &y0);
    to_device(x + width, y + height, &x1, &y1);
    mark_dirty(x0, y0, x1, y1);
    canvas_detail::fill_rect(image_.pixels, image_.width, image_.height, x0, y0, x1, y1,
                             fill_paint(), state_.global_alpha, clip_mask());
  } else {
    CanvasSubpath loop;
    const float corners[4][2] = {
        {x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
    for (const auto &corner : corners) {
      CanvasPoint point;
      to_device(corner[0], corner[1], &point.x, &point.y);
      loop.push_back(point);
    }
    mark_path_dirty(std::vector<CanvasSubpath>{loop}, 0.0f);
    canvas_detail::fill_loop(image_.pixels, image_.width, image_.height, loop, fill_paint(),
                             state_.global_alpha, false, clip_mask());
  }
  generation_++;
}

void Canvas2D::stroke_rect(float x, float y, float width, float height) {
  if (begin_shadow_layer()) {
    stroke_rect(x, y, width, height);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  CanvasSubpath loop;
  const float corners[5][2] = {
      {x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}, {x, y}};
  for (const auto &corner : corners) {
    CanvasPoint point;
    to_device(corner[0], corner[1], &point.x, &point.y);
    loop.push_back(point);
  }
  stroke_subpath(loop, true, stroke_paint());
  mark_path_dirty(std::vector<CanvasSubpath>{loop}, stroke_extent());
  generation_++;
}

void Canvas2D::draw_image(const Image &source, float dx, float dy, float dw, float dh, float sx,
                          float sy, float sw, float sh) {
  if (begin_shadow_layer()) {
    draw_image(source, dx, dy, dw, dh, sx, sy, sw, sh);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  if (state_.transform.axis_aligned()) {
    float x0, y0, x1, y1;
    to_device(dx, dy, &x0, &y0);
    to_device(dx + dw, dy + dh, &x1, &y1);
    mark_dirty(x0, y0, x1, y1);
    canvas_detail::draw_image(image_.pixels, image_.width, image_.height, source, x0, y0, x1 - x0,
                              y1 - y0, sx, sy, sw, sh, state_.global_alpha,
                              state_.image_smoothing, clip_mask());
  } else {
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    const float corners[4][2] = {{dx, dy}, {dx + dw, dy}, {dx + dw, dy + dh}, {dx, dy + dh}};
    for (int i = 0; i < 4; i++) {
      float px = 0.0f, py = 0.0f;
      to_device(corners[i][0], corners[i][1], &px, &py);
      if (i == 0) {
        minX = maxX = px;
        minY = maxY = py;
      } else {
        minX = std::min(minX, px);
        maxX = std::max(maxX, px);
        minY = std::min(minY, py);
        maxY = std::max(maxY, py);
      }
    }
    mark_dirty(minX, minY, maxX, maxY);
    canvas_detail::draw_image_affine(image_.pixels, image_.width, image_.height, source, dx, dy,
                                     dw, dh, sx, sy, sw, sh, state_.transform,
                                     state_.global_alpha, state_.image_smoothing, clip_mask());
  }
  generation_++;
}

bool Canvas2D::get_image_data(int x, int y, int width, int height,
                              std::vector<unsigned char> *out) const {
  if (width <= 0 || height <= 0 || out == nullptr) return false;
  out->assign((size_t)width * (size_t)height * 4, 0);
  for (int py = 0; py < height; py++) {
    int sourceY = y + py;
    if (sourceY < 0 || sourceY >= image_.height) continue;
    for (int px = 0; px < width; px++) {
      int sourceX = x + px;
      if (sourceX < 0 || sourceX >= image_.width) continue;
      const unsigned char *src =
          image_.pixels.data() + ((size_t)sourceY * (size_t)image_.width + (size_t)sourceX) * 4;
      unsigned char *dst = out->data() + ((size_t)py * (size_t)width + (size_t)px) * 4;
      std::memcpy(dst, src, 4);
    }
  }
  return true;
}

}  // namespace xtgui
