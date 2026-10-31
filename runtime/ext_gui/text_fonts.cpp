/*
 * xbintsc GUI engine — CSS family/weight/style matching.
 *
 * The process-wide face database lives in `text_font_db.cpp`; this unit parses
 * a CSS `font-family` list, ranks the faces of a family by the CSS Fonts
 * weight/style rules, and appends generic-family and fallback candidates. Both
 * text backends (`text_stb.cpp` and `text_hb.cpp`) consume the same result.
 */

#include "text_fonts_internal.h"

#include "text_font_paths.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xtgui {
namespace {

std::string lowerAscii(const std::string &text) {
  std::string out = text;
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return out;
}

std::vector<std::string> parseFamilyList(const std::string &text) {
  std::vector<std::string> families;
  std::string current;
  char quote = 0;
  bool escaped = false;
  auto push = [&families](const std::string &value) {
    const std::string trimmed = normalizeFamily(value);
    if (!trimmed.empty()) families.push_back(trimmed);
  };
  for (char c : text) {
    if (escaped) {
      current.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (quote != 0) {
      if (c == quote) {
        quote = 0;
      } else {
        current.push_back(c);
      }
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == ',') {
      push(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  push(current);
  return families;
}

bool genericKind(const std::string &normalized, FontGeneric *kind) {
  if (normalized == "serif" || normalized == "ui-serif" || normalized == "math" ||
      normalized == "fangsong") {
    *kind = FontGeneric::Serif;
    return true;
  }
  if (normalized == "monospace" || normalized == "ui-monospace") {
    *kind = FontGeneric::Monospace;
    return true;
  }
  if (normalized == "cursive") {
    *kind = FontGeneric::Cursive;
    return true;
  }
  if (normalized == "fantasy" || normalized == "ui-rounded") {
    *kind = FontGeneric::Fantasy;
    return true;
  }
  if (normalized == "sans-serif" || normalized == "sans serif" ||
      normalized == "system-ui" || normalized == "ui-sans-serif" ||
      normalized == "-apple-system" || normalized == "blinkmacsystemfont" ||
      normalized == "emoji") {
    *kind = FontGeneric::Sans;
    return true;
  }
  return false;
}

/** Weight preference ranks per CSS Fonts 4 §5.2.1. */
void weightRanks(int desired, int ranks[1001]) {
  for (int i = 0; i <= 1000; i++) ranks[i] = 1000000;
  int next = 0;
  auto push = [ranks, &next](int weight) {
    if (weight < 1 || weight > 1000) return;
    if (ranks[weight] != 1000000) return;
    ranks[weight] = next++;
  };
  if (desired >= 400 && desired <= 500) {
    for (int weight = desired; weight <= 500; weight++) push(weight);
    for (int weight = desired - 1; weight >= 1; weight--) push(weight);
    for (int weight = 501; weight <= 1000; weight++) push(weight);
  } else if (desired < 400) {
    for (int weight = desired; weight >= 1; weight--) push(weight);
    for (int weight = desired + 1; weight <= 1000; weight++) push(weight);
  } else {
    for (int weight = desired; weight <= 1000; weight++) push(weight);
    for (int weight = desired - 1; weight >= 1; weight--) push(weight);
  }
}

void appendOrdered(const std::vector<size_t> &indices, int weight, bool italic, bool truetype_only,
                   std::vector<FontFaceRef> &out) {
  Database &db = fontDatabase();
  std::vector<const FaceRecord *> matching;
  std::vector<const FaceRecord *> opposite;
  for (size_t index : indices) {
    const FaceRecord &face = db.faces[index];
    if (truetype_only && !face.truetype) continue;
    if (face.italic == italic) {
      matching.push_back(&face);
    } else {
      opposite.push_back(&face);
    }
  }
  /* CSS checks the requested style first; the other style is only a fallback
   * when the family has no face in the requested style at all. */
  if (matching.empty()) matching = std::move(opposite);

  int ranks[1001];
  const int wanted = clampedWeight(weight);
  weightRanks(wanted, ranks);
  std::stable_sort(matching.begin(), matching.end(),
                   [&ranks, wanted](const FaceRecord *left, const FaceRecord *right) {
                     return ranks[effectiveWeight(*left, wanted)] <
                            ranks[effectiveWeight(*right, wanted)];
                   });
  for (const FaceRecord *face : matching) out.push_back(face->ref);
}

/** Best weight preference inside one family, or a negative value when the
 * family has no face this backend can load. `style_exact` says whether the
 * requested style (italic vs upright) exists at all, which ranks a family that
 * can satisfy it above one that cannot. */
int bestWeightRank(const std::vector<size_t> &indices, int weight, bool italic, bool truetype_only,
                   bool *style_exact) {
  Database &db = fontDatabase();
  int ranks[1001];
  const int wanted = clampedWeight(weight);
  weightRanks(wanted, ranks);
  bool has_style = false;
  bool has_other = false;
  int best_style = 1000000;
  int best_other = 1000000;
  for (size_t index : indices) {
    const FaceRecord &face = db.faces[index];
    if (truetype_only && !face.truetype) continue;
    const int rank = ranks[effectiveWeight(face, wanted)];
    if (face.italic == italic) {
      has_style = true;
      best_style = std::min(best_style, rank);
    } else {
      has_other = true;
      best_other = std::min(best_other, rank);
    }
  }
  *style_exact = has_style;
  if (has_style) return best_style;
  return has_other ? best_other : -1;
}

/** The file explicitly selected through `XT_GUI_FONT`/`XT_GUI_FONT_MONO` for a
 * generic family, if any. An override must win over platform ranking for every
 * weight/style, not only when its faces happen to match better. */
std::string forcedDefaultPath(FontGeneric kind) {
  const char *sans_env = std::getenv("XT_GUI_FONT");
  if (kind == FontGeneric::Sans && sans_env != nullptr && fileExists(sans_env)) return sans_env;
  if (kind == FontGeneric::Monospace) {
    const char *mono_env = std::getenv("XT_GUI_FONT_MONO");
    if (mono_env != nullptr && fileExists(mono_env)) return mono_env;
    if (sans_env != nullptr && fileExists(sans_env) &&
        lowerAscii(sans_env).find("mono") != std::string::npos) {
      return sans_env;
    }
  }
  return {};
}

void genericCandidates(FontGeneric kind, int weight, bool italic, bool truetype_only,
                       std::vector<FontFaceRef> &out) {
  Database &db = fontDatabase();
  ensureDefaults();
  const std::string forced_path = forcedDefaultPath(kind);

  struct FamilyChoice {
    std::string family;
    size_t path_order = 0;
    bool style_exact = false;
    bool forced = false;
    int weight_rank = 0;
  };
  std::vector<FamilyChoice> choices;
  std::vector<FontFaceRef> raw;
  std::unordered_set<std::string> visited_families;
  size_t path_order = 0;
  for (const std::string &path : db.defaults[kindIndex(kind)]) {
    addFontFile(path);
    const auto path_faces = db.by_path.find(path);
    if (path_faces == db.by_path.end() || path_faces->second.empty()) {
      raw.push_back(FontFaceRef{path, 0});
      continue;
    }
    const FaceRecord &first = db.faces[path_faces->second.front()];
    if (first.primary_family.empty()) {
      raw.push_back(first.ref);
      continue;
    }
    if (!visited_families.insert(first.primary_family).second) continue;
    const auto family = db.by_family.find(first.primary_family);
    if (family == db.by_family.end()) continue;
    bool style_exact = false;
    const int rank = bestWeightRank(family->second, weight, italic, truetype_only, &style_exact);
    if (rank < 0) continue;
    choices.push_back(
        FamilyChoice{first.primary_family, path_order++, style_exact, path == forced_path, rank});
  }

  /* A generic family is UA-defined, so prefer a family that has the requested
   * style and the closest weight (e.g. bold `sans-serif` picks a family with a
   * real bold face) before falling back to the CJK-first path order. An explicit
   * `XT_GUI_FONT`/`XT_GUI_FONT_MONO` always comes first. */
  std::stable_sort(choices.begin(), choices.end(),
                   [](const FamilyChoice &left, const FamilyChoice &right) {
                     if (left.forced != right.forced) return left.forced;
                     if (left.style_exact != right.style_exact) return left.style_exact;
                     if (left.weight_rank != right.weight_rank) {
                       return left.weight_rank < right.weight_rank;
                     }
                     return left.path_order < right.path_order;
                   });
  for (const FamilyChoice &choice : choices) {
    const auto family = db.by_family.find(choice.family);
    if (family != db.by_family.end()) {
      appendOrdered(family->second, weight, italic, truetype_only, out);
    }
  }
  for (const FontFaceRef &ref : raw) out.push_back(ref);
}

void appendFirstGeneric(FontGeneric kind, bool truetype_only, std::vector<FontFaceRef> &out) {
  std::vector<FontFaceRef> faces;
  genericCandidates(kind, 400, false, truetype_only, faces);
  if (!faces.empty()) out.push_back(faces.front());
}

}  // namespace

void xt_font_face_candidates(const FontSpec &spec, bool truetype_only,
                             std::vector<FontFaceRef> &out) {
  out.clear();
  std::vector<std::string> families = parseFamilyList(spec.family);
  if (families.empty()) families.push_back("sans-serif");

  bool accepted = false;
  for (const std::string &family : families) {
    FontGeneric kind = FontGeneric::Sans;
    if (genericKind(family, &kind)) {
      genericCandidates(kind, spec.weight, spec.italic, truetype_only, out);
      accepted = true;
      break;
    }
    /* An @font-face family resolves without scanning the system directories;
     * only fall through to the scan when the author family is absent (or has no
     * face this backend can load). */
    std::vector<size_t> matches = lookupFamily(family);
    size_t before = out.size();
    if (!matches.empty()) appendOrdered(matches, spec.weight, spec.italic, truetype_only, out);
    if (out.size() > before) {
      accepted = true;
      break;
    }
    ensureScanned();
    matches = lookupFamily(family);
    if (matches.empty()) continue;
    before = out.size();
    appendOrdered(matches, spec.weight, spec.italic, truetype_only, out);
    if (out.size() > before) {
      accepted = true;
      break;
    }
  }
  if (!accepted) genericCandidates(FontGeneric::Sans, spec.weight, spec.italic, truetype_only, out);

  std::unordered_set<std::string> seen;
  std::vector<FontFaceRef> unique;
  unique.reserve(out.size());
  for (const FontFaceRef &ref : out) {
    if (seen.insert(faceKey(ref)).second) unique.push_back(ref);
  }
  out.swap(unique);
}

void xt_font_fallback_faces(const FontFaceRef &primary, bool truetype_only,
                            std::vector<FontFaceRef> &out) {
  out.clear();
  ensureDefaults();
  Database &db = fontDatabase();
  std::vector<FontFaceRef> candidates;
  for (const std::string &path : xt_font_symbol_paths()) {
    if (!fileExists(path)) continue;
    addFontFile(path);
    const auto faces = db.by_path.find(path);
    if (faces == db.by_path.end()) continue;
    bool added = false;
    for (size_t index : faces->second) {
      const FaceRecord &face = db.faces[index];
      if (truetype_only && !face.truetype) continue;
      candidates.push_back(face.ref);
      added = true;
      break;
    }
    if (!added && !truetype_only) candidates.push_back(FontFaceRef{path, 0});
  }
  appendFirstGeneric(FontGeneric::Sans, truetype_only, candidates);
  appendFirstGeneric(FontGeneric::Serif, truetype_only, candidates);
  appendFirstGeneric(FontGeneric::Monospace, truetype_only, candidates);

  std::unordered_set<std::string> seen;
  seen.insert(faceKey(primary));
  for (const FontFaceRef &ref : candidates) {
    if (out.size() >= 16) break;
    if (seen.insert(faceKey(ref)).second) out.push_back(ref);
  }
}

}  // namespace xtgui
