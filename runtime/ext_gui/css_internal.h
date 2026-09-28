/*
 * xbintsc GUI engine — shared helpers for the CSS translation units
 * (`css.cpp`, `css_selector.cpp`, `css_sheet.cpp`, `css_match.cpp`).
 */
#ifndef XT_GUI_CSS_INTERNAL_H
#define XT_GUI_CSS_INTERNAL_H

#include <cctype>

#include "css.h"

namespace xtgui {

/** CSS whitespace test, shared by the trim/split helpers and the parsers. */
bool xt_is_css_space(char c);

inline bool xt_css_is_ident_start(char c) {
  return std::isalpha((unsigned char)c) || c == '_' || c == '-' || (unsigned char)c >= 0x80;
}

inline bool xt_css_is_ident_char(char c) {
  return std::isalnum((unsigned char)c) || c == '_' || c == '-' || (unsigned char)c >= 0x80;
}

}  // namespace xtgui

#endif /* XT_GUI_CSS_INTERNAL_H */
