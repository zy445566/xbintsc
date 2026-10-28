/*
 * xbintsc GUI engine — Canvas2D clipping and hit testing.
 *
 * `clip()` rasterises the current path into a device-space coverage mask and
 * intersects it with the region already in effect. `save`/`restore` share the
 * mask through a `shared_ptr`; a later `clip()` installs a new mask, so a saved
 * state can never observe a mutated region.
 *
 * `isPointInPath`/`isPointInStroke` run against the same device-space geometry
 * the rasteriser would use, which keeps them consistent with anti-aliasing and
 * the current transform.
 */

#include "canvas_internal.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace xtgui {

namespace {

bool point_in_loops(const std::vector<CanvasSubpath> &loops, float x, float y, bool even_odd) {
  int winding = 0;
  int crossings = 0;
  for (const CanvasSubpath &loop : loops) {
    if (loop.size() < 2) continue;
    for (size_t i = 0; i < loop.size(); i++) {
      const CanvasPoint &p0 = loop[i];
      const CanvasPoint &p1 = loop[(i + 1) % loop.size()];
      if ((p0.y > y) == (p1.y > y)) continue;
      const float intersection = p0.x + (y - p0.y) * (p1.x - p0.x) / (p1.y - p0.y);
      if (intersection <= x) continue;
      if (p1.y > p0.y) winding++;
      else winding--;
      crossings++;
    }
  }
  return even_odd ? (crossings % 2) != 0 : winding != 0;
}

float distance_to_segment(const CanvasPoint &p, const CanvasPoint &a, const CanvasPoint &b) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float lengthSquared = dx * dx + dy * dy;
  if (lengthSquared <= 1e-9f) return std::hypot(p.x - a.x, p.y - a.y);
  float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared;
  t = std::max(0.0f, std::min(1.0f, t));
  const float cx = a.x + t * dx;
  const float cy = a.y + t * dy;
  return std::hypot(p.x - cx, p.y - cy);
}

bool point_in_stroke_loops(const std::vector<CanvasSubpath> &loops, float x, float y, float half) {
  const CanvasPoint point{x, y};
  for (const CanvasSubpath &loop : loops) {
    if (loop.size() < 2) continue;
    for (size_t i = 0; i + 1 < loop.size(); i++) {
      if (distance_to_segment(point, loop[i], loop[i + 1]) <= half) return true;
    }
    if (loop.front().x != loop.back().x || loop.front().y != loop.back().y) {
      if (distance_to_segment(point, loop.back(), loop.front()) <= half) return true;
    }
  }
  return false;
}

}  // namespace

void Canvas2D::clip(bool even_odd) { clip_paths(path_, even_odd); }

void Canvas2D::clip_paths(const std::vector<CanvasSubpath> &paths, bool even_odd) {
  std::vector<CanvasSubpath> subpaths = device_subpaths_of(paths);
  std::vector<CanvasSubpath> loops;
  for (CanvasSubpath &subpath : subpaths) {
    if (subpath.size() < 3) continue;
    loops.push_back(std::move(subpath));
  }
  auto mask = std::make_shared<CanvasClipMask>();
  mask->width = image_.width;
  mask->height = image_.height;
  mask->data.assign((size_t)image_.width * (size_t)image_.height, 0);
  canvas_detail::CoverageGrid coverage;
  canvas_detail::compute_coverage(image_.width, image_.height, loops, even_odd, &coverage);
  if (!coverage.empty()) {
    const int rows = coverage.y1 - coverage.y0;
    for (int r = 0; r < rows; r++) {
      const uint32_t begin = coverage.row_offset[(size_t)r];
      const uint32_t end = coverage.row_offset[(size_t)r + 1];
      if (begin >= end) continue;
      const int y = coverage.y0 + r;
      const int rx = coverage.row_x[(size_t)r];
      const float *values = coverage.values.data() + begin;
      uint8_t *out = mask->data.data() + (size_t)y * (size_t)image_.width + (size_t)rx;
      for (uint32_t i = begin; i < end; i++) {
        float value = std::max(0.0f, std::min(1.0f, values[i - begin]));
        out[i - begin] = (uint8_t)(value * 255.0f + 0.5f);
      }
    }
  }
  if (state_.clip) {
    const CanvasClipMask &previous = *state_.clip;
    if (previous.width == mask->width && previous.height == mask->height) {
      for (size_t i = 0; i < mask->data.size(); i++) {
        mask->data[i] = (uint8_t)((unsigned)mask->data[i] * (unsigned)previous.data[i] / 255u);
      }
    }
  }
  state_.clip = std::move(mask);
  /* A clip changes future draws only; it touches no pixels, so it must not
   * schedule a texture upload. */
}

bool Canvas2D::is_point_in_path(float x, float y, bool even_odd) const {
  return is_point_in_paths(path_, x, y, even_odd);
}

bool Canvas2D::is_point_in_paths(const std::vector<CanvasSubpath> &paths, float x, float y,
                                 bool even_odd) const {
  float deviceX = 0.0f;
  float deviceY = 0.0f;
  to_device(x, y, &deviceX, &deviceY);
  return point_in_loops(device_subpaths_of(paths), deviceX, deviceY, even_odd);
}

bool Canvas2D::is_point_in_stroke(float x, float y) const {
  return is_point_in_stroke_paths(path_, x, y);
}

bool Canvas2D::is_point_in_stroke_paths(const std::vector<CanvasSubpath> &paths, float x,
                                        float y) const {
  float scale = state_.transform.scale();
  if (!(scale > 0.0f)) scale = 1.0f;
  const float half = std::max(0.5f, state_.line_width * scale * 0.5f);
  float deviceX = 0.0f;
  float deviceY = 0.0f;
  to_device(x, y, &deviceX, &deviceY);
  return point_in_stroke_loops(device_subpaths_of(paths), deviceX, deviceY, half);
}

}  // namespace xtgui
