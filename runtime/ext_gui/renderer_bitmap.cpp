/*
 * xbintsc GUI engine — renderer textures for CPU-rasterised shadows.
 *
 * `paint_shadow.cpp` produces the blurred RGBA mask; this file uploads it once
 * and caches the GPU texture by the mask's paint key. A shadow whose colour
 * changes (the common animation) reuses the same mask and only the draw's tint
 * differs, so the expensive rasterisation and upload happen on the first frame.
 */

#include "renderer_internal.h"

#include <cstdio>
#include <string>
#include <unordered_map>

namespace xtgui {
namespace renderer_detail {

namespace {

struct ShadowTexture {
  SDL_GPUTexture *texture = nullptr;
  int width = 0;
  int height = 0;
  bool used = false;
};

std::unordered_map<std::string, ShadowTexture> g_shadow_textures;

/** Keep a few stale masks for animated shadows that change shape every frame;
 * the rest are released as soon as a paint pass stops drawing them. */
constexpr size_t kMaxStaleShadowTextures = 16;

}  // namespace

void beginBitmapUpload() {
  for (auto &entry : g_shadow_textures) entry.second.used = false;
}

void finishBitmapUpload(SDL_GPUDevice *device) {
  if (g_shadow_textures.size() <= kMaxStaleShadowTextures) return;
  for (auto it = g_shadow_textures.begin(); it != g_shadow_textures.end();) {
    if (it->second.used) {
      ++it;
      continue;
    }
    if (it->second.texture != nullptr) SDL_ReleaseGPUTexture(device, it->second.texture);
    it = g_shadow_textures.erase(it);
  }
}

SDL_GPUTexture *ensureBitmapTexture(SDL_GPUDevice *device, const xtgui::PaintBitmap &bitmap,
                                    XtGuiUploadBatch *batch) {
  if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.pixels.empty()) return nullptr;
  std::string key = bitmap.key + "#" + std::to_string(bitmap.width) + "x" +
                    std::to_string(bitmap.height);
  ShadowTexture &entry = g_shadow_textures[key];
  entry.used = true;
  if (entry.texture != nullptr && entry.width == bitmap.width && entry.height == bitmap.height) {
    return entry.texture;
  }
  xtgui::Image image;
  image.width = bitmap.width;
  image.height = bitmap.height;
  image.pixels = bitmap.pixels;
  SDL_GPUTexture *texture = createImageTexture(device, image, batch);
  if (entry.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.texture);
  entry.texture = texture;
  entry.width = texture != nullptr ? bitmap.width : 0;
  entry.height = texture != nullptr ? bitmap.height : 0;
  return texture;
}

void releaseBitmapTextures(SDL_GPUDevice *device) {
  for (auto &entry : g_shadow_textures) {
    if (entry.second.texture != nullptr) SDL_ReleaseGPUTexture(device, entry.second.texture);
  }
  g_shadow_textures.clear();
}

}  // namespace renderer_detail
}  // namespace xtgui
