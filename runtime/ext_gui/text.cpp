/*
 * xbintsc GUI engine — text stack implementation.
 *
 * HarfBuzz does shaping (ligatures/kerning/complex scripts), FreeType owns the
 * glyph outlines and the vertical metrics. We create one HarfBuzz font per
 * (file, pixel size) and scale it so that advances come back in 26.6 pixels.
 */

#include "text.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ot.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

namespace xtgui {

/* Opaque to callers (declared in text.h); defined here so the loading helpers
 * in the anonymous namespace can build it. */
struct Font {
  FT_Face ft_face = nullptr;
  hb_font_t *hb_font = nullptr;
  float pixel_size = 16.0f;
  float ascent = 0;
  float descent = 0;
  float line_height = 0;

  ~Font() {
    if (hb_font != nullptr) hb_font_destroy(hb_font);
    if (ft_face != nullptr) FT_Done_Face(ft_face);
  }
};

namespace {

const char *const kSansCandidates[] = {
    /* Explicit override wins (also checked per family class below). */
    nullptr,
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Verdana.ttf",
    "/Library/Fonts/Arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
};

const char *const kMonoCandidates[] = {
    nullptr,
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/System/Library/Fonts/Courier.ttc",
    "/Library/Fonts/Courier New.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "C:\\Windows\\Fonts\\consola.ttf",
    "C:\\Windows\\Fonts\\cour.ttf",
};

struct Engine {
  bool initialized = false;
  FT_Library ft = nullptr;
  bool ft_ok = false;
  std::string sans_path;
  std::string mono_path;
  std::unordered_map<std::string, std::unique_ptr<Font>> cache;  // key -> face (may be null)
  std::unordered_map<std::string, hb_face_t *> hb_faces;         // path -> face
};

Engine &engine() {
  static Engine instance;
  return instance;
}

bool fileExists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == nullptr) return false;
  fclose(file);
  return true;
}

/* Deliberately crude, deterministic per-byte advance (fraction of the font
 * size). Used only when no font file can be loaded. */
float charAdvance(unsigned char c) {
  if (c < 0x20 || c == 0x7f) return 0.0f;
  if (c >= 0x80) return 0.55f;
  char ch = (char)c;
  if (ch == ' ') return 0.28f;
  if (std::strchr("ilj|.,:;'!`", ch) != nullptr) return 0.26f;
  if (std::strchr("mwMW@%", ch) != nullptr) return 0.85f;
  if (ch >= '0' && ch <= '9') return 0.56f;
  if (ch >= 'A' && ch <= 'Z') return 0.64f;
  if (std::strchr("()[]{}<>/\\\"", ch) != nullptr) return 0.36f;
  return 0.52f;
}

float approximateWidth(const std::string &utf8, float pixel_size, const std::string &family) {
  bool monospace = family.find("mono") != std::string::npos ||
                   family.find("courier") != std::string::npos;
  float factor = monospace ? 0.6f : 1.0f;
  float width = 0;
  for (char c : utf8) width += charAdvance((unsigned char)c) * factor;
  return width * pixel_size;
}

bool isMonospaceFamily(const std::string &family) {
  return family.find("mono") != std::string::npos || family.find("courier") != std::string::npos ||
         family.find("consol") != std::string::npos || family.find("menlo") != std::string::npos ||
         family.find("code") != std::string::npos;
}

const char *firstExisting(const char *const *candidates, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (candidates[i] != nullptr && fileExists(candidates[i])) return candidates[i];
  }
  return nullptr;
}

void ensureInitialized() {
  Engine &e = engine();
  if (e.initialized) return;
  e.initialized = true;
  if (FT_Init_FreeType(&e.ft) != 0) {
    e.ft = nullptr;
    return;
  }
  e.ft_ok = true;

  const size_t sansCount = sizeof(kSansCandidates) / sizeof(kSansCandidates[0]);
  const size_t monoCount = sizeof(kMonoCandidates) / sizeof(kMonoCandidates[0]);

  const char *env = std::getenv("XT_GUI_FONT");
  if (env != nullptr && env[0] != '\0' && fileExists(env)) e.sans_path = env;
  if (e.sans_path.empty()) {
    const char *path = firstExisting(kSansCandidates, sansCount);
    if (path != nullptr) e.sans_path = path;
  }

  const char *envMono = std::getenv("XT_GUI_FONT_MONO");
  if (envMono != nullptr && envMono[0] != '\0' && fileExists(envMono)) {
    e.mono_path = envMono;
  } else if (env != nullptr && env[0] != '\0' && fileExists(env) && isMonospaceFamily(env)) {
    e.mono_path = env;
  }
  if (e.mono_path.empty()) {
    const char *path = firstExisting(kMonoCandidates, monoCount);
    if (path != nullptr) e.mono_path = path;
  }
  /* No dedicated monospace face: reuse the sans face so metrics stay real. */
  if (e.mono_path.empty()) e.mono_path = e.sans_path;
}

hb_face_t *hbFaceForPath(const std::string &path) {
  Engine &e = engine();
  auto it = e.hb_faces.find(path);
  if (it != e.hb_faces.end()) return it->second;
  hb_blob_t *blob = hb_blob_create_from_file(path.c_str());
  hb_face_t *face = nullptr;
  if (blob != nullptr && hb_blob_get_length(blob) > 0) {
    face = hb_face_create(blob, 0);
  }
  if (blob != nullptr) hb_blob_destroy(blob);
  e.hb_faces[path] = face;
  return face;
}

std::unique_ptr<Font> createFont(const std::string &path, float pixel_size) {
  Engine &e = engine();
  if (!e.ft_ok || path.empty()) return nullptr;

  FT_Face face = nullptr;
  if (FT_New_Face(e.ft, path.c_str(), 0, &face) != 0) return nullptr;
  FT_Set_Pixel_Sizes(face, 0, (FT_UInt)(pixel_size + 0.5f));

  auto font = std::make_unique<Font>();
  font->ft_face = face;
  font->pixel_size = pixel_size;

  FT_Size_Metrics metrics = face->size->metrics;
  font->ascent = metrics.ascender / 64.0f;
  font->descent = -metrics.descender / 64.0f;
  font->line_height = metrics.height / 64.0f;

  hb_face_t *hb_face = hbFaceForPath(path);
  if (hb_face != nullptr) {
    font->hb_font = hb_font_create(hb_face);
    hb_ot_font_set_funcs(font->hb_font);
    int scale = (int)(pixel_size * 64.0f + 0.5f);
    hb_font_set_scale(font->hb_font, scale, scale);
    unsigned ppem = (unsigned)(pixel_size + 0.5f);
    hb_font_set_ppem(font->hb_font, ppem, ppem);
  }
  return font;
}

}  // namespace

Font *xt_text_resolve(const FontSpec &spec) {
  ensureInitialized();
  Engine &e = engine();
  if (!e.ft_ok) return nullptr;

  bool mono = isMonospaceFamily(spec.family);
  const std::string &path = mono ? e.mono_path : e.sans_path;
  if (path.empty()) return nullptr;

  char key[80];
  std::snprintf(key, sizeof(key), "%c|%.2f", mono ? 'm' : 's', (double)spec.pixel_size);
  auto it = e.cache.find(key);
  if (it != e.cache.end()) return it->second.get();

  std::unique_ptr<Font> font = createFont(path, spec.pixel_size);
  Font *raw = font.get();
  e.cache.emplace(key, std::move(font));
  return raw;
}

float xt_text_measure_width(const std::string &utf8, const FontSpec &spec) {
  Font *font = xt_text_resolve(spec);
  if (font == nullptr || font->hb_font == nullptr) {
    return approximateWidth(utf8, spec.pixel_size, spec.family);
  }
  hb_buffer_t *buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, utf8.c_str(), (int)utf8.size(), 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(font->hb_font, buffer, nullptr, 0);
  unsigned count = hb_buffer_get_length(buffer);
  hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, nullptr);
  long long advance = 0;
  for (unsigned i = 0; i < count; i++) advance += positions[i].x_advance;
  hb_buffer_destroy(buffer);
  return (float)advance / 64.0f;
}

void xt_text_metrics(const FontSpec &spec, float *ascent, float *descent, float *line_height) {
  Font *font = xt_text_resolve(spec);
  if (font != nullptr) {
    if (ascent != nullptr) *ascent = font->ascent;
    if (descent != nullptr) *descent = font->descent;
    if (line_height != nullptr) *line_height = font->line_height;
    return;
  }
  if (ascent != nullptr) *ascent = spec.pixel_size * 0.8f;
  if (descent != nullptr) *descent = spec.pixel_size * 0.2f;
  if (line_height != nullptr) *line_height = spec.pixel_size * 1.2f;
}

bool xt_text_ready() {
  ensureInitialized();
  return engine().ft_ok;
}

void xt_text_shutdown() {
  Engine &e = engine();
  e.cache.clear();
  for (auto &entry : e.hb_faces) {
    if (entry.second != nullptr) hb_face_destroy(entry.second);
  }
  e.hb_faces.clear();
  if (e.ft != nullptr) {
    FT_Done_FreeType(e.ft);
    e.ft = nullptr;
  }
  e.ft_ok = false;
  e.initialized = false;
  e.sans_path.clear();
  e.mono_path.clear();
}

}  // namespace xtgui
