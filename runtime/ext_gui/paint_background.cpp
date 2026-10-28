/*
 * xbintsc GUI engine — background layer painting.
 *
 * The renderer has one rounded-rectangle pipeline and no gradient shader, so a
 * gradient is emitted as a run of solid bands: 18 strips for a linear gradient
 * and 32 concentric ellipses for a radial one. Radial ellipses use the shape
 * pipeline's transform slot (a unit circle scaled to the ellipse radii), which
 * keeps the distance-field antialiasing intact.
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace xtgui {
namespace paint_detail {

namespace {

Color gradientColorAt(const BackgroundLayer &layer, float t) {
  const std::vector<BackgroundGradientStop> &stops = layer.stops;
  if (stops.empty()) return Color();
  if (t <= stops.front().offset) return stops.front().color;
  if (t >= stops.back().offset) return stops.back().color;
  for (size_t i = 1; i < stops.size(); i++) {
    if (t > stops[i].offset) continue;
    float span = stops[i].offset - stops[i - 1].offset;
    float u = span > 0.00001f ? (t - stops[i - 1].offset) / span : 0.0f;
    Color a = stops[i - 1].color;
    Color b = stops[i].color;
    Color out;
    out.r = a.r + (b.r - a.r) * u;
    out.g = a.g + (b.g - a.g) * u;
    out.b = a.b + (b.b - a.b) * u;
    out.a = a.a + (b.a - a.a) * u;
    out.valid = true;
    return out;
  }
  return stops.back().color;
}

/** Intersect `clip` with the box's border box (backgrounds never paint outside
 * it, even when the box is not an overflow container). */
PaintClip boxClip(const PaintClip &clip, const LayoutBox &box) {
  float bx = box.x;
  float by = box.y;
  float bw = box.width;
  float bh = box.height;
  if (box.has_transform) {
    LayoutRect bounds = xt_layout_transform_rect(box.world, bx, by, bw, bh);
    bx = bounds.x;
    by = bounds.y;
    bw = bounds.width;
    bh = bounds.height;
  }
  PaintClip out = clip;
  if (!out.active) {
    out.active = true;
    out.x = bx;
    out.y = by;
    out.width = bw;
    out.height = bh;
    return out;
  }
  float x0 = std::max(out.x, bx);
  float y0 = std::max(out.y, by);
  float x1 = std::min(out.x + out.width, bx + bw);
  float y1 = std::min(out.y + out.height, by + bh);
  out.x = x0;
  out.y = y0;
  out.width = std::max(0.0f, x1 - x0);
  out.height = std::max(0.0f, y1 - y0);
  return out;
}

void paintLinear(const LayoutBox &box, const BackgroundLayer &layer, const FilterChain &chain,
                 const PaintClip &clip, DisplayList &out) {
  if (layer.stops.size() < 2 || box.width <= 0.0f || box.height <= 0.0f) return;
  const int strips = 18;
  bool horizontal = (layer.angle > 80.0f && layer.angle < 100.0f) ||
                    (layer.angle > 260.0f && layer.angle < 280.0f);
  bool reverse = (layer.angle > 260.0f && layer.angle < 280.0f) || layer.angle < 10.0f ||
                 layer.angle > 350.0f;
  for (int i = 0; i < strips; i++) {
    float t = ((float)i + 0.5f) / (float)strips;
    Color color = gradientColorAt(layer, reverse ? 1.0f - t : t);
    color = filterColor(chain, color);
    if (color.a <= 0.0f) continue;
    if (horizontal) {
      addRect(out, box.x + box.width * (float)i / (float)strips, box.y,
              box.width / (float)strips + 0.4f, box.height, 0.0f, color, clip, box.world);
    } else {
      addRect(out, box.x, box.y + box.height * (float)i / (float)strips, box.width,
              box.height / (float)strips + 0.4f, 0.0f, color, clip, box.world);
    }
  }
}

/** Default radii for `closest-side` / `farthest-side` / corner keywords. */
void radialRadii(const LayoutBox &box, const BackgroundLayer &layer, const XtStyle &style,
                 float viewportWidth, float viewportHeight, float *cx, float *cy, float *rx,
                 float *ry) {
  float centerX = box.x + resolveLength(layer.center_x, box.width, style.font_size, viewportWidth,
                                        viewportHeight);
  float centerY = box.y + resolveLength(layer.center_y, box.height, style.font_size, viewportWidth,
                                        viewportHeight);
  float left = centerX - box.x;
  float right = box.x + box.width - centerX;
  float top = centerY - box.y;
  float bottom = box.y + box.height - centerY;
  left = std::max(0.0f, left);
  right = std::max(0.0f, right);
  top = std::max(0.0f, top);
  bottom = std::max(0.0f, bottom);

  if (layer.size == RadialSize::Explicit) {
    float radiusX =
        resolveLength(layer.radius_x, box.width, style.font_size, viewportWidth, viewportHeight);
    float radiusY = layer.circle
                        ? radiusX
                        : resolveLength(layer.radius_y, box.height, style.font_size, viewportWidth,
                                        viewportHeight);
    *cx = centerX;
    *cy = centerY;
    *rx = std::max(0.0f, radiusX);
    *ry = std::max(0.0f, radiusY);
    return;
  }

  if (layer.circle) {
    float radius = 0.0f;
    switch (layer.size) {
      case RadialSize::ClosestSide:
        radius = std::min(std::min(left, right), std::min(top, bottom));
        break;
      case RadialSize::FarthestSide:
        radius = std::max(std::max(left, right), std::max(top, bottom));
        break;
      case RadialSize::ClosestCorner:
        radius = std::sqrt(std::min(left, right) * std::min(left, right) +
                           std::min(top, bottom) * std::min(top, bottom));
        break;
      case RadialSize::FarthestCorner:
      default:
        radius = std::sqrt(std::max(left, right) * std::max(left, right) +
                           std::max(top, bottom) * std::max(top, bottom));
        break;
    }
    *rx = *ry = radius;
  } else {
    switch (layer.size) {
      case RadialSize::ClosestCorner:
      case RadialSize::ClosestSide:
        *rx = std::min(left, right);
        *ry = std::min(top, bottom);
        break;
      case RadialSize::FarthestSide:
      case RadialSize::FarthestCorner:
      default:
        *rx = std::max(left, right);
        *ry = std::max(top, bottom);
        break;
    }
  }
  *cx = centerX;
  *cy = centerY;
}

/** One filled ellipse: a unit-radius rounded rect mapped by a local scale. */
void addEllipse(DisplayList &out, float cx, float cy, float rx, float ry, const Color &color,
                const PaintClip &clip, const Matrix2D &world) {
  if (rx <= 0.0f || ry <= 0.0f || color.a <= 0.0f) return;
  Matrix2D local = Matrix2D::translate(cx, cy)
                       .multiply(Matrix2D::scale(rx, ry))
                       .multiply(Matrix2D::translate(-cx, -cy));
  addRect(out, cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, 1.0f, color, clip, world.multiply(local));
}

void paintRadial(const LayoutBox &box, const XtStyle &style, const BackgroundLayer &layer,
                 const FilterChain &chain, const PaintClip &clip, float viewportWidth,
                 float viewportHeight, DisplayList &out) {
  if (layer.stops.size() < 2 || box.width <= 0.0f || box.height <= 0.0f) return;
  float cx = 0.0f;
  float cy = 0.0f;
  float rx = 0.0f;
  float ry = 0.0f;
  radialRadii(box, layer, style, viewportWidth, viewportHeight, &cx, &cy, &rx, &ry);
  if (rx <= 0.0f || ry <= 0.0f) return;
  const int bands = 32;
  for (int i = bands; i >= 1; i--) {
    float t = (float)i / (float)bands;
    Color color = filterColor(chain, gradientColorAt(layer, t));
    if (color.a <= 0.0f) continue;
    addEllipse(out, cx, cy, rx * t, ry * t, color, clip, box.world);
  }
  /* Exact centre colour (the innermost band samples just off zero). */
  Color center = filterColor(chain, gradientColorAt(layer, 0.0f));
  addEllipse(out, cx, cy, 0.6f, 0.6f, center, clip, box.world);
}

void paintImage(const LayoutBox &box, const BackgroundLayer &layer, const FilterChain &chain,
                const PaintClip &clip, DisplayList &out) {
  if (layer.image_src.empty() || box.width <= 0.0f || box.height <= 0.0f) return;
  /* A `background-image: url(...)` layer fills the box. (CSS `background-size`
   * is not modelled, so the intrinsic-size/tiling default becomes a stretch.) */
  PaintImage image;
  image.x = box.x;
  image.y = box.y;
  image.width = box.width;
  image.height = box.height;
  image.src = layer.image_src;
  image.transform = box.world;
  addImage(out, image, clip, chain);
}

}  // namespace

void paintBackgrounds(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                      const FilterChain &chain, const PaintClip &clip, float viewportWidth,
                      float viewportHeight) {
  PaintClip layerClip = boxClip(clip, box);
  if (layerClip.width <= 0.0f || layerClip.height <= 0.0f) return;
  if (style.has_background) {
    /* The rectangle is already the border box, so the incoming clip is enough;
     * narrowing it to the box again would only change the reported clip (and
     * would clip a filtered/rounded fill differently from before M19). */
    addRectFiltered(out, box.x, box.y, box.width, box.height, style.border_radius,
                    style.background, clip, box.world, chain);
  }
  /* CSS paints the first layer on top, so walk the list backwards. */
  for (size_t i = style.background_layers.size(); i-- > 0;) {
    const BackgroundLayer &layer = style.background_layers[i];
    if (layer.kind == BackgroundKind::Linear) {
      paintLinear(box, layer, chain, layerClip, out);
    } else if (layer.kind == BackgroundKind::Radial) {
      paintRadial(box, style, layer, chain, layerClip, viewportWidth, viewportHeight, out);
    } else {
      paintImage(box, layer, chain, layerClip, out);
    }
  }
}

}  // namespace paint_detail
}  // namespace xtgui
