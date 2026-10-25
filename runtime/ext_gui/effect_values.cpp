/*
 * xbintsc GUI engine — CSS transition declarations.
 *
 * Kept apart from `style_values.cpp` so the declaration dispatcher stays under
 * the repository's per-file line budget.
 */
#include <string>
#include <vector>

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
