/*
 * xbintsc GUI engine — `clip-path` value parsing.
 *
 * The geometry is resolved in `paint_clip.cpp`, where the element's border box
 * and transform are known. Values here keep CSS lengths unresolved so `%`, em
 * and viewport units survive the cascade.
 */
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "css_internal.h"
#include "style_internal.h"
#include "style_values_internal.h"

namespace xtgui {

namespace {

std::vector<std::string> splitTokens(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if ((c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') && depth == 0) {
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

/** Split `text` on a top-level keyword (`at` / `round`), keeping both sides. */
bool splitKeyword(const std::string &text, const std::string &keyword, std::string *left,
                  std::string *right) {
  std::string lower = xt_css_lower(text);
  int depth = 0;
  for (size_t i = 0; i + keyword.size() <= text.size(); i++) {
    char c = text[i];
    if (c == '(') depth++;
    else if (c == ')') depth--;
    if (depth != 0) continue;
    if (lower.compare(i, keyword.size(), keyword) != 0) continue;
    bool before = i == 0 || xt_is_css_space(text[i - 1]) || text[i - 1] == ',';
    bool after = i + keyword.size() >= text.size() ||
                 xt_is_css_space(text[i + keyword.size()]) || text[i + keyword.size()] == ',';
    if (!before || !after) continue;
    *left = xt_css_trim(text.substr(0, i));
    *right = xt_css_trim(text.substr(i + keyword.size()));
    return true;
  }
  return false;
}

bool parsePositionToken(const std::string &raw, Length *out, bool *vertical) {
  std::string text = xt_css_lower(xt_css_trim(raw));
  if (text == "left") { *out = {0.0f, Unit::Percent}; *vertical = false; return true; }
  if (text == "right") { *out = {100.0f, Unit::Percent}; *vertical = false; return true; }
  if (text == "top") { *out = {0.0f, Unit::Percent}; *vertical = true; return true; }
  if (text == "bottom") { *out = {100.0f, Unit::Percent}; *vertical = true; return true; }
  if (text == "center") { *out = {50.0f, Unit::Percent}; *vertical = false; return true; }
  Length length = xt_css_parse_length(text);
  if (length.unit == Unit::Invalid || length.unit == Unit::Auto || length.unit == Unit::None)
    return false;
  *out = length;
  *vertical = false;
  return true;
}

bool parseCenter(const std::string &text, Length *x, Length *y) {
  std::vector<std::string> tokens = splitTokens(text);
  if (tokens.empty() || tokens.size() > 2) return false;
  Length values[2];
  bool vertical[2] = {false, false};
  for (size_t i = 0; i < tokens.size(); i++) {
    if (!parsePositionToken(tokens[i], &values[i], &vertical[i])) return false;
  }
  if (tokens.size() == 1) {
    if (vertical[0]) {
      *x = {50.0f, Unit::Percent};
      *y = values[0];
    } else {
      *x = values[0];
      *y = {50.0f, Unit::Percent};
    }
    return true;
  }
  if (vertical[0] && !vertical[1]) {
    *x = values[1];
    *y = values[0];
  } else {
    *x = values[0];
    *y = values[1];
  }
  return true;
}

/** Parse the 1–4 side lengths of `inset(...)` and an optional round radius. */
bool parseInset(const std::string &body, ClipPathSpec *spec) {
  std::string sides = body;
  std::string round;
  if (splitKeyword(body, "round", &sides, &round)) {
    std::vector<std::string> radiusTokens = splitTokens(round);
    if (!radiusTokens.empty()) {
      Length radius = xt_css_parse_length(radiusTokens[0]);
      if (radius.unit != Unit::Invalid && radius.unit != Unit::Auto &&
          radius.unit != Unit::None)
        spec->inset_radius = radius;
    }
  }
  std::vector<std::string> tokens = splitTokens(sides);
  if (tokens.empty() || tokens.size() > 4) return false;
  Length values[4];
  for (size_t i = 0; i < tokens.size(); i++) {
    values[i] = xt_css_parse_length(tokens[i]);
    if (values[i].unit == Unit::Invalid) return false;
  }
  switch (tokens.size()) {
    case 1:
      spec->inset_top = spec->inset_right = spec->inset_bottom = spec->inset_left = values[0];
      break;
    case 2:
      spec->inset_top = spec->inset_bottom = values[0];
      spec->inset_right = spec->inset_left = values[1];
      break;
    case 3:
      spec->inset_top = values[0];
      spec->inset_right = spec->inset_left = values[1];
      spec->inset_bottom = values[2];
      break;
    default:
      spec->inset_top = values[0];
      spec->inset_right = values[1];
      spec->inset_bottom = values[2];
      spec->inset_left = values[3];
      break;
  }
  spec->type = ClipPathSpec::Type::Inset;
  return true;
}

bool parseCircle(const std::string &body, ClipPathSpec *spec) {
  std::string radius;
  std::string center;
  if (splitKeyword(body, "at", &radius, &center)) {
    if (!parseCenter(center, &spec->center_x, &spec->center_y)) return false;
  } else {
    radius = body;
  }
  std::vector<std::string> tokens = splitTokens(radius);
  if (!tokens.empty()) {
    Length length = xt_css_parse_length(tokens[0]);
    if (length.unit == Unit::Invalid) return false;
    spec->radius_x = length;
    spec->radius_y = length;
    spec->radius_explicit = true;
  }
  spec->type = ClipPathSpec::Type::Circle;
  return true;
}

bool parseEllipse(const std::string &body, ClipPathSpec *spec) {
  std::string radii = body;
  std::string center;
  splitKeyword(body, "at", &radii, &center);
  if (!center.empty() && !parseCenter(center, &spec->center_x, &spec->center_y)) return false;
  std::vector<std::string> tokens = splitTokens(radii);
  if (!tokens.empty()) {
    Length rx = xt_css_parse_length(tokens[0]);
    Length ry = tokens.size() > 1 ? xt_css_parse_length(tokens[1]) : rx;
    if (rx.unit == Unit::Invalid || ry.unit == Unit::Invalid) return false;
    spec->radius_x = rx;
    spec->radius_y = ry;
    spec->radius_explicit = true;
  }
  spec->type = ClipPathSpec::Type::Ellipse;
  return true;
}

bool parsePolygon(const std::string &body, ClipPathSpec *spec) {
  std::string text = xt_css_trim(body);
  std::string lower = xt_css_lower(text);
  if (lower.rfind("nonzero", 0) == 0) text = xt_css_trim(text.substr(7));
  else if (lower.rfind("evenodd", 0) == 0) text = xt_css_trim(text.substr(7));
  /* Pairs are separated by commas in CSS, but whitespace is accepted too. */
  std::vector<std::string> tokens = splitTokens(text);
  if (tokens.size() < 3) return false;
  std::vector<Length> points;
  for (const std::string &token : tokens) {
    Length length = xt_css_parse_length(token);
    if (length.unit == Unit::Invalid) return false;
    points.push_back(length);
  }
  if (points.size() < 3) return false;
  if (points.size() % 2 != 0) points.pop_back();
  spec->polygon = std::move(points);
  spec->type = ClipPathSpec::Type::Polygon;
  return true;
}

}  // namespace

bool xt_style_apply_clip_path(XtStyle *style, const std::string &property,
                              const std::string &value) {
  if (property != "clip-path") return false;
  std::string text = xt_css_trim(value);
  std::string lower = xt_css_lower(text);
  ClipPathSpec spec;
  if (lower == "none" || text.empty()) {
    style->clip_path = spec;
    return true;
  }
  if (text.size() < 8 || text.back() != ')') {
    style->clip_path = spec;
    return true;
  }
  if (lower.rfind("inset(", 0) == 0) {
    if (parseInset(text.substr(6, text.size() - 7), &spec)) style->clip_path = spec;
    return true;
  }
  if (lower.rfind("circle(", 0) == 0) {
    if (parseCircle(text.substr(7, text.size() - 8), &spec)) style->clip_path = spec;
    return true;
  }
  if (lower.rfind("ellipse(", 0) == 0) {
    if (parseEllipse(text.substr(8, text.size() - 9), &spec)) style->clip_path = spec;
    return true;
  }
  if (lower.rfind("polygon(", 0) == 0) {
    if (parsePolygon(text.substr(8, text.size() - 9), &spec)) style->clip_path = spec;
    return true;
  }
  /* `url(#…)`, `path()` and other shapes are not part of the subset. */
  style->clip_path = spec;
  return true;
}

}  // namespace xtgui
