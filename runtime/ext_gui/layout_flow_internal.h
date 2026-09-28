/*
 * xbintsc GUI engine — shared internals for the flow layouter.
 *
 * `Layouter` is split by formatting context across three translation units:
 *   - layout_flow.cpp    block/flow core and the public entry points
 *   - layout_inline.cpp  inline formatting context (line breaking, text runs)
 *   - layout_flex.cpp    flex formatting context
 * The struct and its data members live here so each unit can define methods.
 */
#ifndef XT_GUI_LAYOUT_FLOW_INTERNAL_H
#define XT_GUI_LAYOUT_FLOW_INTERNAL_H

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "layout_internal.h"
#include "text.h"

namespace xtgui {
namespace layout_flow_detail {

constexpr float kHuge = 1e18f;

struct InlineItem {
  LayoutBox *box = nullptr;
  const XtStyle *style = nullptr;
  std::string text;
  float width = 0;
  float height = 0;
  float ascent = 0;
  bool is_space = false;
  bool atomic = false;      // inline-block
  bool force_break = false; // explicit newline (white-space: pre)
};

inline float toPx(const Length &length, float base, float em, float root, float vw, float vh,
                  float fallback) {
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
      return fallback;
  }
}

inline bool isAuto(const Length &length) {
  return length.unit == Unit::Auto || length.unit == Unit::Invalid || length.unit == Unit::None;
}

/* Per-character advance as a fraction of the font size is no longer needed
 * here: text metrics come from the shaping stack (see `text.h`). */

inline FontSpec specOf(const XtStyle &style) {
  FontSpec spec;
  spec.family = style.font_family;
  spec.pixel_size = style.font_size;
  spec.weight = style.font_weight;
  spec.italic = style.font_style == FontStyle::Italic;
  return spec;
}

inline float lineHeightOf(const XtStyle &style) {
  if (style.line_height.unit == Unit::Number) {
    return style.line_height.value > 0 ? style.line_height.value * style.font_size
                                       : style.font_size * 1.2f;
  }
  if (style.line_height.unit == Unit::Px) return style.line_height.value;
  float lineHeight = 0;
  xt_text_metrics(specOf(style), nullptr, nullptr, &lineHeight);
  return lineHeight > 0 ? lineHeight : style.font_size * 1.2f;
}

inline float ascentOf(const XtStyle &style) {
  float ascent = 0;
  xt_text_metrics(specOf(style), &ascent, nullptr, nullptr);
  return ascent > 0 ? ascent : style.font_size * 0.8f;
}

struct Layouter {
  const std::unordered_map<const Node *, XtStyle> *styles = nullptr;
  LayoutTree *tree = nullptr;
  float vw = 0;
  float vh = 0;
  float rootFontSize = 16.0f;

  const XtStyle *styleOf(const Node *node) const;

  float px(const Length &length, float base, float em, float fallback = 0.0f) const;

  float clampSize(float value, const Length &minLength, const Length &maxLength, float base,
                  float em) const;

  float textWidth(const std::string &text, const XtStyle &style) const;

  float horizontalEdges(const LayoutBox *box) const;

  float verticalEdges(const LayoutBox *box) const;

  float preferredContentWidth(const LayoutBox *box, float available) const;

  void translate(LayoutBox *box, float dx, float dy) const;

  void resolveEdges(LayoutBox *box, float containingWidth) const;

  /** Lay out `box` at (x, y) and return its outer height (margins included). */
  float layoutBlock(LayoutBox *box, float x, float y, float availableWidth, float availableHeight,
                    float forcedContentWidth = -1, float forcedContentHeight = -1) const;

  /** Lay out the children of `block`; returns the used content height. */
  float layoutChildren(LayoutBox *block, float contentWidth, float availableHeight,
                       float definiteHeight) const;

  void flattenInline(LayoutBox *box, float contentWidth, std::vector<InlineItem> &out) const;

  void computeInlineGeometry(LayoutBox *box) const;

  /** Lay out an inline formatting context; returns its consumed height. */
  float layoutInlineRun(LayoutBox *container, const std::vector<LayoutBox *> &run,
                        float contentWidth, float y) const;

  float layoutFlex(LayoutBox *container, float contentWidth, float definiteHeight) const;

  static float sumOf(const std::vector<float> &values);

  static void applyJustify(JustifyContent justify, float freeSpace, size_t count, float *offset,
                           float *between);

  static float crossOffsetFor(AlignItems align, float crossSize, float itemCross);
};

}  // namespace layout_flow_detail
}  // namespace xtgui

#endif /* XT_GUI_LAYOUT_FLOW_INTERNAL_H */
