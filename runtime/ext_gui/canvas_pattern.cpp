/*
 * xbintsc GUI engine — `CanvasPattern`.
 *
 * A pattern is an image tiled in user space with the four repetition modes.
 * Like a gradient it is owned by the canvas that created it (and addressed by
 * id), so a stale context handle resolves to "no pattern" instead of a dangling
 * pointer. The rasteriser samples it through the inverse transform, which keeps
 * the tiles attached to the shape when it is translated, scaled or rotated.
 */

#include "canvas.h"

#include <atomic>
#include <cmath>
#include <memory>

#include "canvas_internal.h"

namespace xtgui {

namespace {

std::atomic<uint64_t> g_next_pattern_id{1};

}  // namespace

CanvasPattern::CanvasPattern(uint64_t id, const Image &image, Repeat repeat)
    : id_(id), image_(image), repeat_(repeat) {}

bool CanvasPattern::color_at(float x, float y, float out[4]) const {
  const int width = image_.width;
  const int height = image_.height;
  if (width <= 0 || height <= 0) return false;
  /* Tile in user space: wrap the coordinate into one tile for the enabled
   * axes, and reject points outside the tile for the disabled ones. */
  bool repeatX = repeat_ == Repeat::Both || repeat_ == Repeat::X;
  bool repeatY = repeat_ == Repeat::Both || repeat_ == Repeat::Y;
  float u = x;
  float v = y;
  if (repeatX) {
    u = x - std::floor(x / (float)width) * (float)width;
    if (u < 0.0f) u += (float)width;
  } else if (x < 0.0f || x >= (float)width) {
    return false;
  }
  if (repeatY) {
    v = y - std::floor(y / (float)height) * (float)height;
    if (v < 0.0f) v += (float)height;
  } else if (y < 0.0f || y >= (float)height) {
    return false;
  }
  int px = (int)u;
  int py = (int)v;
  if (px < 0) px = 0;
  if (py < 0) py = 0;
  if (px >= width) px = width - 1;
  if (py >= height) py = height - 1;
  const unsigned char *texel =
      image_.pixels.data() + ((size_t)py * (size_t)width + (size_t)px) * 4;
  out[0] = (float)texel[0] / 255.0f;
  out[1] = (float)texel[1] / 255.0f;
  out[2] = (float)texel[2] / 255.0f;
  out[3] = (float)texel[3] / 255.0f;
  return true;
}

CanvasPattern *Canvas2D::create_pattern(const Image &image, CanvasPattern::Repeat repeat) {
  auto pattern =
      std::make_unique<CanvasPattern>(g_next_pattern_id.fetch_add(1), image, repeat);
  CanvasPattern *raw = pattern.get();
  patterns_[raw->id()] = std::move(pattern);
  return raw;
}

CanvasPattern *Canvas2D::find_pattern(uint64_t id) const {
  auto found = patterns_.find(id);
  return found == patterns_.end() ? nullptr : found->second.get();
}

void Canvas2D::set_fill_pattern(uint64_t id) {
  state_.fill_pattern = find_pattern(id) != nullptr ? id : 0;
  if (state_.fill_pattern != 0) state_.fill_gradient = 0;
}

void Canvas2D::set_stroke_pattern(uint64_t id) {
  state_.stroke_pattern = find_pattern(id) != nullptr ? id : 0;
  if (state_.stroke_pattern != 0) state_.stroke_gradient = 0;
}

}  // namespace xtgui
