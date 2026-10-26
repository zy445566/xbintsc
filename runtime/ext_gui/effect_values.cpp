/*
 * xbintsc GUI engine — CSS transition declarations.
 *
 * Kept apart from `style_values.cpp` so the declaration dispatcher stays under
 * the repository's per-file line budget.
 */
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "css_internal.h"
#include "style_internal.h"
#include "style_values_internal.h"

namespace xtgui {

/* -- transitions ---------------------------------------------------------- */

static bool parseTimeSeconds(const std::string &text, float *out) {
  std::string token = xt_css_trim(text);
  if (token.empty()) return false;
  const char *begin = token.c_str();
  char *end = nullptr;
  double value = std::strtod(begin, &end);
  if (end == begin) return false;
  std::string unit = xt_css_lower(xt_css_trim(std::string(end)));
  if (unit == "ms") {
    *out = (float)(value / 1000.0);
  } else if (unit.empty() || unit == "s") {
    *out = (float)value;
  } else {
    return false;
  }
  return true;
}

static bool parseTimingFunction(const std::string &text, TimingFunction *out) {
  std::string token = xt_css_lower(xt_css_trim(text));
  if (token == "linear") {
    *out = TimingFunction::Linear;
  } else if (token == "ease") {
    *out = TimingFunction::Ease;
  } else if (token == "ease-in") {
    *out = TimingFunction::EaseIn;
  } else if (token == "ease-out") {
    *out = TimingFunction::EaseOut;
  } else if (token == "ease-in-out") {
    *out = TimingFunction::EaseInOut;
  } else {
    return false;
  }
  return true;
}

static std::vector<std::string> splitCommas(const std::string &text) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); i++) {
    if (i == text.size() || text[i] == ',') {
      std::string part = xt_css_trim(text.substr(start, i - start));
      if (!part.empty()) parts.push_back(part);
      start = i + 1;
    }
  }
  return parts;
}

static void parseTransitionShorthand(const std::string &value, std::vector<TransitionSpec> *out) {
  out->clear();
  for (const std::string &group : splitCommas(value)) {
    std::vector<std::string> tokens = xt_css_split_whitespace(group);
    if (tokens.empty()) continue;
    TransitionSpec spec;
    int time_seen = 0;
    for (const std::string &token : tokens) {
      float seconds = 0;
      TimingFunction timing = TimingFunction::Ease;
      if (parseTimeSeconds(token, &seconds)) {
        if (time_seen == 0) {
          spec.duration = seconds;
        } else {
          spec.delay = seconds;
        }
        time_seen++;
      } else if (parseTimingFunction(token, &timing)) {
        spec.timing = timing;
      } else {
        spec.property = xt_css_lower(token);
      }
    }
    out->push_back(spec);
  }
}


/* -- filters, shadows and generated content -------------------------------- */

/** Split on whitespace outside function parentheses (`rgb(1, 2, 3)` stays one). */
static std::vector<std::string> splitShadowTokens(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && depth == 0) {
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

static bool parseFilterAmount(const std::string &raw, float *out) {
  std::string text = xt_css_trim(raw);
  bool percent = !text.empty() && text.back() == '%';
  if (percent) text.pop_back();
  float number = 0;
  if (!xt_css_parse_number(text, &number)) return false;
  *out = percent ? number / 100.0f : number;
  return true;
}

static bool parseAngleDegrees(const std::string &raw, float *out) {
  std::string text = xt_css_lower(xt_css_trim(raw));
  struct Unit {
    const char *suffix;
    float scale;
  };
  static const Unit units[] = {{"deg", 1.0f}, {"grad", 0.9f}, {"rad", 57.2957795f},
                               {"turn", 360.0f}};
  for (const Unit &unit : units) {
    size_t length = std::strlen(unit.suffix);
    if (text.size() <= length || text.compare(text.size() - length, length, unit.suffix) != 0)
      continue;
    float number = 0;
    if (!xt_css_parse_number(text.substr(0, text.size() - length), &number)) return false;
    *out = number * unit.scale;
    return true;
  }
  if (xt_css_parse_number(text, out)) return true;
  return false;
}

static bool parseShadowLengths(const std::string &value, Length *dx, Length *dy, Length *blur,
                               Color *color, bool *hasColor) {
  std::vector<std::string> tokens = splitShadowTokens(value);
  Length lengths[3];
  size_t count = 0;
  for (const std::string &token : tokens) {
    Length length = xt_css_parse_length(token);
    if (length.unit != Unit::Invalid && length.unit != Unit::Auto && length.unit != Unit::None) {
      if (count < 3) lengths[count++] = length;
      continue;
    }
    Color parsed = xt_css_parse_color(token);
    if (parsed.valid) {
      *color = parsed;
      *hasColor = true;
    }
  }
  if (count < 2) return false;
  *dx = lengths[0];
  *dy = lengths[1];
  *blur = count > 2 ? lengths[2] : Length{0.0f, Unit::Px};
  return true;
}

static bool parseFilterList(const std::string &value, std::vector<FilterOp> *out) {
  std::string text = xt_css_trim(value);
  std::string lower = xt_css_lower(text);
  if (lower == "none" || text.empty()) {
    out->clear();
    return true;
  }
  out->clear();
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() &&
           (xt_is_css_space(text[i]) || text[i] == ','))
      i++;
    size_t nameStart = i;
    while (i < text.size() && (std::isalpha((unsigned char)text[i]) || text[i] == '-')) i++;
    if (i == nameStart) break;
    std::string name = xt_css_lower(text.substr(nameStart, i - nameStart));
    while (i < text.size() && xt_is_css_space(text[i])) i++;
    if (i >= text.size() || text[i] != '(') break;
    int depth = 0;
    size_t open = i;
    size_t close = std::string::npos;
    for (; i < text.size(); i++) {
      if (text[i] == '(') depth++;
      else if (text[i] == ')') {
        depth--;
        if (depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) break;
    std::string args = xt_css_trim(text.substr(open + 1, close - open - 1));
    i = close + 1;

    FilterOp op;
    bool known = true;
    if (name == "blur") {
      op.kind = FilterKind::Blur;
      Length length = xt_css_parse_length(args);
      if (length.unit == Unit::Invalid) known = false;
      else op.blur = length;
    } else if (name == "drop-shadow") {
      op.kind = FilterKind::DropShadow;
      if (!parseShadowLengths(args, &op.dx, &op.dy, &op.blur, &op.color, &op.has_color))
        known = false;
    } else if (name == "hue-rotate") {
      op.kind = FilterKind::HueRotate;
      if (!parseAngleDegrees(args, &op.amount)) known = false;
    } else {
      if (name == "brightness") op.kind = FilterKind::Brightness;
      else if (name == "contrast") op.kind = FilterKind::Contrast;
      else if (name == "grayscale") op.kind = FilterKind::Grayscale;
      else if (name == "sepia") op.kind = FilterKind::Sepia;
      else if (name == "invert") op.kind = FilterKind::Invert;
      else if (name == "saturate") op.kind = FilterKind::Saturate;
      else if (name == "opacity") op.kind = FilterKind::Opacity;
      else known = false;
      if (known && !parseFilterAmount(args, &op.amount)) known = false;
    }
    if (known) out->push_back(op);
  }
  return true;
}

static bool parseTextShadow(const std::string &value, std::vector<TextShadow> *out) {
  std::string text = xt_css_trim(value);
  std::string lower = xt_css_lower(text);
  out->clear();
  if (lower == "none" || text.empty()) return true;
  for (const std::string &part : splitCommas(value)) {
    TextShadow shadow;
    if (!parseShadowLengths(part, &shadow.dx, &shadow.dy, &shadow.blur, &shadow.color,
                            &shadow.has_color))
      return false;
    out->push_back(shadow);
  }
  return true;
}

bool xt_style_apply_effect(XtStyle *style, const std::string &property,
                           const std::string &value) {
  if (property == "filter") return parseFilterList(value, &style->filters);
  if (property == "backdrop-filter") return parseFilterList(value, &style->backdrop_filters);
  if (property == "text-shadow") return parseTextShadow(value, &style->text_shadow);
  if (property == "content") {
    std::string text = xt_css_lower(xt_css_trim(value));
    if (text == "none" || text == "normal") {
      style->has_content = false;
      style->content.clear();
    } else {
      style->has_content = true;
      style->content = xt_css_trim(value);
    }
    return true;
  }
  return false;
}

bool xt_style_apply_transition(XtStyle *style, const std::string &property,
                               const std::string &value) {
  if (property == "transition") {
    parseTransitionShorthand(value, &style->transitions);
    return true;
  }
  if (property == "transition-property") {
    std::vector<std::string> parts = splitCommas(value);
    style->transitions.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) style->transitions[i].property = xt_css_lower(parts[i]);
    return true;
  }
  if (property == "transition-duration" || property == "transition-delay") {
    std::vector<std::string> parts = splitCommas(value);
    style->transitions.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      float seconds = 0;
      if (!parseTimeSeconds(parts[i], &seconds)) continue;
      if (property == "transition-duration") style->transitions[i].duration = seconds;
      else style->transitions[i].delay = seconds;
    }
    return true;
  }
  if (property == "transition-timing-function") {
    std::vector<std::string> parts = splitCommas(value);
    style->transitions.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      TimingFunction timing = TimingFunction::Ease;
      if (parseTimingFunction(parts[i], &timing)) style->transitions[i].timing = timing;
    }
    return true;
  }
  return false;
}

}  // namespace xtgui
