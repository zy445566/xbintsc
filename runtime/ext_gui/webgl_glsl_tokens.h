/*
 * xbintsc GUI engine — shared GLSL token type for the scanner and block parser.
 */
#ifndef XT_GUI_WEBGL_GLSL_TOKENS_H
#define XT_GUI_WEBGL_GLSL_TOKENS_H

#include <cstddef>
#include <string>

namespace xtgui {

struct GlslToken {
  enum Kind { Ident, Number, Punct, Directive } kind = Punct;
  std::string text;
  size_t begin = 0;
  size_t end = 0;
};

inline bool is_qualifier(const std::string &text) {
  return text == "const" || text == "uniform" || text == "attribute" || text == "varying" ||
         text == "in" || text == "out" || text == "inout" || text == "highp" ||
         text == "mediump" || text == "lowp" || text == "flat" || text == "smooth" ||
         text == "centroid" || text == "invariant" || text == "precision";
}

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_GLSL_TOKENS_H */
