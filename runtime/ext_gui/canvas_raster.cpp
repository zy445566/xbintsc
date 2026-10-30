/*
 * xbintsc GUI engine — the software rasteriser behind `<canvas>`.
 *
 * Everything here is CPU-side and backend-free. Polygons are filled with the
 * nonzero winding rule and vertical sub-scanline sampling (4 samples per pixel
 * row) plus analytic horizontal coverage, which gives cheap anti-aliasing
 * without a full analytic rasteriser. Rectangles take a faster axis-aligned
 * path, and glyphs are composited from their stb_truetype coverage masks.
 */

#include "canvas_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xtgui {
namespace canvas_detail {

namespace {

int clamp_byte(float value) {
  int result = (int)(value + 0.5f);
  if (result < 0) result = 0;
  if (result > 255) result = 255;
  return result;
}

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

/** True when the paint is a single colour, so the per-pixel colour resolution
 * (a branch on gradient/pattern and a 4-float copy) can be hoisted out of the
 * pixel loop. A solid paint is the common case in a multi-element scene. */
bool paint_is_solid(const CanvasPaint &paint) {
  return paint.gradient == nullptr && paint.pattern == nullptr && paint.color != nullptr;
}

void copy_color(const float color[4], float out[4]) {
  out[0] = color[0];
  out[1] = color[1];
  out[2] = color[2];
  out[3] = color[3];
}

unsigned char color_byte(float value) {
  value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  return (unsigned char)(value * 255.0f + 0.5f);
}

/** True when a source-over paint writes its colour unchanged at full coverage
 * (opaque colour, opaque globalAlpha): the rasteriser can store bytes directly
 * instead of running the compositing math per pixel. */
bool paint_is_opaque_source(const CanvasPaint &paint, float alpha) {
  return paint_is_solid(paint) && paint.composite == kCanvasCompositeSourceOver &&
         alpha >= 0.999f && paint.color[3] >= 0.999f;
}

}  // namespace

void blend_pixel(unsigned char *pixel, const float color[4], float coverage, float alpha) {
  blend_pixel_mode(pixel, color, coverage, alpha, kCanvasCompositeSourceOver);
}

/** Source-over with a clip mask applied to the coverage. */
static void blend_clipped(unsigned char *pixel, const float color[4], float coverage, float alpha,
                          const CanvasPaint &paint, const CanvasClipMask *clip, int x, int y) {
  if (clip != nullptr) {
    const float clipCoverage = clip->at(x, y) / 255.0f;
    if (clipCoverage <= 0.0f) return;
    coverage *= clipCoverage;
  }
  blend_pixel_mode(pixel, color, coverage, alpha, paint.composite);
}

void fill_loops(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<std::vector<CanvasPoint>> &loops, const CanvasPaint &paint,
                float alpha, bool even_odd, const CanvasClipMask *clip) {
  CoverageGrid coverage;
  compute_coverage(width, height, loops, even_odd, &coverage);
  if (coverage.empty()) return;
  const bool solid = paint_is_solid(paint);
  float solid_color[4];
  if (solid) copy_color(paint.color, solid_color);
  const int rows = coverage.y1 - coverage.y0;
  for (int r = 0; r < rows; r++) {
    const uint32_t begin = coverage.row_offset[(size_t)r];
    const uint32_t end = coverage.row_offset[(size_t)r + 1];
    if (begin >= end) continue;
    const int py = coverage.y0 + r;
    const int rx = coverage.row_x[(size_t)r];
    const float *row = coverage.values.data() + begin;
    const int row_width = (int)(end - begin);
    unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)rx) * 4;
    for (int i = 0; i < row_width; i++) {
      float c = row[i];
      if (c <= 0.0f) continue;
      if (c > 1.0f) c = 1.0f;
      float color[4];
      if (solid) {
        copy_color(solid_color, color);
      } else {
        paint_color_at(paint, (float)(rx + i) + 0.5f, (float)py + 0.5f, color);
      }
      blend_clipped(dst + (size_t)i * 4, color, c, alpha, paint, clip, rx + i, py);
    }
  }
}

void fill_loop(std::vector<unsigned char> &pixels, int width, int height,
               const std::vector<CanvasPoint> &loop, const CanvasPaint &paint, float alpha,
               bool even_odd, const CanvasClipMask *clip) {
  fill_loops(pixels, width, height, std::vector<std::vector<CanvasPoint>>{loop}, paint, alpha,
             even_odd, clip);
}

void fill_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
               float x1, float y1, const CanvasPaint &paint, float alpha,
               const CanvasClipMask *clip) {
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  int ix0 = std::max(0, (int)std::floor(x0));
  int ix1 = std::min(width, (int)std::ceil(x1));
  int iy0 = std::max(0, (int)std::floor(y0));
  int iy1 = std::min(height, (int)std::ceil(y1));
  const bool solid = paint_is_solid(paint);
  float solid_color[4];
  if (solid) copy_color(paint.color, solid_color);
  /* A fully opaque solid fill at full coverage is a byte store: the same bytes
   * `blend_pixel_mode` would compute, without the per-pixel compositing math.
   * Fractional edges (and clipped or translucent fills) keep the general path,
   * so the output is unchanged for every other case. */
  unsigned char opaque_bytes[4] = {0, 0, 0, 0};
  const bool opaque = paint_is_opaque_source(paint, alpha) && clip == nullptr;
  if (opaque) {
    opaque_bytes[0] = color_byte(solid_color[0]);
    opaque_bytes[1] = color_byte(solid_color[1]);
    opaque_bytes[2] = color_byte(solid_color[2]);
    opaque_bytes[3] = 255;
  }
  for (int py = iy0; py < iy1; py++) {
    float cy = std::min(y1, (float)(py + 1)) - std::max(y0, (float)py);
    if (cy <= 0.0f) continue;
    unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)ix0) * 4;
    if (opaque && cy >= 0.999f) {
      for (int px = ix0; px < ix1; px++) {
        float cx = std::min(x1, (float)(px + 1)) - std::max(x0, (float)px);
        if (cx <= 0.0f) continue;
        unsigned char *pixel = dst + (size_t)(px - ix0) * 4;
        if (cx >= 0.999f) {
          std::memcpy(pixel, opaque_bytes, 4);
        } else {
          float color[4];
          copy_color(solid_color, color);
          blend_pixel(pixel, color, cx, alpha);
        }
      }
      continue;
    }
    for (int px = ix0; px < ix1; px++) {
      float cx = std::min(x1, (float)(px + 1)) - std::max(x0, (float)px);
      if (cx <= 0.0f) continue;
      float color[4];
      if (solid) {
        copy_color(solid_color, color);
      } else {
        paint_color_at(paint, (float)px + 0.5f, (float)py + 0.5f, color);
      }
      blend_clipped(dst + (size_t)(px - ix0) * 4, color, cx * cy, alpha, paint, clip, px, py);
    }
  }
}

void clear_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
                float x1, float y1, const CanvasClipMask *clip) {
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  if (clip == nullptr && x0 <= 0.0f && y0 <= 0.0f && x1 >= (float)width &&
      y1 >= (float)height) {
    std::fill(pixels.begin(), pixels.end(), (unsigned char)0);
    return;
  }
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
      float amount = cx * cy;
      if (clip != nullptr) amount *= clip->at(px, py) / 255.0f;
      float keep = 1.0f - amount;
      if (keep <= 0.0f) {
        pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
      } else {
        pixel[3] = (unsigned char)clamp_byte(pixel[3] * keep);
      }
    }
  }
}

/** Sample one source image pixel at continuous coordinates (`0..width`).
 * `smooth` selects bilinear filtering, otherwise the containing texel is used. */
static void sample_source(const Image &source, float fx, float fy, bool smooth, float out[4]) {
  auto texel_at = [&](int x, int y, float outPixel[4]) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= source.width) x = source.width - 1;
    if (y >= source.height) y = source.height - 1;
    const unsigned char *texel =
        source.pixels.data() + ((size_t)y * (size_t)source.width + (size_t)x) * 4;
    for (int i = 0; i < 4; i++) outPixel[i] = texel[i] / 255.0f;
  };
  if (!smooth) {
    texel_at((int)fx, (int)fy, out);
    return;
  }
  const float x = fx - 0.5f;
  const float y = fy - 0.5f;
  int x0 = (int)std::floor(x);
  int y0 = (int)std::floor(y);
  const float tx = x - (float)x0;
  const float ty = y - (float)y0;
  float top[4];
  float bottom[4];
  float a[4];
  float b[4];
  texel_at(x0, y0, a);
  texel_at(x0 + 1, y0, b);
  for (int i = 0; i < 4; i++) top[i] = a[i] + (b[i] - a[i]) * tx;
  texel_at(x0, y0 + 1, a);
  texel_at(x0 + 1, y0 + 1, b);
  for (int i = 0; i < 4; i++) bottom[i] = a[i] + (b[i] - a[i]) * tx;
  for (int i = 0; i < 4; i++) out[i] = top[i] + (bottom[i] - top[i]) * ty;
}

/** Source pixel centre for a destination pixel of `drawImage`. */
static void draw_image_pixel(std::vector<unsigned char> &pixels, int width, const Image &source,
                             const CanvasPaint &paint, int px, int py, float u, float v, float sx,
                             float sy, float sw, float sh, float alpha, bool smooth,
                             const CanvasClipMask *clip) {
  float sampled[4];
  sample_source(source, sx + u * sw, sy + v * sh, smooth, sampled);
  unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)px) * 4;
  blend_clipped(dst, sampled, 1.0f, alpha, paint, clip, px, py);
}

void draw_image(std::vector<unsigned char> &pixels, int width, int height, const Image &source,
                float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                float alpha, bool smooth, const CanvasClipMask *clip) {
  if (source.width <= 0 || source.height <= 0 || dw <= 0.0f || dh <= 0.0f || sw <= 0.0f ||
      sh <= 0.0f)
    return;
  int ix0 = std::max(0, (int)std::floor(dx));
  int ix1 = std::min(width, (int)std::ceil(dx + dw));
  int iy0 = std::max(0, (int)std::floor(dy));
  int iy1 = std::min(height, (int)std::ceil(dy + dh));
  const CanvasPaint paint;
  /* A 1:1 blit at integer offsets samples exactly one texel per pixel: skip the
   * per-pixel divide and the four bilinear taps. Sprite/offscreen-canvas
   * compositing in a multi-element scene is usually this case. */
  const int dx_int = (int)std::lround(dx);
  const int dy_int = (int)std::lround(dy);
  const int sx_int = (int)std::lround(sx);
  const int sy_int = (int)std::lround(sy);
  const bool one_to_one = std::fabs(dw - sw) < 0.001f && std::fabs(dh - sh) < 0.001f &&
                          std::fabs(dx - (float)dx_int) < 0.001f &&
                          std::fabs(dy - (float)dy_int) < 0.001f &&
                          std::fabs(sx - (float)sx_int) < 0.001f &&
                          std::fabs(sy - (float)sy_int) < 0.001f;
  if (one_to_one) {
    for (int py = iy0; py < iy1; py++) {
      int texelY = sy_int + (py - dy_int);
      if (texelY < 0) texelY = 0;
      if (texelY >= source.height) texelY = source.height - 1;
      const unsigned char *source_row =
          source.pixels.data() + (size_t)texelY * (size_t)source.width * 4;
      unsigned char *dst = pixels.data() + ((size_t)py * (size_t)width + (size_t)ix0) * 4;
      for (int px = ix0; px < ix1; px++) {
        int texelX = sx_int + (px - dx_int);
        if (texelX < 0) texelX = 0;
        if (texelX >= source.width) texelX = source.width - 1;
        const unsigned char *texel = source_row + (size_t)texelX * 4;
        float sampled[4] = {texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f,
                            texel[3] / 255.0f};
        blend_clipped(dst + (size_t)(px - ix0) * 4, sampled, 1.0f, alpha, paint, clip, px, py);
      }
    }
    return;
  }
  for (int py = iy0; py < iy1; py++) {
    for (int px = ix0; px < ix1; px++) {
      float u = ((float)px + 0.5f - dx) / dw;
      float v = ((float)py + 0.5f - dy) / dh;
      if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
      draw_image_pixel(pixels, width, source, paint, px, py, u, v, sx, sy, sw, sh, alpha, smooth,
                       clip);
    }
  }
}


void draw_image_affine(std::vector<unsigned char> &pixels, int width, int height,
                       const Image &source, float dx, float dy, float dw, float dh, float sx,
                       float sy, float sw, float sh, const CanvasTransform &transform,
                       float alpha, bool smooth, const CanvasClipMask *clip) {
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
  const CanvasPaint paint;
  for (int py = iy0; py < iy1; py++) {
    for (int px = ix0; px < ix1; px++) {
      float ux = 0.0f;
      float uy = 0.0f;
      inverse.apply((float)px + 0.5f, (float)py + 0.5f, &ux, &uy);
      float u = (ux - dx) / dw;
      float v = (uy - dy) / dh;
      if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
      draw_image_pixel(pixels, width, source, paint, px, py, u, v, sx, sy, sw, sh, alpha, smooth,
                       clip);
    }
  }
}

void blit_glyph(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<unsigned char> &glyph, int glyphWidth, int glyphHeight, int left,
                int top, float x, float baselineY, float scale_x, const CanvasPaint &paint,
                float alpha, const CanvasClipMask *clip) {
  if (glyphWidth <= 0 || glyphHeight <= 0) return;
  if (scale_x <= 0.0f) scale_x = 1.0f;
  bool condense = scale_x < 0.999f;
  int drawWidth = condense ? std::max(1, (int)std::lround((float)glyphWidth * scale_x))
                           : glyphWidth;
  int originX = (int)std::lround(x) + (int)std::lround((float)left * scale_x);
  int originY = (int)std::lround(baselineY) - top;
  const bool solid = paint_is_solid(paint);
  float solid_color[4];
  if (solid) copy_color(paint.color, solid_color);
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
      if (solid) {
        copy_color(solid_color, color);
      } else {
        paint_color_at(paint, (float)px + 0.5f, (float)py + 0.5f, color);
      }
      blend_clipped(pixels.data() + ((size_t)py * (size_t)width + (size_t)px) * 4, color,
                    coverage, alpha, paint, clip, px, py);
    }
  }
}

}  // namespace canvas_detail
}  // namespace xtgui
