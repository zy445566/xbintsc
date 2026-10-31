/*
 * xbintsc GUI engine — shared font database state.
 *
 * `text_font_db.cpp` owns the process-wide discovery/indexing; `text_fonts.cpp`
 * owns CSS family/weight matching on top of it. The two halves share these
 * internal types and the small helpers that cross the boundary; this header is
 * not part of the public text API.
 */
#ifndef XT_GUI_TEXT_FONTS_INTERNAL_H
#define XT_GUI_TEXT_FONTS_INTERNAL_H

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "text_font_paths.h"
#include "text_fonts.h"

namespace xtgui {

constexpr size_t kGenericCount = 5;

/** One concrete face and the CSS metadata the matcher needs. */
struct FaceRecord {
  FontFaceRef ref;
  std::string primary_family;             // normalized
  std::vector<std::string> families;      // normalized (legacy + typographic)
  int weight = 400;
  /** Authored `@font-face` ranges can cover several weights; system faces have
   * `weight_min == weight_max == weight`. */
  int weight_min = 400;
  int weight_max = 400;
  bool italic = false;
  bool monospace = false;
  bool truetype = true;
  /** True for a face registered by the document's `@font-face` rules. */
  bool user = false;
};

struct Database {
  bool defaults_loaded = false;
  bool scanned = false;
  std::vector<FaceRecord> faces;
  std::unordered_map<std::string, std::vector<size_t>> by_family;
  std::unordered_map<std::string, std::vector<size_t>> by_compact;
  std::unordered_map<std::string, std::vector<size_t>> by_path;
  std::unordered_set<std::string> parsed_paths;
  std::array<std::vector<std::string>, kGenericCount> defaults;
};

Database &fontDatabase();
size_t kindIndex(FontGeneric kind);

inline int clampedWeight(int weight) { return weight < 1 ? 1 : weight > 1000 ? 1000 : weight; }

/** The weight a face actually offers for a requested CSS weight. A descriptor
 * range can satisfy any weight inside it; system faces have a single weight. */
inline int effectiveWeight(const FaceRecord &face, int desired) {
  return desired < face.weight_min ? face.weight_min
                                   : (desired > face.weight_max ? face.weight_max : desired);
}

bool fileExists(const std::string &path);
std::string normalizeFamily(const std::string &name);
std::string faceKey(const FontFaceRef &ref);

void addFontFile(const std::string &path);
void ensureDefaults();
void ensureScanned();
std::vector<size_t> lookupFamily(const std::string &name);

}  // namespace xtgui

#endif /* XT_GUI_TEXT_FONTS_INTERNAL_H */
