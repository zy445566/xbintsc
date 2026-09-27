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

/** A positioned, shaped glyph (pixels). */
struct ShapedGlyph {
  uint32_t glyph = 0;
  float x_advance = 0;
  float x_offset = 0;
  float y_offset = 0;
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
 * advance width in pixels. `out` is not cleared. */
float xt_text_shape_run(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out);

/** Rasterise `glyph` from `font` into a grayscale bitmap. Returns false when
 * the glyph has no outline (e.g. a space) or cannot be rendered. */
bool xt_text_rasterize(Font *font, uint32_t glyph, GlyphImage &out);

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
