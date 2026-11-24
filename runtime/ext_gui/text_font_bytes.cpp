/*
 * xbintsc GUI engine — font file loading and webfont decoding (see
 * `text_font_bytes.h`).
 *
 * The process-wide byte cache is keyed by path and holds the *uncompressed*
 * sfnt bytes, so a `.woff2` source is decompressed once and then shared by the
 * metadata reader and whichever text backend draws it. A failed read or decode
 * is cached as null: a page that points at a missing or corrupt font must not
 * retry it on every restyle.
 */

#include "text_font_bytes.h"

#include <cstdio>
#include <unordered_map>

#include "warnings.h"

namespace xtgui {

namespace {

struct ByteCache {
  std::unordered_map<std::string, std::shared_ptr<const std::vector<unsigned char>>> files;
  /** Whether a path is a WOFF/WOFF2 container (its first four bytes). */
  std::unordered_map<std::string, bool> compressed;
};

ByteCache &byteCache() {
  static ByteCache instance;
  return instance;
}

const WebfontDecoder *g_decoder = nullptr;

std::shared_ptr<std::vector<unsigned char>> readFile(const std::string &path) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return nullptr;
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return nullptr;
  }
  const long length = std::ftell(file);
  if (length <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return nullptr;
  }
  auto bytes = std::make_shared<std::vector<unsigned char>>((size_t)length);
  const size_t read = std::fread(bytes->data(), 1, bytes->size(), file);
  std::fclose(file);
  if (read != bytes->size()) return nullptr;
  return bytes;
}

}  // namespace

void xt_font_webfont_install(const WebfontDecoder *decoder) { g_decoder = decoder; }

bool xt_font_webfont_available() { return g_decoder != nullptr; }

bool xt_font_is_webfont(const unsigned char *data, size_t size) {
  if (data == nullptr || size < 4) return false;
  const uint32_t tag = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                       ((uint32_t)data[2] << 8) | (uint32_t)data[3];
  return tag == kWebfontWoffTag || tag == kWebfontWoff2Tag;
}

bool xt_font_file_is_compressed(const std::string &path) {
  ByteCache &cache = byteCache();
  const auto found = cache.compressed.find(path);
  if (found != cache.compressed.end()) return found->second;
  bool result = false;
  FILE *file = std::fopen(path.c_str(), "rb");
  if (file != nullptr) {
    unsigned char header[4];
    result = std::fread(header, 1, sizeof(header), file) == sizeof(header) &&
             xt_font_is_webfont(header, sizeof(header));
    std::fclose(file);
  }
  cache.compressed.emplace(path, result);
  return result;
}

std::shared_ptr<const std::vector<unsigned char>> xt_font_file_bytes(const std::string &path) {
  ByteCache &cache = byteCache();
  const auto found = cache.files.find(path);
  if (found != cache.files.end()) return found->second;

  std::shared_ptr<const std::vector<unsigned char>> result;
  const std::shared_ptr<std::vector<unsigned char>> raw = readFile(path);
  if (raw != nullptr) {
    if (xt_font_is_webfont(raw->data(), raw->size())) {
      if (g_decoder == nullptr) {
        xt_warn_webfont_unlinked(path);
      } else {
        auto decoded = std::make_shared<std::vector<unsigned char>>();
        if (g_decoder->decode(*raw, decoded.get()) && !decoded->empty()) {
          result = decoded;
        } else {
          xt_warn_webfont_invalid(path);
        }
      }
    } else {
      result = raw;
    }
  }
  cache.files.emplace(path, result);
  return result;
}

void xt_font_bytes_shutdown() {
  ByteCache &cache = byteCache();
  cache.files.clear();
  cache.compressed.clear();
}

}  // namespace xtgui
