/*
 * xbintsc GUI engine — CSS font matching.
 *
 * `text_fonts.cpp` owns the process-wide font database: it discovers installed
 * faces, resolves a CSS `font-family` list to concrete `(file, face index)`
 * pairs, and orders the faces of a family by the CSS Fonts weight/style rules.
 * Both text backends (`text_stb.cpp` and `text_hb.cpp`) consume the same
 * result, so `font-family: "Times New Roman", serif` and `font-weight: 700`
 * select the same real face whichever shaper is linked.
 *
 * Discovery is lazy: a generic family parses only the curated default files
 * (which contain their own weight variants); an arbitrary family name scans the
 * platform font directories once. Metadata comes from `text_font_sfnt.cpp`, not
 * from stb_truetype/FreeType, so the default backend does not pull the shaping
 * libraries in.
 */
#ifndef XT_GUI_TEXT_FONTS_H
#define XT_GUI_TEXT_FONTS_H

#include <string>
#include <vector>

#include "text.h"

namespace xtgui {

/** One concrete face in a font file (a collection holds several). */
struct FontFaceRef {
  std::string path;
  int index = 0;
};

/** Append the faces for `spec`, best match first. Named families are looked up
 * case-insensitively; generic families resolve to the platform defaults. Every
 * family in the CSS list is tried in order and later families are kept as
 * load-failure fallbacks. `truetype_only` excludes CFF/OTTO faces for the
 * stb_truetype backend. `out` is cleared first. */
void xt_font_face_candidates(const FontSpec &spec, bool truetype_only,
                             std::vector<FontFaceRef> &out);

/** Append the fallback faces for codepoints the primary does not cover: symbol
 * faces first, then the default sans, serif and monospace faces. The primary is
 * excluded. `out` is cleared first. */
void xt_font_fallback_faces(const FontFaceRef &primary, bool truetype_only,
                            std::vector<FontFaceRef> &out);

/** Drop the database and every parsed font file (called at text shutdown). */
void xt_font_db_shutdown();

}  // namespace xtgui

#endif /* XT_GUI_TEXT_FONTS_H */
