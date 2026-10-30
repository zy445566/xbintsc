/*
 * xbintsc GUI engine — optional HarfBuzz + FreeType shaping backend.
 *
 * Installed by the `shaping` link feature through `text_bridge.c`. HarfBuzz does
 * shaping (ligatures/kerning/complex scripts), FreeType owns the glyph outlines
 * and the vertical metrics. We create one HarfBuzz font per (file, pixel size)
 * and scale it so that advances come back in 26.6 pixels.
 *
 * A run is shaped with a *single* face, but that face rarely covers every
 * codepoint in it — a CJK face such as Microsoft YaHei / PingFang / Noto Sans
 * CJK has no geometric shapes (U+25AE ▮, U+25AF ▯, …), and a Latin face has no
 * CJK. Each codepoint therefore resolves to the first face in a small fallback
 * chain that has it, the run is shaped in segments (one per face), and each
 * shaped glyph carries the face it came from so the rasteriser draws it from
 * the right atlas entry. Without this a missing glyph came back as `.notdef`
 * (glyph 0) and the renderer silently dropped it.
 */

#include "text_api.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ot.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xtgui {

/* Opaque to callers (declared in text.h); defined here so the loading helpers
 * in the anonymous namespace can build it. */
struct HbFont {
  FT_Face ft_face = nullptr;
  hb_font_t *hb_font = nullptr;
  std::string path;
  bool monospace = false;
  float pixel_size = 16.0f;
  float ascent = 0;
  float descent = 0;
  float line_height = 0;

  ~HbFont() {
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

/* Faces tried after the primary one for codepoints it does not cover. Symbol
 * faces come first (they carry the geometric shapes, arrows, box drawing and
 * dingbats the CJK/Latin primary often lacks), then the other family class so
 * a monospace run still gets CJK and a sans run still gets monospace-only
 * codepoints. The primary and duplicates are dropped when the chain is built. */
const char *const kSymbolCandidates[] = {
    nullptr,
    "C:\\Windows\\Fonts\\seguisym.ttf",  /* Segoe UI Symbol */
    "C:\\Windows\\Fonts\\arialuni.ttf",  /* Arial Unicode MS */
    "/System/Library/Fonts/Apple Symbols.ttf",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
    "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
};

struct Engine {
  bool initialized = false;
  FT_Library ft = nullptr;
  bool ft_ok = false;
  std::string sans_path;
  std::string mono_path;
  std::vector<std::string> sans_fallbacks;
  std::vector<std::string> mono_fallbacks;
  std::unordered_map<std::string, std::unique_ptr<HbFont>> cache;  // key -> face (may be null)
  std::unordered_map<std::string, std::vector<HbFont *>> fallbacks;  // class+size -> faces
  std::unordered_map<std::string, hb_face_t *> hb_faces;         // path -> face
  /** Primary face by (family class, pixel size): `xt_text_hb_resolve` and
   * `xt_text_hb_metrics` are called per text box per layout, and building the
   * 512-byte path key each time showed up in the profile. */
  std::unordered_map<std::string, HbFont *> primary;
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

void addFallbackPath(std::vector<std::string> &paths, const char *path) {
  if (path == nullptr || !fileExists(path)) return;
  for (const std::string &entry : paths) {
    if (entry == path) return;
  }
  paths.push_back(path);
}

/* The chain for a primary face: every existing symbol face, then the other
 * family class (CJK coverage for a monospace primary and vice versa). The
 * primary itself is skipped because it is tried first, per codepoint. */
void buildFallbackChain(std::vector<std::string> &paths, const std::string &primary,
                        const std::string &otherClass) {
  for (const char *candidate : kSymbolCandidates) {
    if (candidate != nullptr && primary != candidate) addFallbackPath(paths, candidate);
  }
  if (!otherClass.empty() && otherClass != primary) addFallbackPath(paths, otherClass.c_str());
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

  buildFallbackChain(e.sans_fallbacks, e.sans_path, e.mono_path);
  buildFallbackChain(e.mono_fallbacks, e.mono_path, e.sans_path);
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

std::unique_ptr<HbFont> createFont(const std::string &path, float pixel_size, bool monospace) {
  Engine &e = engine();
  if (!e.ft_ok || path.empty()) return nullptr;

  FT_Face face = nullptr;
  if (FT_New_Face(e.ft, path.c_str(), 0, &face) != 0) return nullptr;
  FT_Set_Pixel_Sizes(face, 0, (FT_UInt)(pixel_size + 0.5f));

  auto font = std::make_unique<HbFont>();
  font->ft_face = face;
  font->path = path;
  font->monospace = monospace;
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

/* Resolve one face by path, keyed by path+class+size so the primary resolved
 * through a `FontSpec` and the same face resolved as a fallback share one
 * `HbFont` (and one glyph-atlas key). */
HbFont *resolveFacePath(const std::string &path, float pixel_size, bool monospace) {
  Engine &e = engine();
  if (!e.ft_ok || path.empty()) return nullptr;
  char key[512];
  std::snprintf(key, sizeof(key), "p|%c|%.2f|%s", monospace ? 'm' : 's', (double)pixel_size,
                path.c_str());
  auto it = e.cache.find(key);
  if (it != e.cache.end()) return it->second.get();
  std::unique_ptr<HbFont> font = createFont(path, pixel_size, monospace);
  HbFont *raw = font.get();
  e.cache.emplace(key, std::move(font));
  return raw;
}

/* The resolved fallback faces for a primary, in order, cached per class+size. */
const std::vector<HbFont *> &fallbackFaces(HbFont *font) {
  static const std::vector<HbFont *> kEmpty;
  if (font == nullptr) return kEmpty;
  Engine &e = engine();
  char key[80];
  std::snprintf(key, sizeof(key), "f|%c|%.2f", font->monospace ? 'm' : 's',
                (double)font->pixel_size);
  auto it = e.fallbacks.find(key);
  if (it != e.fallbacks.end()) return it->second;

  const std::vector<std::string> &paths =
      font->monospace ? e.mono_fallbacks : e.sans_fallbacks;
  std::vector<HbFont *> faces;
  for (const std::string &path : paths) {
    HbFont *face = resolveFacePath(path, font->pixel_size, font->monospace);
    if (face != nullptr) faces.push_back(face);
  }
  return e.fallbacks.emplace(key, std::move(faces)).first->second;
}

/* The face that draws `code_point`: the primary when it has the glyph, else the
 * first fallback that does, else the primary (so `.notdef` is still skipped). */
HbFont *faceForCodePoint(HbFont *primary, uint32_t code_point) {
  if (primary != nullptr && FT_Get_Char_Index(primary->ft_face, code_point) != 0) return primary;
  for (HbFont *fallback : fallbackFaces(primary)) {
    if (FT_Get_Char_Index(fallback->ft_face, code_point) != 0) return fallback;
  }
  return primary;
}

/* Shape one face's segment of a run and append its glyphs. */
float shapeSegment(const std::string &segment, HbFont *font, std::vector<ShapedGlyph> &out) {
  if (font == nullptr || segment.empty()) return 0;
  float total = 0;
  if (font->hb_font == nullptr) {
    /* HarfBuzz unavailable: fall back to FreeType's per-codepoint advances. */
    size_t index = 0;
    while (index < segment.size()) {
      uint32_t code_point = decodeUtf8(segment, index);
      FT_UInt glyph = FT_Get_Char_Index(font->ft_face, code_point);
      float advance = 0;
      if (FT_Load_Glyph(font->ft_face, glyph, FT_LOAD_DEFAULT) == 0) {
        advance = font->ft_face->glyph->advance.x / 64.0f;
      }
      ShapedGlyph shaped;
      shaped.glyph = glyph;
      shaped.x_advance = advance;
      shaped.font = reinterpret_cast<Font *>(font);
      total += advance;
      out.push_back(shaped);
    }
    return total;
  }

  hb_buffer_t *buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, segment.c_str(), (int)segment.size(), 0, -1);
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
    shaped.font = reinterpret_cast<Font *>(font);
    total += shaped.x_advance;
    out.push_back(shaped);
  }
  hb_buffer_destroy(buffer);
  return total;
}

}  // namespace

HbFont *xt_text_hb_resolve(const FontSpec &spec) {
  ensureInitialized();
  Engine &e = engine();
  if (!e.ft_ok) return nullptr;

  bool mono = isMonospaceFamily(spec.family);
  char key[48];
  std::snprintf(key, sizeof(key), "%c|%.4f", mono ? 'm' : 's', (double)spec.pixel_size);
  auto cached = e.primary.find(key);
  if (cached != e.primary.end()) return cached->second;

  const std::string &path = mono ? e.mono_path : e.sans_path;
  HbFont *font = path.empty() ? nullptr : resolveFacePath(path, spec.pixel_size, mono);
  e.primary.emplace(key, font);
  return font;
}

HbFont *xt_text_hb_scale(HbFont *font, float pixel_size) {
  if (font == nullptr) return nullptr;
  return resolveFacePath(font->path, pixel_size, font->monospace);
}

float xt_text_hb_shape(const std::string &utf8, HbFont *font, std::vector<ShapedGlyph> &out) {
  if (font == nullptr) return 0;
  float total = 0;
  size_t index = 0;
  size_t segment_start = 0;
  HbFont *segment_font = nullptr;
  while (index < utf8.size()) {
    size_t char_start = index;
    uint32_t code_point = decodeUtf8(utf8, index);
    HbFont *chosen = faceForCodePoint(font, code_point);
    if (segment_font == nullptr) {
      segment_font = chosen;
      segment_start = char_start;
    } else if (chosen != segment_font) {
      total += shapeSegment(utf8.substr(segment_start, char_start - segment_start), segment_font, out);
      segment_font = chosen;
      segment_start = char_start;
    }
  }
  if (segment_font != nullptr) {
    total += shapeSegment(utf8.substr(segment_start), segment_font, out);
  }
  return total;
}

bool xt_text_hb_rasterize(HbFont *font, uint32_t glyph, GlyphImage &out) {
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

void xt_text_hb_metrics(const FontSpec &spec, float *ascent, float *descent, float *line_height) {
  HbFont *font = xt_text_hb_resolve(spec);
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

bool xt_text_hb_ready() {
  ensureInitialized();
  return engine().ft_ok;
}

void xt_text_hb_shutdown() {
  Engine &e = engine();
  /* Cached shaped runs point at faces, so drop them before the faces go. */
  e.primary.clear();
  e.fallbacks.clear();
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
  e.sans_fallbacks.clear();
  e.mono_fallbacks.clear();
}

}  // namespace xtgui
namespace xtgui {

namespace {

Font *api_resolve(const FontSpec &spec) {
  return reinterpret_cast<Font *>(xt_text_hb_resolve(spec));
}

Font *api_scale(Font *font, float pixel_size) {
  return reinterpret_cast<Font *>(
      xt_text_hb_scale(reinterpret_cast<HbFont *>(font), pixel_size));
}

float api_shape(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out) {
  return xt_text_hb_shape(utf8, reinterpret_cast<HbFont *>(font), out);
}

bool api_rasterize(Font *font, uint32_t glyph, GlyphImage &out) {
  return xt_text_hb_rasterize(reinterpret_cast<HbFont *>(font), glyph, out);
}

void api_metrics(const FontSpec &spec, float *ascent, float *descent, float *line_height) {
  xt_text_hb_metrics(spec, ascent, descent, line_height);
}

bool api_ready() { return xt_text_hb_ready(); }

void api_shutdown() { xt_text_hb_shutdown(); }

}  // namespace

const TextApi kTextApi = {
    api_resolve,
    api_scale,
    api_shape,
    api_rasterize,
    api_metrics,
    api_ready,
    api_shutdown,
};

}  // namespace xtgui

extern "C" void xt_text_feature_register(void) {
  xtgui::xt_text_api_install(&xtgui::kTextApi);
}
