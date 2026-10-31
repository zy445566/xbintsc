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

static std::string formatCalcBody(const Length &length);
static std::string formatLength(const Length &length);

/** A nested `calc()` operand is parenthesised instead of spelling `calc(calc(…))`,
 * except where operator precedence already groups it the same way. */
static std::string formatCalcOperand(const Length &length, char parent_op) {
  if (length.unit == Unit::Calc) {
    int parent_precedence = (parent_op == '*' || parent_op == '/') ? 2 : 1;
    int own_precedence = (length.op == '*' || length.op == '/') ? 2 : 1;
    if (own_precedence > parent_precedence) return formatCalcBody(length);
    return "(" + formatCalcBody(length) + ")";
  }
  return formatLength(length);
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
    case Unit::Clamp:
    case Unit::Min:
    case Unit::Max: {
      const char *name = length.unit == Unit::Clamp ? "clamp" : (length.unit == Unit::Min ? "min" : "max");
      std::string text = std::string(name) + "(";
      for (size_t i = 0; i < length.args.size(); i++) {
        if (i > 0) text += ", ";
        text += xt_style_length_to_string(length.args[i]);
      }
      text += ")";
      return text;
    }
    case Unit::Calc:
      return "calc(" + formatCalcBody(length) + ")";
    default:
      return "";
  }
}

static std::string formatCalcBody(const Length &length) {
  if (length.args.size() < 2) return "";
  char op = length.op != 0 ? length.op : '?';
  std::string text = formatCalcOperand(length.args[0], op);
  text += " ";
  text.push_back(op);
  text += " ";
  text += formatCalcOperand(length.args[1], op);
  return text;
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

static std::string quoteText(const std::string &text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

static std::string formatFilterList(const std::vector<FilterOp> &filters) {
  if (filters.empty()) return "none";
  std::string out;
  for (const FilterOp &filter : filters) {
    if (!out.empty()) out += " ";
    switch (filter.kind) {
      case FilterKind::Blur:
        out += "blur(" + formatLength(filter.blur) + ")";
        break;
      case FilterKind::Brightness:
        out += "brightness(" + formatNumber(filter.amount) + ")";
        break;
      case FilterKind::Contrast:
        out += "contrast(" + formatNumber(filter.amount) + ")";
        break;
      case FilterKind::Grayscale:
        out += "grayscale(" + formatNumber(filter.amount) + ")";
        break;
      case FilterKind::Sepia:
        out += "sepia(" + formatNumber(filter.amount) + ")";
        break;
      case FilterKind::Invert:
        out += "invert(" + formatNumber(filter.amount) + ")";
        break;
      case FilterKind::Saturate:
        out += "saturate(" + formatNumber(filter.amount) + ")";
        break;
      case FilterKind::HueRotate:
        out += "hue-rotate(" + formatNumber(filter.amount) + "deg)";
        break;
      case FilterKind::Opacity:
        out += "opacity(" + formatNumber(filter.amount) + ")";
        break;
      case FilterKind::DropShadow:
        out += "drop-shadow(" + formatLength(filter.dx) + " " + formatLength(filter.dy);
        if (filter.blur.unit != Unit::Px || filter.blur.value != 0.0f)
          out += " " + formatLength(filter.blur);
        if (filter.has_color) out += " " + formatColor(filter.color);
        out += ")";
        break;
    }
  }
  return out;
}

static std::string formatBoxShadows(const std::vector<BoxShadow> &shadows) {
  if (shadows.empty()) return "none";
  std::string out;
  for (const BoxShadow &shadow : shadows) {
    if (!out.empty()) out += ", ";
    if (shadow.inset) out += "inset ";
    out += formatLength(shadow.dx) + " " + formatLength(shadow.dy);
    bool zeroBlur = shadow.blur.unit == Unit::Px && shadow.blur.value == 0.0f;
    bool zeroSpread = shadow.spread.unit == Unit::Px && shadow.spread.value == 0.0f;
    if (!zeroBlur || !zeroSpread) out += " " + formatLength(shadow.blur);
    if (!zeroSpread) out += " " + formatLength(shadow.spread);
    if (shadow.has_color) out += " " + formatColor(shadow.color);
  }
  return out;
}

static std::string formatTextShadows(const std::vector<TextShadow> &shadows) {
  if (shadows.empty()) return "none";
  std::string out;
  for (const TextShadow &shadow : shadows) {
    if (!out.empty()) out += ", ";
    out += formatLength(shadow.dx) + " " + formatLength(shadow.dy);
    if (shadow.blur.unit != Unit::Px || shadow.blur.value != 0.0f)
      out += " " + formatLength(shadow.blur);
    if (shadow.has_color) out += " " + formatColor(shadow.color);
  }
  return out;
}

static std::string formatClipPath(const ClipPathSpec &clip) {
  switch (clip.type) {
    case ClipPathSpec::Type::None:
      return "none";
    case ClipPathSpec::Type::Inset: {
      std::string out = "inset(" + formatLength(clip.inset_top) + " " +
                        formatLength(clip.inset_right) + " " + formatLength(clip.inset_bottom) +
                        " " + formatLength(clip.inset_left);
      if (clip.inset_radius.unit != Unit::Px || clip.inset_radius.value != 0.0f)
        out += " round " + formatLength(clip.inset_radius);
      return out + ")";
    }
    case ClipPathSpec::Type::Circle:
    case ClipPathSpec::Type::Ellipse: {
      bool circle = clip.type == ClipPathSpec::Type::Circle;
      std::string out = std::string(circle ? "circle(" : "ellipse(");
      if (clip.radius_explicit) {
        if (circle) out += formatLength(clip.radius_x);
        else out += formatLength(clip.radius_x) + " " + formatLength(clip.radius_y);
        out += " at ";
      } else {
        out += "at ";
      }
      out += formatLength(clip.center_x) + " " + formatLength(clip.center_y) + ")";
      return out;
    }
    case ClipPathSpec::Type::Polygon: {
      std::string out = "polygon(";
      for (size_t i = 0; i + 1 < clip.polygon.size(); i += 2) {
        if (i > 0) out += ", ";
        out += formatLength(clip.polygon[i]) + " " + formatLength(clip.polygon[i + 1]);
      }
      return out + ")";
    }
  }
  return "none";
}

std::string xt_style_property_to_string(const XtStyle &style, const std::string &rawProperty) {
  std::string property = xt_css_lower(xt_css_trim(rawProperty));
  static const char *const displayNames[] = {"none", "block", "inline", "inline-block", "flex",
                                             "grid", "list-item"};
  static const char *const positionNames[] = {"static", "relative", "absolute", "fixed"};
  static const char *const flexNames[] = {"row", "row-reverse", "column", "column-reverse"};
  static const char *const wrapNames[] = {"nowrap", "wrap", "wrap-reverse"};
  static const char *const justifyNames[] = {"flex-start", "flex-end", "center", "space-between",
                                             "space-around", "space-evenly"};
  static const char *const alignNames[] = {"stretch", "flex-start", "flex-end", "center", "baseline"};
  static const char *const alignContentNames[] = {"stretch", "flex-start", "flex-end", "center",
                                                  "space-between", "space-around"};
  static const char *const floatNames[] = {"none", "left", "right"};
  static const char *const clearNames[] = {"none", "left", "right", "both"};
  static const char *const alignTextNames[] = {"start", "center", "end", "justify"};
  static const char *const textTransformNames[] = {"none", "uppercase", "lowercase", "capitalize"};
  static const char *const writingModeNames[] = {"horizontal-tb", "vertical-rl", "vertical-lr"};
  static const char *const cursorNames[] = {"auto", "default", "pointer", "text", "move",
                                            "wait", "help", "not-allowed", "crosshair"};
  static const char *const borderStyleNames[] = {"none", "solid", "dashed", "dotted"};

  if (property == "display") {
    if (style.inline_level) return style.display == Display::Grid ? "inline-grid" : "inline-flex";
    return displayNames[(int)style.display];
  }
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
  if (property == "text-transform") return textTransformNames[(int)style.text_transform];
  if (property == "letter-spacing") return formatLength(style.letter_spacing);
  if (property == "writing-mode") return writingModeNames[(int)style.writing_mode];
  if (property == "vertical-align") return formatLength(style.vertical_align);
  if (property == "font-variant-numeric") return style.font_variant_numeric ? "tabular-nums" : "normal";
  if (property == "cursor") return cursorNames[(int)style.cursor];
  if (property == "aspect-ratio") return style.aspect_ratio > 0 ? formatNumber(style.aspect_ratio) : "auto";
  if (property == "grid-area") {
    return formatNumber((float)style.grid_row_start) + " / " +
           formatNumber((float)style.grid_column_start) + " / " +
           formatNumber((float)style.grid_row_end) + " / " +
           formatNumber((float)style.grid_column_end);
  }
  if (property == "outline-width") return formatLength(style.outline_width);
  if (property == "outline-style") return borderStyleNames[(int)style.outline_style];
  if (property == "outline-color") {
    return style.has_outline_color ? formatColor(style.outline_color) : "currentcolor";
  }
  if (property == "outline-offset") return formatLength(style.outline_offset);
  if (property == "background-size") {
    if (style.background_sizes.empty()) return "auto";
    std::string out;
    for (const BackgroundSize &size : style.background_sizes) {
      if (!out.empty()) out += ", ";
      out += formatLength(size.width) + " " + formatLength(size.height);
    }
    return out;
  }
  if (property == "flex-direction") return flexNames[(int)style.flex_direction];
  if (property == "flex-wrap") return wrapNames[(int)style.flex_wrap];
  if (property == "justify-content") return justifyNames[(int)style.justify_content];
  if (property == "align-items") return alignNames[(int)style.align_items];
  if (property == "align-content") return alignContentNames[(int)style.align_content];
  if (property == "gap") return formatLength(style.gap);
  if (property == "row-gap") return formatLength(style.row_gap);
  if (property == "column-gap") return formatLength(style.column_gap);
  if (property == "float") return floatNames[(int)style.float_side];
  if (property == "clear") return clearNames[(int)style.clear];
  if (property == "flex-grow") return formatNumber(style.flex_grow);
  if (property == "flex-shrink") return formatNumber(style.flex_shrink);
  if (property == "flex-basis") return formatLength(style.flex_basis);
  if (property == "border-radius") return formatLength(style.border_radius);
  if (property == "opacity") return formatNumber(style.opacity);
  if (property == "mix-blend-mode") {
    static const char *const names[] = {
        "normal",     "multiply",    "screen",      "overlay",     "darken",
        "lighten",    "color-dodge", "color-burn",  "hard-light",  "soft-light",
        "difference", "exclusion",   "hue",         "saturation",  "color",
        "luminosity"};
    return names[(int)style.mix_blend_mode];
  }
  if (property == "z-index") return style.has_z_index ? formatNumber((float)style.z_index) : "auto";
  if (property == "content") return style.has_content ? quoteText(style.content) : "none";
  if (property == "clip-path") return formatClipPath(style.clip_path);
  if (property == "filter") return formatFilterList(style.filters);
  if (property == "backdrop-filter") return formatFilterList(style.backdrop_filters);
  if (property == "text-shadow") return formatTextShadows(style.text_shadow);
  if (property == "box-shadow") return formatBoxShadows(style.box_shadow);
  if (property == "background-image") {
    if (style.background_layers.empty()) return "none";
    std::string out;
    for (const BackgroundLayer &layer : style.background_layers) {
      if (!out.empty()) out += ", ";
      if (layer.kind == BackgroundKind::Linear) {
        out += layer.repeating ? "repeating-linear-gradient()" : "linear-gradient()";
      } else if (layer.kind == BackgroundKind::Radial) {
        out += layer.repeating ? "repeating-radial-gradient()" : "radial-gradient()";
      } else {
        out += "url(" + layer.image_src + ")";
      }
    }
    return out;
  }
  if (property == "transform") return xt_transform_to_string(style.transform);
  if (property == "transform-origin") {
    return formatLength(style.transform_origin_x) + " " + formatLength(style.transform_origin_y);
  }
  return "";
}

std::string xt_style_length_to_string(const Length &length) { return formatLength(length); }

}  // namespace xtgui
