/*
 * xbintsc GUI engine — text stack (M4).
 *
 * Text is shaped with HarfBuzz and rasterised with FreeType. Both libraries
 * are statically linked into `gui.a`; this module hides them behind a small
 * interface so layout and paint do not see the third-party types.
 *
 * Fonts are resolved lazily from well-known system locations (or a path in
 * `XT_GUI_FONT` / `XT_GUI_FONT_MONO`) and cached per size. Only regular
 * upright faces are used today; weight/italic selection is a later refinement.
 */
#ifndef XT_GUI_TEXT_H
#define XT_GUI_TEXT_H

#include <cstdint>
#include <string>
#include <vector>

namespace xtgui {

/** Font selector derived from a computed style. */
struct FontSpec {
  std::string family;
  float pixel_size = 16.0f;
  int weight = 400;
  bool italic = false;
};

/** Opaque loaded face (defined in text.cpp). */
struct Font;

/** A positioned, shaped glyph (pixels). `font` is the face the glyph id
 * belongs to — the primary face or, for a codepoint it does not cover, a
 * fallback face — so the rasteriser can load the right face. */
struct ShapedGlyph {
  uint32_t glyph = 0;
  float x_advance = 0;
  float x_offset = 0;
  float y_offset = 0;
  Font *font = nullptr;
};

/** One shaped run: its glyphs and total advance. */
struct ShapedRun {
  float width = 0;
  std::vector<ShapedGlyph> glyphs;
};

/** An 8-bit grayscale glyph bitmap, as produced by FreeType. */
struct GlyphImage {
  int width = 0;
  int height = 0;
  int left = 0;  // x offset from the pen
  int top = 0;   // y offset above the baseline
  std::vector<unsigned char> pixels;
};

/** Load (or fetch from the cache) the font for `spec`, or null if none is
 * available. The returned pointer stays valid until `xt_text_shutdown`. */
Font *xt_text_resolve(const FontSpec &spec);

/** Shape `utf8` with `font`, appending glyphs to `out`; returns the total
 * advance width in pixels. `out` is not cleared. Codepoints `font` does not
 * cover are shaped with the first fallback face that has them, and each
 * glyph's `font` says which face it belongs to. */
float xt_text_shape_run(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out);

/** Shape `utf8` through the process-wide bounded cache (`text_cache.cpp`).
 * Returns a stable pointer for the lifetime of the cache entry (valid until the
 * next cache generation is dropped); callers must not hold it across frames.
 * Layout and paint both go through this, so a word is shaped once instead of
 * once per layout pass and again per repaint. `nullptr` for a null face. */
const ShapedRun *xt_text_shape_cached(const std::string &utf8, Font *font);

/** Current cache occupancy (bytes of keys + glyphs, number of entries). */
uint64_t xt_text_cache_bytes();
uint64_t xt_text_cache_entries();
/** Drop every entry (called by `xt_text_shutdown`, and by tests). */
void xt_text_cache_clear();

/** The same face as `font` at a different pixel size (used to rasterise glyphs
 * at the display's scale factor), or null when it cannot be loaded. */
Font *xt_text_scale(Font *font, float pixel_size);

/** Rasterise `glyph` from `font` into a grayscale bitmap. Returns false when
 * the glyph has no outline (e.g. a space) or cannot be rendered. */
bool xt_text_rasterize(Font *font, uint32_t glyph, GlyphImage &out);

/** `xt_text_rasterize` through a bounded, two-generation cache. A canvas that
 * redraws the same text every frame otherwise runs FreeType for every glyph of
 * every `fillText`. The pointer stays valid for the rest of the frame (until a
 * later call fills a generation and drops the spare one); callers must not hold
 * it across frames. Returns null for a glyph without a bitmap. */
const GlyphImage *xt_text_glyph_cached(Font *font, uint32_t glyph);

/** Drop every cached glyph bitmap (called by `xt_text_shutdown`). */
void xt_text_glyph_cache_clear(void);

/** Shaped horizontal advance of `utf8` in pixels. Falls back to a deterministic
 * approximation when no font file can be loaded. */
float xt_text_measure_width(const std::string &utf8, const FontSpec &spec);

/** Vertical metrics in pixels: `ascent` upward, `descent` downward (positive),
 * `line_height` for `line-height: normal`. Falls back to a deterministic
 * approximation when no font file can be loaded. */
void xt_text_metrics(const FontSpec &spec, float *ascent, float *descent, float *line_height);

/** True once at least one real font file has been loaded. */
bool xt_text_ready();

/** Release every cached face (used at shutdown and by tests). */
void xt_text_shutdown();

}  // namespace xtgui

#endif /* XT_GUI_TEXT_H */
