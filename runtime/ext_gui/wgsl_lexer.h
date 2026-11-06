/*
 * xbintsc GUI engine — WGSL tokeniser.
 *
 * WGSL's lexical grammar is close to C's. `>>` is kept as one token (it is a
 * shift operator); the type parser splits it back into two `>` when it closes
 * nested generic arguments such as `array<vec4<f32>, 4>`.
 */
#ifndef XT_GUI_WGSL_LEXER_H
#define XT_GUI_WGSL_LEXER_H

#include <string>
#include <vector>

namespace xtgui {
namespace wgsl {

enum class Tok { End, Ident, Int, Float, Punct };

struct Token {
  Tok kind = Tok::End;
  std::string text;
  int line = 1;
  int column = 1;
};

/** Tokenise `source`; on an error returns false and fills `error`. */
bool tokenize(const std::string &source, std::vector<Token> *out, std::string *error);

}  // namespace wgsl
}  // namespace xtgui

#endif /* XT_GUI_WGSL_LEXER_H */
