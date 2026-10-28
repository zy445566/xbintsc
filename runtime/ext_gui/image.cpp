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

/** One classified `data:` URL: whether its payload is SVG and the intrinsic
 * size parsed from it (or from the raster header). Classification happens once
 * per URL and is cached, so the renderer's vector probe does not decode the
 * payload on every frame. */
struct DataUrlEntry {
  bool svg = false;
  SizeEntry size;
};

std::unordered_map<std::string, DataUrlEntry> &dataUrlCache() {
  static std::unordered_map<std::string, DataUrlEntry> cache;
  return cache;
}

std::string lowercase(std::string value) {
  for (char &c : value) c = (char)std::tolower((unsigned char)c);
  return value;
}

std::string trim(const std::string &text) {
  size_t start = 0;
  while (start < text.size() && std::isspace((unsigned char)text[start])) start++;
  size_t end = text.size();
  while (end > start && std::isspace((unsigned char)text[end - 1])) end--;
  return text.substr(start, end - start);
}

/** Decode `%XX` escapes. `+` stays a literal plus: a data URL uses URL
 * percent-encoding, not form encoding. A malformed escape is left verbatim. */
std::string percentDecode(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '%' && i + 2 < text.size() && std::isxdigit((unsigned char)text[i + 1]) &&
        std::isxdigit((unsigned char)text[i + 2])) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        return std::tolower((unsigned char)c) - 'a' + 10;
      };
      out.push_back((char)(hex(text[i + 1]) * 16 + hex(text[i + 2])));
      i += 2;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

/** True when one of the header's `;`-separated parameters is `base64`
 * (case-insensitive, as the data-URL grammar specifies). */
bool headerHasBase64(const std::string &header) {
  size_t start = 0;
  while (start <= header.size()) {
    size_t end = header.find(';', start);
    if (end == std::string::npos) end = header.size();
    if (lowercase(trim(header.substr(start, end - start))) == "base64") return true;
    if (end == header.size()) break;
    start = end + 1;
  }
  return false;
}

/** The MIME type part of a data URL's header, lowercased. */
std::string headerMime(const std::string &header) {
  size_t semi = header.find(';');
  return lowercase(trim(header.substr(0, semi)));
}

/** A cheap content sniff for a data URL whose MIME type is generic: look for
 * `<svg` in the first chunk (after an XML declaration, doctype or comment). */
bool bytesLookLikeSvg(const std::vector<unsigned char> &bytes) {
  if (bytes.empty()) return false;
  const size_t limit = bytes.size() < 1024 ? bytes.size() : 1024;
  const std::string prefix(reinterpret_cast<const char *>(bytes.data()), limit);
  return prefix.find("<svg") != std::string::npos;
}

bool base64Decode(const std::string &text, std::vector<unsigned char> *out) {
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
  out->reserve(text.size() * 3 / 4 + 4);
  uint32_t accumulator = 0;
  int bits = 0;
  for (unsigned char c : text) {
    if (c == '=' || std::isspace(c)) continue;
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

/** Decode a `data:` URL's payload. Both `;base64` and percent-encoded (or
 * plain) bodies are accepted; in the base64 case the body is percent-decoded
 * first, which is what a browser does. `mime` receives the lowercased header
 * type (empty when the URL omits one). */
bool decodeDataUrl(const std::string &url, std::vector<unsigned char> *out, std::string *mime) {
  const size_t comma = url.find(',');
  if (comma == std::string::npos || comma < 5) return false;
  const std::string header = url.substr(5, comma - 5);
  if (mime != nullptr) *mime = headerMime(header);
  const std::string body = percentDecode(url.substr(comma + 1));
  if (headerHasBase64(header)) return base64Decode(body, out);
  out->assign(body.begin(), body.end());
  return !out->empty();
}

/** Classify (`SVG` vs raster) and measure a `data:` URL once. */
DataUrlEntry &dataUrlEntry(const std::string &url) {
  auto &cache = dataUrlCache();
  auto found = cache.find(url);
  if (found != cache.end()) return found->second;

  DataUrlEntry entry;
  std::vector<unsigned char> bytes;
  std::string mime;
  if (decodeDataUrl(url, &bytes, &mime)) {
    /* Trust an explicit raster MIME type; sniff only when the type is absent
     * or generic, so a PNG that happens to contain `<svg` in a text chunk is
     * not mistaken for vector art. */
    const bool generic_mime =
        mime.empty() || mime == "text/plain" || mime == "application/octet-stream";
    entry.svg = mime == "image/svg+xml" || (generic_mime && bytesLookLikeSvg(bytes));
    if (entry.svg) {
      const std::string markup(bytes.begin(), bytes.end());
      int width = 0;
      int height = 0;
      if (xt_svg_markup_size(markup, &width, &height)) {
        entry.size.valid = true;
        entry.size.width = width;
        entry.size.height = height;
      }
    } else {
      int width = 0;
      int height = 0;
      int channels = 0;
      if (stbi_info_from_memory(bytes.data(), (int)bytes.size(), &width, &height, &channels) != 0) {
        entry.size.valid = true;
        entry.size.width = width;
        entry.size.height = height;
      }
    }
  }
  return cache.emplace(url, std::move(entry)).first->second;
}

}  // namespace

const Image *xt_image_load(const std::string &raw_path, int target_width, int target_height) {
  /* A `data:` URL carries its bytes inline (a canvas used as a CSS
   * `background-image` reaches this path through `toDataURL()`). */
  if (raw_path.rfind("data:", 0) == 0) {
    const DataUrlEntry &info = dataUrlEntry(raw_path);
    int width = target_width;
    int height = target_height;
    if (info.svg && width <= 0 && height <= 0 && info.size.valid) {
      width = info.size.width;
      height = info.size.height;
    }
    /* A vector source is rasterised at the drawn size, so that size is part of
     * its cache identity. A raster data URL decodes once at its intrinsic
     * resolution. */
    std::string key = raw_path;
    if (info.svg) {
      key = "data-svg:" + std::to_string(width) + "x" + std::to_string(height) + ":" + raw_path;
    }
    auto &cache = imageCache();
    auto found = cache.find(key);
    if (found != cache.end()) {
      return found->second.width > 0 ? &found->second : nullptr;
    }
    Image image;
    std::vector<unsigned char> bytes;
    std::string mime;
    if (decodeDataUrl(raw_path, &bytes, &mime)) {
      if (info.svg) {
        const std::string markup(bytes.begin(), bytes.end());
        xt_svg_markup_render(markup, width, height, &image);
      } else {
        int decoded_width = 0;
        int decoded_height = 0;
        int channels = 0;
        unsigned char *data = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                                    &decoded_width, &decoded_height, &channels, 4);
        if (data != nullptr) {
          image.width = decoded_width;
          image.height = decoded_height;
          image.pixels.assign(data, data + (size_t)decoded_width * (size_t)decoded_height * 4);
          stbi_image_free(data);
        }
      }
    }
    auto inserted = cache.emplace(key, std::move(image));
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
    const DataUrlEntry &info = dataUrlEntry(raw_path);
    if (info.size.valid) {
      if (width != nullptr) *width = info.size.width;
      if (height != nullptr) *height = info.size.height;
      return true;
    }
    if (info.svg) return false;
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

bool xt_image_is_vector(const std::string &path) {
  if (path.rfind("data:", 0) == 0) return dataUrlEntry(path).svg;
  return xt_svg_is_svg(normalizePath(path));
}

void xt_image_shutdown() {
  imageCache().clear();
  sizeCache().clear();
  dataUrlCache().clear();
  xt_svg_shutdown();
}

}  // namespace xtgui
