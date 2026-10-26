/*
 * xbintsc GUI engine — `@media` query evaluation.
 *
 * The CSS parser keeps each rule's raw media prelude and this unit decides
 * whether it applies to the current viewport. The subset mirrors what a desktop
 * window can answer: width/height ranges, orientation, hover/pointer class and
 * the two accessibility preferences. Unknown features evaluate to false, which
 * is what a browser does (and keeps e.g. `@media print` blocks inert).
 */
#include "css_internal.h"

#include <cmath>
#include <string>
#include <vector>

namespace xtgui {

namespace {

std::string lowerTrim(const std::string &text) { return xt_css_lower(xt_css_trim(text)); }

/** Split `text` on the word `and` at nesting depth zero. */
std::vector<std::string> splitAnd(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (size_t i = 0; i < text.size();) {
    char c = text[i];
    if (c == '(') depth++;
    else if (c == ')') depth--;
    bool atWord = (c == 'a' || c == 'A') && i + 3 <= text.size() &&
                  xt_css_lower(text.substr(i, 3)) == "and" &&
                  (i == 0 || xt_is_css_space(text[i - 1])) &&
                  (i + 3 == text.size() || xt_is_css_space(text[i + 3]));
    if (atWord && depth == 0) {
      std::string part = xt_css_trim(current);
      if (!part.empty()) parts.push_back(part);
      current.clear();
      i += 3;
      continue;
    }
    current.push_back(c);
    i++;
  }
  std::string part = xt_css_trim(current);
  if (!part.empty()) parts.push_back(part);
  return parts;
}

/** Resolve a media-feature length. `rem`/`em` assume the 16px initial font. */
bool mediaLength(const std::string &raw, float vw, float vh, float *out) {
  Length length = xt_css_parse_length(raw);
  switch (length.unit) {
    case Unit::Px:
    case Unit::Number:
      *out = length.value;
      return true;
    case Unit::Em:
    case Unit::Rem:
      *out = length.value * 16.0f;
      return true;
    case Unit::Vw:
      *out = vw * length.value / 100.0f;
      return true;
    case Unit::Vh:
      *out = vh * length.value / 100.0f;
      return true;
    default:
      return false;
  }
}

/** Compare `actual` with a feature value, honouring a min-/max- prefix. */
bool compareLength(const std::string &name, const std::string &rawValue, float actual, float vw,
                   float vh) {
  float value = 0.0f;
  if (!mediaLength(rawValue, vw, vh, &value)) return false;
  const bool min = name.rfind("min-", 0) == 0;
  const bool max = name.rfind("max-", 0) == 0;
  if (min) return actual >= value;
  if (max) return actual <= value;
  return std::fabs(actual - value) < 0.01f;
}

bool splitFeature(const std::string &inner, std::string *name, std::string *value) {
  int depth = 0;
  for (size_t i = 0; i < inner.size(); i++) {
    char c = inner[i];
    if (c == '(') depth++;
    else if (c == ')') depth--;
    else if (c == ':' && depth == 0) {
      *name = lowerTrim(inner.substr(0, i));
      *value = lowerTrim(inner.substr(i + 1));
      return true;
    }
  }
  return false;
}

/** `(width >= 600px)` / `(600px <= width)` range syntax. */
bool evaluateRange(const std::string &inner, float vw, float vh, bool *matched) {
  static const char *const ops[] = {">=", "<=", ">", "<", "="};
  for (const char *op : ops) {
    size_t pos = inner.find(op);
    if (pos == std::string::npos) continue;
    std::string left = lowerTrim(inner.substr(0, pos));
    std::string right = lowerTrim(inner.substr(pos + std::string(op).size()));
    std::string opText(op);
    /* `600px < width` means width > 600px: flip the comparison. */
    bool flipped = left != "width" && left != "height";
    std::string feature = flipped ? right : left;
    std::string valueText = flipped ? left : right;
    float actual = 0.0f;
    if (feature == "width") actual = vw;
    else if (feature == "height") actual = vh;
    else return false;
    float value = 0.0f;
    if (!mediaLength(valueText, vw, vh, &value)) return false;
    if (flipped) {
      if (opText == ">") opText = "<";
      else if (opText == "<") opText = ">";
      else if (opText == ">=") opText = "<=";
      else if (opText == "<=") opText = ">=";
    }
    if (opText == ">=") *matched = actual >= value;
    else if (opText == "<=") *matched = actual <= value;
    else if (opText == ">") *matched = actual > value;
    else if (opText == "<") *matched = actual < value;
    else *matched = std::fabs(actual - value) < 0.01f;
    return true;
  }
  return false;
}

bool evaluateFeature(const std::string &inner, float vw, float vh) {
  bool rangeMatched = false;
  if (evaluateRange(inner, vw, vh, &rangeMatched)) return rangeMatched;

  std::string name;
  std::string value;
  if (!splitFeature(inner, &name, &value)) return false;
  if (name.empty() || value.empty()) return false;

  if (name == "width" || name == "min-width" || name == "max-width") {
    return compareLength(name, value, vw, vw, vh);
  }
  if (name == "height" || name == "min-height" || name == "max-height") {
    return compareLength(name, value, vh, vw, vh);
  }
  if (name == "orientation") {
    if (value == "landscape") return vw >= vh;
    if (value == "portrait") return vh > vw;
    return false;
  }
  if (name == "hover" || name == "any-hover") return value == "hover";
  if (name == "pointer" || name == "any-pointer") return value == "fine";
  if (name == "prefers-color-scheme") return value == "light";
  if (name == "prefers-reduced-motion") return value == "no-preference";
  if (name == "prefers-contrast") return value == "no-preference";
  if (name == "scripting") return value == "enabled";
  if (name == "display-mode") return value == "browser";
  return false;
}

}  // namespace

bool xt_css_media_matches(const std::string &query, float viewport_width, float viewport_height) {
  std::string text = lowerTrim(query);
  if (text.empty()) return true;

  bool any = false;
  for (const std::string &rawQuery : xt_css_split_commas(text)) {
    std::string part = lowerTrim(rawQuery);
    bool negate = false;
    if (part.rfind("not ", 0) == 0) {
      negate = true;
      part = xt_css_trim(part.substr(4));
    } else if (part.rfind("only ", 0) == 0) {
      part = xt_css_trim(part.substr(5));
    }
    bool matched = true;
    bool first = true;
    for (const std::string &segment : splitAnd(part)) {
      std::string token = xt_css_trim(segment);
      if (token.empty()) continue;
      if (first && token[0] != '(') {
        /* Media type: a desktop window is `screen`, never `print`. */
        if (token == "all" || token == "screen") {
          matched = true;
        } else {
          matched = false;
        }
      } else {
        std::string inner = token;
        if (inner.front() == '(' && inner.back() == ')') {
          inner = xt_css_trim(inner.substr(1, inner.size() - 2));
        }
        if (!evaluateFeature(inner, viewport_width, viewport_height)) matched = false;
      }
      first = false;
    }
    if (negate) matched = !matched;
    if (matched) {
      any = true;
      break;
    }
  }
  return any;
}

}  // namespace xtgui
