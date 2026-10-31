/*
 * xbintsc GUI engine — CSS Grid and alignment declarations.
 *
 * Kept apart from `style_values.cpp` so the declaration dispatcher stays under
 * the repository's per-file line budget.
 */
#include <string>
#include <vector>

#include "style_internal.h"
#include "style_values_internal.h"

namespace xtgui {

/* -- grid values ---------------------------------------------------------- */

/** Split a track list on whitespace, ignoring spaces inside `minmax(...)` /
 * `repeat(...)`. */
static std::vector<std::string> splitTrackTokens(const std::string &text) {
  std::vector<std::string> tokens;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && depth == 0) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

static bool parseFr(const std::string &text, float *out) {
  std::string token = xt_css_lower(xt_css_trim(text));
  if (token.size() < 3 || token.compare(token.size() - 2, 2, "fr") != 0) return false;
  float value = 0;
  if (!xt_css_parse_number(token.substr(0, token.size() - 2), &value)) return false;
  *out = value;
  return true;
}

static bool parseTrackBound(const std::string &text, bool is_max, GridTrack *track) {
  std::string token = xt_css_lower(xt_css_trim(text));
  float fr = 0;
  if (parseFr(token, &fr)) {
    track->fr = fr;
    if (!is_max) track->min = {0.0f, Unit::Px};
    return true;
  }
  if (token == "auto") {
    if (is_max) track->max = {0.0f, Unit::Auto};
    else track->min = {0.0f, Unit::Auto};
    return true;
  }
  Length length = xt_css_parse_length(token);
  if (length.unit == Unit::Invalid) return false;
  if (is_max) track->max = length;
  else track->min = length;
  return true;
}

static bool parseOneTrack(const std::string &raw, GridTrack *track) {
  std::string token = xt_css_lower(xt_css_trim(raw));
  if (token == "auto") {
    *track = GridTrack{};
    return true;
  }
  if (token.size() > 7 && token.compare(0, 7, "minmax(") == 0 && token.back() == ')') {
    std::string inner = token.substr(7, token.size() - 8);
    size_t comma = inner.find(',');
    if (comma == std::string::npos) return false;
    *track = GridTrack{};
    if (!parseTrackBound(inner.substr(0, comma), false, track)) return false;
    if (!parseTrackBound(inner.substr(comma + 1), true, track)) return false;
    return true;
  }
  *track = GridTrack{};
  return parseTrackBound(token, true, track);
}

static bool parseGridTracks(const std::string &value, std::vector<GridTrack> *out) {
  std::string text = xt_css_lower(xt_css_trim(value));
  out->clear();
  if (text.empty() || text == "none") return true;
  std::vector<std::string> tokens = splitTrackTokens(text);
  for (const std::string &token : tokens) {
    if (token.size() > 7 && token.compare(0, 7, "repeat(") == 0 && token.back() == ')') {
      std::string inner = token.substr(7, token.size() - 8);
      size_t comma = inner.find(',');
      if (comma == std::string::npos) return false;
      float repeatCount = 0;
      if (!xt_css_parse_number(xt_css_trim(inner.substr(0, comma)), &repeatCount)) return false;
      int count = (int)repeatCount;
      if (count < 1) count = 1;
      if (count > 64) count = 64;
      std::vector<GridTrack> repeated;
      for (const std::string &part : splitTrackTokens(inner.substr(comma + 1))) {
        GridTrack track;
        if (!parseOneTrack(part, &track)) return false;
        repeated.push_back(track);
      }
      for (int i = 0; i < count; i++)
        for (const GridTrack &track : repeated) out->push_back(track);
      continue;
    }
    GridTrack track;
    if (!parseOneTrack(token, &track)) return false;
    out->push_back(track);
  }
  return true;
}

static bool parseGridLineNumber(const std::string &text, int *out) {
  std::string token = xt_css_lower(xt_css_trim(text));
  if (token == "auto" || token.empty()) {
    *out = 0;
    return true;
  }
  if (token.compare(0, 5, "span ") == 0) {
    float span = 0;
    if (!xt_css_parse_number(xt_css_trim(token.substr(5)), &span)) return false;
    *out = (int)span;
    return true;
  }
  float number = 0;
  if (!xt_css_parse_number(token, &number)) return false;
  *out = (int)number;
  return true;
}

static void parseGridLine(const std::string &value, int *start, int *end) {
  std::string text = xt_css_trim(value);
  size_t slash = text.find('/');
  *start = 0;
  *end = 0;
  if (slash == std::string::npos) {
    parseGridLineNumber(text, start);
    return;
  }
  parseGridLineNumber(text.substr(0, slash), start);
  parseGridLineNumber(text.substr(slash + 1), end);
}

static bool parseAlignKeyword(const std::string &value, AlignItems *out) {
  std::string text = xt_css_lower(xt_css_trim(value));
  if (text == "stretch") *out = AlignItems::Stretch;
  else if (text == "flex-start" || text == "start" || text == "self-start") *out = AlignItems::FlexStart;
  else if (text == "flex-end" || text == "end" || text == "self-end") *out = AlignItems::FlexEnd;
  else if (text == "center") *out = AlignItems::Center;
  else if (text == "baseline") *out = AlignItems::Baseline;
  else return false;
  return true;
}


bool xt_style_apply_grid_property(XtStyle *style, const std::string &property,
                                  const std::string &value) {
  if (property == "align-items") {
    parseAlignKeyword(value, &style->align_items);
    return true;
  }
  if (property == "justify-items") {
    parseAlignKeyword(value, &style->justify_items);
    return true;
  }
  if (property == "align-self") {
    std::string text = xt_css_lower(xt_css_trim(value));
    if (text == "auto") {
      style->has_align_self = false;
    } else if (parseAlignKeyword(text, &style->align_self)) {
      style->has_align_self = true;
    }
    return true;
  }
  if (property == "justify-self") {
    std::string text = xt_css_lower(xt_css_trim(value));
    if (text == "auto") {
      style->has_justify_self = false;
    } else if (parseAlignKeyword(text, &style->justify_self)) {
      style->has_justify_self = true;
    }
    return true;
  }
  if (property == "place-items") {
    std::vector<std::string> parts = xt_css_split_whitespace(value);
    if (!parts.empty()) {
      parseAlignKeyword(parts[0], &style->align_items);
      style->justify_items = style->align_items;
      if (parts.size() > 1) parseAlignKeyword(parts[1], &style->justify_items);
    }
    return true;
  }
  if (property == "place-self") {
    std::vector<std::string> parts = xt_css_split_whitespace(value);
    if (!parts.empty()) {
      if (parseAlignKeyword(parts[0], &style->align_self)) style->has_align_self = true;
      style->justify_self = style->align_self;
      style->has_justify_self = style->has_align_self;
      if (parts.size() > 1 && parseAlignKeyword(parts[1], &style->justify_self))
        style->has_justify_self = true;
    }
    return true;
  }
  if (property == "grid-template-columns") {
    parseGridTracks(value, &style->grid_columns);
    return true;
  }
  if (property == "grid-template-rows") {
    parseGridTracks(value, &style->grid_rows);
    return true;
  }
  if (property == "grid-column") {
    parseGridLine(value, &style->grid_column_start, &style->grid_column_end);
    return true;
  }
  if (property == "grid-row") {
    parseGridLine(value, &style->grid_row_start, &style->grid_row_end);
    return true;
  }
  if (property == "grid-area") {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    for (char c : value) {
      if (c == '(') depth++;
      if (c == ')') depth--;
      if (c == '/' && depth == 0) {
        parts.push_back(xt_css_trim(current));
        current.clear();
      } else {
        current.push_back(c);
      }
    }
    parts.push_back(xt_css_trim(current));
    style->grid_row_start = style->grid_column_start = 0;
    style->grid_row_end = style->grid_column_end = 0;
    if (parts.size() >= 1) parseGridLineNumber(parts[0], &style->grid_row_start);
    if (parts.size() >= 2) parseGridLineNumber(parts[1], &style->grid_column_start);
    if (parts.size() >= 3) parseGridLineNumber(parts[2], &style->grid_row_end);
    if (parts.size() >= 4) parseGridLineNumber(parts[3], &style->grid_column_end);
    return true;
  }

  return false;
}

}  // namespace xtgui
