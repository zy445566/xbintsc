/*
 * xbintsc GUI engine — SVG decoding. See `svg.h` for the contract.
 *
 * The heavy lifting is the vendored NanoSVG library (`nanosvg.h` +
 * `nanosvgrast.h`, zlib licence, fetched verbatim). This file is only the glue:
 * path detection, a parsed-document cache and the scale-to-fit call. NanoSVG
 * emits straight (non-premultiplied) RGBA8, which is exactly the `Image`
 * layout the rest of the engine expects.
 */

#include "svg.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
/* NanoSVG is vendored verbatim, so silence the one warning it raises under
 * `-Wall` instead of patching the upstream sources. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "nanosvg.h"
#include "nanosvgrast.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>

namespace xtgui {
namespace {

/** NanoSVG's default when the SVG declares neither width/height nor a viewBox
 * (SVG 1.1 says a replaced element falls back to 300x150). */
constexpr float kDefaultWidth = 300.0f;
constexpr float kDefaultHeight = 150.0f;
/** Refuse to allocate a monstrous texture if an `<img>` is scaled absurdly. */
constexpr int kMaxDimension = 8192;

std::string lowercase(std::string value) {
  for (char &c : value) c = (char)std::tolower((unsigned char)c);
  return value;
}

/** True when the file name ends in `.svg` (case-insensitive). */
bool hasSvgExtension(const std::string &path) {
  if (path.size() < 4) return false;
  return lowercase(path.substr(path.size() - 4)) == ".svg";
}

/** Cheap content sniff for a file whose extension is not enough (no extension,
 * or a generated temporary). Looks for `<svg` in the first chunk, which is
 * present after any XML declaration, doctype or leading comment of a sane
 * size. */
bool prefixLooksLikeSvg(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char buffer[1024];
  in.read(buffer, sizeof(buffer));
  std::streamsize size = in.gcount();
  if (size <= 0) return false;
  std::string prefix(buffer, (size_t)size);
  return prefix.find("<svg") != std::string::npos;
}

std::unordered_map<std::string, NSVGimage *> &parsedCache() {
  static std::unordered_map<std::string, NSVGimage *> cache;
  return cache;
}

/** Remember the extension/sniff verdict so a raster image is not reopened on
 * every frame (the loader asks for every `<img>` each time it paints). */
std::unordered_map<std::string, bool> &detectCache() {
  static std::unordered_map<std::string, bool> cache;
  return cache;
}

NSVGrasterizer *&rasterizer() {
  static NSVGrasterizer *value = nullptr;
  return value;
}

/** Parse `path` once and remember the result (including failures, so a missing
 * file is not retried every frame). */
NSVGimage *parseCached(const std::string &path) {
  auto &cache = parsedCache();
  auto found = cache.find(path);
  if (found != cache.end()) return found->second;
  NSVGimage *image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
  cache.emplace(path, image);
  return image;
}

float intrinsicWidth(const NSVGimage *image) {
  return image->width > 0.0f ? image->width : kDefaultWidth;
}

float intrinsicHeight(const NSVGimage *image) {
  return image->height > 0.0f ? image->height : kDefaultHeight;
}

}  // namespace

bool xt_svg_is_svg(const std::string &path) {
  auto &cache = detectCache();
  auto found = cache.find(path);
  if (found != cache.end()) return found->second;
  bool result = hasSvgExtension(path) || prefixLooksLikeSvg(path);
  cache.emplace(path, result);
  return result;
}

bool xt_svg_size(const std::string &path, int *width, int *height) {
  NSVGimage *image = parseCached(path);
  if (image == nullptr) return false;
  float w = intrinsicWidth(image);
  float h = intrinsicHeight(image);
  if (w <= 0.0f || h <= 0.0f) return false;
  if (width != nullptr) *width = (int)(w + 0.5f);
  if (height != nullptr) *height = (int)(h + 0.5f);
  return true;
}

bool xt_svg_render(const std::string &path, int width, int height, Image *out) {
  if (out == nullptr) return false;
  NSVGimage *image = parseCached(path);
  if (image == nullptr) return false;

  float source_width = intrinsicWidth(image);
  float source_height = intrinsicHeight(image);
  int target_width = width > 0 ? width : (int)(source_width + 0.5f);
  int target_height = height > 0 ? height : (int)(source_height + 0.5f);
  if (target_width <= 0 || target_height <= 0) return false;
  target_width = std::min(target_width, kMaxDimension);
  target_height = std::min(target_height, kMaxDimension);

  /* Rasterise at the source aspect ratio, at the larger of the two axis scales
   * (so neither axis is under-sampled), then let the quad stretch the texture
   * to the box — exactly how a bitmap would behave under CSS `object-fit:
   * fill`. The common case (layout preserved the aspect ratio) makes this equal
   * to the requested size. */
  float scale = std::max((float)target_width / source_width, (float)target_height / source_height);
  scale = std::min(scale, (float)kMaxDimension / source_width);
  scale = std::min(scale, (float)kMaxDimension / source_height);
  int raster_width = (int)(source_width * scale + 0.5f);
  int raster_height = (int)(source_height * scale + 0.5f);
  if (raster_width < 1 || raster_height < 1) return false;

  NSVGrasterizer *&rast = rasterizer();
  if (rast == nullptr) rast = nsvgCreateRasterizer();
  if (rast == nullptr) return false;

  out->width = raster_width;
  out->height = raster_height;
  out->pixels.assign((size_t)raster_width * (size_t)raster_height * 4u, 0);
  nsvgRasterize(rast, image, 0.0f, 0.0f, scale, out->pixels.data(), raster_width, raster_height,
                raster_width * 4);
  return true;
}

void xt_svg_shutdown() {
  for (auto &entry : parsedCache()) {
    if (entry.second != nullptr) nsvgDelete(entry.second);
  }
  parsedCache().clear();
  detectCache().clear();
  if (rasterizer() != nullptr) {
    nsvgDeleteRasterizer(rasterizer());
    rasterizer() = nullptr;
  }
}

}  // namespace xtgui
