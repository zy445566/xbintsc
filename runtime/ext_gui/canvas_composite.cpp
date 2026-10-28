/*
 * xbintsc GUI engine — Canvas2D `globalCompositeOperation`.
 *
 * Every raster path funnels its per-pixel work through `blend_pixel_mode`,
 * which implements the Porter-Duff operators and the separable/non-separable
 * blend modes from the CSS compositing specification. Colours are kept
 * non-premultiplied (the canvas bitmap is RGBA8 straight alpha); the composite
 * is evaluated in premultiplied form and divided back out.
 */

#include "canvas_internal.h"

#include <algorithm>
#include <cmath>

namespace xtgui {
namespace canvas_detail {

namespace {

float clamp_unit(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

float channel_blend(int mode, float s, float d) {
  switch (mode) {
    case kCanvasCompositeMultiply:
      return s * d;
    case kCanvasCompositeScreen:
      return s + d - s * d;
    case kCanvasCompositeOverlay:
      return d <= 0.5f ? 2.0f * s * d : 1.0f - 2.0f * (1.0f - s) * (1.0f - d);
    case kCanvasCompositeDarken:
      return std::min(s, d);
    case kCanvasCompositeLighten:
      return std::max(s, d);
    case kCanvasCompositeColorDodge:
      if (d <= 0.0f) return 0.0f;
      if (s >= 1.0f) return 1.0f;
      return std::min(1.0f, d / (1.0f - s));
    case kCanvasCompositeColorBurn:
      if (d >= 1.0f) return 1.0f;
      if (s <= 0.0f) return 0.0f;
      return 1.0f - std::min(1.0f, (1.0f - d) / s);
    case kCanvasCompositeHardLight:
      return s <= 0.5f ? 2.0f * s * d : 1.0f - 2.0f * (1.0f - s) * (1.0f - d);
    case kCanvasCompositeSoftLight: {
      if (s <= 0.5f) return d - (1.0f - 2.0f * s) * d * (1.0f - d);
      float d2 = d <= 0.25f ? ((16.0f * d - 12.0f) * d + 4.0f) * d : std::sqrt(d);
      return d + (2.0f * s - 1.0f) * (d2 - d);
    }
    case kCanvasCompositeDifference:
      return std::fabs(s - d);
    case kCanvasCompositeExclusion:
      return s + d - 2.0f * s * d;
    default:
      return s;
  }
}

float luminance(const float c[3]) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }

void clip_color(float c[3]) {
  const float l = luminance(c);
  float n = std::min(c[0], std::min(c[1], c[2]));
  float x = std::max(c[0], std::max(c[1], c[2]));
  if (n < 0.0f) {
    const float scale = l / (l - n);
    for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) * scale;
  }
  if (x > 1.0f) {
    const float scale = (1.0f - l) / (x - l);
    for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) * scale;
  }
}

void set_luminance(float c[3], float l) {
  const float delta = l - luminance(c);
  for (int i = 0; i < 3; i++) c[i] += delta;
  clip_color(c);
}

float saturation(const float c[3]) {
  return std::max(c[0], std::max(c[1], c[2])) - std::min(c[0], std::min(c[1], c[2]));
}

void set_saturation(float c[3], float s) {
  int maxIndex = 0;
  int minIndex = 0;
  for (int i = 1; i < 3; i++) {
    if (c[i] > c[maxIndex]) maxIndex = i;
    if (c[i] < c[minIndex]) minIndex = i;
  }
  if (maxIndex == minIndex) return;
  float out[3] = {0.0f, 0.0f, 0.0f};
  out[maxIndex] = s;
  out[minIndex] = 0.0f;
  int middle = 3 - maxIndex - minIndex;
  out[middle] = s * (c[middle] - c[minIndex]) / (c[maxIndex] - c[minIndex]);
  for (int i = 0; i < 3; i++) c[i] = out[i];
}

/** Non-separable modes (`hue`…`luminosity`). */
void non_separable_blend(int mode, const float s[3], const float d[3], float out[3]) {
  float source[3] = {s[0], s[1], s[2]};
  float dest[3] = {d[0], d[1], d[2]};
  switch (mode) {
    case kCanvasCompositeHue:
      set_saturation(source, saturation(dest));
      set_luminance(source, luminance(dest));
      break;
    case kCanvasCompositeSaturation:
      set_saturation(dest, saturation(source));
      set_luminance(dest, luminance(dest));
      for (int i = 0; i < 3; i++) source[i] = dest[i];
      break;
    case kCanvasCompositeColor:
      set_luminance(source, luminance(dest));
      break;
    case kCanvasCompositeLuminosity:
      set_luminance(dest, luminance(source));
      for (int i = 0; i < 3; i++) source[i] = dest[i];
      break;
    default:
      break;
  }
  for (int i = 0; i < 3; i++) out[i] = clamp_unit(source[i]);
}

bool is_non_separable(int mode) {
  return mode == kCanvasCompositeHue || mode == kCanvasCompositeSaturation ||
         mode == kCanvasCompositeColor || mode == kCanvasCompositeLuminosity;
}

bool is_separable_blend(int mode) {
  return mode >= kCanvasCompositeMultiply && mode <= kCanvasCompositeExclusion && !is_non_separable(mode);
}

}  // namespace

void blend_pixel_mode(unsigned char *pixel, const float color[4], float coverage, float alpha,
                      int composite) {
  if (composite == kCanvasCompositeSourceOver && alpha >= 0.999f && coverage >= 0.999f &&
      color[3] >= 0.999f) {
    pixel[0] = (unsigned char)(clamp_unit(color[0]) * 255.0f + 0.5f);
    pixel[1] = (unsigned char)(clamp_unit(color[1]) * 255.0f + 0.5f);
    pixel[2] = (unsigned char)(clamp_unit(color[2]) * 255.0f + 0.5f);
    pixel[3] = 255;
    return;
  }

  float src[3] = {clamp_unit(color[0]), clamp_unit(color[1]), clamp_unit(color[2])};
  const float sa = clamp_unit(color[3] * alpha);
  float dst[3] = {pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f};
  const float da = pixel[3] / 255.0f;

  float outA = 0.0f;
  float outC[3] = {0.0f, 0.0f, 0.0f};  // premultiplied

  if (is_separable_blend(composite) || is_non_separable(composite)) {
    float blended[3];
    if (is_non_separable(composite)) {
      non_separable_blend(composite, src, dst, blended);
    } else {
      for (int i = 0; i < 3; i++) blended[i] = channel_blend(composite, src[i], dst[i]);
    }
    outA = sa + da - sa * da;
    for (int i = 0; i < 3; i++) {
      outC[i] = (1.0f - da) * src[i] * sa + (1.0f - sa) * dst[i] * da + sa * da * blended[i];
    }
  } else {
    switch (composite) {
      case kCanvasCompositeSourceIn:
        outA = sa * da;
        for (int i = 0; i < 3; i++) outC[i] = src[i] * sa * da;
        break;
      case kCanvasCompositeSourceOut:
        outA = sa * (1.0f - da);
        for (int i = 0; i < 3; i++) outC[i] = src[i] * sa * (1.0f - da);
        break;
      case kCanvasCompositeSourceAtop:
        outA = da;
        for (int i = 0; i < 3; i++) outC[i] = src[i] * sa * da + dst[i] * da * (1.0f - sa);
        break;
      case kCanvasCompositeDestinationOver:
        outA = da + sa * (1.0f - da);
        for (int i = 0; i < 3; i++) outC[i] = dst[i] * da + src[i] * sa * (1.0f - da);
        break;
      case kCanvasCompositeDestinationIn:
        outA = da * sa;
        for (int i = 0; i < 3; i++) outC[i] = dst[i] * da * sa;
        break;
      case kCanvasCompositeDestinationOut:
        outA = da * (1.0f - sa);
        for (int i = 0; i < 3; i++) outC[i] = dst[i] * da * (1.0f - sa);
        break;
      case kCanvasCompositeDestinationAtop:
        outA = sa;
        for (int i = 0; i < 3; i++) outC[i] = dst[i] * da * sa + src[i] * sa * (1.0f - da);
        break;
      case kCanvasCompositeLighter:
        outA = std::min(1.0f, sa + da);
        for (int i = 0; i < 3; i++) outC[i] = std::min(1.0f, src[i] * sa + dst[i] * da);
        break;
      case kCanvasCompositeCopy:
        outA = sa;
        for (int i = 0; i < 3; i++) outC[i] = src[i] * sa;
        break;
      case kCanvasCompositeXor:
        outA = sa * (1.0f - da) + da * (1.0f - sa);
        for (int i = 0; i < 3; i++) {
          outC[i] = src[i] * sa * (1.0f - da) + dst[i] * da * (1.0f - sa);
        }
        break;
      default:  // source-over
        outA = sa + da * (1.0f - sa);
        for (int i = 0; i < 3; i++) outC[i] = src[i] * sa + dst[i] * da * (1.0f - sa);
        break;
    }
  }

  /* Partial coverage (anti-aliasing or a clip edge) interpolates between the
   * untouched destination and the full composite, which is correct for every
   * operator, including `destination-in`/`copy`. */
  if (coverage < 0.999f) {
    const float c = clamp_unit(coverage);
    outA = da * (1.0f - c) + outA * c;
    for (int i = 0; i < 3; i++) outC[i] = dst[i] * da * (1.0f - c) + outC[i] * c;
  }

  if (outA <= 0.00001f) {
    pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
    return;
  }
  const float inverse = 1.0f / outA;
  for (int i = 0; i < 3; i++) {
    pixel[i] = (unsigned char)(clamp_unit(outC[i] * inverse) * 255.0f + 0.5f);
  }
  pixel[3] = (unsigned char)(clamp_unit(outA) * 255.0f + 0.5f);
}

}  // namespace canvas_detail
}  // namespace xtgui
