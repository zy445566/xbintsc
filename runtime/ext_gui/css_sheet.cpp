/*
 * xbintsc GUI engine — CSS declaration and stylesheet parsing.
 *
 * At-rules are no longer all skipped: `@media` blocks are parsed recursively
 * and each rule keeps its (possibly nested) query, which the cascade evaluates
 * against the viewport. Every other at-rule is ignored with its block.
 */

#include "css_internal.h"

#include <algorithm>
#include <cctype>

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

/** Strip matching quotes from an at-rule prelude name. */
std::string unquote(std::string text) {
  text = xt_css_trim(text);
  if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
      text.back() == text.front()) {
    text = text.substr(1, text.size() - 2);
  }
  return text;
}

bool parseKeyframeOffset(const std::string &text, float *offset) {
  std::string token = xt_css_lower(xt_css_trim(text));
  if (token == "from") {
    *offset = 0.0f;
    return true;
  }
  if (token == "to") {
    *offset = 1.0f;
    return true;
  }
  if (!token.empty() && token.back() == '%') {
    float value = 0.0f;
    if (!xt_css_parse_number(token.substr(0, token.size() - 1), &value)) return false;
    *offset = value / 100.0f;
    return true;
  }
  return false;
}

/** Parse the body of an `@keyframes` block into one rule. A selector list such
 * as `0%, 50%` creates one step per offset with the same declarations. */
void parseKeyframesBlock(const std::string &block, const std::string &prelude,
                         StyleSheet *sheet) {
  KeyframesRule rule;
  rule.name = unquote(prelude);
  if (rule.name.empty()) return;
  size_t index = 0;
  while (index < block.size()) {
    while (index < block.size() && xt_is_css_space(block[index])) index++;
    if (index >= block.size()) break;
    size_t brace = block.find('{', index);
    if (brace == std::string::npos) break;
    size_t close = findBlockEnd(block, brace);
    if (close == std::string::npos) break;
    std::string selectors = block.substr(index, brace - index);
    std::vector<Declaration> declarations =
        xt_css_parse_declarations(block.substr(brace + 1, close - brace - 1));
    for (const std::string &part : xt_css_split_commas(selectors)) {
      float offset = 0.0f;
      if (!parseKeyframeOffset(part, &offset)) continue;
      Keyframe keyframe;
      keyframe.offset = offset;
      keyframe.declarations = declarations;
      rule.keyframes.push_back(std::move(keyframe));
    }
    index = close + 1;
  }
  if (rule.keyframes.empty()) return;
  std::stable_sort(rule.keyframes.begin(), rule.keyframes.end(),
                   [](const Keyframe &a, const Keyframe &b) { return a.offset < b.offset; });
  sheet->keyframes.push_back(std::move(rule));
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
        } else if (name.size() >= 9 && name.compare(name.size() - 9, 9, "keyframes") == 0) {
          parseKeyframesBlock(text.substr(brace + 1, close - brace - 1),
                              text.substr(nameEnd, brace - nameEnd), sheet);
        }
        /* Other block at-rules (`@supports`, `@font-face`, …) are not part of
         * the engine's subset: ignore the whole block. */
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

/** Record the sheet-wide facts the cascade consults per run (see `StyleSheet`).
 * `:hover`/`:focus` are matched by substring so a nested `:not(:hover)` rule is
 * still recognised — a false positive only costs the restyle that is already
 * being skipped. */
void computeSheetFlags(StyleSheet *sheet) {
  for (const Rule &rule : sheet->rules) {
    for (const ComplexSelector &selector : rule.selectors) {
      for (const CompoundSelector &compound : selector.compounds) {
        if (!compound.pseudo_element.empty()) sheet->has_pseudo_elements = true;
        for (const std::string &pseudo : compound.pseudoClasses) {
          if (pseudo.find("hover") != std::string::npos) sheet->has_hover = true;
          if (pseudo.find("focus") != std::string::npos) sheet->has_focus = true;
        }
      }
    }
    for (const Declaration &declaration : rule.declarations) {
      if (declaration.property.compare(0, 10, "transition") == 0) sheet->has_transitions = true;
      if (declaration.property.compare(0, 9, "animation") == 0) sheet->has_animations = true;
    }
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

const KeyframesRule *xt_css_find_keyframes(const StyleSheet &sheet, const std::string &name) {
  /* Later `@keyframes` blocks with the same name replace earlier ones. */
  for (size_t i = sheet.keyframes.size(); i-- > 0;) {
    if (sheet.keyframes[i].name == name) return &sheet.keyframes[i];
  }
  return nullptr;
}

StyleSheet xt_css_parse_stylesheet(const std::string &text) {
  StyleSheet sheet;
  std::string clean = stripComments(text);
  int order = 0;
  parseRules(clean, "", &order, &sheet);
  computeSheetFlags(&sheet);
  return sheet;
}

}  // namespace xtgui
