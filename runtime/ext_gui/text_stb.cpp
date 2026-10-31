/*
 * xbintsc GUI engine — default stb_truetype text backend.
 *
 * Text is shaped and rasterised with the bundled stb_truetype. The optional
 * `shaping` feature installs `text_hb.cpp` (HarfBuzz + FreeType) instead; this
 * backend is always linked so a Latin-only DOM program keeps the small binary.
 *
 * Trade-off: shaping is a per-codepoint mapping plus the font's `kern` table.
 * It covers Latin/CJK/Cyrillic/Greek well, but it does not run OpenType
 * ligatures or complex-script shaping (Arabic/Indic/Thai) and it only accepts
 * TrueType outlines, not CFF/OTF or variable fonts. The fallback chain still
 * switches faces for codepoints the primary lacks.
 *
 * Faces are selected by `text_fonts.cpp`: the CSS family list, weight and style
 * resolve to a `(path, collection index)` pair, and only the ordered candidates
 * this backend can actually load (TrueType outlines) are tried.
 */

#include "text_api.h"
#include "text_fonts.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xtgui {

/* Opaque to callers (declared in text.h); defined here so the loading helpers
 * in the anonymous namespace can build it. */
struct Font {
  stbtt_fontinfo info{};
  /* The font file is shared by every pixel size of the same path; stbtt keeps
   * pointers into it, so it must outlive this face. */
  std::shared_ptr<const std::vector<unsigned char>> data;
  std::string path;
  int face_index = 0;
  float pixel_size = 16.0f;
  float scale = 1.0f;
  float ascent = 0;
  float descent = 0;
  float line_height = 0;
};

namespace {

struct Engine {
  std::unordered_map<std::string, std::shared_ptr<const std::vector<unsigned char>>> data;
  std::unordered_map<std::string, std::unique_ptr<Font>> cache;  // key -> face (may be null)
  std::unordered_map<std::string, std::vector<Font *>> fallbacks;  // face+size -> faces
  std::unordered_map<std::string, Font *> primary;  // spec -> face (may be null)
};

Engine &engine() {
  static Engine instance;
  return instance;
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

/* Read a font file once and share the bytes across every pixel size. */
std::shared_ptr<const std::vector<unsigned char>> fontData(const std::string &path) {
  Engine &e = engine();
  auto found = e.data.find(path);
  if (found != e.data.end()) return found->second;

  FILE *file = fopen(path.c_str(), "rb");
  if (file == nullptr) return nullptr;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return nullptr;
  }
  long length = ftell(file);
  if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return nullptr;
  }
  auto bytes = std::make_shared<std::vector<unsigned char>>((size_t)length);
  size_t read = fread(bytes->data(), 1, bytes->size(), file);
  fclose(file);
  if (read != bytes->size()) return nullptr;
  std::shared_ptr<const std::vector<unsigned char>> shared = bytes;
  e.data.emplace(path, shared);
  return shared;
}

std::unique_ptr<Font> createFont(const std::string &path, int face_index, float pixel_size) {
  if (path.empty()) return nullptr;
  std::shared_ptr<const std::vector<unsigned char>> data = fontData(path);
  if (data == nullptr || data->empty()) return nullptr;

  const int offset = stbtt_GetFontOffsetForIndex(data->data(), face_index);
  if (offset < 0) return nullptr;
  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, data->data(), offset)) return nullptr;

  auto font = std::make_unique<Font>();
  font->info = info;
  font->data = std::move(data);
  font->path = path;
  font->face_index = face_index;
  font->pixel_size = pixel_size;
  font->scale = stbtt_ScaleForPixelHeight(&font->info, pixel_size);

  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
  font->ascent = (float)ascent * font->scale;
  font->descent = (float)-descent * font->scale;
  font->line_height = (float)(ascent - descent + line_gap) * font->scale;
  return font;
}

/* Resolve one face by path+index, keyed by path+collection index+size so the
 * primary resolved through a `FontSpec` and the same face resolved as a
 * fallback share one `Font` (and one glyph-atlas key). */
Font *resolveFacePath(const std::string &path, int face_index, float pixel_size) {
  Engine &e = engine();
  if (path.empty()) return nullptr;
  char header[64];
  std::snprintf(header, sizeof(header), "p|%d|%.2f|", face_index, (double)pixel_size);
  std::string key = header;
  key += path;
  auto it = e.cache.find(key);
  if (it != e.cache.end()) return it->second.get();
  std::unique_ptr<Font> font = createFont(path, face_index, pixel_size);
  Font *raw = font.get();
  e.cache.emplace(std::move(key), std::move(font));
  return raw;
}

/* The resolved fallback faces for a primary, in order, cached per face+size. */
const std::vector<Font *> &fallbackFaces(Font *font) {
  static const std::vector<Font *> kEmpty;
  if (font == nullptr) return kEmpty;
  Engine &e = engine();
  char header[64];
  std::snprintf(header, sizeof(header), "f|%.2f|", (double)font->pixel_size);
  std::string key = header;
  key += font->path;
  key += '#';
  key += std::to_string(font->face_index);
  auto it = e.fallbacks.find(key);
  if (it != e.fallbacks.end()) return it->second;

  std::vector<FontFaceRef> candidates;
  xt_font_fallback_faces(FontFaceRef{font->path, font->face_index}, true, candidates);
  std::vector<Font *> faces;
  for (const FontFaceRef &ref : candidates) {
    Font *face = resolveFacePath(ref.path, ref.index, font->pixel_size);
    if (face != nullptr) faces.push_back(face);
  }
  return e.fallbacks.emplace(std::move(key), std::move(faces)).first->second;
}

/* The face that draws `code_point`: the primary when it has the glyph, else the
 * first fallback that does, else the primary (so `.notdef` is still skipped). */
Font *faceForCodePoint(Font *primary, uint32_t code_point) {
  if (primary != nullptr && stbtt_FindGlyphIndex(&primary->info, (int)code_point) != 0) {
    return primary;
  }
  for (Font *fallback : fallbackFaces(primary)) {
    if (stbtt_FindGlyphIndex(&fallback->info, (int)code_point) != 0) return fallback;
  }
  return primary;
}

/* Map one face's segment of a run to glyphs, using advances and the font's
 * `kern` table. This is intentionally a simple shaper: no ligature/GSUB
 * substitution and no complex-script positioning. */
float shapeSegment(const std::string &segment, Font *font, std::vector<ShapedGlyph> &out) {
  if (font == nullptr || segment.empty()) return 0;
  float total = 0;
  size_t index = 0;
  int previous = 0;
  while (index < segment.size()) {
    uint32_t code_point = decodeUtf8(segment, index);
    int glyph = stbtt_FindGlyphIndex(&font->info, (int)code_point);
    int advance = 0;
    int left_side_bearing = 0;
    stbtt_GetGlyphHMetrics(&font->info, glyph, &advance, &left_side_bearing);
    float x_advance = (float)advance * font->scale;
    if (previous != 0) {
      int kerning = stbtt_GetGlyphKernAdvance(&font->info, previous, glyph);
      if (kerning != 0) {
        float shift = (float)kerning * font->scale;
        if (!out.empty()) out.back().x_advance += shift;
        total += shift;
      }
    }
    ShapedGlyph shaped;
    shaped.glyph = (uint32_t)glyph;
    shaped.x_advance = x_advance;
    shaped.font = font;
    total += x_advance;
    out.push_back(shaped);
    previous = glyph;
  }
  return total;
}

}  // namespace

Font *xt_text_stb_resolve(const FontSpec &spec) {
  Engine &e = engine();
  char suffix[80];
  std::snprintf(suffix, sizeof(suffix), "|%d|%d|%.4f", spec.weight, spec.italic ? 1 : 0,
                (double)spec.pixel_size);
  std::string key = spec.family;
  key += suffix;
  auto cached = e.primary.find(key);
  if (cached != e.primary.end()) return cached->second;

  std::vector<FontFaceRef> faces;
  xt_font_face_candidates(spec, true, faces);
  Font *font = nullptr;
  for (const FontFaceRef &ref : faces) {
    font = resolveFacePath(ref.path, ref.index, spec.pixel_size);
    if (font != nullptr) break;
  }
  e.primary.emplace(std::move(key), font);
  return font;
}

Font *xt_text_stb_scale(Font *font, float pixel_size) {
  if (font == nullptr) return nullptr;
  return resolveFacePath(font->path, font->face_index, pixel_size);
}

float xt_text_stb_shape(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out) {
  if (font == nullptr) return 0;
  float total = 0;
  size_t index = 0;
  size_t segment_start = 0;
  Font *segment_font = nullptr;
  while (index < utf8.size()) {
    size_t char_start = index;
    uint32_t code_point = decodeUtf8(utf8, index);
    Font *chosen = faceForCodePoint(font, code_point);
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

bool xt_text_stb_rasterize(Font *font, uint32_t glyph, GlyphImage &out) {
  out.width = 0;
  out.height = 0;
  out.left = 0;
  out.top = 0;
  out.pixels.clear();
  if (font == nullptr) return false;

  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  stbtt_GetGlyphBitmapBox(&font->info, (int)glyph, font->scale, font->scale, &x0, &y0, &x1, &y1);
  const int width = x1 - x0;
  const int height = y1 - y0;
  if (width <= 0 || height <= 0) return false;

  out.width = width;
  out.height = height;
  out.left = x0;
  out.top = -y0;
  out.pixels.resize((size_t)width * height);
  stbtt_MakeGlyphBitmap(&font->info, out.pixels.data(), width, height, width, font->scale,
                        font->scale, (int)glyph);
  return true;
}

void xt_text_stb_metrics(const FontSpec &spec, float *ascent, float *descent, float *line_height) {
  Font *font = xt_text_stb_resolve(spec);
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

bool xt_text_stb_ready() {
  FontSpec spec;
  return xt_text_stb_resolve(spec) != nullptr;
}

void xt_text_stb_shutdown() {
  Engine &e = engine();
  /* Cached shaped runs point at faces, so drop them before the faces go. */
  e.primary.clear();
  e.fallbacks.clear();
  e.cache.clear();
  e.data.clear();
}

}  // namespace xtgui

namespace xtgui {

const TextApi *xt_text_stb_api() {
  static const TextApi api = {
      xt_text_stb_resolve,
      xt_text_stb_scale,
      xt_text_stb_shape,
      xt_text_stb_rasterize,
      xt_text_stb_metrics,
      xt_text_stb_ready,
      xt_text_stb_shutdown,
  };
  return &api;
}

}  // namespace xtgui
