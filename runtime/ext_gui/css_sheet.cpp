/*
 * xbintsc GUI engine — CSS declaration and stylesheet parsing.
 *
 * At-rules are no longer all skipped: `@media` blocks are parsed recursively
 * and each rule keeps its (possibly nested) query, which the cascade evaluates
 * against the viewport. Every other at-rule is ignored with its block.
 */

#include "css_internal.h"

namespace xtgui {

namespace {

// Strip C-style block comments before the sheet is tokenised.
std::string stripComments(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      size_t end = text.find("*/", i + 2);
      i = (end == std::string::npos) ? text.size() : end + 1;
      continue;
    }
    out.push_back(text[i]);
  }
  return out;
}

/** Find the first `needle` at nesting depth zero (parens/brackets/quotes). */
size_t findTopLevel(const std::string &text, char needle, size_t from) {
  int depth = 0;
  char quote = 0;
  for (size_t i = from; i < text.size(); i++) {
    char c = text[i];
    if (quote != 0) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') quote = c;
    else if (c == '(' || c == '[') depth++;
    else if (c == ')' || c == ']') depth--;
    else if (c == needle && depth == 0) return i;
  }
  return std::string::npos;
}

/** The `}` matching the `{` at `open`, respecting strings and nested blocks. */
size_t findBlockEnd(const std::string &text, size_t open) {
  int depth = 0;
  char quote = 0;
  for (size_t i = open; i < text.size(); i++) {
    char c = text[i];
    if (quote != 0) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') quote = c;
    else if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

/** Join a nested `@media` prelude: distribute `inner` over an outer comma list. */
std::string combineMedia(const std::string &outer, const std::string &inner) {
  if (outer.empty()) return inner;
  if (inner.empty()) return outer;
  std::vector<std::string> outerParts = xt_css_split_commas(outer);
  std::vector<std::string> innerParts = xt_css_split_commas(inner);
  std::string combined;
  for (const std::string &o : outerParts) {
    for (const std::string &i : innerParts) {
      if (!combined.empty()) combined += ", ";
      combined += o + " and " + i;
    }
  }
  return combined;
}

std::string stripImportant(std::string value, bool *important) {
  size_t pos = value.rfind('!');
  if (pos == std::string::npos) return value;
  std::string flag = xt_css_lower(xt_css_trim(value.substr(pos + 1)));
  if (flag == "important") {
    *important = true;
    return xt_css_trim(value.substr(0, pos));
  }
  return value;
}

/** Parse one rule list, carrying the enclosing `@media` prelude. */
void parseRules(const std::string &text, const std::string &media, int *order,
                StyleSheet *sheet) {
  size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && xt_is_css_space(text[index])) index++;
    if (index >= text.size()) break;

    if (text[index] == '@') {
      size_t nameStart = index + 1;
      size_t nameEnd = nameStart;
      while (nameEnd < text.size() &&
             (std::isalnum((unsigned char)text[nameEnd]) || text[nameEnd] == '-'))
        nameEnd++;
      std::string name = xt_css_lower(text.substr(nameStart, nameEnd - nameStart));
      size_t brace = text.find('{', nameEnd);
      size_t semi = text.find(';', nameEnd);
      if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
        size_t close = findBlockEnd(text, brace);
        if (close == std::string::npos) break;
        if (name == "media") {
          std::string query = xt_css_trim(text.substr(nameEnd, brace - nameEnd));
          std::string nested = combineMedia(media, query);
          parseRules(text.substr(brace + 1, close - brace - 1), nested, order, sheet);
        }
        /* Other block at-rules (`@supports`, `@keyframes`, `@font-face`, …) are
         * not part of the engine's subset: ignore the whole block. */
        index = close + 1;
      } else {
        index = (semi == std::string::npos) ? text.size() : semi + 1;
      }
      continue;
    }

    size_t brace = text.find('{', index);
    if (brace == std::string::npos) break;
    size_t close = findBlockEnd(text, brace);
    if (close == std::string::npos) break;
    Rule rule;
    rule.selectors = xt_css_parse_selector_list(text.substr(index, brace - index));
    rule.declarations = xt_css_parse_declarations(text.substr(brace + 1, close - brace - 1));
    rule.order = (*order)++;
    rule.media = media;
    if (!rule.selectors.empty() && !rule.declarations.empty()) sheet->rules.push_back(std::move(rule));
    index = close + 1;
  }
}

}  // namespace

std::vector<Declaration> xt_css_parse_declarations(const std::string &text) {
  std::vector<Declaration> declarations;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = findTopLevel(text, ';', start);
    if (end == std::string::npos) end = text.size();
    std::string part = text.substr(start, end - start);
    start = end + 1;
    size_t colon = findTopLevel(part, ':', 0);
    if (colon == std::string::npos) continue;
    Declaration declaration;
    declaration.property = xt_css_lower(xt_css_trim(part.substr(0, colon)));
    declaration.value = stripImportant(xt_css_trim(part.substr(colon + 1)), &declaration.important);
    if (!declaration.property.empty() && !declaration.value.empty()) {
      declarations.push_back(std::move(declaration));
    }
  }
  return declarations;
}

StyleSheet xt_css_parse_stylesheet(const std::string &text) {
  StyleSheet sheet;
  std::string clean = stripComments(text);
  int order = 0;
  parseRules(clean, "", &order, &sheet);
  return sheet;
}

}  // namespace xtgui
