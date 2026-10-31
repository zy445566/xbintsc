/*
 * xbintsc GUI engine — text front-end.
 *
 * The engine always links the bundled stb_truetype backend (`text_stb.cpp`).
 * The optional `shaping` link feature installs a HarfBuzz + FreeType backend
 * (`text_hb.cpp`) through `TextApi`; all public text entry points dispatch
 * through the active table, so the core never references either library
 * directly and a Latin-only page keeps the small DOM-only binary.
 */

#include "text_api.h"

#include "perf.h"
#include "text_fonts.h"

namespace xtgui {

namespace {

const TextApi *g_api = nullptr;

}  // namespace

const TextApi *xt_text_api() { return g_api != nullptr ? g_api : xt_text_stb_api(); }

void xt_text_api_install(const TextApi *api) { g_api = api; }

Font *xt_text_resolve(const FontSpec &spec) { return xt_text_api()->resolve(spec); }

Font *xt_text_scale(Font *font, float pixel_size) {
  return xt_text_api()->scale(font, pixel_size);
}

float xt_text_measure_width(const std::string &utf8, const FontSpec &spec) {
  if (utf8.empty()) return 0.0f;
  Font *font = xt_text_resolve(spec);
  if (font == nullptr) {
    /* No font file: keep layout deterministic (bytes rather than codepoints;
     * this path is only hit on hosts with no usable system font). */
    return (float)utf8.size() * (spec.pixel_size * 0.55f + spec.letter_spacing);
  }
  const ShapedRun *run = xt_text_shape_cached(utf8, font, spec.letter_spacing);
  return run != nullptr ? run->width : 0.0f;
}

float xt_text_shape_run(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out) {
  if (font == nullptr) return 0;
  g_perf_frame.textShapeCalls += 1;
  return xt_text_api()->shape_run(utf8, font, out);
}

bool xt_text_rasterize(Font *font, uint32_t glyph, GlyphImage &out) {
  return xt_text_api()->rasterize(font, glyph, out);
}

void xt_text_metrics(const FontSpec &spec, float *ascent, float *descent, float *line_height) {
  xt_text_api()->metrics(spec, ascent, descent, line_height);
}

bool xt_text_ready() { return xt_text_api()->ready(); }

void xt_text_shutdown() {
  /* Cached shaped runs and glyphs point at backend faces; drop them first. */
  xt_text_cache_clear();
  xt_text_glyph_cache_clear();
  xt_text_api()->shutdown();
  xt_font_db_shutdown();
}

}  // namespace xtgui
