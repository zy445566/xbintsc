/*
 * xbintsc GUI engine — WGSL tokeniser implementation.
 */

#include "wgsl_lexer.h"

#include <cctype>

namespace xtgui {
namespace wgsl {
namespace {

bool is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_ident_char(char c) {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

void push(std::vector<Token> *out, Tok kind, std::string text, int line, int column) {
  Token token;
  token.kind = kind;
  token.text = std::move(text);
  token.line = line;
  token.column = column;
  out->push_back(std::move(token));
}

}  // namespace

bool tokenize(const std::string &source, std::vector<Token> *out, std::string *error) {
  out->clear();
  size_t at = 0;
  int line = 1;
  int column = 1;
  auto advance = [&](size_t count) {
    for (size_t i = 0; i < count && at < source.size(); i++) {
      if (source[at] == '\n') {
        line++;
        column = 1;
      } else {
        column++;
      }
      at++;
    }
  };

  while (at < source.size()) {
    const char c = source[at];
    if (c == '\n' || c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
      advance(1);
      continue;
    }
    /* Comments (WGSL allows nesting in block comments). */
    if (c == '/' && at + 1 < source.size() && source[at + 1] == '/') {
      while (at < source.size() && source[at] != '\n') advance(1);
      continue;
    }
    if (c == '/' && at + 1 < source.size() && source[at + 1] == '*') {
      const int start_line = line;
      const int start_column = column;
      advance(2);
      int depth = 1;
      while (at < source.size() && depth > 0) {
        if (source[at] == '/' && at + 1 < source.size() && source[at + 1] == '*') {
          depth++;
          advance(2);
        } else if (source[at] == '*' && at + 1 < source.size() && source[at + 1] == '/') {
          depth--;
          advance(2);
        } else {
          advance(1);
        }
      }
      if (depth > 0) {
        *error = std::to_string(start_line) + ":" + std::to_string(start_column) +
                 ": unterminated block comment";
        return false;
      }
      continue;
    }
    if (is_ident_start(c)) {
      const int start_line = line;
      const int start_column = column;
      size_t end = at;
      while (end < source.size() && is_ident_char(source[end])) end++;
      std::string text = source.substr(at, end - at);
      advance(end - at);
      push(out, Tok::Ident, std::move(text), start_line, start_column);
      continue;
    }
    if (is_digit(c) || (c == '.' && at + 1 < source.size() && is_digit(source[at + 1]))) {
      const int start_line = line;
      const int start_column = column;
      const size_t begin = at;
      bool is_float = false;
      if (c == '0' && at + 1 < source.size() && (source[at + 1] == 'x' || source[at + 1] == 'X')) {
        advance(2);
        while (at < source.size() && (is_digit(source[at]) || (std::tolower((unsigned char)source[at]) >= 'a' &&
                                                                std::tolower((unsigned char)source[at]) <= 'f'))) {
          advance(1);
        }
        while (at < source.size() && (source[at] == 'i' || source[at] == 'u')) advance(1);
        push(out, Tok::Int, source.substr(begin, at - begin), start_line, start_column);
        continue;
      }
      while (at < source.size() && is_digit(source[at])) advance(1);
      if (at < source.size() && source[at] == '.') {
        is_float = true;
        advance(1);
        while (at < source.size() && is_digit(source[at])) advance(1);
      }
      if (at < source.size() && (source[at] == 'e' || source[at] == 'E')) {
        is_float = true;
        advance(1);
        if (at < source.size() && (source[at] == '+' || source[at] == '-')) advance(1);
        while (at < source.size() && is_digit(source[at])) advance(1);
      }
      if (at < source.size() &&
          (source[at] == 'f' || source[at] == 'F' || source[at] == 'h' || source[at] == 'H')) {
        is_float = true;
        advance(1);
      } else if (at < source.size() && (source[at] == 'i' || source[at] == 'u')) {
        advance(1);
      }
      push(out, is_float ? Tok::Float : Tok::Int, source.substr(begin, at - begin), start_line,
           start_column);
      continue;
    }
    if (c == '@') {
      advance(1);
      const int start_line = line;
      const int start_column = column;
      size_t end = at;
      while (end < source.size() && is_ident_char(source[end])) end++;
      if (end == at) {
        *error = std::to_string(start_line) + ":" + std::to_string(start_column - 1) +
                 ": expected an attribute name after '@'";
        return false;
      }
      std::string text = "@" + source.substr(at, end - at);
      advance(end - at);
      push(out, Tok::Ident, std::move(text), start_line, start_column);
      continue;
    }
    const int start_line = line;
    const int start_column = column;
    static const char *const kPunct[] = {
        "<<=", ">>=", "->", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>",
        "+=",  "-=",  "*=", "/=", "%=", "&=", "|=", "^=", "++", "--"};
    bool matched = false;
    for (const char *candidate : kPunct) {
      const std::string text(candidate);
      if (source.compare(at, text.size(), text) == 0) {
        push(out, Tok::Punct, text, start_line, start_column);
        advance(text.size());
        matched = true;
        break;
      }
    }
    if (matched) continue;
    if (std::string("(){}[]<>=+-*/%&|^!~;:,.?").find(c) != std::string::npos) {
      push(out, Tok::Punct, std::string(1, c), start_line, start_column);
      advance(1);
      continue;
    }
    *error = std::to_string(line) + ":" + std::to_string(column) + ": unexpected character '" +
             std::string(1, c) + "'";
    return false;
  }
  push(out, Tok::End, std::string(), line, column);
  return true;
}

}  // namespace wgsl
}  // namespace xtgui
