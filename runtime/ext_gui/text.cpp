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
#include <cstddef>
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
    /* Fonts with CJK coverage come first: the default face is a single face, so
     * it has to be able to render Chinese/Japanese/Korean text as well as
     * Latin (see "Implemented text" in doc/gui.md). */
    "C:\\Windows\\Fonts\\msyh.ttc",      /* Microsoft YaHei (Win 8.1+) */
    "C:\\Windows\\Fonts\\msyh.ttf",      /* Microsoft YaHei (Win 7) */
    "C:\\Windows\\Fonts\\Deng.ttf",      /* DengXian */
    "C:\\Windows\\Fonts\\simsun.ttc",    /* SimSun */
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/STHeiti Light.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Verdana.ttf",
    "/Library/Fonts/Arial.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
};

const char *const kMonoCandidates[] = {
    nullptr,
    "C:\\Windows\\Fonts\\msyh.ttc",      /* CJK-capable fallback (see above) */
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/System/Library/Fonts/Courier.ttc",
    "/Library/Fonts/Courier New.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
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

/* Decode one UTF-8 sequence starting at `index`; advances `index`. */
uint32_t decodeUtf8(const std::string &text, size_t &index) {
  unsigned char first = (unsigned char)text[index];
  if (first < 0x80) {
    index += 1;
    return first;
  }
  uint32_t code_point = 0;
  size_t extra = 0;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    extra = 1;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    extra = 2;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    extra = 3;
  } else {
    index += 1;
    return 0xfffd;
  }
  if (index + extra >= text.size()) {
    index = text.size();
    return 0xfffd;
  }
  for (size_t i = 1; i <= extra; i++) {
    unsigned char next = (unsigned char)text[index + i];
    if ((next & 0xc0) != 0x80) {
      index += 1;
      return 0xfffd;
    }
    code_point = (code_point << 6) | (next & 0x3f);
  }
  index += extra + 1;
  return code_point;
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
  if (font == nullptr) return approximateWidth(utf8, spec.pixel_size, spec.family);
  std::vector<ShapedGlyph> glyphs;
  return xt_text_shape_run(utf8, font, glyphs);
}

float xt_text_shape_run(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out) {
  if (font == nullptr) return 0;
  float total = 0;
  if (font->hb_font == nullptr) {
    /* HarfBuzz unavailable: fall back to FreeType's per-codepoint advances. */
    size_t index = 0;
    while (index < utf8.size()) {
      uint32_t code_point = decodeUtf8(utf8, index);
      FT_UInt glyph = FT_Get_Char_Index(font->ft_face, code_point);
      float advance = 0;
      if (FT_Load_Glyph(font->ft_face, glyph, FT_LOAD_DEFAULT) == 0) {
        advance = font->ft_face->glyph->advance.x / 64.0f;
      }
      ShapedGlyph shaped;
      shaped.glyph = glyph;
      shaped.x_advance = advance;
      total += advance;
      out.push_back(shaped);
    }
    return total;
  }

  hb_buffer_t *buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, utf8.c_str(), (int)utf8.size(), 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(font->hb_font, buffer, nullptr, 0);
  unsigned count = hb_buffer_get_length(buffer);
  hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, nullptr);
  hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, nullptr);
  for (unsigned i = 0; i < count; i++) {
    ShapedGlyph shaped;
    shaped.glyph = infos[i].codepoint;
    shaped.x_advance = positions[i].x_advance / 64.0f;
    shaped.x_offset = positions[i].x_offset / 64.0f;
    shaped.y_offset = positions[i].y_offset / 64.0f;
    total += shaped.x_advance;
    out.push_back(shaped);
  }
  hb_buffer_destroy(buffer);
  return total;
}

bool xt_text_rasterize(Font *font, uint32_t glyph, GlyphImage &out) {
  out.width = 0;
  out.height = 0;
  out.left = 0;
  out.top = 0;
  out.pixels.clear();
  if (font == nullptr || font->ft_face == nullptr) return false;
  if (FT_Load_Glyph(font->ft_face, glyph, FT_LOAD_DEFAULT) != 0) return false;
  if (FT_Render_Glyph(font->ft_face->glyph, FT_RENDER_MODE_NORMAL) != 0) return false;
  FT_GlyphSlot slot = font->ft_face->glyph;
  const FT_Bitmap &bitmap = slot->bitmap;
  out.left = slot->bitmap_left;
  out.top = slot->bitmap_top;
  if (bitmap.width == 0 || bitmap.rows == 0) return false;
  out.width = (int)bitmap.width;
  out.height = (int)bitmap.rows;
  out.pixels.resize((size_t)bitmap.width * bitmap.rows);
  for (unsigned row = 0; row < bitmap.rows; row++) {
    const unsigned char *source = bitmap.buffer + (ptrdiff_t)row * bitmap.pitch;
    std::memcpy(out.pixels.data() + (size_t)row * bitmap.width, source, bitmap.width);
  }
  return true;
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
