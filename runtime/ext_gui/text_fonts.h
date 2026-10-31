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

/** One source URL from an author `@font-face` rule. Relative paths are read as
 * local files (the `file:` protocol) from the process working directory; remote
 * URLs are ignored. */
struct UserFontSource {
  std::string url;
  std::string format;
};

/** One resolved-author `@font-face` request. The declared `family` is matched
 * by CSS even when it differs from the font file's internal family name. A
 * missing weight/style descriptor (`has_* == false`) takes the value from the
 * font file's own metadata. */
struct UserFontFace {
  std::string family;
  std::vector<UserFontSource> sources;
  bool has_weight = false;
  int weight_min = 400;
  int weight_max = 400;
  bool has_italic = false;
  bool italic = false;
};

/** Replace the document's author font faces. Every call clears the previous
 * set, so `loadHTML` starts from the new document's `@font-face` rules. */
void xt_font_set_user_faces(const std::vector<UserFontFace> &faces);

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
