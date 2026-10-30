/*
 * xbintsc GUI engine — text backend table.
 *
 * The default backend is the bundled stb_truetype implementation
 * (`text_stb.cpp`). The optional `shaping` link feature installs a
 * HarfBuzz + FreeType backend (`text_hb.cpp`) through this table, so the core
 * never references either library directly. A page with only Latin text can
 * therefore keep the ~3 MiB DOM-only binary, while a complex-script page can
 * opt into full OpenType shaping.
 */
#ifndef XT_GUI_TEXT_API_H
#define XT_GUI_TEXT_API_H

#include <string>
#include <vector>

#include "text.h"

namespace xtgui {

struct TextApi {
  Font *(*resolve)(const FontSpec &spec);
  Font *(*scale)(Font *font, float pixel_size);
  float (*shape_run)(const std::string &utf8, Font *font, std::vector<ShapedGlyph> &out);
  bool (*rasterize)(Font *font, uint32_t glyph, GlyphImage &out);
  void (*metrics)(const FontSpec &spec, float *ascent, float *descent, float *line_height);
  bool (*ready)();
  void (*shutdown)();
};

/** The built-in stb_truetype backend, always linked. */
const TextApi *xt_text_stb_api();

/** Install the optional backend (called by the `shaping` feature bridge). */
void xt_text_api_install(const TextApi *api);

/** The installed backend, or the stb_truetype default. */
const TextApi *xt_text_api();

}  // namespace xtgui

/** Implemented by `text_hb.cpp`; called by the feature's C bridge. */
extern "C" void xt_text_feature_register(void);

#endif /* XT_GUI_TEXT_API_H */
