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

static std::vector<std::string> splitCommas(const std::string &text);
static std::vector<std::string> splitTokens(const std::string &text);

/** Parse a CSS timing function. Besides the five keywords this accepts
 * `cubic-bezier(x1, y1, x2, y2)` and `steps(n[, start|end|jump-*])`. */
static bool parseTimingFunction(const std::string &text, TimingFunction *out) {
  std::string token = xt_css_lower(xt_css_trim(text));
  if (token == "linear") {
    *out = TimingFunction();
    out->kind = TimingFunction::Kind::Linear;
  } else if (token == "ease") {
    *out = TimingFunction();
  } else if (token == "ease-in") {
    *out = TimingFunction();
    out->kind = TimingFunction::Kind::EaseIn;
  } else if (token == "ease-out") {
    *out = TimingFunction();
    out->kind = TimingFunction::Kind::EaseOut;
  } else if (token == "ease-in-out") {
    *out = TimingFunction();
    out->kind = TimingFunction::Kind::EaseInOut;
  } else if (token == "step-start") {
    *out = TimingFunction();
    out->kind = TimingFunction::Kind::StepsStart;
    out->steps = 1;
  } else if (token == "step-end") {
    *out = TimingFunction();
    out->kind = TimingFunction::Kind::StepsEnd;
    out->steps = 1;
  } else if (token.compare(0, 13, "cubic-bezier(") == 0 && token.back() == ')') {
    std::vector<std::string> args =
        splitCommas(token.substr(13, token.size() - 14));
    if (args.size() != 4) return false;
    float values[4];
    for (int i = 0; i < 4; i++) {
      if (!xt_css_parse_number(args[i], &values[i])) return false;
    }
    TimingFunction fn;
    fn.kind = TimingFunction::Kind::CubicBezier;
    fn.x1 = values[0] < 0.0f ? 0.0f : (values[0] > 1.0f ? 1.0f : values[0]);
    fn.y1 = values[1];
    fn.x2 = values[2] < 0.0f ? 0.0f : (values[2] > 1.0f ? 1.0f : values[2]);
    fn.y2 = values[3];
    *out = fn;
  } else if (token.compare(0, 6, "steps(") == 0 && token.back() == ')') {
    std::string inner = token.substr(6, token.size() - 7);
    for (char &c : inner) {
      if (c == ',') c = ' ';
    }
    std::vector<std::string> args = xt_css_split_whitespace(inner);
    if (args.empty()) return false;
    float count = 0;
    if (!xt_css_parse_number(args[0], &count) || count < 1.0f) return false;
    bool stepStart = false;
    if (args.size() > 1) {
      std::string position = xt_css_lower(args[1]);
      if (position == "start" || position == "jump-start" || position == "jump-both") {
        stepStart = true;
      } else if (position != "end" && position != "jump-end" && position != "jump-none") {
        return false;
      }
    }
    TimingFunction fn;
    fn.kind = stepStart ? TimingFunction::Kind::StepsStart : TimingFunction::Kind::StepsEnd;
    fn.steps = (int)(count + 0.5f);
    *out = fn;
  } else {
    return false;
  }
  return true;
}

static std::vector<std::string> splitCommas(const std::string &text) {
  /* Top-level commas only: a timing function or colour function may contain
   * its own comma (`steps(4, end)`, `rgb(1, 2, 3)`). */
  return xt_css_split_commas(text);
}

static void parseTransitionShorthand(const std::string &value, std::vector<TransitionSpec> *out) {
  out->clear();
  for (const std::string &group : splitCommas(value)) {
    std::vector<std::string> tokens = splitTokens(group);
    if (tokens.empty()) continue;
    TransitionSpec spec;
    int time_seen = 0;
    for (const std::string &token : tokens) {
      float seconds = 0;
      TimingFunction timing;
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
static std::vector<std::string> splitTokens(const std::string &text) {
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
  std::vector<std::string> tokens = splitTokens(value);
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

/** Parse a comma-separated `box-shadow` list. Offsets are `<dx> <dy>
 * [<blur> [<spread>]]`, and the optional `inset` keyword plus a colour may
 * appear anywhere in the entry, as in CSS. */
static bool parseBoxShadow(const std::string &value, std::vector<BoxShadow> *out) {
  std::string text = xt_css_trim(value);
  std::string lower = xt_css_lower(text);
  out->clear();
  if (lower == "none" || text.empty()) return true;
  for (const std::string &part : splitCommas(value)) {
    BoxShadow shadow;
    Length lengths[4];
    size_t count = 0;
    for (const std::string &token : splitTokens(part)) {
      std::string tokenLower = xt_css_lower(token);
      if (tokenLower == "inset") {
        shadow.inset = true;
        continue;
      }
      Length length = xt_css_parse_length(token);
      if (length.unit != Unit::Invalid && length.unit != Unit::Auto &&
          length.unit != Unit::None) {
        if (count < 4) lengths[count++] = length;
        continue;
      }
      Color color = xt_css_parse_color(token);
      if (color.valid) {
        shadow.color = color;
        shadow.has_color = true;
      }
    }
    if (count < 2) return false;
    shadow.dx = lengths[0];
    shadow.dy = lengths[1];
    shadow.blur = count > 2 ? lengths[2] : Length{0.0f, Unit::Px};
    shadow.spread = count > 3 ? lengths[3] : Length{0.0f, Unit::Px};
    out->push_back(shadow);
  }
  return true;
}

static bool parseAnimationShorthand(const std::string &value, std::vector<AnimationSpec> *out) {
  out->clear();
  for (const std::string &group : splitCommas(value)) {
    std::vector<std::string> tokens = splitTokens(group);
    if (tokens.empty()) continue;
    AnimationSpec spec;
    int time_seen = 0;
    for (const std::string &token : tokens) {
      std::string lower = xt_css_lower(token);
      float seconds = 0;
      TimingFunction timing;
      if (parseTimeSeconds(token, &seconds)) {
        if (time_seen == 0) spec.duration = seconds;
        else if (time_seen == 1) spec.delay = seconds;
        time_seen++;
      } else if (parseTimingFunction(token, &timing)) {
        spec.timing = timing;
      } else if (lower == "infinite") {
        spec.iterations = -1.0f;
      } else if (lower == "normal") {
        spec.direction = AnimationDirection::Normal;
      } else if (lower == "reverse") {
        spec.direction = AnimationDirection::Reverse;
      } else if (lower == "alternate") {
        spec.direction = AnimationDirection::Alternate;
      } else if (lower == "alternate-reverse") {
        spec.direction = AnimationDirection::AlternateReverse;
      } else if (lower == "none") {
        spec.fill = AnimationFillMode::None;
      } else if (lower == "forwards") {
        spec.fill = AnimationFillMode::Forwards;
      } else if (lower == "backwards") {
        spec.fill = AnimationFillMode::Backwards;
      } else if (lower == "both") {
        spec.fill = AnimationFillMode::Both;
      } else if (lower == "paused") {
        spec.paused = true;
      } else if (lower == "running") {
        spec.paused = false;
      } else {
        float count = 0;
        if (xt_css_parse_number(token, &count)) spec.iterations = count;
        else spec.name = token;
      }
    }
    out->push_back(spec);
  }
  return true;
}

bool xt_style_apply_effect(XtStyle *style, const std::string &property,
                           const std::string &value) {
  if (property == "filter") return parseFilterList(value, &style->filters);
  if (property == "backdrop-filter") return parseFilterList(value, &style->backdrop_filters);
  if (property == "text-shadow") return parseTextShadow(value, &style->text_shadow);
  if (property == "box-shadow") return parseBoxShadow(value, &style->box_shadow);
  if (property == "animation") {
    parseAnimationShorthand(value, &style->animations);
    return true;
  }
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
      TimingFunction timing;
      if (parseTimingFunction(parts[i], &timing)) style->transitions[i].timing = timing;
    }
    return true;
  }
  return false;
}

bool xt_style_apply_animation(XtStyle *style, const std::string &property,
                              const std::string &value) {
  if (property == "animation-name") {
    std::vector<std::string> parts = splitCommas(value);
    style->animations.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      style->animations[i].name = parts[i];
    }
    return true;
  }
  if (property == "animation-duration" || property == "animation-delay") {
    std::vector<std::string> parts = splitCommas(value);
    style->animations.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      float seconds = 0;
      if (!parseTimeSeconds(parts[i], &seconds)) continue;
      if (property == "animation-duration") style->animations[i].duration = seconds;
      else style->animations[i].delay = seconds;
    }
    return true;
  }
  if (property == "animation-timing-function") {
    std::vector<std::string> parts = splitCommas(value);
    style->animations.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      TimingFunction timing;
      if (parseTimingFunction(parts[i], &timing)) style->animations[i].timing = timing;
    }
    return true;
  }
  if (property == "animation-iteration-count") {
    std::vector<std::string> parts = splitCommas(value);
    style->animations.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      std::string lower = xt_css_lower(parts[i]);
      float count = 0;
      if (lower == "infinite") style->animations[i].iterations = -1.0f;
      else if (xt_css_parse_number(parts[i], &count)) style->animations[i].iterations = count;
    }
    return true;
  }
  if (property == "animation-direction") {
    static const char *const names[] = {"normal", "reverse", "alternate", "alternate-reverse"};
    std::vector<std::string> parts = splitCommas(value);
    style->animations.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      std::string lower = xt_css_lower(parts[i]);
      for (int d = 0; d < 4; d++) {
        if (lower == names[d]) style->animations[i].direction = (AnimationDirection)d;
      }
    }
    return true;
  }
  if (property == "animation-fill-mode") {
    static const char *const names[] = {"none", "forwards", "backwards", "both"};
    std::vector<std::string> parts = splitCommas(value);
    style->animations.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      std::string lower = xt_css_lower(parts[i]);
      for (int f = 0; f < 4; f++) {
        if (lower == names[f]) style->animations[i].fill = (AnimationFillMode)f;
      }
    }
    return true;
  }
  if (property == "animation-play-state") {
    std::vector<std::string> parts = splitCommas(value);
    style->animations.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      style->animations[i].paused = xt_css_lower(parts[i]) == "paused";
    }
    return true;
  }
  return false;
}

}  // namespace xtgui
