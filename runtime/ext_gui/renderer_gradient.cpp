/*
 * xbintsc GUI engine — radial `background-image` rasterisation.
 *
 * The shape pipeline fills one shape with one flat colour, and the bands a
 * radial gradient used to be drawn as are nested ellipses: every band covers the
 * whole interior, so each band's alpha composites over the previous ones and a
 * vignette whose stops fade to `transparent` ends up opaque. The ramp is
 * therefore resolved per pixel into an RGBA texture, which the image pipeline
 * draws as one quad and composites exactly once.
 */

#include "renderer_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace xtgui {
namespace renderer_detail {

namespace {

/** A rasterised gradient, keyed by the paint pass's cache key plus the logical
 * box size. The texture is re-rasterised in place when the device pixel size
 * changes (a DPI change), so a resized gradient never leaves a stale texture. */
struct GradientTexture {
  SDL_GPUTexture *texture = nullptr;
  int width = 0;
  int height = 0;
  /** Set by the upload pass that draws this gradient; see `finishGradientUpload`. */
  bool used = false;
};

std::unordered_map<std::string, GradientTexture> g_gradient_textures;

/** How many rasterised gradients that are no longer drawn are kept. A gradient's
 * stops can animate (a `hue-rotate` loop, say), which would otherwise rasterise a
 * fresh texture every frame and never free the old ones. */
constexpr size_t kMaxStaleGradients = 8;

/** Largest texture edge a gradient is rasterised at. A radial ramp is smooth, so
 * a larger box is rasterised smaller and stretched: the sampled colours are
 * unchanged (the stretch is uniform), only the resolution of the ramp drops. */
constexpr int kMaxGradientEdge = 1024;

unsigned char byteOf(float value) {
  if (!(value > 0.0f)) return 0;  // NaN and negatives
  if (value > 1.0f) value = 1.0f;
  return (unsigned char)(value * 255.0f + 0.5f);
}

/** Rasterise `gradient` into `out`: the box maps 1:1 onto the texture. The ramp
 * covers the whole box — past the ending shape CSS keeps the last stop's colour
 * rather than fading out — so there is no shape edge to antialias; the texels
 * are sampled with linear filtering. */
void rasterizeGradient(const PaintGradient &gradient, float logical_width, float logical_height,
                       int pixel_width, int pixel_height, Image *out) {
  out->width = pixel_width;
  out->height = pixel_height;
  out->pixels.assign((size_t)pixel_width * (size_t)pixel_height * 4u, 0);
  const float pixel_x = logical_width / (float)pixel_width;  // logical px per texel
  const float pixel_y = logical_height / (float)pixel_height;
  const float inv_rx = 1.0f / gradient.radius_x;
  const float inv_ry = 1.0f / gradient.radius_y;
  for (int y = 0; y < pixel_height; y++) {
    const float local_y = ((float)y + 0.5f) * pixel_y;
    unsigned char *row = out->pixels.data() + (size_t)y * (size_t)pixel_width * 4u;
    for (int x = 0; x < pixel_width; x++) {
      const float local_x = ((float)x + 0.5f) * pixel_x;
      const float u = (local_x - gradient.center_x) * inv_rx;
      const float v = (local_y - gradient.center_y) * inv_ry;
      const float t = std::sqrt(u * u + v * v);
      Color color =
          paint_detail::gradientColorAt(gradient.stops, gradient.repeating, gradient.period, t);
      if (color.a <= 0.0f) continue;
      unsigned char *texel = row + (size_t)x * 4u;
      texel[0] = byteOf(color.r);
      texel[1] = byteOf(color.g);
      texel[2] = byteOf(color.b);
      texel[3] = byteOf(color.a);
    }
  }
}

/** The device size a gradient of `pixel_width` x `pixel_height` is rasterised at,
 * preserving its aspect ratio under `kMaxGradientEdge`. */
void rasterSize(int pixel_width, int pixel_height, int *width, int *height) {
  *width = std::max(1, pixel_width);
  *height = std::max(1, pixel_height);
  if (*width <= kMaxGradientEdge && *height <= kMaxGradientEdge) return;
  float shrink = (float)kMaxGradientEdge / (float)std::max(*width, *height);
  *width = std::max(1, (int)((float)*width * shrink + 0.5f));
  *height = std::max(1, (int)((float)*height * shrink + 0.5f));
}

}  // namespace

void beginGradientUpload() {
  for (auto &entry : g_gradient_textures) entry.second.used = false;
}

void finishGradientUpload(SDL_GPUDevice *device) {
  if (g_gradient_textures.size() <= kMaxStaleGradients) return;
  /* The display list of the frame being built may hold pointers to the textures
   * it used, so only gradients it did *not* use can go. */
  for (auto it = g_gradient_textures.begin(); it != g_gradient_textures.end();) {
    if (it->second.used) {
      ++it;
      continue;
    }
    if (it->second.texture != nullptr) SDL_ReleaseGPUTexture(device, it->second.texture);
    it = g_gradient_textures.erase(it);
  }
}

SDL_GPUTexture *ensureGradientTexture(SDL_GPUDevice *device, const xtgui::PaintImage &image,
                                      int pixel_width, int pixel_height, XtGuiUploadBatch *batch) {
  if (image.gradient == nullptr || image.width <= 0.0f || image.height <= 0.0f) return nullptr;
  const xtgui::PaintGradient &gradient = *image.gradient;
  if (gradient.radius_x <= 0.0f || gradient.radius_y <= 0.0f || gradient.stops.size() < 2) {
    return nullptr;
  }
  int width = 0;
  int height = 0;
  rasterSize(pixel_width, pixel_height, &width, &height);
  /* The key carries the ramp, its geometry and the logical box size: two boxes
   * of different sizes can resolve the same px radius but not the same picture. */
  char box[64];
  std::snprintf(box, sizeof(box), "@%.3f,%.3f", image.width, image.height);
  std::string key = gradient.key + box;
  GradientTexture &entry = g_gradient_textures[key];
  entry.used = true;
  if (entry.texture != nullptr && entry.width == width && entry.height == height) {
    return entry.texture;
  }
  Image raster;
  rasterizeGradient(gradient, image.width, image.height, width, height, &raster);
  SDL_GPUTexture *texture = createImageTexture(device, raster, batch);
  if (entry.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.texture);
  entry.texture = texture;
  entry.width = texture != nullptr ? width : 0;
  entry.height = texture != nullptr ? height : 0;
  return texture;
}

void releaseGradientTextures(SDL_GPUDevice *device) {
  for (auto &entry : g_gradient_textures) {
    if (entry.second.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.second.texture);
  }
  g_gradient_textures.clear();
}

}  // namespace renderer_detail
}  // namespace xtgui
