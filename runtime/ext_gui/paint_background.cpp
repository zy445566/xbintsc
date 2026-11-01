/*
 * xbintsc GUI engine — background layer painting.
 *
 * `background-color` is one rounded rectangle and a `background-image` layer is
 * a gradient or an image. Both gradient kinds are emitted as a single
 * `PaintGradient` that the renderer rasterises per pixel
 * (`renderer_gradient.cpp`). Stacking translucent bands instead composites their
 * alpha once per band, which turns a vignette whose stops fade to `transparent`
 * into an opaque disc, and a run of bands cannot draw a stop narrower than one
 * band: a 1px divider line either falls between two samples or becomes a block.
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
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
 * line, then distribute the omitted ones between their neighbours. The last
 * explicit stop is preserved: for a normal gradient it holds that colour, and
 * for a repeating one it defines the period. */
std::vector<GradientStop> resolveStops(const BackgroundLayer &layer, float line_length,
                                       const XtStyle &style, float viewportWidth,
                                       float viewportHeight) {
  std::vector<GradientStop> stops;
  stops.reserve(layer.stops.size());
  for (const BackgroundGradientStop &stop : layer.stops) {
    GradientStop resolved;
    resolved.color = stop.color;
    /* NaN means "this stop has no offset"; a resolved offset may legitimately be
     * negative (`red -10%`, or a `calc()` that lands below the line's start), so
     * it must not double as the "unset" marker — that turned a negative offset
     * into an implicit one and re-spaced the whole ramp. */
    resolved.offset = std::numeric_limits<float>::quiet_NaN();
    if (stop.offset.unit == Unit::Percent) {
      resolved.offset = stop.offset.value / 100.0f;
    } else if (stop.offset.unit != Unit::Auto && stop.offset.unit != Unit::Invalid &&
               stop.offset.unit != Unit::None) {
      /* A percentage inside `calc()` is relative to the gradient line, exactly
       * like a bare percentage; resolving it against a zero base made
       * `calc(10% - 1px)` come out as `-1px`. */
      float pixels =
          resolveLength(stop.offset, line_length, style.font_size, viewportWidth, viewportHeight);
      resolved.offset = line_length > 0.00001f ? pixels / line_length : 0.0f;
    }
    stops.push_back(resolved);
  }
  if (stops.empty()) return stops;

  /* Explicit offsets are non-decreasing, as in CSS. */
  bool have_previous = false;
  float previous = 0.0f;
  for (GradientStop &stop : stops) {
    if (std::isnan(stop.offset)) continue;
    if (have_previous && stop.offset < previous) stop.offset = previous;
    previous = stop.offset;
    have_previous = true;
  }

  /* Fill implicit offsets by distributing them between their neighbours. */
  size_t i = 0;
  while (i < stops.size()) {
    if (!std::isnan(stops[i].offset)) {
      i++;
      continue;
    }
    size_t j = i;
    while (j < stops.size() && std::isnan(stops[j].offset)) j++;
    float before = (i == 0) ? 0.0f : stops[i - 1].offset;
    float after = (j >= stops.size()) ? 1.0f : stops[j].offset;
    if (after < before) after = before;
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
  std::snprintf(buffer, sizeof(buffer), "%d;%.3f,%.3f,%.3f,%.3f;%.3f,%.3f,%.4f,%.4f,%.3f;%d;%.5f",
                gradient.kind == PaintGradient::Kind::Linear ? 1 : 0, gradient.center_x,
                gradient.center_y, gradient.radius_x, gradient.radius_y, gradient.start_x,
                gradient.start_y, gradient.dir_x, gradient.dir_y, gradient.length,
                gradient.repeating ? 1 : 0, gradient.period);
  key += buffer;
  return key;
}

/** The CSS gradient line of `layer` inside `box`: the box-local point where the
 * ramp starts, the direction increasing offsets run in, and the line's length.
 * CSS angles grow clockwise from "to top" and y grows downwards, hence the
 * negated cosine. The line passes through the box centre and its length is the
 * box's extent projected onto the direction, so `t` spans exactly 0..1 across
 * the box and `%` offsets in `calc()` resolve against it. */
void linearLine(const LayoutBox &box, float angle, float *start_x, float *start_y, float *dir_x,
                float *dir_y, float *length) {
  const float radians = angle * 3.14159265358979f / 180.0f;
  const float dx = std::sin(radians);
  const float dy = -std::cos(radians);
  const float span = std::fabs(box.width * dx) + std::fabs(box.height * dy);
  *dir_x = dx;
  *dir_y = dy;
  *length = span;
  *start_x = box.width * 0.5f - dx * span * 0.5f;
  *start_y = box.height * 0.5f - dy * span * 0.5f;
}

/** Fold the colour half of the filter chain into `stops` and emit the layer as
 * one gradient image. `brightness()` and `opacity()` stay out of the stops: the
 * image tint carries them exactly, and leaving them out keeps the ramp — and
 * therefore the texture cache key — stable while a fade animates. */
void emitGradient(const LayoutBox &box, const FilterChain &chain, const PaintClip &clip,
                  const std::shared_ptr<PaintGradient> &gradient,
                  const std::vector<GradientStop> &stops, DisplayList &out) {
  FilterChain stopChain = chain;
  stopChain.opacity = 1.0f;
  for (size_t i = stopChain.ops.size(); i-- > 0;) {
    FilterKind kind = stopChain.ops[i].kind;
    if (kind == FilterKind::Brightness || kind == FilterKind::Opacity) {
      stopChain.ops.erase(stopChain.ops.begin() + (std::ptrdiff_t)i);
    }
  }
  gradient->stops.reserve(stops.size());
  bool visible = false;
  for (const GradientStop &stop : stops) {
    GradientStop resolved = stop;
    resolved.color = filterColor(stopChain, resolved.color);
    visible = visible || resolved.color.a > 0.0f;
    gradient->stops.push_back(resolved);
  }
  if (!visible) return;
  gradient->key = gradientKey(*gradient);

  /* One quad for the whole layer: the renderer resolves the ramp per pixel, so
   * the layer composites exactly once instead of once per translucent band, and
   * a stop narrower than a band keeps its exact position and width. */
  PaintImage image;
  image.x = box.x;
  image.y = box.y;
  image.width = box.width;
  image.height = box.height;
  image.transform = box.world;
  image.gradient = gradient;
  addImage(out, std::move(image), clip, chain);
}

void paintLinear(const LayoutBox &box, const XtStyle &style, const BackgroundLayer &layer,
                 const FilterChain &chain, const PaintClip &clip, float viewportWidth,
                 float viewportHeight, DisplayList &out) {
  if (layer.stops.size() < 2 || box.width <= 0.0f || box.height <= 0.0f) return;
  float start_x = 0.0f;
  float start_y = 0.0f;
  float dir_x = 1.0f;
  float dir_y = 0.0f;
  float length = 0.0f;
  linearLine(box, layer.angle, &start_x, &start_y, &dir_x, &dir_y, &length);
  if (length <= 0.00001f) return;
  std::vector<GradientStop> stops =
      resolveStops(layer, length, style, viewportWidth, viewportHeight);
  if (stops.size() < 2) return;
  auto gradient = std::make_shared<PaintGradient>();
  gradient->kind = PaintGradient::Kind::Linear;
  gradient->start_x = start_x;
  gradient->start_y = start_y;
  gradient->dir_x = dir_x;
  gradient->dir_y = dir_y;
  gradient->length = length;
  gradient->repeating = layer.repeating;
  gradient->period = stops.back().offset;
  emitGradient(box, chain, clip, gradient, stops, out);
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
  auto gradient = std::make_shared<PaintGradient>();
  gradient->kind = PaintGradient::Kind::Radial;
  gradient->center_x = cx - box.x;
  gradient->center_y = cy - box.y;
  gradient->radius_x = rx;
  gradient->radius_y = ry;
  gradient->repeating = layer.repeating;
  gradient->period = stops.back().offset;
  emitGradient(box, chain, clip, gradient, stops, out);
}

void paintImage(const LayoutBox &box, const BackgroundLayer &layer, const XtStyle &style,
                size_t index, const FilterChain &chain, const PaintClip &clip, DisplayList &out,
                float viewportWidth, float viewportHeight) {
  if (layer.image_src.empty() || box.width <= 0.0f || box.height <= 0.0f) return;
  const BackgroundSize *size =
      index < style.background_sizes.size() ? &style.background_sizes[index] : nullptr;
  if (size != nullptr) {
    bool width_auto = size->width.unit == Unit::Auto || size->width.unit == Unit::Invalid ||
                      size->width.unit == Unit::None;
    bool height_auto = size->height.unit == Unit::Auto || size->height.unit == Unit::Invalid ||
                       size->height.unit == Unit::None;
    float tile_width =
        width_auto ? box.width
                   : resolveLength(size->width, box.width, style.font_size, viewportWidth,
                                   viewportHeight);
    float tile_height =
        height_auto ? box.height
                    : resolveLength(size->height, box.height, style.font_size, viewportWidth,
                                    viewportHeight);
    if (tile_width > 0.0f && tile_height > 0.0f) {
      /* The default `background-repeat` is `repeat`; the size is per layer. */
      int columns = (int)std::ceil(box.width / tile_width);
      int rows = (int)std::ceil(box.height / tile_height);
      if (columns < 1) columns = 1;
      if (rows < 1) rows = 1;
      const int kMaxTiles = 4096;
      if (columns * rows > kMaxTiles) {
        columns = 1;
        rows = 1;
      }
      for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
          PaintImage image;
          image.x = box.x + (float)column * tile_width;
          image.y = box.y + (float)row * tile_height;
          image.width = tile_width;
          image.height = tile_height;
          image.src = layer.image_src;
          image.transform = box.world;
          addImage(out, image, clip, chain);
        }
      }
      return;
    }
  }
  /* No `background-size`: keep the historical stretch to the box. */
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
    addRectFiltered(out, box.x, box.y, box.width, box.height, box.border_radius,
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
      paintImage(box, layer, style, i, chain, layerClip, out, viewportWidth, viewportHeight);
    }
  }
}

}  // namespace paint_detail
}  // namespace xtgui
