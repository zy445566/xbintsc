/*
 * xbintsc GUI engine — CSS declaration and stylesheet parsing.
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
  size_t index = 0;
  int order = 0;
  while (index < clean.size()) {
    while (index < clean.size() && xt_is_css_space(clean[index])) index++;
    if (index >= clean.size()) break;
    if (clean[index] == '@') {
      size_t brace = clean.find('{', index);
      size_t semi = clean.find(';', index);
      if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
        int depth = 0;
        size_t i = brace;
        for (; i < clean.size(); i++) {
          if (clean[i] == '{') depth++;
          else if (clean[i] == '}') {
            depth--;
            if (depth == 0) break;
          }
        }
        index = (i < clean.size()) ? i + 1 : clean.size();
      } else {
        index = (semi == std::string::npos) ? clean.size() : semi + 1;
      }
      continue;
    }
    size_t brace = clean.find('{', index);
    if (brace == std::string::npos) break;
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = brace; i < clean.size(); i++) {
      if (clean[i] == '{') depth++;
      else if (clean[i] == '}') {
        depth--;
        if (depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) break;
    Rule rule;
    rule.selectors = xt_css_parse_selector_list(clean.substr(index, brace - index));
    rule.declarations = xt_css_parse_declarations(clean.substr(brace + 1, close - brace - 1));
    rule.order = order++;
    if (!rule.selectors.empty() && !rule.declarations.empty()) sheet.rules.push_back(std::move(rule));
    index = close + 1;
  }
  return sheet;
}

}  // namespace xtgui
