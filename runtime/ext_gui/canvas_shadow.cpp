/*
 * xbintsc GUI engine — Canvas2D shadows and clip-region compositing.
 *
 * A drawing operation that has a visible `shadowColor` is rendered into a
 * scratch layer first, so the shadow is derived from exactly the pixels the
 * operation covers (including glyph coverage and image alpha). The layer's
 * alpha is offset and box-blurred, tinted with `shadowColor`, composited, and
 * only then is the operation's own colour composited on top.
 *
 * The scratch layer is deliberately drawn *without* the clip mask: the shadow
 * must be able to leave the clip region and be clipped once, at composite time.
 */

#include "canvas_internal.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace xtgui {

namespace {

void box_blur_pass(std::vector<float> &source, std::vector<float> &temp, int width, int height,
                   int radius) {
  if (radius <= 0) return;
  const float scale = 1.0f / (float)(radius * 2 + 1);
  temp.resize(source.size());
  /* Horizontal. */
  for (int y = 0; y < height; y++) {
    const float *row = source.data() + (size_t)y * (size_t)width;
    float *out = temp.data() + (size_t)y * (size_t)width;
    float sum = 0.0f;
    for (int x = -radius; x <= radius; x++) sum += row[std::min(width - 1, std::max(0, x))];
    for (int x = 0; x < width; x++) {
      out[x] = sum * scale;
      const int remove = std::min(width - 1, std::max(0, x - radius));
      const int add = std::min(width - 1, std::max(0, x + radius + 1));
      sum += row[add] - row[remove];
    }
  }
  /* Vertical. */
  for (int x = 0; x < width; x++) {
    float sum = 0.0f;
    for (int y = -radius; y <= radius; y++) {
      sum += temp[(size_t)std::min(height - 1, std::max(0, y)) * (size_t)width + (size_t)x];
    }
    for (int y = 0; y < height; y++) {
      source[(size_t)y * (size_t)width + (size_t)x] = sum * scale;
      const int remove = std::min(height - 1, std::max(0, y - radius));
      const int add = std::min(height - 1, std::max(0, y + radius + 1));
      sum += temp[(size_t)add * (size_t)width + (size_t)x] -
             temp[(size_t)remove * (size_t)width + (size_t)x];
    }
  }
}

}  // namespace

bool Canvas2D::shadow_active() const {
  if (in_shadow_layer_) return false;
  return state_.shadow_color[3] > 0.0f;
}

const CanvasClipMask *Canvas2D::clip_mask() const {
  /* The scratch shadow layer is painted unclipped; the mask is applied when the
   * finished layer is composited back. */
  if (in_shadow_layer_ || !state_.clip) return nullptr;
  return state_.clip.get();
}

bool Canvas2D::begin_shadow_layer() {
  if (in_shadow_layer_ || !shadow_active()) return false;
  shadow_layer_.width = image_.width;
  shadow_layer_.height = image_.height;
  shadow_layer_.pixels.assign((size_t)image_.width * (size_t)image_.height * 4, 0);
  std::swap(shadow_layer_.pixels, image_.pixels);
  in_shadow_layer_ = true;
  return true;
}

void Canvas2D::end_shadow_layer() {
  if (!in_shadow_layer_) return;
  in_shadow_layer_ = false;
  std::swap(image_.pixels, shadow_layer_.pixels);
  composite_shadow_layer(shadow_layer_);
  composite_layer(shadow_layer_);
  /* The shadow can reach beyond the draw's own bounds (offset + blur), so the
   * conservative upload region is the whole canvas. */
  mark_all_dirty();
  generation_++;
}

void Canvas2D::composite_layer(const Image &layer) {
  if (layer.width != image_.width || layer.height != image_.height) return;
  const CanvasClipMask *clip = clip_mask();
  for (int y = 0; y < layer.height; y++) {
    const unsigned char *src = layer.pixels.data() + (size_t)y * (size_t)layer.width * 4;
    unsigned char *dst = image_.pixels.data() + (size_t)y * (size_t)image_.width * 4;
    for (int x = 0; x < layer.width; x++) {
      const unsigned char *texel = src + (size_t)x * 4;
      if (texel[3] == 0) continue;
      float color[4] = {texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f,
                        texel[3] / 255.0f};
      float coverage = clip != nullptr ? clip->at(x, y) / 255.0f : 1.0f;
      if (coverage <= 0.0f) continue;
      canvas_detail::blend_pixel_mode(dst + (size_t)x * 4, color, coverage, 1.0f,
                                      state_.global_composite);
    }
  }
}

void Canvas2D::composite_shadow_layer(const Image &layer) {
  if (layer.width != image_.width || layer.height != image_.height) return;
  if (state_.shadow_color[3] <= 0.0f) return;
  const int width = layer.width;
  const int height = layer.height;
  std::vector<float> alpha((size_t)width * (size_t)height, 0.0f);
  for (int y = 0; y < height; y++) {
    const unsigned char *row = layer.pixels.data() + (size_t)y * (size_t)width * 4;
    float *out = alpha.data() + (size_t)y * (size_t)width;
    for (int x = 0; x < width; x++) out[x] = row[(size_t)x * 4 + 3] / 255.0f;
  }

  /* `shadowBlur` is the diameter of the blur; three box passes approximate a
   * Gaussian closely enough for UI work and stay O(width * height). */
  const float blur = std::isfinite(state_.shadow_blur) ? state_.shadow_blur : 0.0f;
  if (blur > 0.0f) {
    int radius = (int)std::lround(blur * 0.5f);
    if (radius < 1) radius = 1;
    if (radius > 64) radius = 64;
    std::vector<float> temp;
    for (int pass = 0; pass < 3; pass++) box_blur_pass(alpha, temp, width, height, radius);
  }

  const int offsetX = (int)std::lround(state_.shadow_offset_x);
  const int offsetY = (int)std::lround(state_.shadow_offset_y);
  const CanvasClipMask *clip = clip_mask();
  const float shadowAlpha = state_.shadow_color[3];
  for (int y = 0; y < height; y++) {
    const int sourceY = y - offsetY;
    if (sourceY < 0 || sourceY >= height) continue;
    const float *row = alpha.data() + (size_t)sourceY * (size_t)width;
    unsigned char *dst = image_.pixels.data() + (size_t)y * (size_t)image_.width * 4;
    for (int x = 0; x < width; x++) {
      const int sourceX = x - offsetX;
      if (sourceX < 0 || sourceX >= width) continue;
      const float coverage = row[sourceX];
      if (coverage <= 0.0f) continue;
      float color[4] = {state_.shadow_color[0], state_.shadow_color[1], state_.shadow_color[2],
                        shadowAlpha};
      float clipCoverage = clip != nullptr ? clip->at(x, y) / 255.0f : 1.0f;
      if (clipCoverage <= 0.0f) continue;
      canvas_detail::blend_pixel_mode(dst + (size_t)x * 4, color, coverage * clipCoverage, 1.0f,
                                      kCanvasCompositeSourceOver);
    }
  }
}

}  // namespace xtgui
