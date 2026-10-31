/*
 * xbintsc GUI engine — CSS `backdrop-filter`.
 *
 * The backdrop is whatever has already been painted behind the element, which
 * only the renderer can see. Paint therefore resolves the filter list into the
 * two things the compositor needs: a Gaussian sigma and one combined 3x3 colour
 * matrix. The renderer snapshots the scene, applies both in a full-screen
 * shader, and composites the result back inside the element's rounded border
 * box (see `renderer_layer.cpp`).
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>

namespace xtgui {
namespace paint_detail {

namespace {

struct ColorMatrix {
  float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  float offset[3] = {0, 0, 0};
};

/** `out = left * right` for row-major 3x3 matrices. */
ColorMatrix multiply(const ColorMatrix &left, const ColorMatrix &right) {
  ColorMatrix result;
  for (int row = 0; row < 3; row++) {
    for (int column = 0; column < 3; column++) {
      float sum = 0.0f;
      for (int k = 0; k < 3; k++) sum += left.m[row * 3 + k] * right.m[k * 3 + column];
      result.m[row * 3 + column] = sum;
    }
    result.offset[row] = left.m[row * 3] * right.offset[0] +
                         left.m[row * 3 + 1] * right.offset[1] +
                         left.m[row * 3 + 2] * right.offset[2] + left.offset[row];
  }
  return result;
}

ColorMatrix identity() { return ColorMatrix(); }

ColorMatrix brightness(float amount) {
  ColorMatrix matrix;
  matrix.m[0] = matrix.m[4] = matrix.m[8] = amount;
  return matrix;
}

ColorMatrix contrast(float amount) {
  ColorMatrix matrix = brightness(amount);
  float shift = 0.5f - 0.5f * amount;
  matrix.offset[0] = matrix.offset[1] = matrix.offset[2] = shift;
  return matrix;
}

ColorMatrix invert(float amount) {
  ColorMatrix matrix;
  float scale = 1.0f - 2.0f * amount;
  matrix.m[0] = matrix.m[4] = matrix.m[8] = scale;
  matrix.offset[0] = matrix.offset[1] = matrix.offset[2] = amount;
  return matrix;
}

ColorMatrix grayscale(float amount) {
  const float lr = 0.2126f, lg = 0.7152f, lb = 0.0722f;
  ColorMatrix matrix;
  matrix.m[0] = 1.0f - amount + amount * lr;
  matrix.m[1] = amount * lg;
  matrix.m[2] = amount * lb;
  matrix.m[3] = amount * lr;
  matrix.m[4] = 1.0f - amount + amount * lg;
  matrix.m[5] = amount * lb;
  matrix.m[6] = amount * lr;
  matrix.m[7] = amount * lg;
  matrix.m[8] = 1.0f - amount + amount * lb;
  return matrix;
}

ColorMatrix sepia(float amount) {
  const float base[9] = {0.393f, 0.769f, 0.189f, 0.349f, 0.686f,
                         0.168f, 0.272f, 0.534f, 0.131f};
  ColorMatrix matrix;
  for (int i = 0; i < 9; i++) matrix.m[i] = (1.0f - amount) * (i % 4 == 0 ? 1.0f : 0.0f) +
                                            amount * base[i];
  return matrix;
}

ColorMatrix saturate(float amount) {
  const float lr = 0.213f, lg = 0.715f, lb = 0.072f;
  ColorMatrix matrix;
  matrix.m[0] = lr + (1.0f - lr) * amount;
  matrix.m[1] = lg - lg * amount;
  matrix.m[2] = lb - lb * amount;
  matrix.m[3] = lr - lr * amount;
  matrix.m[4] = lg + (1.0f - lg) * amount;
  matrix.m[5] = lb - lb * amount;
  matrix.m[6] = lr - lr * amount;
  matrix.m[7] = lg - lg * amount;
  matrix.m[8] = lb + (1.0f - lb) * amount;
  return matrix;
}

ColorMatrix hueRotate(float degrees) {
  float radians = degrees * 3.14159265358979323846f / 180.0f;
  float c = std::cos(radians);
  float s = std::sin(radians);
  ColorMatrix matrix;
  matrix.m[0] = 0.213f + c * 0.787f - s * 0.213f;
  matrix.m[1] = 0.715f - c * 0.715f - s * 0.715f;
  matrix.m[2] = 0.072f - c * 0.072f + s * 0.928f;
  matrix.m[3] = 0.213f - c * 0.213f + s * 0.143f;
  matrix.m[4] = 0.715f + c * 0.285f + s * 0.140f;
  matrix.m[5] = 0.072f - c * 0.072f - s * 0.283f;
  matrix.m[6] = 0.213f - c * 0.213f - s * 0.787f;
  matrix.m[7] = 0.715f - c * 0.715f + s * 0.715f;
  matrix.m[8] = 0.072f + c * 0.928f + s * 0.072f;
  return matrix;
}

/** Combine the filter list in application order: the first function applies to
 * the backdrop first, so each new matrix multiplies the accumulated one on the
 * left. Drop shadows are a silhouette effect that the colour matrix cannot
 * express; they are ignored rather than misapplied. */
void compileFilters(const std::vector<FilterOp> &filters, const XtStyle &style,
                    float viewportWidth, float viewportHeight, ColorMatrix *matrix, float *blur,
                    float *alpha) {
  *matrix = identity();
  *blur = 0.0f;
  *alpha = 1.0f;
  for (const FilterOp &filter : filters) {
    switch (filter.kind) {
      case FilterKind::Blur:
        *blur += resolveLength(filter.blur, 0.0f, style.font_size, viewportWidth, viewportHeight);
        break;
      case FilterKind::Brightness:
        *matrix = multiply(brightness(filter.amount), *matrix);
        break;
      case FilterKind::Contrast:
        *matrix = multiply(contrast(filter.amount), *matrix);
        break;
      case FilterKind::Grayscale:
        *matrix = multiply(grayscale(filter.amount), *matrix);
        break;
      case FilterKind::Sepia:
        *matrix = multiply(sepia(filter.amount), *matrix);
        break;
      case FilterKind::Invert:
        *matrix = multiply(invert(filter.amount), *matrix);
        break;
      case FilterKind::Saturate:
        *matrix = multiply(saturate(filter.amount), *matrix);
        break;
      case FilterKind::HueRotate:
        *matrix = multiply(hueRotate(filter.amount), *matrix);
        break;
      case FilterKind::Opacity:
        *alpha *= filter.amount;
        break;
      case FilterKind::DropShadow:
        break;
    }
  }
}

}  // namespace

bool addBackdropFilter(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                       const PaintClip &clip, float viewportWidth, float viewportHeight) {
  if (style.backdrop_filters.empty()) return false;
  ColorMatrix matrix;
  float blur = 0.0f;
  float alpha = 1.0f;
  compileFilters(style.backdrop_filters, style, viewportWidth, viewportHeight, &matrix, &blur,
                 &alpha);
  if (blur <= 0.0f && alpha >= 1.0f && matrix.m[0] == 1.0f && matrix.m[1] == 0.0f &&
      matrix.m[2] == 0.0f && matrix.m[3] == 0.0f && matrix.m[4] == 1.0f &&
      matrix.m[5] == 0.0f && matrix.m[6] == 0.0f && matrix.m[7] == 0.0f &&
      matrix.m[8] == 1.0f) {
    return false;
  }
  PaintBackdrop backdrop;
  float x = box.x;
  float y = box.y;
  float width = box.width;
  float height = box.height;
  backdrop.radius = box.border_radius;
  if (box.has_transform) {
    LayoutRect bounds = xt_layout_transform_rect(box.world, x, y, width, height);
    x = bounds.x;
    y = bounds.y;
    width = bounds.width;
    height = bounds.height;
  }
  backdrop.x = x;
  backdrop.y = y;
  backdrop.width = width;
  backdrop.height = height;
  backdrop.blur = blur;
  backdrop.alpha = alpha;
  for (int i = 0; i < 9; i++) backdrop.color[i] = matrix.m[i];
  for (int i = 0; i < 3; i++) backdrop.offset[i] = matrix.offset[i];
  out.backdrops.push_back(backdrop);
  out.ops.push_back(
      PaintOp{PaintKind::Backdrop, (uint32_t)(out.backdrops.size() - 1), clip});
  return true;
}

}  // namespace paint_detail
}  // namespace xtgui
