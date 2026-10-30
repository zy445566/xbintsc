/*
 * xbintsc GUI engine — cached glyph bitmaps.
 *
 * `xt_text_rasterize` runs stb_truetype for one glyph. Canvas `fillText` used to
 * call it for every glyph of every draw, so a redrawn label paid the whole
 * rasterisation cost again on every frame. This unit keeps the bitmaps in two
 * bounded generations, the same policy as the shaped-run cache in
 * `text_cache.cpp`: a hot generation survives, and a page that walks a huge
 * alphabet cannot grow the cache without bound.
 *
 * The cache is process-wide (fonts are), keyed by the face pointer and the
 * glyph id. It is deliberately leaked at shutdown like the other engine
 * caches; `xt_text_glyph_cache_clear` drops its contents when the fonts go.
 */

#include "text.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace xtgui {

namespace {

/** Per-generation bitmap budget and entry cap. */
constexpr size_t kGlyphCacheBytes = 2u * 1024u * 1024u;
constexpr size_t kGlyphCacheEntries = 8192;

struct GlyphCache {
  std::unordered_map<uint64_t, GlyphImage> current;
  std::unordered_map<uint64_t, GlyphImage> spare;
  size_t current_bytes = 0;
  size_t spare_bytes = 0;
};

GlyphCache &glyph_cache() {
  /* Deliberately leaked: `Font` objects are destroyed at shutdown after this
   * cache would be, and the key holds their addresses (never dereferenced). */
  static GlyphCache *cache = new GlyphCache();
  return *cache;
}

uint64_t glyph_key(Font *font, uint32_t glyph) {
  return (uint64_t)(uintptr_t)font << 32 | (uint64_t)glyph;
}

/** Drop the old generation and make the current one the spare. */
void rotate(GlyphCache &cache) {
  cache.current.swap(cache.spare);
  std::swap(cache.current_bytes, cache.spare_bytes);
  cache.spare.clear();
  cache.spare_bytes = 0;
}

}  // namespace

const GlyphImage *xt_text_glyph_cached(Font *font, uint32_t glyph) {
  if (font == nullptr) return nullptr;
  GlyphCache &cache = glyph_cache();
  const uint64_t key = glyph_key(font, glyph);
  auto found = cache.current.find(key);
  if (found != cache.current.end()) {
    return found->second.pixels.empty() ? nullptr : &found->second;
  }
  GlyphImage image;
  auto spare = cache.spare.find(key);
  if (spare != cache.spare.end()) {
    image = std::move(spare->second);
    cache.spare_bytes -= image.pixels.size();
    cache.spare.erase(spare);
  } else if (!xt_text_rasterize(font, glyph, image)) {
    /* Cache the miss too: a space (or .notdef) would otherwise retry stb_truetype
     * on every `fillText`. */
    image.width = 0;
    image.height = 0;
    image.left = 0;
    image.top = 0;
  }
  const size_t bytes = image.pixels.size();
  if ((cache.current_bytes + bytes > kGlyphCacheBytes ||
       cache.current.size() >= kGlyphCacheEntries) &&
      !cache.current.empty()) {
    rotate(cache);
  }
  cache.current_bytes += bytes;
  auto inserted = cache.current.emplace(key, std::move(image));
  return inserted.first->second.pixels.empty() ? nullptr : &inserted.first->second;
}

void xt_text_glyph_cache_clear(void) {
  GlyphCache &cache = glyph_cache();
  cache.current.clear();
  cache.spare.clear();
  cache.current_bytes = 0;
  cache.spare_bytes = 0;
}

}  // namespace xtgui
