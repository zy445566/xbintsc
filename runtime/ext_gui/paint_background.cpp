/*
 * xbintsc GUI engine — background layer painting.
 *
 * `background-color` is one rounded rectangle and a `background-image` layer is
 * a gradient or an image. A linear gradient is emitted as a run of solid bands
 * (18 strips, 64 while repeating). A radial gradient is emitted as a single
 * `PaintGradient` that the renderer rasterises per pixel
 * (`renderer_gradient.cpp`): stacking translucent radial bands composites their
 * alpha once per band, which turns a vignette whose stops fade to `transparent`
 * into an opaque disc.
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace xtgui {
namespace paint_detail {

namespace {

/** Blend two stops in premultiplied alpha, as CSS specifies, and convert the
 * result back to the straight alpha both pipelines blend. Without it a
 * `transparent` stop (rgba(0,0,0,0)) drags the ramp towards black, so a
 * `transparent -> white` gradient fades through grey. */
Color mixGradientStops(const Color &from, const Color &to, float amount) {
  float alpha = from.a + (to.a - from.a) * amount;
  Color out;
  out.a = alpha;
  if (alpha > 0.00001f) {
    out.r = (from.r * from.a + (to.r * to.a - from.r * from.a) * amount) / alpha;
    out.g = (from.g * from.a + (to.g * to.a - from.g * from.a) * amount) / alpha;
    out.b = (from.b * from.a + (to.b * to.a - from.b * from.a) * amount) / alpha;
  } else {
    out.r = from.r;
    out.g = from.g;
    out.b = from.b;
  }
  out.valid = true;
  return out;
}

}  // namespace

Color gradientColorAt(const std::vector<GradientStop> &stops, bool repeating, float period,
                      float t) {
  if (stops.empty()) return Color();
  /* A repeating gradient restarts its stop pattern at the last stop's offset. */
  if (repeating) {
    if (period <= 0.00001f) return stops.back().color;
    t -= std::floor(t / period) * period;
  }
  if (t <= stops.front().offset) return stops.front().color;
  if (t >= stops.back().offset) return stops.back().color;
  for (size_t i = 1; i < stops.size(); i++) {
    if (t > stops[i].offset) continue;
    float span = stops[i].offset - stops[i - 1].offset;
    float u = span > 0.00001f ? (t - stops[i - 1].offset) / span : 0.0f;
    return mixGradientStops(stops[i - 1].color, stops[i].color, u);
  }
  return stops.back().color;
}

namespace {

/** Resolve the declaration's percentage/length offsets against the gradient
 * line, then distribute the omitted (`Unit::Auto`) ones between their
 * neighbours. The last explicit stop is preserved: for a normal gradient it
 * holds that colour, and for a repeating one it defines the period. */
std::vector<GradientStop> resolveStops(const BackgroundLayer &layer, float line_length,
                                       const XtStyle &style, float viewportWidth,
                                       float viewportHeight) {
  std::vector<GradientStop> stops;
  for (const BackgroundGradientStop &stop : layer.stops) {
    GradientStop resolved;
    resolved.color = stop.color;
    resolved.offset = -1.0f;
    if (stop.offset.unit == Unit::Percent) {
      resolved.offset = stop.offset.value / 100.0f;
    } else if (stop.offset.unit != Unit::Auto && stop.offset.unit != Unit::Invalid &&
               stop.offset.unit != Unit::None) {
      float pixels = resolveLength(stop.offset, 0.0f, style.font_size, viewportWidth,
                                   viewportHeight);
      resolved.offset = line_length > 0.00001f ? pixels / line_length : 0.0f;
    }
    stops.push_back(resolved);
  }
  if (stops.empty()) return stops;

  /* Explicit offsets are non-decreasing, as in CSS. */
  float last = -1.0f;
  for (GradientStop &stop : stops) {
    if (stop.offset < 0.0f) continue;
    if (stop.offset < last) stop.offset = last;
    last = stop.offset;
  }

  /* Fill implicit offsets by distributing them between their neighbours. */
  size_t i = 0;
  while (i < stops.size()) {
    if (stops[i].offset >= 0.0f) {
      i++;
      continue;
    }
    size_t j = i;
    while (j < stops.size() && stops[j].offset < 0.0f) j++;
    float before = (i == 0) ? 0.0f : stops[i - 1].offset;
    float after = (j >= stops.size()) ? 1.0f : stops[j].offset;
    size_t count = j - i;
    for (size_t k = 0; k < count; k++) {
      float offset = 0.0f;
      if (i == 0 && j >= stops.size()) {
        offset = count > 1 ? (float)k / (float)(count - 1) : 0.0f;
      } else if (i == 0) {
        offset = after * (float)k / (float)count;
      } else if (j >= stops.size()) {
        offset = before + (1.0f - before) * (float)(k + 1) / (float)count;
      } else {
        offset = before + (after - before) * (float)(k + 1) / (float)(count + 1);
      }
      stops[i + k].offset = offset;
    }
    i = j;
  }
  return stops;
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

void paintLinear(const LayoutBox &box, const XtStyle &style, const BackgroundLayer &layer,
                 const FilterChain &chain, const PaintClip &clip, float viewportWidth,
                 float viewportHeight, DisplayList &out) {
  if (layer.stops.size() < 2 || box.width <= 0.0f || box.height <= 0.0f) return;
  /* Repeating patterns need more samples than one gradient ramp: a 10%
   * stripe is only two bands at the old 18-strip resolution. */
  const int strips = layer.repeating ? 64 : 18;
  bool horizontal = (layer.angle > 80.0f && layer.angle < 100.0f) ||
                    (layer.angle > 260.0f && layer.angle < 280.0f);
  bool reverse = (layer.angle > 260.0f && layer.angle < 280.0f) || layer.angle < 10.0f ||
                 layer.angle > 350.0f;
  const float line_length = horizontal ? box.width : box.height;
  std::vector<GradientStop> stops = resolveStops(layer, line_length, style, viewportWidth,
                                                 viewportHeight);
  if (stops.size() < 2) return;
  const float period = stops.back().offset;
  for (int i = 0; i < strips; i++) {
    float t = ((float)i + 0.5f) / (float)strips;
    Color color = gradientColorAt(stops, layer.repeating, period, reverse ? 1.0f - t : t);
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

/** Radii of a keyword-sized ellipse: it keeps the box's aspect ratio and is
 * scaled until it meets the corner at `(px, py)` from the centre, so the 100%
 * stop lands on that corner as it does in CSS. Taking the side distances instead
 * left the corners of a `farthest-corner` gradient unpainted. */
void cornerEllipseRadii(float width, float height, float px, float py, float *rx, float *ry) {
  float x = 2.0f * px / width;
  float y = 2.0f * py / height;
  float scale = std::sqrt(x * x + y * y);
  *rx = scale * width * 0.5f;
  *ry = scale * height * 0.5f;
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
      case RadialSize::ClosestSide:
        *rx = std::min(left, right);
        *ry = std::min(top, bottom);
        break;
      case RadialSize::FarthestSide:
        *rx = std::max(left, right);
        *ry = std::max(top, bottom);
        break;
      case RadialSize::ClosestCorner:
        cornerEllipseRadii(box.width, box.height, std::min(left, right), std::min(top, bottom), rx,
                           ry);
        break;
      case RadialSize::FarthestCorner:
      default:
        cornerEllipseRadii(box.width, box.height, std::max(left, right), std::max(top, bottom), rx,
                           ry);
        break;
    }
  }
  *cx = centerX;
  *cy = centerY;
}

/** Stable identity of a rasterised gradient: the ramp plus its geometry. The
 * renderer keys its texture cache by this string, so an unchanged declaration
 * rasterises once. */
std::string gradientKey(const PaintGradient &gradient) {
  char buffer[192];
  std::string key;
  for (const GradientStop &stop : gradient.stops) {
    std::snprintf(buffer, sizeof(buffer), "%.4f/%.5f,%.5f,%.5f,%.5f;", stop.offset, stop.color.r,
                  stop.color.g, stop.color.b, stop.color.a);
    key += buffer;
  }
  std::snprintf(buffer, sizeof(buffer), "%.3f,%.3f,%.3f,%.3f;%d;%.5f", gradient.center_x,
                gradient.center_y, gradient.radius_x, gradient.radius_y,
                gradient.repeating ? 1 : 0, gradient.period);
  key += buffer;
  return key;
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
  std::vector<GradientStop> stops = resolveStops(layer, std::max(rx, ry), style, viewportWidth,
                                                 viewportHeight);
  if (stops.size() < 2) return;
  /* The colour functions fold into the stops (a per-stop application is exact
   * for the linear ones); `brightness()` and `opacity()` stay out, because the
   * image tint carries them exactly and leaving them out keeps the ramp — and
   * therefore the texture cache key — stable while a fade animates. */
  FilterChain stopChain = chain;
  stopChain.opacity = 1.0f;
  for (size_t i = stopChain.ops.size(); i-- > 0;) {
    FilterKind kind = stopChain.ops[i].kind;
    if (kind == FilterKind::Brightness || kind == FilterKind::Opacity) {
      stopChain.ops.erase(stopChain.ops.begin() + (ptrdiff_t)i);
    }
  }
  auto gradient = std::make_shared<PaintGradient>();
  gradient->stops.reserve(stops.size());
  bool visible = false;
  for (size_t i = 0; i < stops.size(); i++) {
    GradientStop resolved = stops[i];
    resolved.color = filterColor(stopChain, resolved.color);
    visible = visible || resolved.color.a > 0.0f;
    gradient->stops.push_back(resolved);
  }
  if (!visible) return;
  gradient->center_x = cx - box.x;
  gradient->center_y = cy - box.y;
  gradient->radius_x = rx;
  gradient->radius_y = ry;
  gradient->repeating = layer.repeating;
  gradient->period = stops.back().offset;
  gradient->key = gradientKey(*gradient);

  /* One quad for the whole layer: the renderer resolves the ramp per pixel, so
   * the layer composites exactly once instead of once per translucent band. */
  PaintImage image;
  image.x = box.x;
  image.y = box.y;
  image.width = box.width;
  image.height = box.height;
  image.transform = box.world;
  image.gradient = gradient;
  addImage(out, std::move(image), clip, chain);
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
      paintLinear(box, style, layer, chain, layerClip, viewportWidth, viewportHeight, out);
    } else if (layer.kind == BackgroundKind::Radial) {
      paintRadial(box, style, layer, chain, layerClip, viewportWidth, viewportHeight, out);
    } else {
      paintImage(box, layer, chain, layerClip, out);
    }
  }
}

}  // namespace paint_detail
}  // namespace xtgui
