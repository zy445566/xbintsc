/*
 * xbintsc GUI engine — bounded shaping cache.
 *
 * Layout measures every word of the document on every layout pass, and the
 * renderer shapes every text run again to place its glyphs. Without a cache a
 * document with a few thousand words runs thousands of HarfBuzz shapes per
 * frame, which is what made a large page unusable. This module memoises
 * `(face, text) -> glyphs + advance` behind a two-generation cache: when the
 * budget is exceeded the *spare* generation is dropped and becomes the current
 * one, so no entry-by-entry LRU bookkeeping is needed on the hot path (a
 * document that cycles through many strings keeps at most two budgets).
 *
 * The key is the resolved face pointer plus the text: fallback faces, sizes and
 * `XT_GUI_FONT` overrides each resolve to their own face, and
 * `xt_text_shutdown()` clears the cache before the faces are freed.
 */

#include "text.h"

#include "perf.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xtgui {
namespace {

/** Glyph + key budget per generation. A shaped glyph is 24 bytes, so 2 MiB is
 * roughly 80k glyphs (more than any single document draws). */
constexpr uint64_t kMaxBytes = 2ull * 1024 * 1024;
constexpr uint64_t kMaxEntries = 8192;

struct Key {
  Font *font = nullptr;
  std::string text;

  bool operator==(const Key &other) const {
    return font == other.font && text == other.text;
  }
};

struct KeyHash {
  size_t operator()(const Key &key) const {
    size_t hash = std::hash<const void *>()(key.font);
    // FNV-ish mix of the text, then combine — cheap and stable.
    size_t textHash = std::hash<std::string>()(key.text);
    hash ^= textHash + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
  }
};

struct Generation {
  std::unordered_map<Key, ShapedRun, KeyHash> runs;
  uint64_t bytes = 0;
};

Generation g_generations[2];
int g_current = 0;

uint64_t estimateBytes(const Key &key, const ShapedRun &run) {
  /* Key/Run objects plus the map node's own overhead (a rough, conservative
   * constant: the budget is a guard, not an allocator). */
  return (uint64_t)sizeof(Key) + (uint64_t)key.text.size() + (uint64_t)sizeof(ShapedRun) +
         (uint64_t)run.glyphs.size() * sizeof(ShapedGlyph) + 64;
}

/** Rotate to the spare generation when the current one is full. */
void respectBudget() {
  Generation &current = g_generations[g_current];
  if (current.runs.size() <= kMaxEntries && current.bytes <= kMaxBytes) return;
  int spare = 1 - g_current;
  g_generations[spare].runs.clear();
  g_generations[spare].bytes = 0;
  g_current = spare;
}

}  // namespace

const ShapedRun *xt_text_shape_cached(const std::string &utf8, Font *font) {
  if (font == nullptr) return nullptr;
  if (utf8.empty()) return nullptr;
  Key key;
  key.font = font;
  key.text = utf8;
  Generation &generation = g_generations[g_current];
  auto found = generation.runs.find(key);
  if (found != generation.runs.end()) {
    g_perf_frame.textShapeHits += 1;
    return &found->second;
  }

  auto inserted = generation.runs.emplace(std::move(key), ShapedRun());
  ShapedRun &run = inserted.first->second;
  run.width = xt_text_shape_run(utf8, font, run.glyphs);
  generation.bytes += estimateBytes(inserted.first->first, run);
  respectBudget();
  return &run;
}

uint64_t xt_text_cache_bytes() {
  return g_generations[0].bytes + g_generations[1].bytes;
}

uint64_t xt_text_cache_entries() {
  return (uint64_t)(g_generations[0].runs.size() + g_generations[1].runs.size());
}

void xt_text_cache_clear() {
  g_generations[0].runs.clear();
  g_generations[0].bytes = 0;
  g_generations[1].runs.clear();
  g_generations[1].bytes = 0;
  g_current = 0;
}

}  // namespace xtgui
