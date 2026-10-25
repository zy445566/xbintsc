/*
 * xbintsc GUI engine — CSS linear-gradient values.
 *
 * Kept apart from `style_values.cpp` so the declaration dispatcher stays under
 * the repository's per-file line budget.
 */
#include <cctype>
#include <string>

#include "style_internal.h"
#include "style_values_internal.h"

namespace xtgui {

/* -- linear-gradient backgrounds ----------------------------------------- */

/** Parse a colour at the start of a gradient stop, leaving the offset text in
 * `rest`. Handles `#rgb`, `rgb()/rgba()`, `hsl()` and named colours. */
static bool parseGradientColor(const std::string &token, Color *color, std::string *rest) {
  std::string text = xt_css_trim(token);
  if (text.empty()) return false;
  size_t end = 0;
  if (text[0] == '#') {
    /* Skip the leading '#' (the hex digits start at index 1); including it in
     * the isxdigit loop made every hex stop parse as invalid, so a gradient
     * written with `#rrggbb` produced no background at all. */
    end = 1;
    while (end < text.size() && std::isxdigit((unsigned char)text[end])) end++;
  } else if (text.compare(0, 4, "rgb(") == 0 || text.compare(0, 5, "rgba(") == 0 ||
             text.compare(0, 4, "hsl(") == 0 || text.compare(0, 5, "hsla(") == 0) {
    int depth = 0;
    for (; end < text.size(); end++) {
      if (text[end] == '(') depth++;
      else if (text[end] == ')') {
        depth--;
        if (depth == 0) {
          end++;
          break;
        }
      }
    }
  } else {
    while (end < text.size() && text[end] != ' ' && text[end] != '\t' && text[end] != '\n' &&
           text[end] != '\r') end++;
  }
  if (end == 0) return false;
  std::string colorText = text.substr(0, end);
  *color = xt_css_parse_color(colorText);
  if (!color->valid && xt_css_lower(colorText) == "transparent") {
    color->valid = true;
    color->r = color->g = color->b = 0.0f;
    color->a = 0.0f;
  }
  if (!color->valid) return false;
  *rest = xt_css_trim(text.substr(end));
  return true;
}

/** Parse one `[<px>%]` / `<number>` offset. */
static bool parseGradientOffset(const std::string &raw, float *out) {
  std::string text = xt_css_trim(raw);
  if (text.empty()) return false;
  bool percent = text.back() == '%';
  if (percent) text.pop_back();
  float value = 0;
  if (!xt_css_parse_number(text, &value)) return false;
  if (percent) value /= 100.0f;
  else if (value > 1.0f) value /= 100.0f;
  if (value < 0) value = 0;
  if (value > 1) value = 1;
  *out = value;
  return true;
}

/** Parse the first `linear-gradient(...)` in a background value. */
static bool parseLinearGradient(const std::string &value, BackgroundGradient *gradient) {
  std::string text = xt_css_lower(value);
  /* The last linear layer is normally the base colour: a `background` shorthand
   * often puts a translucent white gloss first and the actual bar/panel colour
   * after it. (Layers are painted independently in a browser; the engine paints
   * one base gradient.) */
  size_t start = text.rfind("linear-gradient(");
  if (start == std::string::npos) return false;
  size_t open = start + 15;  // the '('
  size_t bodyStart = open + 1;
  int depth = 0;
  size_t close = std::string::npos;
  for (size_t i = bodyStart; i < text.size(); i++) {
    if (text[i] == '(') depth++;
    else if (text[i] == ')') {
      if (depth == 0) {
        close = i;
        break;
      }
      depth--;
    }
  }
  if (close == std::string::npos) return false;
  std::string body = xt_css_trim(value.substr(bodyStart, close - bodyStart));
  /* The first component may be an angle / direction; stops are the rest. */
  size_t firstComma = std::string::npos;
  int d = 0;
  for (size_t i = 0; i < body.size(); i++) {
    if (body[i] == '(') d++;
    else if (body[i] == ')') d--;
    else if (body[i] == ',' && d == 0) {
      firstComma = i;
      break;
    }
  }
  if (firstComma == std::string::npos) return false;
  std::string first = xt_css_lower(xt_css_trim(body.substr(0, firstComma)));
  float angle = 180.0f;
  bool hasAngle = false;
  if (first == "to right") { angle = 90; hasAngle = true; }
  else if (first == "to left") { angle = 270; hasAngle = true; }
  else if (first == "to bottom") { angle = 180; hasAngle = true; }
  else if (first == "to top") { angle = 0; hasAngle = true; }
  else if (first.size() > 3 && first.compare(first.size() - 3, 3, "deg") == 0) {
    float number = 0;
    if (xt_css_parse_number(first.substr(0, first.size() - 3), &number)) {
      angle = number;
      hasAngle = true;
    }
  }
  std::string stopsText = hasAngle ? body.substr(firstComma + 1) : body;
  std::vector<std::string> parts;
  std::string current;
  d = 0;
  for (char c : stopsText) {
    if (c == '(') d++;
    else if (c == ')') d--;
    if (c == ',' && d == 0) {
      parts.push_back(xt_css_trim(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(xt_css_trim(current));
  if (parts.size() < 2) return false;

  std::vector<BackgroundGradientStop> stops;
  float lastOffset = -1.0f;
  for (const std::string &part : parts) {
    Color color;
    std::string rest;
    if (!parseGradientColor(part, &color, &rest)) continue;
    BackgroundGradientStop stop;
    stop.color = color;
    stop.offset = -1.0f;
    if (!rest.empty()) {
      std::vector<std::string> offsets = xt_css_split_whitespace(rest);
      if (!offsets.empty()) parseGradientOffset(offsets[0], &stop.offset);
    }
    if (stop.offset < 0) stop.offset = -1.0f;
    else if (stop.offset < lastOffset) stop.offset = lastOffset;
    if (stop.offset >= 0) lastOffset = stop.offset;
    stops.push_back(stop);
  }
  if (stops.size() < 2) return false;
  /* Fill in implicit offsets by distributing them between their neighbours. */
  size_t i = 0;
  while (i < stops.size()) {
    if (stops[i].offset >= 0) {
      i++;
      continue;
    }
    size_t j = i;
    while (j < stops.size() && stops[j].offset < 0) j++;
    float before = (i == 0) ? 0.0f : stops[i - 1].offset;
    float after = (j >= stops.size()) ? 1.0f : stops[j].offset;
    size_t count = j - i;
    for (size_t k = 0; k < count; k++)
      stops[i + k].offset = before + (after - before) * (float)(k + 1) / (float)(count + 1);
    i = j;
  }
  stops.front().offset = 0.0f;
  stops.back().offset = 1.0f;
  gradient->active = true;
  gradient->angle = angle;
  gradient->stops = stops;
  return true;
}

bool xt_style_apply_gradient(XtStyle *style, const std::string &value) {
  BackgroundGradient gradient;
  if (!parseLinearGradient(value, &gradient)) return false;
  style->background_gradient = gradient;
  return true;
}

}  // namespace xtgui
