/*
 * xbintsc GUI engine — image decoding implementation.
 */

#include "image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include "stb_image.h"

#include <cctype>
#include <string>
#include <unordered_map>

#include "svg.h"

namespace xtgui {
namespace {

struct SizeEntry {
  bool valid = false;
  int width = 0;
  int height = 0;
};

std::unordered_map<std::string, Image> &imageCache() {
  static std::unordered_map<std::string, Image> cache;
  return cache;
}

std::unordered_map<std::string, SizeEntry> &sizeCache() {
  static std::unordered_map<std::string, SizeEntry> cache;
  return cache;
}

/** Strip a `file://` prefix and percent-decode a local path. */
std::string normalizePath(const std::string &input) {
  std::string path = input;
  if (path.rfind("file://", 0) == 0) {
    path = path.substr(7);
    /* `file:///abs` -> `/abs`; `file://host/abs` is not supported. */
    size_t slash = path.find('/');
    if (slash != std::string::npos && slash != 0) path = path.substr(slash);
  }
  std::string out;
  out.reserve(path.size());
  for (size_t i = 0; i < path.size(); i++) {
    if (path[i] == '%' && i + 2 < path.size() && std::isxdigit((unsigned char)path[i + 1]) &&
        std::isxdigit((unsigned char)path[i + 2])) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        return std::tolower((unsigned char)c) - 'a' + 10;
      };
      out.push_back((char)(hex(path[i + 1]) * 16 + hex(path[i + 2])));
      i += 2;
    } else {
      out.push_back(path[i]);
    }
  }
  return out;
}

/** Decode a `data:` URL's base64 payload (the only encoding the engine emits
 * and the common one a page assigns). */
bool decodeDataUrl(const std::string &url, std::vector<unsigned char> *out) {
  const size_t comma = url.find(',');
  if (comma == std::string::npos) return false;
  const std::string header = url.substr(0, comma);
  if (header.find(";base64") == std::string::npos) return false;
  static const signed char kDecode[128] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
      52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
      -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
      15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
      -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
      41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};
  out->clear();
  out->reserve((url.size() - comma) * 3 / 4 + 4);
  uint32_t accumulator = 0;
  int bits = 0;
  for (size_t i = comma + 1; i < url.size(); i++) {
    const unsigned char c = (unsigned char)url[i];
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    if (c >= 128) return false;
    const int value = kDecode[c];
    if (value < 0) return false;
    accumulator = (accumulator << 6) | (uint32_t)value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out->push_back((unsigned char)((accumulator >> bits) & 0xFF));
    }
  }
  return !out->empty();
}

}  // namespace

const Image *xt_image_load(const std::string &raw_path, int target_width, int target_height) {
  /* A `data:` URL carries its bytes inline (a canvas used as a CSS
   * `background-image` reaches this path through `toDataURL()`). */
  if (raw_path.rfind("data:", 0) == 0) {
    auto &cache = imageCache();
    auto found = cache.find(raw_path);
    if (found != cache.end()) {
      return found->second.width > 0 ? &found->second : nullptr;
    }
    Image image;
    std::vector<unsigned char> bytes;
    if (decodeDataUrl(raw_path, &bytes)) {
      int width = 0;
      int height = 0;
      int channels = 0;
      unsigned char *data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &width, &height,
                                                  &channels, 4);
      if (data != nullptr) {
        image.width = width;
        image.height = height;
        image.pixels.assign(data, data + (size_t)width * (size_t)height * 4);
        stbi_image_free(data);
      }
    }
    auto inserted = cache.emplace(raw_path, std::move(image));
    return inserted.first->second.width > 0 ? &inserted.first->second : nullptr;
  }
  std::string path = normalizePath(raw_path);
  bool svg = xt_svg_is_svg(path);
  /* Raster formats always decode at their intrinsic resolution, so their cache
   * key is the path. SVG is rasterised at the drawn pixel size, which is part
   * of its identity. */
  std::string key = path;
  if (svg && (target_width > 0 || target_height > 0)) {
    key += "#" + std::to_string(target_width) + "x" + std::to_string(target_height);
  }
  auto &cache = imageCache();
  auto found = cache.find(key);
  if (found != cache.end()) {
    return found->second.width > 0 ? &found->second : nullptr;
  }

  Image image;
  if (svg) {
    xt_svg_render(path, target_width, target_height, &image);
  } else {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (data != nullptr) {
      image.width = width;
      image.height = height;
      image.pixels.assign(data, data + (size_t)width * (size_t)height * 4);
      stbi_image_free(data);
    }
  }
  auto inserted = cache.emplace(key, std::move(image));
  return inserted.first->second.width > 0 ? &inserted.first->second : nullptr;
}

bool xt_image_size(const std::string &raw_path, int *width, int *height) {
  if (raw_path.rfind("data:", 0) == 0) {
    const Image *image = xt_image_load(raw_path);
    if (image == nullptr) return false;
    if (width != nullptr) *width = image->width;
    if (height != nullptr) *height = image->height;
    return true;
  }
  std::string path = normalizePath(raw_path);
  if (xt_svg_is_svg(path)) return xt_svg_size(path, width, height);
  auto &cache = sizeCache();
  auto found = cache.find(path);
  if (found == cache.end()) {
    SizeEntry entry;
    int channels = 0;
    entry.valid = stbi_info(path.c_str(), &entry.width, &entry.height, &channels) != 0;
    found = cache.emplace(path, entry).first;
  }
  if (!found->second.valid) return false;
  if (width != nullptr) *width = found->second.width;
  if (height != nullptr) *height = found->second.height;
  return true;
}

bool xt_image_decode(const unsigned char *data, size_t size, Image *out) {
  if (data == nullptr || out == nullptr) return false;
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *decoded =
      stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
  if (decoded == nullptr) return false;
  out->width = width;
  out->height = height;
  out->pixels.assign(decoded, decoded + (size_t)width * (size_t)height * 4);
  stbi_image_free(decoded);
  return true;
}

bool xt_image_is_vector(const std::string &path) { return xt_svg_is_svg(normalizePath(path)); }

void xt_image_shutdown() {
  imageCache().clear();
  sizeCache().clear();
  xt_svg_shutdown();
}

}  // namespace xtgui
