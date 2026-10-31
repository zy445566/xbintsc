/*
 * xbintsc GUI engine — text-oriented CSS declarations.
 *
 * `letter-spacing`, `text-transform`, `writing-mode`, `vertical-align`, the
 * `font` shorthand and the font-feature hints live here so the main declaration
 * dispatcher stays under the repository's per-file line budget.
 */

#include "style_internal.h"
#include "style_values_internal.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace xtgui {

namespace {

std::string lowerTrim(const std::string &text) { return xt_css_lower(xt_css_trim(text)); }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

/** Split at whitespace outside `(...)` so `calc(1em + 2px)` stays one token. */
std::vector<std::string> splitTokens(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if (isSpace(c) && depth == 0) {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

/** `normal` is zero; lengths are kept symbolic for `finalizeStyle` (em is the
 * element's own font size, not the parent's). */
bool parseLetterSpacing(const std::string &value, Length *out) {
  std::string text = lowerTrim(value);
  if (text == "normal") {
    *out = Length{0.0f, Unit::Px};
    return true;
  }
  Length length = xt_css_parse_length(text);
  if (length.unit == Unit::Invalid || length.unit == Unit::Auto || length.unit == Unit::None ||
      length.unit == Unit::Percent) {
    return false;
  }
  *out = length;
  return true;
}

bool parseTextTransform(const std::string &value, TextTransform *out) {
  std::string text = lowerTrim(value);
  if (text == "none") *out = TextTransform::None;
  else if (text == "uppercase") *out = TextTransform::Uppercase;
  else if (text == "lowercase") *out = TextTransform::Lowercase;
  else if (text == "capitalize") *out = TextTransform::Capitalize;
  else return false;
  return true;
}

bool parseWritingMode(const std::string &value, WritingMode *out) {
  std::string text = lowerTrim(value);
  if (text == "horizontal-tb") *out = WritingMode::HorizontalTB;
  else if (text == "vertical-rl") *out = WritingMode::VerticalRL;
  else if (text == "vertical-lr") *out = WritingMode::VerticalLR;
  else return false;
  return true;
}

/** Only the length form is acted on; the keywords are accepted so a declaration
 * is not dropped (the used files only carry a length). */
bool parseVerticalAlign(const std::string &value, Length *out) {
  std::string text = lowerTrim(value);
  if (text == "baseline" || text == "top" || text == "middle" || text == "bottom" ||
      text == "text-top" || text == "text-bottom") {
    *out = Length{0.0f, Unit::Px};
    return true;
  }
  if (text == "sub") {
    *out = Length{-0.2f, Unit::Em};
    return true;
  }
  if (text == "super") {
    *out = Length{0.4f, Unit::Em};
    return true;
  }
  Length length = xt_css_parse_length(text);
  if (length.unit == Unit::Invalid || length.unit == Unit::Auto || length.unit == Unit::None) {
    return false;
  }
  *out = length;
  return true;
}

bool parseFontVariantNumeric(const std::string &value, bool *out) {
  std::vector<std::string> tokens = splitTokens(lowerTrim(value));
  if (tokens.empty()) return false;
  bool tabular = false;
  for (const std::string &token : tokens) {
    if (token == "tabular-nums" || token == "lining-nums" || token == "oldstyle-nums") tabular = true;
  }
  *out = tabular;
  return true;
}

bool isFontStyleKeyword(const std::string &token) {
  return token == "italic" || token == "oblique";
}

bool isFontWeightKeyword(const std::string &token) {
  return token == "bold" || token == "bolder" || token == "lighter" || token == "normal";
}

}  // namespace

std::string xt_apply_text_transform(const std::string &text, TextTransform transform) {
  if (transform == TextTransform::None) return text;
  std::string out = text;
  if (transform == TextTransform::Uppercase || transform == TextTransform::Lowercase) {
    for (char &c : out) {
      unsigned char byte = (unsigned char)c;
      if (byte >= 0x80) continue;  // Non-ASCII bytes are left alone.
      c = transform == TextTransform::Uppercase ? (char)std::toupper(byte)
                                                : (char)std::tolower(byte);
    }
    return out;
  }
  /* capitalize: the first ASCII letter of every whitespace-separated word. */
  bool wordStart = true;
  for (char &c : out) {
    unsigned char byte = (unsigned char)c;
    if (byte >= 0x80) {
      wordStart = false;
      continue;
    }
    if (std::isspace(byte)) {
      wordStart = true;
      continue;
    }
    if (wordStart) c = (char)std::toupper(byte);
    wordStart = false;
  }
  return out;
}

bool xt_style_apply_text_property(XtStyle *style, const std::string &property,
                                  const std::string &rawValue, const ApplyContext &context) {
  std::string value = xt_css_trim(rawValue);

  if (property == "letter-spacing") {
    parseLetterSpacing(value, &style->letter_spacing);
    return true;
  }
  if (property == "text-transform") {
    parseTextTransform(value, &style->text_transform);
    return true;
  }
  if (property == "writing-mode") {
    parseWritingMode(value, &style->writing_mode);
    return true;
  }
  if (property == "vertical-align") {
    parseVerticalAlign(value, &style->vertical_align);
    return true;
  }
  if (property == "aspect-ratio") {
    std::string text = lowerTrim(value);
    if (text == "auto") {
      style->aspect_ratio = 0.0f;
      return true;
    }
    float width = 0;
    float height = 1;
    size_t slash = text.find('/');
    if (slash == std::string::npos) {
      if (!xt_css_parse_number(text, &width) || width <= 0) return true;
    } else if (!xt_css_parse_number(xt_css_trim(text.substr(0, slash)), &width) ||
               !xt_css_parse_number(xt_css_trim(text.substr(slash + 1)), &height) || width <= 0 ||
               height <= 0) {
      return true;
    }
    style->aspect_ratio = width / height;
    return true;
  }
  if (property == "font-variant-numeric") {
    parseFontVariantNumeric(value, &style->font_variant_numeric);
    return true;
  }
  if (property == "text-wrap" || property == "text-rendering") {
    /* Accepted for author compatibility: line breaking is greedy and the text
     * backend has no rendering-quality switch of its own. */
    return true;
  }
  if (property != "font") return false;

  std::string lower = lowerTrim(value);
  if (lower == "inherit") {
    if (const XtStyle *parent = context.parent) {
      style->font_size = parent->font_size;
      style->font_weight = parent->font_weight;
      style->font_family = parent->font_family;
      style->font_style = parent->font_style;
      style->line_height = parent->line_height;
      style->letter_spacing = parent->letter_spacing;
      style->font_variant_numeric = parent->font_variant_numeric;
    }
    return true;
  }

  std::vector<std::string> tokens = splitTokens(value);
  if (tokens.empty()) return true;

  bool italic = false;
  int weight = 400;
  size_t index = 0;
  for (; index < tokens.size(); index++) {
    std::string token = xt_css_lower(tokens[index]);
    if (isFontStyleKeyword(token)) {
      italic = true;
      continue;
    }
    if (isFontWeightKeyword(token)) {
      if (token == "bold" || token == "bolder") weight = 700;
      else if (token == "lighter") weight = 100;
      else weight = 400;
      continue;
    }
    float number = 0;
    if (xt_css_parse_number(token, &number)) {
      weight = (int)number;
      continue;
    }
    break;
  }
  if (index >= tokens.size()) return true;

  /* `size[/line-height]`; a separate `/` token is tolerated. */
  std::string sizeToken = tokens[index];
  std::string lineToken;
  size_t slash = sizeToken.find('/');
  if (slash != std::string::npos) {
    lineToken = sizeToken.substr(slash + 1);
    sizeToken = sizeToken.substr(0, slash);
  }
  Length size = xt_css_parse_length(sizeToken);
  if (size.unit == Unit::Invalid || size.unit == Unit::Auto || size.unit == Unit::None) return true;
  index++;
  if (lineToken.empty() && index < tokens.size() && tokens[index] == "/") {
    index++;
    if (index < tokens.size()) lineToken = tokens[index++];
  }
  if (index >= tokens.size()) return true;

  std::string family;
  for (size_t i = index; i < tokens.size(); i++) {
    if (!family.empty()) family += " ";
    family += tokens[i];
  }

  style->font_style = italic ? FontStyle::Italic : FontStyle::Normal;
  style->font_weight = weight;
  style->font_size = xt_style_resolve_absolute(size, context.parentFontSize,
                                               context.parentFontSize, context.rootFontSize,
                                               context.viewportWidth, context.viewportHeight);
  if (style->font_size <= 0.0f) style->font_size = 16.0f;
  if (!lineToken.empty()) {
    std::string text = xt_css_lower(lineToken);
    float number = 0;
    bool hasUnit = false;
    for (const char *suffix : {"px", "%", "em", "rem", "vw", "vh", "pt", "pc", "in", "cm", "mm", "q"}) {
      size_t length = std::strlen(suffix);
      if (text.size() >= length && text.compare(text.size() - length, length, suffix) == 0) {
        hasUnit = true;
        break;
      }
    }
    if (!hasUnit && xt_css_parse_number(text, &number)) {
      style->line_height = {number, Unit::Number};
    } else {
      Length line = xt_css_parse_length(text);
      if (line.unit != Unit::Invalid) style->line_height = line;
    }
  } else {
    style->line_height = Length{};
  }
  style->font_family = family;
  return true;
}

}  // namespace xtgui
