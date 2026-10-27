/*
 * xbintsc GUI engine — `#pragma unroll_loop_start` expansion.
 *
 * three.js marks its material light loops with
 *
 *     #pragma unroll_loop_start
 *     for ( int i = 0; i < N; i ++ ) { ... UNROLLED_LOOP_INDEX ... [ i ] ... }
 *     #pragma unroll_loop_end
 *
 * and expands them in JavaScript before upload. On this runtime the expansion
 * is best done here: the loop body indexes sampler arrays (`shadowMap[ i ]`),
 * and Vulkan GLSL only accepts a *constant* sampler-array index. Expanding the
 * pragma turns every `i` (and `UNROLLED_LOOP_INDEX`) into a literal, so the
 * shadow-map chunks compile without relying on the regex engine or on three.js
 * having unrolled them first. WebGL ignores the unknown pragmas either way.
 *
 * The transformation is deliberately small: only the `for (int i = A; i < B;
 * i ++)` shape three.js emits is recognised. Anything else is left untouched.
 */

#include "webgl_glsl.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace xtgui {
namespace {

const char *const kStartPragma = "#pragma unroll_loop_start";
const char *const kEndPragma = "#pragma unroll_loop_end";

/** Skip whitespace and comments starting at `at`. */
size_t skip_space(const std::string &text, size_t at) {
  while (at < text.size()) {
    const char c = text[at];
    if (std::isspace((unsigned char)c) != 0) {
      at++;
      continue;
    }
    if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
      at += 2;
      while (at < text.size() && text[at] != '\n') at++;
      continue;
    }
    if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') {
      at += 2;
      while (at + 1 < text.size() && !(text[at] == '*' && text[at + 1] == '/')) at++;
      if (at + 1 < text.size()) at += 2;
      continue;
    }
    break;
  }
  return at;
}

/** Index just past the end of a `//` / `#` line starting at `at`. */
size_t line_end(const std::string &text, size_t at) {
  while (at < text.size() && text[at] != '\n') at++;
  return at;
}

/** Match the `}` for the `{` at `open`, skipping comments. */
size_t matching_brace(const std::string &text, size_t open, size_t *out) {
  int depth = 0;
  for (size_t i = open; i < text.size(); i++) {
    const char c = text[i];
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      i += 2;
      while (i < text.size() && text[i] != '\n') i++;
      if (i < text.size()) continue;
      break;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      i += 2;
      while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) i++;
      if (i + 1 < text.size()) i++;
      continue;
    }
    if (c == '{') depth++;
    if (c == '}') {
      depth--;
      if (depth == 0) {
        *out = i;
        return i;
      }
    }
  }
  return std::string::npos;
}

struct LoopHeader {
  std::string variable;
  long start = 0;
  long end = 0;
  bool inclusive = false;
};

/** Tokenise a for-header into identifiers, numbers and punctuation. */
std::vector<std::string> tokens(const std::string &text) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (std::isspace((unsigned char)c) != 0) {
      i++;
      continue;
    }
    if (std::isalnum((unsigned char)c) != 0 || c == '_') {
      size_t end = i + 1;
      while (end < text.size() &&
             (std::isalnum((unsigned char)text[end]) != 0 || text[end] == '_')) {
        end++;
      }
      out.push_back(text.substr(i, end - i));
      i = end;
      continue;
    }
    if ((c == '+' && i + 1 < text.size() && text[i + 1] == '+') ||
        (c == '-' && i + 1 < text.size() && text[i + 1] == '-')) {
      out.push_back(text.substr(i, 2));
      i += 2;
      continue;
    }
    if ((c == '+' || c == '-' || c == '<' || c == '=' || c == '!') && i + 1 < text.size() &&
        text[i + 1] == '=') {
      out.push_back(text.substr(i, 2));
      i += 2;
      continue;
    }
    out.push_back(std::string(1, c));
    i++;
  }
  return out;
}

bool parse_header(const std::string &header, LoopHeader *out) {
  std::vector<std::string> parts = tokens(header);
  size_t at = 0;
  /* Optional qualifiers (`int`, `uint`, `highp int`, …). */
  while (at < parts.size() && (parts[at] == "int" || parts[at] == "uint" || parts[at] == "highp" ||
                              parts[at] == "mediump" || parts[at] == "lowp")) {
    at++;
  }
  if (at >= parts.size()) return false;
  out->variable = parts[at++];
  if (at >= parts.size() || parts[at] != "=") return false;
  at++;
  if (at >= parts.size()) return false;
  char *end = nullptr;
  out->start = std::strtol(parts[at].c_str(), &end, 10);
  if (end == nullptr || *end != 0) return false;
  at++;
  if (at >= parts.size() || parts[at] != ";") return false;
  at++;
  /* `i < N` / `i <= N`; the repeated variable may be spelled differently. */
  if (at >= parts.size()) return false;
  at++;
  if (at >= parts.size()) return false;
  out->inclusive = parts[at] == "<=";
  if (!out->inclusive && parts[at] != "<") return false;
  at++;
  if (at >= parts.size()) return false;
  out->end = std::strtol(parts[at].c_str(), &end, 10);
  if (end == nullptr || *end != 0) return false;
  at++;
  if (at >= parts.size() || parts[at] != ";") return false;
  at++;
  if (at >= parts.size()) return false;
  at++;
  if (at >= parts.size() || parts[at] != "++") return false;
  return true;
}

/** Replace whole-word `name` with `value`. */
std::string substitute(const std::string &body, const std::string &name, long value) {
  const std::string text = std::to_string(value);
  std::string out;
  out.reserve(body.size());
  size_t i = 0;
  while (i < body.size()) {
    const char c = body[i];
    if (std::isalpha((unsigned char)c) == 0 && c != '_') {
      out += c;
      i++;
      continue;
    }
    size_t end = i + 1;
    while (end < body.size() &&
           (std::isalnum((unsigned char)body[end]) != 0 || body[end] == '_')) {
      end++;
    }
    const std::string word = body.substr(i, end - i);
    if (word == name || word == "UNROLLED_LOOP_INDEX") {
      out += text;
    } else {
      out += word;
    }
    i = end;
  }
  return out;
}

}  // namespace

std::string unroll_glsl_pragmas(const std::string &source) {
  if (source.find(kStartPragma) == std::string::npos) return source;
  std::string result;
  size_t cursor = 0;
  while (cursor < source.size()) {
    const size_t pragma = source.find(kStartPragma, cursor);
    if (pragma == std::string::npos) {
      result.append(source, cursor, std::string::npos);
      break;
    }
    result.append(source, cursor, pragma - cursor);
    size_t at = line_end(source, pragma + std::string(kStartPragma).size());
    const size_t after_header = skip_space(source, at);
    if (source.compare(after_header, 3, "for") != 0) {
      /* Not the shape three.js emits: keep the pragma and move on. */
      result.append(source, pragma, at - pragma);
      cursor = at;
      continue;
    }
    size_t open_paren = skip_space(source, after_header + 3);
    if (open_paren >= source.size() || source[open_paren] != '(') {
      result.append(source, pragma, at - pragma);
      cursor = at;
      continue;
    }
    int depth = 0;
    size_t close_paren = open_paren;
    for (; close_paren < source.size(); close_paren++) {
      if (source[close_paren] == '(') depth++;
      if (source[close_paren] == ')') {
        depth--;
        if (depth == 0) break;
      }
    }
    if (close_paren >= source.size()) {
      result.append(source, pragma, at - pragma);
      cursor = at;
      continue;
    }
    LoopHeader loop;
    if (!parse_header(source.substr(open_paren + 1, close_paren - open_paren - 1), &loop) ||
        loop.end < loop.start || loop.end - loop.start > 4096) {
      result.append(source, pragma, at - pragma);
      cursor = at;
      continue;
    }
    size_t open_brace = skip_space(source, close_paren + 1);
    if (open_brace >= source.size() || source[open_brace] != '{') {
      result.append(source, pragma, at - pragma);
      cursor = at;
      continue;
    }
    size_t close_brace = 0;
    if (matching_brace(source, open_brace, &close_brace) == std::string::npos) {
      result.append(source, pragma, at - pragma);
      cursor = at;
      continue;
    }
    size_t end = skip_space(source, close_brace + 1);
    if (source.compare(end, std::string(kEndPragma).size(), kEndPragma) != 0) {
      result.append(source, pragma, at - pragma);
      cursor = at;
      continue;
    }
    end = line_end(source, end + std::string(kEndPragma).size());
    const std::string body = source.substr(open_brace + 1, close_brace - open_brace - 1);
    const long last = loop.inclusive ? loop.end : loop.end - 1;
    for (long i = loop.start; i <= last; i++) {
      result += substitute(body, loop.variable, i);
      result += '\n';
    }
    cursor = end;
  }
  return result;
}

}  // namespace xtgui
