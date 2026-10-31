/*
 * xbintsc GUI engine — UI/interaction declarations.
 *
 * `outline` is a real painted property (the focus ring). The rest of this unit
 * accepts the platform hints a browser stylesheet carries (`cursor`,
 * `touch-action`, `user-select`, scrolling hints) so an authored declaration is
 * not silently dropped; only `cursor` maps to an actual SDL cursor, because the
 * engine has no touch gestures, text selection or smooth scrolling yet.
 */

#include "style_internal.h"
#include "style_values_internal.h"

#include <string>
#include <vector>

namespace xtgui {

namespace {

std::string lowerTrim(const std::string &text) { return xt_css_lower(xt_css_trim(text)); }

std::vector<std::string> splitTokens(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if ((c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') && depth == 0) {
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

bool parseBorderStyle(const std::string &value, BorderStyle *out) {
  std::string text = lowerTrim(value);
  if (text == "none") *out = BorderStyle::None;
  else if (text == "solid") *out = BorderStyle::Solid;
  else if (text == "dashed") *out = BorderStyle::Dashed;
  else if (text == "dotted") *out = BorderStyle::Dotted;
  else return false;
  return true;
}

bool parseCursor(const std::string &value, CursorShape *out) {
  std::string text = lowerTrim(value);
  if (text == "auto") *out = CursorShape::Auto;
  else if (text == "default") *out = CursorShape::Default;
  else if (text == "pointer" || text == "hand") *out = CursorShape::Pointer;
  else if (text == "text" || text == "vertical-text") *out = CursorShape::Text;
  else if (text == "move") *out = CursorShape::Move;
  else if (text == "wait" || text == "progress") *out = CursorShape::Wait;
  else if (text == "help") *out = CursorShape::Help;
  else if (text == "not-allowed" || text == "no-drop") *out = CursorShape::NotAllowed;
  else if (text == "crosshair") *out = CursorShape::Crosshair;
  else return false;
  return true;
}

void resetOutline(XtStyle *style) {
  style->outline_width = Length{0.0f, Unit::Px};
  style->outline_style = BorderStyle::None;
  style->outline_color = Color();
  style->has_outline_color = false;
}

}  // namespace

bool xt_style_apply_ui_property(XtStyle *style, const std::string &property,
                                const std::string &rawValue) {
  std::string value = xt_css_trim(rawValue);

  if (property == "outline") {
    resetOutline(style);
    for (const std::string &token : splitTokens(value)) {
      Length length = xt_css_parse_length(token);
      if (length.unit != Unit::Invalid && length.unit != Unit::Auto &&
          length.unit != Unit::None) {
        style->outline_width = length;
        continue;
      }
      if (parseBorderStyle(token, &style->outline_style)) continue;
      Color color = xt_css_parse_color(token);
      if (color.valid) {
        style->outline_color = color;
        style->has_outline_color = true;
      }
    }
    return true;
  }
  if (property == "outline-width") {
    Length length = xt_css_parse_length(value);
    if (length.unit != Unit::Invalid && length.unit != Unit::Auto &&
        length.unit != Unit::None) {
      style->outline_width = length;
    }
    return true;
  }
  if (property == "outline-style") {
    parseBorderStyle(value, &style->outline_style);
    return true;
  }
  if (property == "outline-color") {
    std::string text = lowerTrim(value);
    if (text == "currentcolor" || text == "inherit") {
      style->has_outline_color = false;
    } else {
      Color color = xt_css_parse_color(value);
      if (color.valid) {
        style->outline_color = color;
        style->has_outline_color = true;
      }
    }
    return true;
  }
  if (property == "outline-offset") {
    Length length = xt_css_parse_length(value);
    if (length.unit != Unit::Invalid && length.unit != Unit::Auto &&
        length.unit != Unit::None) {
      style->outline_offset = length;
    }
    return true;
  }
  if (property == "cursor") {
    parseCursor(value, &style->cursor);
    return true;
  }
  if (property == "isolation") {
    style->isolation = lowerTrim(value) == "isolate";
    return true;
  }
  /* Accepted platform hints with no engine subsystem behind them. */
  if (property == "touch-action" || property == "user-select" ||
      property == "overscroll-behavior" || property == "scroll-behavior" ||
      property == "vector-effect" || property == "-webkit-font-smoothing" ||
      property == "-webkit-tap-highlight-color") {
    return true;
  }
  return false;
}

}  // namespace xtgui
