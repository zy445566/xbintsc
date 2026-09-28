/*
 * xbintsc GUI engine — shared helpers for the DOM translation units
 * (`dom.cpp`, `dom_parse.cpp`, `dom_serialize.cpp`).
 */
#ifndef XT_GUI_DOM_INTERNAL_H
#define XT_GUI_DOM_INTERNAL_H

#include <cctype>
#include <string>

#include "dom.h"

namespace xtgui {

inline bool xt_dom_is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool xt_dom_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline std::string xt_dom_lower(const std::string &text) {
  std::string out = text;
  for (char &c : out) c = (char)std::tolower((unsigned char)c);
  return out;
}

inline std::string xt_dom_trim(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && xt_dom_is_space(text[begin])) begin++;
  while (end > begin && xt_dom_is_space(text[end - 1])) end--;
  return text.substr(begin, end - begin);
}

}  // namespace xtgui

#endif /* XT_GUI_DOM_INTERNAL_H */
