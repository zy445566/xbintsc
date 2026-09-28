/*
 * xbintsc GUI engine — CSS declaration application (value parsing and how each
 * property maps onto `XtStyle`).
 */

#include "style_internal.h"

#include <cstdlib>
#include <vector>

namespace xtgui {

/* -- small helpers -------------------------------------------------------- */

static void setEdge(Edges *edges, const std::string &side, Length value) {
  if (side == "top") edges->top = value;
  else if (side == "right") edges->right = value;
  else if (side == "bottom") edges->bottom = value;
  else if (side == "left") edges->left = value;
}

static bool parseEdgeLengths(const std::string &value, Edges *edges) {
  std::vector<std::string> parts = xt_css_split_whitespace(value);
  if (parts.empty() || parts.size() > 4) return false;
  Length values[4];
  for (size_t i = 0; i < parts.size(); i++) {
    Length length = xt_css_parse_length(parts[i]);
    if (length.unit == Unit::Invalid) {
      std::string keyword = xt_css_lower(parts[i]);
      if (keyword == "thin") length = {1.0f, Unit::Px};
      else if (keyword == "medium") length = {3.0f, Unit::Px};
      else if (keyword == "thick") length = {5.0f, Unit::Px};
      else return false;
    }
    values[i] = length;
  }
  switch (parts.size()) {
    case 1:
      edges->set(values[0]);
      break;
    case 2:
      edges->top = edges->bottom = values[0];
      edges->right = edges->left = values[1];
      break;
    case 3:
      edges->top = values[0];
      edges->right = edges->left = values[1];
      edges->bottom = values[2];
      break;
    default:
      edges->top = values[0];
      edges->right = values[1];
      edges->bottom = values[2];
      edges->left = values[3];
      break;
  }
  return true;
}

static bool assignLength(Length *target, const std::string &value) {
  Length length = xt_css_parse_length(value);
  if (length.unit == Unit::Invalid) return false;
  *target = length;
  return true;
}

static bool assignColor(Color *target, bool *has, const std::string &value) {
  Color color = xt_css_parse_color(value);
  if (!color.valid) return false;
  *target = color;
  *has = true;
  return true;
}

float xt_style_resolve_absolute(const Length &length, float base, float em, float root, float vw,
                                float vh) {
  switch (length.unit) {
    case Unit::Px:
    case Unit::Number:
      return length.value;
    case Unit::Percent:
      return base * length.value / 100.0f;
    case Unit::Em:
      return em * length.value;
    case Unit::Rem:
      return root * length.value;
    case Unit::Vw:
      return vw * length.value / 100.0f;
    case Unit::Vh:
      return vh * length.value / 100.0f;
    default:
      return 0.0f;
  }
}

static bool parseEnum(const std::string &value, const char *const *names, int count, int *out) {
  std::string text = xt_css_lower(xt_css_trim(value));
  for (int i = 0; i < count; i++) {
    if (text == names[i]) {
      *out = i;
      return true;
    }
  }
  return false;
}

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

/* -- declaration application ---------------------------------------------- */

void xt_style_apply_declaration(XtStyle *style, const std::string &property,
                                const std::string &rawValue, const ApplyContext &context) {
  std::string value = xt_css_trim(rawValue);

  if (property == "display") {
    static const char *const names[] = {"none", "block", "inline", "inline-block", "flex", "list-item"};
    int index = 0;
    if (parseEnum(value, names, 6, &index)) style->display = (Display)index;
    return;
  }
  if (property == "position") {
    static const char *const names[] = {"static", "relative", "absolute", "fixed"};
    int index = 0;
    if (parseEnum(value, names, 4, &index)) style->position = (Position)index;
    return;
  }
  if (property == "width") { assignLength(&style->width, value); return; }
  if (property == "height") { assignLength(&style->height, value); return; }
  if (property == "min-width") { assignLength(&style->min_width, value); return; }
  if (property == "min-height") { assignLength(&style->min_height, value); return; }
  if (property == "max-width") { assignLength(&style->max_width, value); return; }
  if (property == "max-height") { assignLength(&style->max_height, value); return; }
  if (property == "top") { assignLength(&style->top, value); return; }
  if (property == "right") { assignLength(&style->right, value); return; }
  if (property == "bottom") { assignLength(&style->bottom, value); return; }
  if (property == "left") { assignLength(&style->left, value); return; }

  if (property == "margin") { parseEdgeLengths(value, &style->margin); return; }
  if (property == "padding") { parseEdgeLengths(value, &style->padding); return; }
  if (property == "margin-top") { setEdge(&style->margin, "top", xt_css_parse_length(value)); return; }
  if (property == "margin-right") { setEdge(&style->margin, "right", xt_css_parse_length(value)); return; }
  if (property == "margin-bottom") { setEdge(&style->margin, "bottom", xt_css_parse_length(value)); return; }
  if (property == "margin-left") { setEdge(&style->margin, "left", xt_css_parse_length(value)); return; }
  if (property == "padding-top") { setEdge(&style->padding, "top", xt_css_parse_length(value)); return; }
  if (property == "padding-right") { setEdge(&style->padding, "right", xt_css_parse_length(value)); return; }
  if (property == "padding-bottom") { setEdge(&style->padding, "bottom", xt_css_parse_length(value)); return; }
  if (property == "padding-left") { setEdge(&style->padding, "left", xt_css_parse_length(value)); return; }

  if (property == "border-width") { parseEdgeLengths(value, &style->border_width); return; }
  if (property == "border-top-width") { setEdge(&style->border_width, "top", xt_css_parse_length(value)); return; }
  if (property == "border-right-width") { setEdge(&style->border_width, "right", xt_css_parse_length(value)); return; }
  if (property == "border-bottom-width") { setEdge(&style->border_width, "bottom", xt_css_parse_length(value)); return; }
  if (property == "border-left-width") { setEdge(&style->border_width, "left", xt_css_parse_length(value)); return; }
  if (property == "border-radius") {
    Length length = xt_css_parse_length(value);
    if (length.unit == Unit::Px || length.unit == Unit::Number) style->border_radius = length.value;
    return;
  }
  if (property == "border-style" || property == "border-top-style" || property == "border-right-style" ||
      property == "border-bottom-style" || property == "border-left-style") {
    static const char *const names[] = {"none", "solid", "dashed", "dotted"};
    int index = 0;
    if (parseEnum(value, names, 4, &index)) style->border_style = (BorderStyle)index;
    return;
  }
  if (property == "border-color" || property == "border-top-color" || property == "border-right-color" ||
      property == "border-bottom-color" || property == "border-left-color") {
    assignColor(&style->border_color, &style->has_border_color, value);
    return;
  }
  if (property == "border" || property == "border-top" || property == "border-right" ||
      property == "border-bottom" || property == "border-left") {
    for (const std::string &token : xt_css_split_whitespace(value)) {
      Length length = xt_css_parse_length(token);
      if (length.unit == Unit::Px || length.unit == Unit::Number) {
        if (property == "border") style->border_width.set(length);
        else setEdge(&style->border_width, property.substr(7), length);
        continue;
      }
      static const char *const names[] = {"none", "solid", "dashed", "dotted"};
      int index = 0;
      if (parseEnum(token, names, 4, &index)) {
        style->border_style = (BorderStyle)index;
        continue;
      }
      Color color = xt_css_parse_color(token);
      if (color.valid) {
        style->border_color = color;
        style->has_border_color = true;
      }
    }
    return;
  }

  if (property == "color") {
    Color color = xt_css_parse_color(value);
    if (color.valid) style->color = color;
    return;
  }
  if (property == "background-color") { assignColor(&style->background, &style->has_background, value); return; }
  if (property == "background") {
    Color color = xt_css_parse_color(value);
    if (color.valid) {
      style->background = color;
      style->has_background = true;
    }
    return;
  }

  if (property == "font-size") {
    Length length = xt_css_parse_length(value);
    if (length.unit == Unit::Invalid) return;
    /* `em`/`%` resolve against the *parent* font size, never the element's own
     * (possibly UA-set) size. */
    style->font_size = xt_style_resolve_absolute(length, context.parentFontSize, context.parentFontSize,
                                                 context.rootFontSize, context.viewportWidth, context.viewportHeight);
    if (style->font_size <= 0.0f) style->font_size = 16.0f;
    return;
  }
  if (property == "font-weight") {
    std::string text = xt_css_lower(value);
    if (text == "normal") style->font_weight = 400;
    else if (text == "bold") style->font_weight = 700;
    else if (text == "lighter") style->font_weight = 100;
    else if (text == "bolder") style->font_weight = 700;
    else {
      float number = 0;
      if (xt_css_parse_number(text, &number)) style->font_weight = (int)number;
    }
    return;
  }
  if (property == "font-family") {
    style->font_family = value;
    return;
  }
  if (property == "font-style") {
    style->font_style = (xt_css_lower(value) == "italic") ? FontStyle::Italic : FontStyle::Normal;
    return;
  }
  if (property == "line-height") {
    std::string text = xt_css_lower(value);
    if (text == "normal") {
      style->line_height = {};
      return;
    }
    Length length = xt_css_parse_length(text);
    if (length.unit == Unit::Invalid) return;
    /* Relative units are resolved in `finalizeStyle`, once the element's own
     * font size is settled. */
    style->line_height = length;
    return;
  }
  if (property == "text-align") {
    std::string text = xt_css_lower(value);
    if (text == "left" || text == "start") style->text_align = TextAlign::Start;
    else if (text == "right" || text == "end") style->text_align = TextAlign::End;
    else if (text == "center") style->text_align = TextAlign::Center;
    else if (text == "justify") style->text_align = TextAlign::Justify;
    return;
  }
  if (property == "white-space") {
    static const char *const names[] = {"normal", "nowrap", "pre", "pre-wrap", "pre-line"};
    int index = 0;
    if (parseEnum(value, names, 5, &index)) style->white_space = (WhiteSpace)index;
    return;
  }

  if (property == "flex-direction") {
    static const char *const names[] = {"row", "row-reverse", "column", "column-reverse"};
    int index = 0;
    if (parseEnum(value, names, 4, &index)) style->flex_direction = (FlexDirection)index;
    return;
  }
  if (property == "justify-content") {
    static const char *const names[] = {"flex-start", "flex-end", "center", "space-between",
                                        "space-around", "space-evenly"};
    int index = 0;
    if (parseEnum(value, names, 6, &index)) style->justify_content = (JustifyContent)index;
    return;
  }
  if (property == "align-items") {
    static const char *const names[] = {"stretch", "flex-start", "flex-end", "center", "baseline"};
    int index = 0;
    if (parseEnum(value, names, 5, &index)) style->align_items = (AlignItems)index;
    return;
  }
  if (property == "gap" || property == "row-gap" || property == "column-gap") {
    assignLength(&style->gap, value);
    return;
  }
  if (property == "flex-grow") {
    float number = 0;
    if (xt_css_parse_number(value, &number)) style->flex_grow = number;
    return;
  }
  if (property == "flex-shrink") {
    float number = 0;
    if (xt_css_parse_number(value, &number)) style->flex_shrink = number;
    return;
  }
  if (property == "flex-basis") { assignLength(&style->flex_basis, value); return; }
  if (property == "flex") {
    std::string text = xt_css_lower(value);
    if (text == "none") {
      style->flex_grow = 0;
      style->flex_shrink = 0;
      style->flex_basis = {0.0f, Unit::Auto};
      return;
    }
    if (text == "auto") {
      style->flex_grow = 1;
      style->flex_shrink = 1;
      style->flex_basis = {0.0f, Unit::Auto};
      return;
    }
    if (text == "initial") {
      style->flex_grow = 0;
      style->flex_shrink = 1;
      style->flex_basis = {0.0f, Unit::Auto};
      return;
    }
    std::vector<std::string> parts = xt_css_split_whitespace(text);
    style->flex_grow = 0;
    style->flex_shrink = 1;
    style->flex_basis = {0.0f, Unit::Auto};
    size_t index = 0;
    float number = 0;
    if (index < parts.size() && xt_css_parse_number(parts[index], &number)) {
      style->flex_grow = number;
      index++;
      if (index < parts.size() && xt_css_parse_number(parts[index], &number)) {
        style->flex_shrink = number;
        index++;
      }
      if (index >= parts.size()) style->flex_basis = {0.0f, Unit::Px};
    }
    if (index < parts.size()) assignLength(&style->flex_basis, parts[index]);
    return;
  }

  if (property == "opacity") {
    float number = 0;
    if (xt_css_parse_number(value, &number)) style->opacity = number < 0 ? 0 : (number > 1 ? 1 : number);
    return;
  }
  if (property == "overflow" || property == "overflow-x" || property == "overflow-y") {
    static const char *const names[] = {"visible", "hidden", "scroll", "auto"};
    int index = 0;
    if (parseEnum(value, names, 4, &index)) style->overflow = (Overflow)index;
    return;
  }
  if (property == "z-index") {
    float number = 0;
    if (xt_css_parse_number(value, &number)) {
      style->z_index = (int)number;
      style->has_z_index = true;
    }
    return;
  }
  if (property == "transition") {
    parseTransitionShorthand(value, &style->transitions);
    return;
  }
  if (property == "transition-property") {
    std::vector<std::string> parts = splitCommas(value);
    style->transitions.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) style->transitions[i].property = xt_css_lower(parts[i]);
    return;
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
    return;
  }
  if (property == "transition-timing-function") {
    std::vector<std::string> parts = splitCommas(value);
    style->transitions.resize(parts.size());
    for (size_t i = 0; i < parts.size(); i++) {
      TimingFunction timing = TimingFunction::Ease;
      if (parseTimingFunction(parts[i], &timing)) style->transitions[i].timing = timing;
    }
    return;
  }
}

}  // namespace xtgui
