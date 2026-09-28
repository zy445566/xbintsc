/*
 * xbintsc GUI engine — built-in user-agent stylesheet.
 */

#include "style_internal.h"

#include <unordered_map>

namespace xtgui {

namespace {

bool isBlockTag(const std::string &tag) {
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

}  // namespace

void xt_style_apply_user_agent(const Node *node, XtStyle *style) {
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
  if (tag == "img") style->display = Display::InlineBlock;
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

}  // namespace xtgui
