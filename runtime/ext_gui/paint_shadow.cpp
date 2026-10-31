/*
 * xbintsc GUI engine — CSS shadows with a real Gaussian blur.
 *
 * `box-shadow` and a blurred `text-shadow` are convolutions of a shape mask
 * with a Gaussian kernel, which a stack of a few offset copies cannot express.
 * The paint pass therefore rasterises the silhouette (a rounded rectangle, or
 * the glyph coverage of a text run), applies a separable Gaussian on the CPU,
 * and emits the result as one tinted image quad. The texture is cached by the
 * shape/blur key, so a static document rasterises each shadow once and an
 * animated colour only changes the quad's tint.
 *
 * CSS defines the blur radius as twice the standard deviation:
 * `sigma = blur / 2` (https://drafts.fxtf.org/css-backgrounds/#shadow-blur).
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "text.h"

namespace xtgui {
namespace paint_detail {

namespace {

constexpr float kTextShadowScale = 2.0f;
constexpr size_t kMaxCachedShadows = 48;

std::unordered_map<std::string, std::shared_ptr<const PaintBitmap>> g_shadow_cache;
std::deque<std::string> g_shadow_order;

std::shared_ptr<const PaintBitmap> cachedShadow(const std::string &key) {
  auto found = g_shadow_cache.find(key);
  if (found == g_shadow_cache.end()) return nullptr;
  return found->second;
}

void rememberShadow(const std::string &key, std::shared_ptr<const PaintBitmap> bitmap) {
  if (g_shadow_cache.size() >= kMaxCachedShadows && !g_shadow_order.empty()) {
    g_shadow_cache.erase(g_shadow_order.front());
    g_shadow_order.pop_front();
  }
  g_shadow_cache[key] = bitmap;
  g_shadow_order.push_back(key);
}

float clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

/** Separable Gaussian on the alpha channel of an RGBA mask. `sigma` is in
 * bitmap pixels. The RGB channels stay white: the image shader multiplies them
 * by the shadow colour and blends with the alpha. */
void blurAlpha(std::vector<unsigned char> &pixels, int width, int height, float sigma) {
  if (sigma <= 0.05f || width <= 0 || height <= 0) return;
  int radius = (int)std::ceil(sigma * 3.0f);
  if (radius < 1) radius = 1;
  if (radius > 512) radius = 512;
  std::vector<float> kernel((size_t)radius * 2 + 1);
  float total = 0.0f;
  for (int i = -radius; i <= radius; i++) {
    float weight = std::exp(-(float)(i * i) / (2.0f * sigma * sigma));
    kernel[(size_t)(i + radius)] = weight;
    total += weight;
  }
  for (float &weight : kernel) weight /= total;

  const size_t count = (size_t)width * (size_t)height;
  std::vector<float> source(count);
  for (size_t i = 0; i < count; i++) source[i] = (float)pixels[i * 4 + 3] / 255.0f;
  std::vector<float> horizontal(count);

  for (int y = 0; y < height; y++) {
    const size_t row = (size_t)y * (size_t)width;
    for (int x = 0; x < width; x++) {
      float sum = 0.0f;
      for (int k = -radius; k <= radius; k++) {
        int sample = x + k;
        if (sample < 0) sample = 0;
        if (sample >= width) sample = width - 1;
        sum += source[row + (size_t)sample] * kernel[(size_t)(k + radius)];
      }
      horizontal[row + (size_t)x] = sum;
    }
  }
  std::vector<float> vertical(count);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float sum = 0.0f;
      for (int k = -radius; k <= radius; k++) {
        int sample = y + k;
        if (sample < 0) sample = 0;
        if (sample >= height) sample = height - 1;
        sum += horizontal[(size_t)sample * (size_t)width + (size_t)x] *
               kernel[(size_t)(k + radius)];
      }
      vertical[(size_t)y * (size_t)width + (size_t)x] = sum;
    }
  }
  for (size_t i = 0; i < count; i++) {
    float value = vertical[i];
    value = clamp01(value);
    pixels[i * 4 + 3] = (unsigned char)(value * 255.0f + 0.5f);
  }
}

/** Signed distance to a rounded rectangle; negative inside. */
float roundedRectDistance(float px, float py, float x, float y, float width, float height,
                          float radius) {
  float half_w = width * 0.5f;
  float half_h = height * 0.5f;
  float cx = x + half_w;
  float cy = y + half_h;
  if (radius > half_w) radius = half_w;
  if (radius > half_h) radius = half_h;
  float qx = std::fabs(px - cx) - (half_w - radius);
  float qy = std::fabs(py - cy) - (half_h - radius);
  float outside_x = qx > 0.0f ? qx : 0.0f;
  float outside_y = qy > 0.0f ? qy : 0.0f;
  float inside = std::min(std::max(qx, qy), 0.0f);
  return inside + std::sqrt(outside_x * outside_x + outside_y * outside_y) - radius;
}

void fillRoundedMask(std::vector<unsigned char> &pixels, int width, int height, float x, float y,
                     float box_width, float box_height, float radius, bool invert) {
  pixels.assign((size_t)width * (size_t)height * 4u, 0);
  for (int py = 0; py < height; py++) {
    for (int px = 0; px < width; px++) {
      float distance =
          roundedRectDistance((float)px + 0.5f, (float)py + 0.5f, x, y, box_width, box_height,
                              radius);
      float coverage = clamp01(0.5f - distance);
      if (invert) coverage = 1.0f - coverage;
      unsigned char *texel = &pixels[((size_t)py * (size_t)width + (size_t)px) * 4u];
      texel[0] = 255;
      texel[1] = 255;
      texel[2] = 255;
      texel[3] = (unsigned char)(coverage * 255.0f + 0.5f);
    }
  }
}

std::string floatKey(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", (double)value);
  return buffer;
}

}  // namespace

/** Append a bitmap-backed image op without running the filter chain again (the
 * shadow colour already carries it). */
void addBitmapImage(DisplayList &out, std::shared_ptr<const PaintBitmap> bitmap, float x, float y,
                    const Color &color, const Matrix2D &transform, const PaintClip &clip) {
  if (color.a <= 0.0f || bitmap == nullptr || bitmap->width <= 0 || bitmap->height <= 0) return;
  PaintImage image;
  image.x = x;
  image.y = y;
  image.width = (float)bitmap->width / bitmap->scale;
  image.height = (float)bitmap->height / bitmap->scale;
  image.transform = transform;
  image.tint = color;
  image.bitmap = std::move(bitmap);
  out.images.push_back(std::move(image));
  out.ops.push_back(PaintOp{PaintKind::Image, (uint32_t)(out.images.size() - 1), clip});
}

std::shared_ptr<const PaintBitmap> textShadowMask(const std::string &text, const FontSpec &font,
                                                  float blur) {
  if (text.empty() || blur <= 0.0f) return nullptr;
  char key_buffer[160];
  std::snprintf(key_buffer, sizeof(key_buffer), "ts:%.2f:%.0f:%d:%d:", (double)blur,
                (double)font.pixel_size, font.weight, font.italic ? 1 : 0);
  std::string key = key_buffer + font.family + "|" + text;
  if (std::shared_ptr<const PaintBitmap> cached = cachedShadow(key)) return cached;

  Font *logical = xt_text_resolve(font);
  if (logical == nullptr) return nullptr;
  const ShapedRun *shaped = xt_text_shape_cached(text, logical);
  if (shaped == nullptr || shaped->glyphs.empty()) return nullptr;

  const float scale = kTextShadowScale;
  struct RasterGlyph {
    GlyphImage image;
    float x = 0.0f;
    float baseline_y = 0.0f;
  };
  std::vector<RasterGlyph> glyphs;
  glyphs.reserve(shaped->glyphs.size());
  float pen_x = 0.0f;
  float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
  for (const ShapedGlyph &glyph : shaped->glyphs) {
    if (glyph.glyph != 0) {
      Font *face = glyph.font != nullptr ? glyph.font : logical;
      Font *scaled = xt_text_scale(face, font.pixel_size * scale);
      if (scaled == nullptr) scaled = face;
      GlyphImage image;
      if (xt_text_rasterize(scaled, glyph.glyph, image) && image.width > 0 &&
          image.height > 0) {
        RasterGlyph entry;
        entry.image = std::move(image);
        entry.x = (pen_x + glyph.x_offset) * scale + (float)entry.image.left;
        entry.baseline_y = -(float)entry.image.top;
        glyphs.push_back(std::move(entry));
        const RasterGlyph &added = glyphs.back();
        min_x = std::min(min_x, added.x / scale);
        max_x = std::max(max_x, (added.x + (float)added.image.width) / scale);
        min_y = std::min(min_y, added.baseline_y / scale);
        max_y = std::max(max_y, (added.baseline_y + (float)added.image.height) / scale);
      }
    }
    pen_x += glyph.x_advance;
  }
  if (glyphs.empty() || min_x > max_x) return nullptr;

  float sigma = blur * 0.5f;
  float padding = std::ceil(sigma * 3.0f) + 2.0f;
  int width = (int)std::ceil((max_x - min_x + padding * 2.0f) * scale);
  int height = (int)std::ceil((max_y - min_y + padding * 2.0f) * scale);
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  if ((long long)width * height > 16 * 1024 * 1024) return nullptr;

  auto bitmap = std::make_shared<PaintBitmap>();
  bitmap->width = width;
  bitmap->height = height;
  bitmap->scale = scale;
  bitmap->key = key;
  bitmap->pixels.assign((size_t)width * (size_t)height * 4u, 0);
  const float origin_x = min_x - padding;
  const float origin_y = min_y - padding;
  bitmap->origin_x = origin_x;
  bitmap->origin_y = origin_y;
  for (const RasterGlyph &glyph : glyphs) {
    int offset_x = (int)std::lround((glyph.x / scale - origin_x) * scale);
    int offset_y = (int)std::lround((glyph.baseline_y / scale - origin_y) * scale);
    const GlyphImage &image = glyph.image;
    for (int row = 0; row < image.height; row++) {
      int y = offset_y + row;
      if (y < 0 || y >= height) continue;
      for (int column = 0; column < image.width; column++) {
        int x = offset_x + column;
        if (x < 0 || x >= width) continue;
        unsigned char coverage = image.pixels[(size_t)row * (size_t)image.width + (size_t)column];
        if (coverage == 0) continue;
        unsigned char *texel = &bitmap->pixels[((size_t)y * (size_t)width + (size_t)x) * 4u];
        texel[0] = 255;
        texel[1] = 255;
        texel[2] = 255;
        texel[3] = std::max(texel[3], coverage);
      }
    }
  }
  blurAlpha(bitmap->pixels, width, height, sigma * scale);
  rememberShadow(key, bitmap);
  return bitmap;
}

void addBoxShadows(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                   const FilterChain &chain, const PaintClip &clip, bool inset,
                   float viewportWidth, float viewportHeight) {
  for (size_t index = style.box_shadow.size(); index-- > 0;) {
    const BoxShadow &shadow = style.box_shadow[index];
    if (shadow.inset != inset) continue;
    float dx = resolveLength(shadow.dx, 0.0f, style.font_size, viewportWidth, viewportHeight);
    float dy = resolveLength(shadow.dy, 0.0f, style.font_size, viewportWidth, viewportHeight);
    float blur = resolveLength(shadow.blur, 0.0f, style.font_size, viewportWidth, viewportHeight);
    float spread = resolveLength(shadow.spread, 0.0f, style.font_size, viewportWidth,
                                 viewportHeight);
    if (blur < 0.0f) blur = 0.0f;
    Color color = shadow.has_color ? filterColor(chain, shadow.color) : filterColor(chain, style.color);
    if (color.a <= 0.0f) continue;

    if (!inset && blur <= 0.0f) {
      /* No blur: the silhouette is the box itself, expanded by `spread` and
       * moved by the offset. A rounded rectangle is exact here. */
      float x = box.x + dx - spread;
      float y = box.y + dy - spread;
      float width = box.width + spread * 2.0f;
      float height = box.height + spread * 2.0f;
      float radius = style.border_radius + spread;
      addRect(out, x, y, width, height, radius, color, clip, box.world);
      continue;
    }

    const float scale = 1.0f;
    const float padding = std::ceil(blur * 1.5f) + 2.0f;
    const std::string key =
        std::string("bs:") + (inset ? "i:" : "o:") + floatKey(box.width) + ":" +
        floatKey(box.height) + ":" + floatKey(style.border_radius) + ":" + floatKey(blur) +
        ":" + floatKey(spread) + ":" + floatKey(box.border_left) + ":" +
        floatKey(box.border_right) + ":" + floatKey(box.border_top) + ":" +
        floatKey(box.border_bottom) + ":" + floatKey(dx) + ":" + floatKey(dy);

    std::shared_ptr<const PaintBitmap> bitmap = cachedShadow(key);
    if (bitmap == nullptr) {
      auto generated = std::make_shared<PaintBitmap>();
      generated->scale = scale;
      generated->key = key;
      if (!inset) {
        float shadow_width = std::max(0.0f, box.width + spread * 2.0f);
        float shadow_height = std::max(0.0f, box.height + spread * 2.0f);
        float radius = std::max(0.0f, style.border_radius + spread);
        generated->width = std::max(1, (int)std::ceil(shadow_width + padding * 2.0f));
        generated->height = std::max(1, (int)std::ceil(shadow_height + padding * 2.0f));
        if ((long long)generated->width * generated->height > 16 * 1024 * 1024) continue;
        fillRoundedMask(generated->pixels, generated->width, generated->height, padding, padding,
                        shadow_width, shadow_height, radius, false);
        blurAlpha(generated->pixels, generated->width, generated->height, blur * 0.5f);
      } else {
        /* Inset: the mask is the *outside* of the (spread-shrunk) border box,
         * blurred and then cropped to the padding box. */
        float padding_box_width =
            std::max(0.0f, box.width - box.border_left - box.border_right);
        float padding_box_height =
            std::max(0.0f, box.height - box.border_top - box.border_bottom);
        float shape_local_x = padding + spread - box.border_left + dx;
        float shape_local_y = padding + spread - box.border_top + dy;
        float shape_width = std::max(0.0f, box.width - spread * 2.0f);
        float shape_height = std::max(0.0f, box.height - spread * 2.0f);
        float radius = std::max(0.0f, style.border_radius - spread);
        generated->width = std::max(1, (int)std::ceil(padding_box_width + padding * 2.0f));
        generated->height = std::max(1, (int)std::ceil(padding_box_height + padding * 2.0f));
        if ((long long)generated->width * generated->height > 16 * 1024 * 1024) continue;
        fillRoundedMask(generated->pixels, generated->width, generated->height, shape_local_x,
                        shape_local_y, shape_width, shape_height, radius, true);
        blurAlpha(generated->pixels, generated->width, generated->height, blur * 0.5f);
        /* Crop to the padding box: an inner shadow never paints over the
         * border. */
        const float left = padding;
        const float top = padding;
        const float right = padding + padding_box_width;
        const float bottom = padding + padding_box_height;
        for (int py = 0; py < generated->height; py++) {
          for (int px = 0; px < generated->width; px++) {
            if ((float)px >= left && (float)px < right && (float)py >= top &&
                (float)py < bottom)
              continue;
            generated->pixels[((size_t)py * (size_t)generated->width + (size_t)px) * 4u + 3] = 0;
          }
        }
      }
      rememberShadow(key, generated);
      bitmap = generated;
    }

    float image_x = 0.0f;
    float image_y = 0.0f;
    if (!inset) {
      image_x = box.x + dx - spread - padding;
      image_y = box.y + dy - spread - padding;
    } else {
      image_x = box.x + box.border_left - padding;
      image_y = box.y + box.border_top - padding;
    }
    addBitmapImage(out, bitmap, image_x, image_y, color, box.world, clip);
  }
}

}  // namespace paint_detail
}  // namespace xtgui
