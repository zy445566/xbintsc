/*
 * xbintsc GUI engine — computed-style diagnostics (`getComputedStyle`).
 */

#include "style_internal.h"

#include <cstdio>

namespace xtgui {

static std::string formatNumber(float number) {
  char buffer[64];
  if (number == (float)(long)number) {
    snprintf(buffer, sizeof(buffer), "%ld", (long)number);
  } else {
    snprintf(buffer, sizeof(buffer), "%.4g", number);
  }
  return buffer;
}

static std::string formatLength(const Length &length) {
  switch (length.unit) {
    case Unit::Auto:
      return "auto";
    case Unit::None:
      return "none";
    case Unit::Number:
      return formatNumber(length.value);
    case Unit::Px:
      return formatNumber(length.value) + "px";
    case Unit::Percent:
      return formatNumber(length.value) + "%";
    case Unit::Em:
      return formatNumber(length.value) + "em";
    case Unit::Rem:
      return formatNumber(length.value) + "rem";
    case Unit::Vw:
      return formatNumber(length.value) + "vw";
    case Unit::Vh:
      return formatNumber(length.value) + "vh";
    default:
      return "";
  }
}

static std::string formatColor(const Color &color) {
  if (color.a >= 1.0f) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "rgb(%ld, %ld, %ld)", (long)(color.r * 255.0f + 0.5f),
             (long)(color.g * 255.0f + 0.5f), (long)(color.b * 255.0f + 0.5f));
    return buffer;
  }
  char buffer[96];
  snprintf(buffer, sizeof(buffer), "rgba(%ld, %ld, %ld, %.2f)", (long)(color.r * 255.0f + 0.5f),
           (long)(color.g * 255.0f + 0.5f), (long)(color.b * 255.0f + 0.5f), color.a);
  return buffer;
}

std::string xt_style_property_to_string(const XtStyle &style, const std::string &rawProperty) {
  std::string property = xt_css_lower(xt_css_trim(rawProperty));
  static const char *const displayNames[] = {"none", "block", "inline", "inline-block", "flex", "list-item"};
  static const char *const positionNames[] = {"static", "relative", "absolute", "fixed"};
  static const char *const flexNames[] = {"row", "row-reverse", "column", "column-reverse"};
  static const char *const justifyNames[] = {"flex-start", "flex-end", "center", "space-between",
                                             "space-around", "space-evenly"};
  static const char *const alignNames[] = {"stretch", "flex-start", "flex-end", "center", "baseline"};
  static const char *const alignTextNames[] = {"start", "center", "end", "justify"};

  if (property == "display") return displayNames[(int)style.display];
  if (property == "position") return positionNames[(int)style.position];
  if (property == "color") return formatColor(style.color);
  if (property == "background-color" || property == "background") {
    return style.has_background ? formatColor(style.background) : "rgba(0, 0, 0, 0)";
  }
  if (property == "width") return formatLength(style.width);
  if (property == "height") return formatLength(style.height);
  if (property == "min-width") return formatLength(style.min_width);
  if (property == "min-height") return formatLength(style.min_height);
  if (property == "max-width") return formatLength(style.max_width);
  if (property == "max-height") return formatLength(style.max_height);
  if (property == "top") return formatLength(style.top);
  if (property == "right") return formatLength(style.right);
  if (property == "bottom") return formatLength(style.bottom);
  if (property == "left") return formatLength(style.left);
  if (property == "margin-top") return formatLength(style.margin.top);
  if (property == "margin-right") return formatLength(style.margin.right);
  if (property == "margin-bottom") return formatLength(style.margin.bottom);
  if (property == "margin-left") return formatLength(style.margin.left);
  if (property == "padding-top") return formatLength(style.padding.top);
  if (property == "padding-right") return formatLength(style.padding.right);
  if (property == "padding-bottom") return formatLength(style.padding.bottom);
  if (property == "padding-left") return formatLength(style.padding.left);
  if (property == "font-size") return formatNumber(style.font_size) + "px";
  if (property == "font-weight") return formatNumber((float)style.font_weight);
  if (property == "font-family") return style.font_family;
  if (property == "line-height") {
    return style.line_height.unit == Unit::Auto ? "normal" : formatLength(style.line_height);
  }
  if (property == "text-align") return alignTextNames[(int)style.text_align];
  if (property == "flex-direction") return flexNames[(int)style.flex_direction];
  if (property == "justify-content") return justifyNames[(int)style.justify_content];
  if (property == "align-items") return alignNames[(int)style.align_items];
  if (property == "gap") return formatLength(style.gap);
  if (property == "flex-grow") return formatNumber(style.flex_grow);
  if (property == "flex-shrink") return formatNumber(style.flex_shrink);
  if (property == "flex-basis") return formatLength(style.flex_basis);
  if (property == "border-radius") return formatNumber(style.border_radius) + "px";
  if (property == "opacity") return formatNumber(style.opacity);
  if (property == "z-index") return style.has_z_index ? formatNumber((float)style.z_index) : "auto";
  return "";
}

}  // namespace xtgui
