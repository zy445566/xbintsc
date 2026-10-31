/*
 * xbintsc GUI engine — platform font locations and default families.
 *
 * The candidate lists are shared by the font database and its fallback chain so
 * the default sans/serif/monospace families are chosen in one place. Paths for
 * every platform are listed; only the ones that exist on the host are used, so
 * the same binary code covers macOS, Windows and Linux without per-platform
 * font-name tables in the text backends.
 */
#ifndef XT_GUI_TEXT_FONT_PATHS_H
#define XT_GUI_TEXT_FONT_PATHS_H

#include <string>
#include <vector>

namespace xtgui {

enum class FontGeneric {
  Sans,
  Serif,
  Monospace,
  Cursive,
  Fantasy,
};

/** Candidate files for a generic CSS family, in preference order. A family is
 * taken from the first file that exists (and can be parsed); the rest of that
 * family's weight/style faces are then found across these files. */
const std::vector<std::string> &xt_font_default_paths(FontGeneric kind);

/** Faces used for codepoints the requested family does not cover. */
const std::vector<std::string> &xt_font_symbol_paths();

/** Directories scanned for arbitrary `font-family` names, recursively. */
std::vector<std::string> xt_font_search_directories();

}  // namespace xtgui

#endif /* XT_GUI_TEXT_FONT_PATHS_H */
