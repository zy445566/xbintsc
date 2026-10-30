/*
 * xbintsc GUI engine — text stack (M4).
 *
 * Text is shaped and rasterised with the bundled stb_truetype. The engine's
 * original stack used HarfBuzz + FreeType; this implementation keeps the same
 * public interface (`text.h`) but removes both libraries, which were the
 * largest fixed cost in a DOM-only binary.
 *
 * Trade-off: shaping is a per-codepoint mapping plus the font's `kern` table.
 * It covers Latin/CJK/Cyrillic/Greek well, but it does not run OpenType
 * ligatures or complex-script shaping (Arabic/Indic/Thai) and it only accepts
 * TrueType outlines, not CFF/OTF or variable fonts. The fallback chain still
 * switches faces for codepoints the primary lacks.
 */

#include "text.h"

#include "perf.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

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
struct Font {
  stbtt_fontinfo info{};
  /* The font file is shared by every pixel size of the same path; stbtt keeps
   * pointers into it, so it must outlive this face. */
  std::shared_ptr<const std::vector<unsigned char>> data;
  std::string path;
  bool monospace = false;
  float pixel_size = 16.0f;
  float scale = 1.0f;
  float ascent = 0;
  float descent = 0;
  float line_height = 0;
};

namespace {

const char *const kSansCandidates[] = {
    /* Explicit override wins (also checked per family class below). */
    nullptr,
    /* Fonts with CJK coverage come first: the default face is a single face, so
     * it has to be able to render Chinese/Japanese/Korean text as well as
     * Latin. */
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
 * faces come first (shape/arrow/box-drawing coverage), then the other family
 * class so a monospace run still gets CJK and a sans run still gets
 * monospace-only codepoints. The primary and duplicates are skipped. */
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
  bool font_loaded = false;
  std::string sans_path;
  std::string mono_path;
  std::vector<std::string> sans_fallbacks;
  std::vector<std::string> mono_fallbacks;
  std::unordered_map<std::string, std::shared_ptr<const std::vector<unsigned char>>> data;
  std::unordered_map<std::string, std::unique_ptr<Font>> cache;  // key -> face (may be null)
  std::unordered_map<std::string, std::vector<Font *>> fallbacks;  // class+size -> faces
  std::unordered_map<std::string, Font *> primary;
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

std::unique_ptr<Font> createFont(const std::string &path, float pixel_size, bool monospace) {
  Engine &e = engine();
  if (path.empty()) return nullptr;
  std::shared_ptr<const std::vector<unsigned char>> data = fontData(path);
  if (data == nullptr || data->empty()) return nullptr;

  const int offset = stbtt_GetFontOffsetForIndex(data->data(), 0);
  if (offset < 0) return nullptr;
  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, data->data(), offset)) return nullptr;

  auto font = std::make_unique<Font>();
  font->info = info;
  font->data = std::move(data);
  font->path = path;
  font->monospace = monospace;
  font->pixel_size = pixel_size;
  font->scale = stbtt_ScaleForPixelHeight(&font->info, pixel_size);

  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
  font->ascent = (float)ascent * font->scale;
  font->descent = (float)-descent * font->scale;
  font->line_height = (float)(ascent - descent + line_gap) * font->scale;
  e.font_loaded = true;
  return font;
}

/* Resolve one face by path, keyed by path+class+size so the primary resolved
 * through a `FontSpec` and the same face resolved as a fallback share one
 * `Font` (and one glyph-atlas key). */
Font *resolveFacePath(const std::string &path, float pixel_size, bool monospace) {
  Engine &e = engine();
  if (path.empty()) return nullptr;
  char key[512];
  std::snprintf(key, sizeof(key), "p|%c|%.2f|%s", monospace ? 'm' : 's', (double)pixel_size,
                path.c_str());
  auto it = e.cache.find(key);
  if (it != e.cache.end()) return it->second.get();
  std::unique_ptr<Font> font = createFont(path, pixel_size, monospace);
  Font *raw = font.get();
  e.cache.emplace(key, std::move(font));
  return raw;
}

/* The resolved fallback faces for a primary, in order, cached per class+size. */
const std::vector<Font *> &fallbackFaces(Font *font) {
  static const std::vector<Font *> kEmpty;
  if (font == nullptr) return kEmpty;
  Engine &e = engine();
  char key[80];
  std::snprintf(key, sizeof(key), "f|%c|%.2f", font->monospace ? 'm' : 's',
                (double)font->pixel_size);
  auto it = e.fallbacks.find(key);
  if (it != e.fallbacks.end()) return it->second;

  const std::vector<std::string> &paths =
      font->monospace ? e.mono_fallbacks : e.sans_fallbacks;
  std::vector<Font *> faces;
  for (const std::string &path : paths) {
    Font *face = resolveFacePath(path, font->pixel_size, font->monospace);
    if (face != nullptr) faces.push_back(face);
  }
  return e.fallbacks.emplace(key, std::move(faces)).first->second;
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

Font *xt_text_resolve(const FontSpec &spec) {
  ensureInitialized();
  Engine &e = engine();

  bool mono = isMonospaceFamily(spec.family);
  char key[48];
  std::snprintf(key, sizeof(key), "%c|%.4f", mono ? 'm' : 's', (double)spec.pixel_size);
  auto cached = e.primary.find(key);
  if (cached != e.primary.end()) return cached->second;

  const std::string &path = mono ? e.mono_path : e.sans_path;
  Font *font = path.empty() ? nullptr : resolveFacePath(path, spec.pixel_size, mono);
  /* A candidate can exist but not be loadable (a CFF/OTF collection, for
   * example). Walk the class's remaining candidates before giving up. */
  if (font == nullptr) {
    const char *const *candidates = mono ? kMonoCandidates : kSansCandidates;
    const size_t count = mono ? sizeof(kMonoCandidates) / sizeof(kMonoCandidates[0])
                              : sizeof(kSansCandidates) / sizeof(kSansCandidates[0]);
    for (size_t i = 0; i < count && font == nullptr; i++) {
      if (candidates[i] == nullptr || path == candidates[i]) continue;
      font = resolveFacePath(candidates[i], spec.pixel_size, mono);
    }
  }
  e.primary.emplace(key, font);
  return font;
}

Font *xt_text_scale(Font *font, float pixel_size) {
  if (font == nullptr) return nullptr;
  return resolveFacePath(font->path, pixel_size, font->monospace);
}

float xt_text_measure_width(const std::string &utf8, const FontSpec &spec) {
  Font *font = xt_text_resolve(spec);
  if (font == nullptr) return approximateWidth(utf8, spec.pixel_size, spec.family);
  if (utf8.empty()) return 0.0f;
  const ShapedRun *run = xt_text_shape_cached(utf8, font);
  return run != nullptr ? run->width : 0.0f;
}

float xt_text_shape_run(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out) {
  if (font == nullptr) return 0;
  g_perf_frame.textShapeCalls += 1;
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

bool xt_text_rasterize(Font *font, uint32_t glyph, GlyphImage &out) {
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
  return engine().font_loaded;
}

void xt_text_shutdown() {
  Engine &e = engine();
  /* Cached shaped runs point at faces, so drop them before the faces go. */
  xt_text_cache_clear();
  xt_text_glyph_cache_clear();
  e.primary.clear();
  e.fallbacks.clear();
  e.cache.clear();
  e.data.clear();
  e.font_loaded = false;
  e.initialized = false;
  e.sans_path.clear();
  e.mono_path.clear();
  e.sans_fallbacks.clear();
  e.mono_fallbacks.clear();
}

}  // namespace xtgui
