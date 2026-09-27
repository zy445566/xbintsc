/*
 * xbintsc GUI engine — cascade, inheritance and computed style.
 */

#include "style.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace xtgui {

XtStyle xt_style_initial() { return XtStyle(); }

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

static float resolveAbsolute(const Length &length, float base, float em, float root, float vw, float vh) {
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

/* -- user-agent stylesheet ------------------------------------------------ */

static bool isBlockTag(const std::string &tag) {
  static const std::unordered_map<std::string, bool> tags = {
      {"address", true},  {"article", true}, {"aside", true},   {"blockquote", true},
      {"dd", true},       {"div", true},     {"dl", true},      {"dt", true},
      {"fieldset", true}, {"figcaption", true}, {"figure", true}, {"footer", true},
      {"form", true},     {"h1", true},      {"h2", true},      {"h3", true},
      {"h4", true},       {"h5", true},      {"h6", true},      {"header", true},
      {"hr", true},       {"li", true},      {"main", true},    {"nav", true},
      {"ol", true},       {"p", true},       {"pre", true},     {"section", true},
      {"table", true},    {"tbody", true},   {"td", true},      {"tfoot", true},
      {"th", true},       {"thead", true},   {"tr", true},      {"ul", true},
  };
  return tags.count(tag) != 0;
}

static void applyUserAgentStyle(const Node *node, XtStyle *style) {
  const std::string &tag = node->tag;
  if (tag == "html" || tag == "body") style->display = Display::Block;
  if (isBlockTag(tag)) style->display = Display::Block;
  if (tag == "head" || tag == "style" || tag == "script" || tag == "title" || tag == "meta" ||
      tag == "link" || tag == "base" || tag == "template") {
    style->display = Display::None;
  }
  if (tag == "br") style->display = Display::Block;
  if (tag == "button" || tag == "input" || tag == "select" || tag == "textarea") {
    style->display = Display::InlineBlock;
  }
  if (tag == "body") style->margin.set({8.0f, Unit::Px});
  if (tag == "p") style->margin.top = style->margin.bottom = {16.0f, Unit::Px};
  if (tag == "blockquote") {
    style->margin.top = style->margin.bottom = {16.0f, Unit::Px};
    style->margin.left = style->margin.right = {40.0f, Unit::Px};
  }
  if (tag == "h1") {
    style->font_size = 32.0f;
    style->font_weight = 700;
    style->margin.top = style->margin.bottom = {21.44f, Unit::Px};
  } else if (tag == "h2") {
    style->font_size = 24.0f;
    style->font_weight = 700;
    style->margin.top = style->margin.bottom = {19.92f, Unit::Px};
  } else if (tag == "h3") {
    style->font_size = 18.72f;
    style->font_weight = 700;
    style->margin.top = style->margin.bottom = {18.72f, Unit::Px};
  } else if (tag == "h4") {
    style->font_size = 16.0f;
    style->font_weight = 700;
    style->margin.top = style->margin.bottom = {21.28f, Unit::Px};
  } else if (tag == "h5") {
    style->font_size = 13.28f;
    style->font_weight = 700;
    style->margin.top = style->margin.bottom = {22.18f, Unit::Px};
  } else if (tag == "h6") {
    style->font_size = 10.72f;
    style->font_weight = 700;
    style->margin.top = style->margin.bottom = {24.97f, Unit::Px};
  }
  if (tag == "strong" || tag == "b") style->font_weight = 700;
  if (tag == "dt") style->font_weight = 700;
  if (tag == "em" || tag == "i") style->font_style = FontStyle::Italic;
  if (tag == "small") style->font_size = 13.333f;
  if (tag == "a") {
    style->color = {0.0f, 0.0f, 0.933f, 1.0f};
  }
  if (tag == "ul" || tag == "ol") {
    style->padding.left = {40.0f, Unit::Px};
    style->margin.top = style->margin.bottom = {16.0f, Unit::Px};
  }
  if (tag == "pre" || tag == "code" || tag == "kbd" || tag == "samp") {
    style->font_family = "monospace";
  }
  if (tag == "pre") style->white_space = WhiteSpace::Pre;
  if (tag == "hr") {
    style->border_width.top = {1.0f, Unit::Px};
    style->border_style = BorderStyle::Solid;
    style->border_color = {0.5f, 0.5f, 0.5f, 1.0f};
    style->has_border_color = true;
    style->margin.top = style->margin.bottom = {8.0f, Unit::Px};
  }
}

/* -- declaration application ---------------------------------------------- */

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

struct ApplyContext {
  float rootFontSize = 16.0f;
  float parentFontSize = 16.0f;
  float viewportWidth = 0.0f;
  float viewportHeight = 0.0f;
};

static void applyDeclaration(XtStyle *style, const std::string &property, const std::string &rawValue,
                             const ApplyContext &context) {
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
    style->font_size = resolveAbsolute(length, context.parentFontSize, context.parentFontSize,
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
}

/* -- cascade -------------------------------------------------------------- */

namespace {

struct Candidate {
  int important = 0;
  int specificity = 0;
  int order = 0;
  const Declaration *declaration = nullptr;
};

bool candidateBefore(const Candidate &a, const Candidate &b) {
  if (a.important != b.important) return a.important < b.important;
  if (a.specificity != b.specificity) return a.specificity < b.specificity;
  return a.order < b.order;
}

static void finalizeStyle(XtStyle *style, const ApplyContext &context) {
  switch (style->line_height.unit) {
    case Unit::Em:
    case Unit::Percent:
    case Unit::Rem:
    case Unit::Vw:
    case Unit::Vh:
      style->line_height.value = resolveAbsolute(style->line_height, style->font_size, style->font_size,
                                                 context.rootFontSize, context.viewportWidth,
                                                 context.viewportHeight);
      style->line_height.unit = Unit::Px;
      break;
    default:
      break;
  }
}

void computeNode(const Node *node, const XtStyle *parent, const StyleSheet &sheet,
                 std::unordered_map<const Node *, XtStyle> &out, const ApplyContext &context,
                 const MatchState &state) {
  const XtStyle *childParent = parent;
  if (node->isElement()) {
    XtStyle style = xt_style_initial();
    if (parent != nullptr) {
      /* Inherited properties start from the parent; everything else keeps its
       * initial value until a declaration overrides it. */
      style.color = parent->color;
      style.font_size = parent->font_size;
      style.font_weight = parent->font_weight;
      style.font_family = parent->font_family;
      style.font_style = parent->font_style;
      style.line_height = parent->line_height;
      style.text_align = parent->text_align;
      style.white_space = parent->white_space;
    } else {
      style.color = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    applyUserAgentStyle(node, &style);

    ApplyContext nodeContext = context;
    nodeContext.parentFontSize = (parent != nullptr) ? parent->font_size : context.rootFontSize;

    std::vector<Candidate> candidates;
    for (const Rule &rule : sheet.rules) {
      for (const ComplexSelector &selector : rule.selectors) {
        if (!xt_css_match(node, selector, state)) continue;
        for (const Declaration &declaration : rule.declarations) {
          candidates.push_back({declaration.important ? 1 : 0, selector.specificity, rule.order, &declaration});
        }
      }
    }
    const std::string *inlineStyle = node->attr("style");
    std::vector<Declaration> inlineDeclarations;
    if (inlineStyle != nullptr) {
      inlineDeclarations = xt_css_parse_declarations(*inlineStyle);
      for (const Declaration &declaration : inlineDeclarations) {
        candidates.push_back({declaration.important ? 1 : 0, 1 << 28, INT_MAX, &declaration});
      }
    }
    std::stable_sort(candidates.begin(), candidates.end(), candidateBefore);
    for (const Candidate &candidate : candidates) {
      applyDeclaration(&style, candidate.declaration->property, candidate.declaration->value, nodeContext);
    }
    finalizeStyle(&style, nodeContext);

    out[node] = style;
    childParent = &out[node];
  }
  for (const std::unique_ptr<Node> &child : node->children) {
    computeNode(child.get(), childParent, sheet, out, context, state);
  }
}

}  // namespace

void xt_style_compute(const Node *root, const StyleSheet &sheet,
                      std::unordered_map<const Node *, XtStyle> &out, float viewportWidth,
                      float viewportHeight, const MatchState &state) {
  out.clear();
  ApplyContext context;
  context.viewportWidth = viewportWidth;
  context.viewportHeight = viewportHeight;
  computeNode(root, nullptr, sheet, out, context, state);
}

/* -- diagnostics ---------------------------------------------------------- */

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
