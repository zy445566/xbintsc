/*
 * xbintsc GUI engine — the software rasteriser behind `<canvas>`.
 *
 * Everything here is CPU-side and backend-free. Polygons are filled with the
 * nonzero winding rule and vertical sub-scanline sampling (4 samples per pixel
 * row) plus analytic horizontal coverage, which gives cheap anti-aliasing
 * without a full analytic rasteriser. Rectangles take a faster axis-aligned
 * path, and glyphs are composited from their FreeType coverage masks.
 */

#include "canvas_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xtgui {
namespace canvas_detail {

namespace {

constexpr int kSubSamples = 4;

int clamp_byte(float value) {
  int result = (int)(value + 0.5f);
  if (result < 0) result = 0;
  if (result > 255) result = 255;
  return result;
}

/** One edge crossing on the current sub-scanline. */
struct Crossing {
  float x;
  int winding;
};

/** Resolve `paint` at a device-space pixel centre. Gradients are defined in
 * user space, so the pixel is mapped back through the inverse transform. */
void paint_color_at(const CanvasPaint &paint, float x, float y, float out[4]) {
  if (paint.pattern != nullptr || paint.gradient != nullptr) {
    const CanvasTransform &inverse = paint.inverse;
    float userX = inverse.a * x + inverse.c * y + inverse.e;
    float userY = inverse.b * x + inverse.d * y + inverse.f;
    if (paint.pattern != nullptr) {
      if (paint.pattern->color_at(userX, userY, out)) return;
    } else if (paint.gradient->color_at(userX, userY, out)) {
      return;
    }
  }
  if (paint.color != nullptr) {
    for (int i = 0; i < 4; i++) out[i] = paint.color[i];
    return;
  }
  out[0] = out[1] = out[2] = out[3] = 0.0f;
}

}  // namespace

void blend_pixel(unsigned char *pixel, const float color[4], float coverage, float alpha) {
  /* Fully opaque source-over is a plain copy; this is the hot path for paper
     textures, terrain and the opaque parts of every sprite. */
  if (alpha >= 0.999f && coverage >= 0.999f && color[3] >= 0.999f) {
    pixel[0] = (unsigned char)clamp_byte(color[0] * 255.0f);
    pixel[1] = (unsigned char)clamp_byte(color[1] * 255.0f);
    pixel[2] = (unsigned char)clamp_byte(color[2] * 255.0f);
    pixel[3] = 255;
    return;
  }
  float sa = color[3] * coverage * alpha;
  if (sa <= 0.0f) return;
  if (sa > 1.0f) sa = 1.0f;
  float da = pixel[3] / 255.0f;
  float out_a = sa + da * (1.0f - sa);
  if (out_a <= 0.0f) {
    pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
    return;
  }
  const float inv = 1.0f / out_a;
  pixel[0] = (unsigned char)clamp_byte((color[0] * sa + (pixel[0] / 255.0f) * da * (1.0f - sa)) * inv * 255.0f);
  pixel[1] = (unsigned char)clamp_byte((color[1] * sa + (pixel[1] / 255.0f) * da * (1.0f - sa)) * inv * 255.0f);
  pixel[2] = (unsigned char)clamp_byte((color[2] * sa + (pixel[2] / 255.0f) * da * (1.0f - sa)) * inv * 255.0f);
  pixel[3] = (unsigned char)clamp_byte(out_a * 255.0f);
}

/** Add the horizontal span `[xa, xb)` into a bbox-relative coverage row. */
static void add_span(float *row, int rowOriginX, float xa, float xb, float weight, int clipX0,
                     int clipX1) {
  if (xb <= xa) return;
  if (xa < (float)clipX0) xa = (float)clipX0;
  if (xb > (float)clipX1) xb = (float)clipX1;
  if (xb <= xa) return;
  int ia = (int)std::floor(xa);
  int ib = (int)std::ceil(xb) - 1;
  if (ib < ia) return;
  if (ia == ib) {
    row[ia - rowOriginX] += (xb - xa) * weight;
    return;
  }
  row[ia - rowOriginX] += ((float)(ia + 1) - xa) * weight;
  for (int x = ia + 1; x < ib; x++) row[x - rowOriginX] += weight;
  float last = xb - (float)ib;
  if (last > 0.0f) row[ib - rowOriginX] += last * weight;
}

void fill_loops(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<std::vector<CanvasPoint>> &loops, const CanvasPaint &paint,
                float alpha, bool even_odd) {
  if (width <= 0 || height <= 0 || loops.empty()) return;
  bool havePoint = false;
  float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
  for (const std::vector<CanvasPoint> &loop : loops) {
    for (const CanvasPoint &point : loop) {
      if (!havePoint) {
        minX = maxX = point.x;
        minY = maxY = point.y;
        havePoint = true;
      } else {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
      }
    }
  }
  if (!havePoint) return;
  int x0 = std::max(0, (int)std::floor(minX));
  int x1 = std::min(width, (int)std::ceil(maxX));
  int y0 = std::max(0, (int)std::floor(minY));
  int y1 = std::min(height, (int)std::ceil(maxY));
  if (x1 <= x0 || y1 <= y0) return;

  int boxWidth = x1 - x0;
  std::vector<float> coverage((size_t)boxWidth * (size_t)(y1 - y0), 0.0f);
  const float weight = 1.0f / (float)kSubSamples;

  std::vector<Crossing> crossings;
  for (int py = y0; py < y1; py++) {
    float *row = coverage.data() + (size_t)(py - y0) * (size_t)boxWidth;
    for (int sample = 0; sample < kSubSamples; sample++) {
      float sy = (float)py + ((float)sample + 0.5f) / (float)kSubSamples;
      crossings.clear();
      for (const std::vector<CanvasPoint> &loop : loops) {
        if (loop.size() < 3) continue;
        for (size_t i = 0; i < loop.size(); i++) {
          const CanvasPoint &p0 = loop[i];
          const CanvasPoint &p1 = loop[(i + 1) % loop.size()];
          if (p0.y == p1.y) continue;
          float top = std::min(p0.y, p1.y);
          float bottom = std::max(p0.y, p1.y);
          if (sy < top || sy >= bottom) continue;
          float t = (sy - p0.y) / (p1.y - p0.y);
          Crossing crossing;
          crossing.x = p0.x + t * (p1.x - p0.x);
          crossing.winding = p1.y > p0.y ? 1 : -1;
          crossings.push_back(crossing);
        }
      }
      if (crossings.empty()) continue;
      std::sort(crossings.begin(), crossings.end(),
                [](const Crossing &a, const Crossing &b) { return a.x < b.x; });
      if (even_odd) {
        /* Every second crossing toggles the covered span, regardless of the
         * direction each contour is wound. */
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
          add_span(row, x0, crossings[i].x, crossings[i + 1].x, weight, x0, x1);
        }
        continue;
      }
      int winding = 0;
      float spanStart = 0.0f;
      for (const Crossing &crossing : crossings) {
        if (winding == 0) spanStart = crossing.x;
        winding += crossing.winding;
        if (winding == 0) add_span(row, x0, spanStart, crossing.x, weight, x0, x1);
      }
    }
  }

  for (int py = y0; py < y1; py++) {
    const float *row = coverage.data() + (size_t)(py - y0) * (size_t)boxWidth;
    unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)x0) * 4;
    for (int px = 0; px < boxWidth; px++) {
      float c = row[px];
      if (c <= 0.0f) continue;
      if (c > 1.0f) c = 1.0f;
      float color[4];
      paint_color_at(paint, (float)(x0 + px) + 0.5f, (float)py + 0.5f, color);
      blend_pixel(dst + (size_t)px * 4, color, c, alpha);
    }
  }
}

void fill_loop(std::vector<unsigned char> &pixels, int width, int height,
               const std::vector<CanvasPoint> &loop, const CanvasPaint &paint, float alpha,
               bool even_odd) {
  fill_loops(pixels, width, height, std::vector<std::vector<CanvasPoint>>{loop}, paint, alpha,
             even_odd);
}

void fill_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
               float x1, float y1, const CanvasPaint &paint, float alpha) {
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  int ix0 = std::max(0, (int)std::floor(x0));
  int ix1 = std::min(width, (int)std::ceil(x1));
  int iy0 = std::max(0, (int)std::floor(y0));
  int iy1 = std::min(height, (int)std::ceil(y1));
  for (int py = iy0; py < iy1; py++) {
    float cy = std::min(y1, (float)(py + 1)) - std::max(y0, (float)py);
    if (cy <= 0.0f) continue;
    unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)ix0) * 4;
    for (int px = ix0; px < ix1; px++) {
      float cx = std::min(x1, (float)(px + 1)) - std::max(x0, (float)px);
      if (cx <= 0.0f) continue;
      float color[4];
      paint_color_at(paint, (float)px + 0.5f, (float)py + 0.5f, color);
      blend_pixel(dst + (size_t)(px - ix0) * 4, color, cx * cy, alpha);
    }
  }
}

void clear_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
                float x1, float y1) {
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  int ix0 = std::max(0, (int)std::floor(x0));
  int ix1 = std::min(width, (int)std::ceil(x1));
  int iy0 = std::max(0, (int)std::floor(y0));
  int iy1 = std::min(height, (int)std::ceil(y1));
  for (int py = iy0; py < iy1; py++) {
    float cy = std::min(y1, (float)(py + 1)) - std::max(y0, (float)py);
    if (cy <= 0.0f) continue;
    unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)ix0) * 4;
    for (int px = ix0; px < ix1; px++) {
      float cx = std::min(x1, (float)(px + 1)) - std::max(x0, (float)px);
      if (cx <= 0.0f) continue;
      unsigned char *pixel = dst + (size_t)(px - ix0) * 4;
      float keep = 1.0f - cx * cy;
      if (keep <= 0.0f) {
        pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
      } else {
        pixel[3] = (unsigned char)clamp_byte(pixel[3] * keep);
      }
    }
  }
}

void draw_image(std::vector<unsigned char> &pixels, int width, int height, const Image &source,
                float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                float alpha) {
  if (source.width <= 0 || source.height <= 0 || dw <= 0.0f || dh <= 0.0f || sw <= 0.0f ||
      sh <= 0.0f)
    return;
  int ix0 = std::max(0, (int)std::floor(dx));
  int ix1 = std::min(width, (int)std::ceil(dx + dw));
  int iy0 = std::max(0, (int)std::floor(dy));
  int iy1 = std::min(height, (int)std::ceil(dy + dh));
  for (int py = iy0; py < iy1; py++) {
    for (int px = ix0; px < ix1; px++) {
      float u = ((float)px + 0.5f - dx) / dw;
      float v = ((float)py + 0.5f - dy) / dh;
      if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
      int sxi = (int)(sx + u * sw);
      int syi = (int)(sy + v * sh);
      if (sxi < 0) sxi = 0;
      if (syi < 0) syi = 0;
      if (sxi >= source.width) sxi = source.width - 1;
      if (syi >= source.height) syi = source.height - 1;
      const unsigned char *texel =
          source.pixels.data() + ((size_t)syi * (size_t)source.width + (size_t)sxi) * 4;
      unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)px) * 4;
      if (alpha >= 0.999f) {
        const unsigned char ta = texel[3];
        if (ta == 255) {
          dst[0] = texel[0]; dst[1] = texel[1]; dst[2] = texel[2]; dst[3] = 255;
          continue;
        }
        if (ta == 0) continue;
      }
      float color[4] = {texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, texel[3] / 255.0f};
      blend_pixel(dst, color, 1.0f, alpha);
    }
  }
}


void draw_image_affine(std::vector<unsigned char> &pixels, int width, int height,
                       const Image &source, float dx, float dy, float dw, float dh, float sx,
                       float sy, float sw, float sh, const CanvasTransform &transform,
                       float alpha) {
  if (source.width <= 0 || source.height <= 0 || dw <= 0.0f || dh <= 0.0f || sw <= 0.0f ||
      sh <= 0.0f)
    return;
  CanvasTransform inverse = transform.inverted();
  float cornersX[4];
  float cornersY[4];
  const float corners[4][2] = {{dx, dy}, {dx + dw, dy}, {dx + dw, dy + dh}, {dx, dy + dh}};
  float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
  for (int i = 0; i < 4; i++) {
    transform.apply(corners[i][0], corners[i][1], &cornersX[i], &cornersY[i]);
    if (i == 0) {
      minX = maxX = cornersX[i];
      minY = maxY = cornersY[i];
    } else {
      minX = std::min(minX, cornersX[i]);
      maxX = std::max(maxX, cornersX[i]);
      minY = std::min(minY, cornersY[i]);
      maxY = std::max(maxY, cornersY[i]);
    }
  }
  int ix0 = std::max(0, (int)std::floor(minX));
  int ix1 = std::min(width, (int)std::ceil(maxX));
  int iy0 = std::max(0, (int)std::floor(minY));
  int iy1 = std::min(height, (int)std::ceil(maxY));
  for (int py = iy0; py < iy1; py++) {
    for (int px = ix0; px < ix1; px++) {
      float ux = 0.0f;
      float uy = 0.0f;
      inverse.apply((float)px + 0.5f, (float)py + 0.5f, &ux, &uy);
      float u = (ux - dx) / dw;
      float v = (uy - dy) / dh;
      if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
      int sxi = (int)(sx + u * sw);
      int syi = (int)(sy + v * sh);
      if (sxi < 0) sxi = 0;
      if (syi < 0) syi = 0;
      if (sxi >= source.width) sxi = source.width - 1;
      if (syi >= source.height) syi = source.height - 1;
      const unsigned char *texel =
          source.pixels.data() + ((size_t)syi * (size_t)source.width + (size_t)sxi) * 4;
      unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)px) * 4;
      if (alpha >= 0.999f) {
        const unsigned char ta = texel[3];
        if (ta == 255) {
          dst[0] = texel[0]; dst[1] = texel[1]; dst[2] = texel[2]; dst[3] = 255;
          continue;
        }
        if (ta == 0) continue;
      }
      float color[4] = {texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, texel[3] / 255.0f};
      blend_pixel(dst, color, 1.0f, alpha);
    }
  }
}

void blit_glyph(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<unsigned char> &glyph, int glyphWidth, int glyphHeight, int left,
                int top, float x, float baselineY, float scale_x, const CanvasPaint &paint,
                float alpha) {
  if (glyphWidth <= 0 || glyphHeight <= 0) return;
  if (scale_x <= 0.0f) scale_x = 1.0f;
  bool condense = scale_x < 0.999f;
  int drawWidth = condense ? std::max(1, (int)std::lround((float)glyphWidth * scale_x))
                           : glyphWidth;
  int originX = (int)std::lround(x) + (int)std::lround((float)left * scale_x);
  int originY = (int)std::lround(baselineY) - top;
  for (int gy = 0; gy < glyphHeight; gy++) {
    int py = originY + gy;
    if (py < 0 || py >= height) continue;
    for (int gx = 0; gx < drawWidth; gx++) {
      int px = originX + gx;
      if (px < 0 || px >= width) continue;
      /* Nearest sample from the glyph coverage mask when condensing. */
      int sourceX = condense ? std::min(glyphWidth - 1, (int)((float)gx / scale_x)) : gx;
      float coverage = glyph[(size_t)gy * (size_t)glyphWidth + (size_t)sourceX] / 255.0f;
      if (coverage <= 0.0f) continue;
      float color[4];
      paint_color_at(paint, (float)px + 0.5f, (float)py + 0.5f, color);
      blend_pixel(pixels.data() + ((size_t)py * (size_t)width + (size_t)px) * 4, color, coverage,
                  alpha);
    }
  }
}

}  // namespace canvas_detail
}  // namespace xtgui
