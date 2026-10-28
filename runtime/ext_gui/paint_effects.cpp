/*
 * xbintsc GUI engine — CSS filter and shadow approximations.
 *
 * Colour functions are applied per painted shape (a group would need an
 * offscreen layer). `blur()` is approximated by spreading nine translucent
 * copies around the shape, and `drop-shadow()` by a shadow silhouette per box.
 * Both are deterministic and need no render-target pass, which keeps the
 * single-pass painter intact; `backdrop-filter` still needs a backdrop snapshot
 * and is documented as not composited.
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "style_internal.h"

namespace xtgui {
namespace paint_detail {

namespace {

float clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

Color lerpColor(const Color &from, const Color &to, float amount) {
  Color out;
  out.r = from.r + (to.r - from.r) * amount;
  out.g = from.g + (to.g - from.g) * amount;
  out.b = from.b + (to.b - from.b) * amount;
  out.a = from.a + (to.a - from.a) * amount;
  out.valid = true;
  return out;
}

float luminance(const Color &color) {
  return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
}

Color applySepia(const Color &color, float amount) {
  Color sepia;
  sepia.r = color.r * 0.393f + color.g * 0.769f + color.b * 0.189f;
  sepia.g = color.r * 0.349f + color.g * 0.686f + color.b * 0.168f;
  sepia.b = color.r * 0.272f + color.g * 0.534f + color.b * 0.131f;
  sepia.a = color.a;
  sepia.valid = true;
  return lerpColor(color, sepia, amount);
}

Color applySaturate(const Color &color, float amount) {
  float luma = luminance(color);
  Color out;
  out.r = luma + (color.r - luma) * amount;
  out.g = luma + (color.g - luma) * amount;
  out.b = luma + (color.b - luma) * amount;
  out.a = color.a;
  out.valid = true;
  return out;
}

Color applyHueRotate(const Color &color, float degrees) {
  float radians = degrees * 3.141592653589793f / 180.0f;
  float c = std::cos(radians);
  float s = std::sin(radians);
  Color out;
  out.r = color.r * (0.213f + c * 0.787f - s * 0.213f) +
          color.g * (0.715f - c * 0.715f - s * 0.715f) +
          color.b * (0.072f - c * 0.072f + s * 0.928f);
  out.g = color.r * (0.213f - c * 0.213f + s * 0.143f) +
          color.g * (0.715f + c * 0.285f + s * 0.140f) +
          color.b * (0.072f - c * 0.072f - s * 0.283f);
  out.b = color.r * (0.213f - c * 0.213f - s * 0.787f) +
          color.g * (0.715f - c * 0.715f + s * 0.715f) +
          color.b * (0.072f + c * 0.928f + s * 0.072f);
  out.a = color.a;
  out.valid = true;
  return out;
}

struct Tap {
  float dx;
  float dy;
};
constexpr float kBlurTapWeight = 0.12f;

std::vector<Tap> blurTaps(float radius) {
  if (radius <= 0.0f) return {Tap{0.0f, 0.0f}};
  std::vector<Tap> taps;
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) taps.push_back(Tap{x * radius, y * radius});
  }
  return taps;
}

}  // namespace

float resolveLength(const Length &length, float base, float em, float viewportWidth,
                    float viewportHeight) {
  return xt_style_resolve_absolute(length, base, em, 16.0f, viewportWidth, viewportHeight);
}

FilterChain extendFilter(const FilterChain &parent, const std::vector<FilterOp> &filters,
                         const XtStyle &style, float viewportWidth, float viewportHeight) {
  FilterChain chain = parent;
  /* Drop shadows are silhouettes of one box, never inherited. */
  chain.shadows.clear();
  for (const FilterOp &filter : filters) {
    if (filter.kind == FilterKind::Blur) {
      float blur = resolveLength(filter.blur, 0.0f, style.font_size, viewportWidth, viewportHeight);
      if (blur > 0.0f) chain.blur += blur;
    } else if (filter.kind == FilterKind::DropShadow) {
      FilterChain::Shadow shadow;
      shadow.dx = resolveLength(filter.dx, 0.0f, style.font_size, viewportWidth, viewportHeight);
      shadow.dy = resolveLength(filter.dy, 0.0f, style.font_size, viewportWidth, viewportHeight);
      shadow.blur =
          resolveLength(filter.blur, 0.0f, style.font_size, viewportWidth, viewportHeight);
      shadow.color = filter.has_color ? filter.color : style.color;
      chain.shadows.push_back(shadow);
    } else {
      chain.ops.push_back(filter);
    }
  }
  return chain;
}

Color filterColor(const FilterChain &chain, Color color) {
  for (const FilterOp &filter : chain.ops) {
    switch (filter.kind) {
      case FilterKind::Brightness:
        color.r *= filter.amount;
        color.g *= filter.amount;
        color.b *= filter.amount;
        break;
      case FilterKind::Contrast:
        color.r = (color.r - 0.5f) * filter.amount + 0.5f;
        color.g = (color.g - 0.5f) * filter.amount + 0.5f;
        color.b = (color.b - 0.5f) * filter.amount + 0.5f;
        break;
      case FilterKind::Grayscale: {
        float luma = luminance(color);
        Color gray{luma, luma, luma, color.a, true};
        color = lerpColor(color, gray, filter.amount);
        break;
      }
      case FilterKind::Sepia:
        color = applySepia(color, filter.amount);
        break;
      case FilterKind::Invert:
        color.r = color.r + (1.0f - 2.0f * color.r) * filter.amount;
        color.g = color.g + (1.0f - 2.0f * color.g) * filter.amount;
        color.b = color.b + (1.0f - 2.0f * color.b) * filter.amount;
        break;
      case FilterKind::Saturate:
        color = applySaturate(color, filter.amount);
        break;
      case FilterKind::HueRotate:
        color = applyHueRotate(color, filter.amount);
        break;
      case FilterKind::Opacity:
        color.a *= filter.amount;
        break;
      default:
        break;
    }
    color.r = clamp01(color.r);
    color.g = clamp01(color.g);
    color.b = clamp01(color.b);
    color.a = clamp01(color.a);
  }
  color.a = clamp01(color.a * chain.opacity);
  return color;
}

Color imageTint(const FilterChain &chain) {
  /* The image shader multiplies the sampled texel by this colour, which can
   * express brightness and opacity exactly. Other colour functions need a
   * per-pixel pass; leaving the tint neutral is less wrong than a channel
   * multiply, so they are skipped for images. */
  Color tint{1.0f, 1.0f, 1.0f, 1.0f};
  for (const FilterOp &filter : chain.ops) {
    if (filter.kind == FilterKind::Brightness) {
      tint.r *= filter.amount;
      tint.g *= filter.amount;
      tint.b *= filter.amount;
    } else if (filter.kind == FilterKind::Opacity) {
      tint.a *= filter.amount;
    }
  }
  tint.r = clamp01(tint.r);
  tint.g = clamp01(tint.g);
  tint.b = clamp01(tint.b);
  tint.a = clamp01(tint.a * chain.opacity);
  return tint;
}

void addRect(DisplayList &out, float x, float y, float width, float height, float radius,
             const Color &color, const PaintClip &clip, const Matrix2D &transform) {
  if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f) return;
  PaintRect rect;
  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;
  rect.radius = radius;
  rect.color = color;
  rect.transform = transform;
  out.rects.push_back(rect);
  out.ops.push_back(PaintOp{PaintKind::Rect, (uint32_t)(out.rects.size() - 1), clip});
}

void addRectFiltered(DisplayList &out, float x, float y, float width, float height, float radius,
                     const Color &color, const PaintClip &clip, const Matrix2D &transform,
                     const FilterChain &chain) {
  Color filtered = filterColor(chain, color);
  if (filtered.a <= 0.0f) return;
  if (chain.blur <= 0.0f) {
    addRect(out, x, y, width, height, radius, filtered, clip, transform);
    return;
  }
  for (const Tap &tap : blurTaps(chain.blur)) {
    Color copy = filtered;
    copy.a *= kBlurTapWeight;
    addRect(out, x + tap.dx, y + tap.dy, width, height, radius, copy, clip, transform);
  }
}

void addBoxDropShadows(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                       const FilterChain &chain, const PaintClip &clip) {
  if (chain.shadows.empty()) return;
  for (const FilterChain::Shadow &shadow : chain.shadows) {
    Color color = filterColor(chain, shadow.color);
    if (color.a <= 0.0f) continue;
    std::vector<Tap> taps = blurTaps(shadow.blur);
    for (const Tap &tap : taps) {
      Color copy = color;
      if (shadow.blur > 0.0f) copy.a *= kBlurTapWeight;
      addRect(out, box.x + shadow.dx + tap.dx, box.y + shadow.dy + tap.dy, box.width, box.height,
              style.border_radius, copy, clip, box.world);
    }
  }
}

void addTextRun(DisplayList &out, float x, float baseline, float width, float height,
                const std::string &text, const FontSpec &font, const Color &color,
                const Matrix2D &transform, const PaintClip &clip, const XtStyle &style,
                const FilterChain &chain, float viewportWidth, float viewportHeight) {
  if (text.empty() || color.a <= 0.0f) return;
  auto emit = [&](float dx, float dy, Color runColor, bool shadow) {
    if (runColor.a <= 0.0f) return;
    PaintText run;
    run.x = x + dx;
    run.baseline = baseline + dy;
    run.width = width;
    run.height = height;
    run.color = runColor;
    run.text = text;
    run.font = font;
    run.transform = transform;
    run.shadow = shadow;
    out.texts.push_back(std::move(run));
    out.ops.push_back(PaintOp{PaintKind::Text, (uint32_t)(out.texts.size() - 1), clip});
  };

  /* `filter: drop-shadow()` first (behind the glyphs). */
  for (const FilterChain::Shadow &shadow : chain.shadows) {
    Color shadowColor = filterColor(chain, shadow.color);
    for (const Tap &tap : blurTaps(shadow.blur)) {
      Color copy = shadowColor;
      if (shadow.blur > 0.0f) copy.a *= kBlurTapWeight;
      emit(shadow.dx + tap.dx, shadow.dy + tap.dy, copy, true);
    }
  }

  /* `text-shadow`, bottom-most first: CSS paints later layers under earlier
   * ones, so reverse the declaration list. */
  for (size_t index = style.text_shadow.size(); index-- > 0;) {
    const TextShadow &shadow = style.text_shadow[index];
    float dx = resolveLength(shadow.dx, 0.0f, style.font_size, viewportWidth, viewportHeight);
    float dy = resolveLength(shadow.dy, 0.0f, style.font_size, viewportWidth, viewportHeight);
    float blur =
        resolveLength(shadow.blur, 0.0f, style.font_size, viewportWidth, viewportHeight);
    Color shadowColor = shadow.has_color ? filterColor(chain, shadow.color) : color;
    for (const Tap &tap : blurTaps(blur)) {
      Color copy = shadowColor;
      if (blur > 0.0f) copy.a *= kBlurTapWeight;
      emit(dx + tap.dx, dy + tap.dy, copy, true);
    }
  }

  /* `filter: blur()` spreads the glyphs themselves. */
  if (chain.blur > 0.0f) {
    for (const Tap &tap : blurTaps(chain.blur)) {
      Color copy = color;
      copy.a *= kBlurTapWeight;
      emit(tap.dx, tap.dy, copy, false);
    }
    return;
  }
  emit(0.0f, 0.0f, color, false);
}

void addImage(DisplayList &out, PaintImage image, const PaintClip &clip,
              const FilterChain &chain) {
  if (image.width <= 0.0f || image.height <= 0.0f) return;
  image.tint = imageTint(chain);
  if (image.tint.a <= 0.0f) return;
  if (chain.blur <= 0.0f) {
    out.images.push_back(std::move(image));
    out.ops.push_back(PaintOp{PaintKind::Image, (uint32_t)(out.images.size() - 1), clip});
    return;
  }
  for (const Tap &tap : blurTaps(chain.blur)) {
    PaintImage copy = image;
    copy.x += tap.dx;
    copy.y += tap.dy;
    copy.tint.a *= kBlurTapWeight;
    out.images.push_back(std::move(copy));
    out.ops.push_back(PaintOp{PaintKind::Image, (uint32_t)(out.images.size() - 1), clip});
  }
}

}  // namespace paint_detail
}  // namespace xtgui
