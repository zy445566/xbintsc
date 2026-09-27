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

#include <string>

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

/** Load (or fetch from the cache) the font for `spec`, or null if none is
 * available. The returned pointer stays valid until `xt_text_shutdown`. */
Font *xt_text_resolve(const FontSpec &spec);

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
