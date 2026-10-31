/*
 * xbintsc GUI engine — CSS font matching (see text_fonts.h).
 */

#include "text_fonts.h"

#include "text_font_paths.h"
#include "text_font_sfnt.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xtgui {
namespace {

constexpr size_t kGenericCount = 5;

struct FaceRecord {
  FontFaceRef ref;
  std::string primary_family;             // normalized
  std::vector<std::string> families;      // normalized (legacy + typographic)
  int weight = 400;
  bool italic = false;
  bool monospace = false;
  bool truetype = true;
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

Database &database() {
  static Database instance;
  return instance;
}

size_t kindIndex(FontGeneric kind) { return (size_t)kind; }

bool fileExists(const std::string &path) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  std::fclose(file);
  return true;
}

std::string lowerAscii(const std::string &text) {
  std::string out = text;
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return out;
}

/** CSS family names compare case-insensitively with whitespace collapsed. Only
 * ASCII is case-folded so non-ASCII names still match byte-for-byte. */
std::string normalizeFamily(const std::string &name) {
  std::string out;
  bool pending_space = false;
  for (unsigned char raw : name) {
    if (raw <= 0x20 || raw == 0x7f) {
      pending_space = !out.empty();
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    char c = (char)raw;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    out.push_back(c);
  }
  return out;
}

std::string compactFamily(const std::string &normalized) {
  std::string out;
  for (unsigned char c : normalized) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back((char)c);
  }
  return out;
}

std::string faceKey(const FontFaceRef &ref) {
  return ref.path + '\x1f' + std::to_string(ref.index);
}

bool hasFontExtension(const std::string &path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return false;
  const std::string ext = lowerAscii(path.substr(dot));
  return ext == ".ttf" || ext == ".ttc" || ext == ".otf" || ext == ".otc";
}

void appendUnique(std::vector<std::string> &paths, const std::string &path) {
  for (const std::string &entry : paths) {
    if (entry == path) return;
  }
  paths.push_back(path);
}

/** The defaults list with an environment override at the front. The override is
 * only kept when the file exists, which mirrors the old `XT_GUI_FONT`
 * behaviour (a bad path was ignored). */
std::vector<std::string> defaultPaths(FontGeneric kind) {
  std::vector<std::string> paths;
  const char *sans_env = std::getenv("XT_GUI_FONT");
  const char *mono_env = std::getenv("XT_GUI_FONT_MONO");
  if (kind == FontGeneric::Sans && sans_env != nullptr && fileExists(sans_env)) {
    paths.push_back(sans_env);
  }
  if (kind == FontGeneric::Monospace) {
    if (mono_env != nullptr && fileExists(mono_env)) {
      paths.push_back(mono_env);
    } else if (sans_env != nullptr && fileExists(sans_env) &&
               lowerAscii(sans_env).find("mono") != std::string::npos) {
      paths.push_back(sans_env);
    }
  }
  for (const std::string &path : xt_font_default_paths(kind)) {
    if (fileExists(path)) appendUnique(paths, path);
  }
  return paths;
}

void addFace(const SfntFace &raw, const std::string &path) {
  Database &db = database();
  const FontFaceRef ref{path, raw.index};
  const std::string key = faceKey(ref);
  const auto found = db.by_path.find(path);
  if (found != db.by_path.end()) {
    for (size_t index : found->second) {
      if (faceKey(db.faces[index].ref) == key) return;
    }
  }

  FaceRecord face;
  face.ref = ref;
  face.primary_family = normalizeFamily(raw.primary_family);
  face.weight = std::min(1000, std::max(1, raw.weight));
  face.italic = raw.italic;
  face.monospace = raw.monospace;
  face.truetype = raw.truetype;
  for (const std::string &name : raw.families) {
    const std::string normalized = normalizeFamily(name);
    if (normalized.empty()) continue;
    if (std::find(face.families.begin(), face.families.end(), normalized) ==
        face.families.end()) {
      face.families.push_back(normalized);
    }
  }
  if (face.primary_family.empty() && !face.families.empty()) {
    face.primary_family = face.families.front();
  }

  const size_t index = db.faces.size();
  db.faces.push_back(std::move(face));
  db.by_path[path].push_back(index);
  for (const std::string &name : db.faces[index].families) {
    db.by_family[name].push_back(index);
    const std::string compact = compactFamily(name);
    if (!compact.empty()) db.by_compact[compact].push_back(index);
  }
}

void addFontFile(const std::string &path) {
  Database &db = database();
  if (!db.parsed_paths.insert(path).second) return;
  std::vector<SfntFace> faces;
  xt_sfnt_read_faces(path, faces);
  for (const SfntFace &face : faces) addFace(face, path);
  /* Keep an empty vector so `by_path[path]` distinguishes "parsed but no
   * usable face" from "not parsed yet". */
  db.by_path.emplace(path, std::vector<size_t>());
}

void ensureDefaults() {
  Database &db = database();
  if (db.defaults_loaded) return;
  db.defaults_loaded = true;
  for (int i = 0; i < (int)kGenericCount; i++) {
    db.defaults[(size_t)i] = defaultPaths((FontGeneric)i);
    for (const std::string &path : db.defaults[(size_t)i]) addFontFile(path);
  }
}

void scanDirectory(const std::string &directory, std::vector<std::string> &paths) {
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      directory, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  while (!error && iterator != end) {
    const std::filesystem::directory_entry &entry = *iterator;
    std::error_code entry_error;
    if (entry.is_regular_file(entry_error)) {
      std::string path = entry.path().string();
      if (hasFontExtension(path)) paths.push_back(std::move(path));
    }
    iterator.increment(error);
    if (error) break;
  }
}

void ensureScanned() {
  Database &db = database();
  if (db.scanned) return;
  db.scanned = true;
  ensureDefaults();

  std::vector<std::string> paths;
  for (const std::string &directory : xt_font_search_directories()) {
    scanDirectory(directory, paths);
  }
  /* A font supplied through XT_GUI_FONT may live outside the platform font
   * directories; scanning its directory finds the bold/italic siblings. */
  for (const char *variable : {"XT_GUI_FONT", "XT_GUI_FONT_MONO"}) {
    const char *value = std::getenv(variable);
    if (value == nullptr || !fileExists(value)) continue;
    const std::filesystem::path parent = std::filesystem::path(value).parent_path();
    if (!parent.empty()) scanDirectory(parent.string(), paths);
  }
  for (const std::string &path : paths) addFontFile(path);
}

std::vector<size_t> lookupFamily(const std::string &name) {
  Database &db = database();
  const std::string normalized = normalizeFamily(name);
  if (!normalized.empty()) {
    const auto found = db.by_family.find(normalized);
    if (found != db.by_family.end()) return found->second;
  }
  const std::string compact = compactFamily(normalized);
  if (!compact.empty()) {
    const auto found = db.by_compact.find(compact);
    if (found != db.by_compact.end()) return found->second;
  }
  return {};
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
  Database &db = database();
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
  weightRanks(std::min(1000, std::max(1, weight)), ranks);
  std::stable_sort(matching.begin(), matching.end(),
                   [&ranks](const FaceRecord *left, const FaceRecord *right) {
                     return ranks[left->weight] < ranks[right->weight];
                   });
  for (const FaceRecord *face : matching) out.push_back(face->ref);
}

/** Best weight preference inside one family, or a negative value when the
 * family has no face this backend can load. `style_exact` says whether the
 * requested style (italic vs upright) exists at all, which ranks a family that
 * can satisfy it above one that cannot. */
int bestWeightRank(const std::vector<size_t> &indices, int weight, bool italic, bool truetype_only,
                   bool *style_exact) {
  Database &db = database();
  int ranks[1001];
  weightRanks(std::min(1000, std::max(1, weight)), ranks);
  bool has_style = false;
  bool has_other = false;
  int best_style = 1000000;
  int best_other = 1000000;
  for (size_t index : indices) {
    const FaceRecord &face = db.faces[index];
    if (truetype_only && !face.truetype) continue;
    if (face.italic == italic) {
      has_style = true;
      best_style = std::min(best_style, ranks[face.weight]);
    } else {
      has_other = true;
      best_other = std::min(best_other, ranks[face.weight]);
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
  Database &db = database();
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
    ensureScanned();
    const std::vector<size_t> matches = lookupFamily(family);
    if (matches.empty()) continue;
    const size_t before = out.size();
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
  Database &db = database();
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

void xt_font_db_shutdown() {
  Database &db = database();
  db.defaults_loaded = false;
  db.scanned = false;
  db.faces.clear();
  db.by_family.clear();
  db.by_compact.clear();
  db.by_path.clear();
  db.parsed_paths.clear();
  for (std::vector<std::string> &paths : db.defaults) paths.clear();
}

}  // namespace xtgui
