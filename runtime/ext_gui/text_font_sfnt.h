/*
 * xbintsc GUI engine — sfnt (TrueType/OpenType) metadata reader.
 *
 * `text_font_sfnt.cpp` reads only the tables needed to match a CSS family and
 * weight: `name` (family/style names), `OS/2` (usWeightClass/fsSelection),
 * `head` (macStyle) and `post` (isFixedPitch). It does not decode outlines or
 * shape text, and it accepts both single faces and collections (`.ttc`/`.otc`).
 *
 * The reader is deliberately dependency-free: the default text backend links
 * stb_truetype and the optional `shaping` backend links FreeType/HarfBuzz, but
 * font discovery must work with either, so neither library is used here.
 */
#ifndef XT_GUI_TEXT_FONT_SFNT_H
#define XT_GUI_TEXT_FONT_SFNT_H

#include <string>
#include <vector>

namespace xtgui {

/** One face in a font file. `families` holds every name a family lookup may
 * match (legacy name ID 1 and typographic name ID 16), with `primary_family`
 * being the name CSS reports and child faces inherit. */
struct SfntFace {
  int index = 0;
  std::string primary_family;
  std::vector<std::string> families;
  std::string style;
  int weight = 400;
  bool italic = false;
  bool monospace = false;
  /** False for CFF/OTTO fonts, which the default stb_truetype backend cannot
   * outline; the `shaping` backend can load them. */
  bool truetype = true;
};

/** Read every face in `path`, appending them to `out` in collection order.
 * Returns false when the file cannot be opened or is not an sfnt container;
 * `out` may still contain the faces read before an error. */
bool xt_sfnt_read_faces(const std::string &path, std::vector<SfntFace> &out);

}  // namespace xtgui

#endif /* XT_GUI_TEXT_FONT_SFNT_H */
