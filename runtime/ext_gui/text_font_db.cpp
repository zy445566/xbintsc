/*
 * xbintsc GUI engine — font discovery and the process-wide face database.
 *
 * `text_fonts.cpp` consumes this database for CSS family/weight matching. This
 * unit owns discovery: the curated generic-family files, the one-time scan of
 * the platform font directories, sfnt metadata extraction and the document's
 * author `@font-face` faces.
 */

#include "text_fonts_internal.h"

#include "text_font_paths.h"
#include "text_font_sfnt.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
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

std::string trimText(const std::string &text) {
  size_t first = 0;
  size_t last = text.size();
  auto space = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
  };
  while (first < last && space((unsigned char)text[first])) first++;
  while (last > first && space((unsigned char)text[last - 1])) last--;
  return text.substr(first, last - first);
}

bool startsWithIgnoreCase(const std::string &text, const char *prefix) {
  size_t index = 0;
  for (; prefix[index] != '\0'; index++) {
    if (index >= text.size()) return false;
    char c = text[index];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != prefix[index]) return false;
  }
  return true;
}

std::string decodePercentEscapes(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '%' && i + 2 < text.size() && std::isxdigit((unsigned char)text[i + 1]) &&
        std::isxdigit((unsigned char)text[i + 2])) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
      };
      out.push_back((char)(hex(text[i + 1]) * 16 + hex(text[i + 2])));
      i += 2;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

/** Drop a URL's `?query`/`#fragment`; neither is part of the local file name. */
std::string stripUrlSuffix(const std::string &text) {
  const size_t cut = text.find_first_of("?#");
  return cut == std::string::npos ? text : text.substr(0, cut);
}

/** Map an `@font-face` URL to a local path. Relative paths are interpreted as
 * local files (the `file:` protocol) against the process working directory, the
 * same policy as image sources. Remote and `data:` URLs have no loader here, so
 * they return an empty string and the next `src` entry is tried. */
std::string localFontPath(const std::string &url) {
  const std::string text = trimText(url);
  if (text.empty()) return {};
  const std::string lower = lowerAscii(text);
  if (startsWithIgnoreCase(lower, "http:") || startsWithIgnoreCase(lower, "https:") ||
      startsWithIgnoreCase(lower, "ftp:") || startsWithIgnoreCase(lower, "data:") ||
      text.rfind("//", 0) == 0) {
    return {};
  }
  if (startsWithIgnoreCase(lower, "file:")) {
    std::string path = text.substr(5);
    if (path.rfind("//", 0) == 0) {
      path = path.substr(2);
      /* `file://host/abs` is not supported; keep only the path. A Windows
       * drive letter is a path, not an authority. */
      const bool drive =
          path.size() >= 2 && std::isalpha((unsigned char)path[0]) && path[1] == ':';
      if (!drive && !path.empty() && path[0] != '/') {
        const size_t slash = path.find('/');
        if (slash == std::string::npos) return {};
        path = path.substr(slash);
      }
    }
    /* `file:///C:/…` -> `C:/…`. */
    if (path.size() >= 3 && path[0] == '/' && std::isalpha((unsigned char)path[1]) &&
        path[2] == ':') {
      path = path.substr(1);
    }
    return decodePercentEscapes(stripUrlSuffix(path));
  }
  return decodePercentEscapes(stripUrlSuffix(text));
}

std::string compactFamily(const std::string &normalized) {
  std::string out;
  for (unsigned char c : normalized) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back((char)c);
  }
  return out;
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
  Database &db = fontDatabase();
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
  face.weight = clampedWeight(raw.weight);
  face.weight_min = face.weight;
  face.weight_max = face.weight;
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

/** Rebuild the lookup maps after user faces are replaced. User faces are
 * indexed first so an author `@font-face` named like a system family wins ties;
 * `by_path` stays system-only because it drives generic-family default
 * selection, which must not follow an author's declared name. */
void rebuildIndexes() {
  Database &db = fontDatabase();
  db.by_family.clear();
  db.by_compact.clear();
  db.by_path.clear();
  auto index = [&db](size_t index, bool with_path) {
    const FaceRecord &face = db.faces[index];
    if (with_path) db.by_path[face.ref.path].push_back(index);
    for (const std::string &name : face.families) {
      db.by_family[name].push_back(index);
      const std::string compact = compactFamily(name);
      if (!compact.empty()) db.by_compact[compact].push_back(index);
    }
  };
  for (size_t i = 0; i < db.faces.size(); i++) {
    if (db.faces[i].user) index(i, false);
  }
  for (size_t i = 0; i < db.faces.size(); i++) {
    if (!db.faces[i].user) index(i, true);
  }
}

void addUserFace(const UserFontFace &request) {
  Database &db = fontDatabase();
  const std::string family = normalizeFamily(request.family);
  if (family.empty()) return;
  for (const UserFontSource &source : request.sources) {
    const std::string path = localFontPath(source.url);
    if (path.empty() || !fileExists(path)) continue;
    std::vector<SfntFace> faces;
    if (!xt_sfnt_read_faces(path, faces) || faces.empty()) continue;
    const SfntFace &metadata = faces.front();
    FaceRecord face;
    face.ref = FontFaceRef{path, metadata.index};
    face.primary_family = family;
    face.families.push_back(family);
    if (request.has_weight) {
      const int minimum = clampedWeight(request.weight_min);
      const int maximum = clampedWeight(request.weight_max);
      face.weight_min = std::min(minimum, maximum);
      face.weight_max = std::max(minimum, maximum);
      face.weight = face.weight_min + (face.weight_max - face.weight_min) / 2;
    } else {
      face.weight = clampedWeight(metadata.weight);
      face.weight_min = face.weight;
      face.weight_max = face.weight;
    }
    face.italic = request.has_italic ? request.italic : metadata.italic;
    face.monospace = metadata.monospace;
    face.truetype = metadata.truetype;
    face.user = true;
    db.faces.push_back(std::move(face));
    return;
  }
}

std::string userFaceKey(const UserFontFace &face) {
  std::string key = normalizeFamily(face.family);
  key += '|';
  if (face.has_weight) {
    key += std::to_string(std::min(face.weight_min, face.weight_max));
    key += '-';
    key += std::to_string(std::max(face.weight_min, face.weight_max));
  } else {
    key += "auto";
  }
  key += face.has_italic ? (face.italic ? "|italic" : "|normal") : "|auto";
  return key;
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

}  // namespace

Database &fontDatabase() {
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

std::string faceKey(const FontFaceRef &ref) {
  return ref.path + '\x1f' + std::to_string(ref.index);
}

void addFontFile(const std::string &path) {
  Database &db = fontDatabase();
  if (!db.parsed_paths.insert(path).second) return;
  std::vector<SfntFace> faces;
  xt_sfnt_read_faces(path, faces);
  for (const SfntFace &face : faces) addFace(face, path);
  /* Keep an empty vector so `by_path[path]` distinguishes "parsed but no
   * usable face" from "not parsed yet". */
  db.by_path.emplace(path, std::vector<size_t>());
}

void ensureDefaults() {
  Database &db = fontDatabase();
  if (db.defaults_loaded) return;
  db.defaults_loaded = true;
  for (int i = 0; i < (int)kGenericCount; i++) {
    db.defaults[(size_t)i] = defaultPaths((FontGeneric)i);
    for (const std::string &path : db.defaults[(size_t)i]) addFontFile(path);
  }
}

void ensureScanned() {
  Database &db = fontDatabase();
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
  Database &db = fontDatabase();
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

void xt_font_set_user_faces(const std::vector<UserFontFace> &faces) {
  Database &db = fontDatabase();
  db.faces.erase(std::remove_if(db.faces.begin(), db.faces.end(),
                                [](const FaceRecord &face) { return face.user; }),
                 db.faces.end());

  /* A later `@font-face` with the same family and descriptors replaces an
   * earlier one, so keep only the last request per key. */
  std::unordered_map<std::string, size_t> last;
  for (size_t i = 0; i < faces.size(); i++) last[userFaceKey(faces[i])] = i;
  for (size_t i = 0; i < faces.size(); i++) {
    const auto found = last.find(userFaceKey(faces[i]));
    if (found != last.end() && found->second == i) addUserFace(faces[i]);
  }
  rebuildIndexes();
}

void xt_font_db_shutdown() {
  Database &db = fontDatabase();
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
