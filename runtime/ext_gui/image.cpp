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
#include <unordered_map>

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

}  // namespace

const Image *xt_image_load(const std::string &raw_path) {
  std::string path = normalizePath(raw_path);
  auto &cache = imageCache();
  auto found = cache.find(path);
  if (found != cache.end()) {
    return found->second.width > 0 ? &found->second : nullptr;
  }

  Image image;
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
  auto inserted = cache.emplace(path, std::move(image));
  return inserted.first->second.width > 0 ? &inserted.first->second : nullptr;
}

bool xt_image_size(const std::string &raw_path, int *width, int *height) {
  std::string path = normalizePath(raw_path);
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

void xt_image_shutdown() {
  imageCache().clear();
  sizeCache().clear();
}

}  // namespace xtgui
