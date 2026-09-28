/*
 * xbintsc GUI engine — block/flow layout core.
 *
 * The box tree is built in `layout.cpp`; this unit turns that tree into
 * positioned boxes and owns the `Layouter` methods shared by the inline
 * (`layout_inline.cpp`) and flex (`layout_flex.cpp`) formatting contexts.
 */

#include "layout_flow_internal.h"

namespace xtgui {
namespace layout_flow_detail {

const XtStyle *Layouter::styleOf(const Node *node) const {
  auto it = styles->find(node);
  return it == styles->end() ? nullptr : &it->second;
}

float Layouter::px(const Length &length, float base, float em, float fallback) const {
  return toPx(length, base, em, rootFontSize, vw, vh, fallback);
}

float Layouter::clampSize(float value, const Length &minLength, const Length &maxLength, float base,
                          float em) const {
  if (!isAuto(minLength)) value = std::max(value, px(minLength, base, em, value));
  if (!isAuto(maxLength)) value = std::min(value, px(maxLength, base, em, value));
  return value;
}

float Layouter::textWidth(const std::string &text, const XtStyle &style) const {
  return xt_text_measure_width(text, specOf(style));
}

float Layouter::horizontalEdges(const LayoutBox *box) const {
  return box->margin_left + box->margin_right + box->border_left + box->border_right +
         box->padding_left + box->padding_right;
}

float Layouter::verticalEdges(const LayoutBox *box) const {
  return box->margin_top + box->margin_bottom + box->border_top + box->border_bottom +
         box->padding_top + box->padding_bottom;
}

float Layouter::preferredContentWidth(const LayoutBox *box, float available) const {
  const XtStyle &style = *box->style;
  if (!isAuto(style.width)) return px(style.width, available, style.font_size, 0.0f);
  if (box->is_image && box->intrinsic_width > 0) return box->intrinsic_width;
  float best = 0;
  for (const std::unique_ptr<LayoutBox> &child : box->children) {
    if (child->display == Display::None) continue;
    if (child->is_text) {
      best = std::max(best, textWidth(child->text, *child->style));
    } else {
      best = std::max(best,
                      preferredContentWidth(child.get(), available) + horizontalEdges(child.get()));
    }
  }
  return best;
}

void Layouter::translate(LayoutBox *box, float dx, float dy) const {
  box->x += dx;
  box->y += dy;
  box->content_x += dx;
  box->content_y += dy;
  for (TextFragment &fragment : box->fragments) {
    fragment.x += dx;
    fragment.y += dy;
  }
  for (std::unique_ptr<LayoutBox> &child : box->children) translate(child.get(), dx, dy);
}

void Layouter::resolveEdges(LayoutBox *box, float containingWidth) const {
  const XtStyle &style = *box->style;
  float em = style.font_size;
  box->margin_top = px(style.margin.top, containingWidth, em);
  box->margin_right = px(style.margin.right, containingWidth, em);
  box->margin_bottom = px(style.margin.bottom, containingWidth, em);
  box->margin_left = px(style.margin.left, containingWidth, em);
  box->padding_top = px(style.padding.top, containingWidth, em);
  box->padding_right = px(style.padding.right, containingWidth, em);
  box->padding_bottom = px(style.padding.bottom, containingWidth, em);
  box->padding_left = px(style.padding.left, containingWidth, em);
  if (style.border_style == BorderStyle::None) {
    box->border_top = box->border_right = box->border_bottom = box->border_left = 0;
  } else {
    box->border_top = px(style.border_width.top, containingWidth, em);
    box->border_right = px(style.border_width.right, containingWidth, em);
    box->border_bottom = px(style.border_width.bottom, containingWidth, em);
    box->border_left = px(style.border_width.left, containingWidth, em);
  }
}

float Layouter::layoutBlock(LayoutBox *box, float x, float y, float availableWidth,
                            float availableHeight, float forcedContentWidth,
                            float forcedContentHeight) const {
  const XtStyle &style = *box->style;
  float em = style.font_size;
  resolveEdges(box, availableWidth);

  float contentWidth;
  if (forcedContentWidth >= 0) {
    contentWidth = forcedContentWidth;
  } else if (isAuto(style.width)) {
    contentWidth = availableWidth - horizontalEdges(box);
  } else {
    contentWidth = px(style.width, availableWidth, em);
  }
  contentWidth = clampSize(contentWidth, style.min_width, style.max_width, availableWidth, em);
  if (contentWidth < 0) contentWidth = 0;

  box->x = x + box->margin_left;
  box->y = y + box->margin_top;
  box->content_x = box->x + box->border_left + box->padding_left;
  box->content_y = box->y + box->border_top + box->padding_top;
  box->content_width = contentWidth;
  box->width =
      box->border_left + box->padding_left + contentWidth + box->padding_right + box->border_right;

  float definiteHeight = -1;
  if (!isAuto(style.height) && !(style.height.unit == Unit::Percent && availableHeight < 0)) {
    definiteHeight = px(style.height, availableHeight, em, -1);
  }

  float contentHeight;
  if (forcedContentHeight >= 0) {
    layoutChildren(box, contentWidth, availableHeight, forcedContentHeight);
    contentHeight = forcedContentHeight;
  } else {
    contentHeight = layoutChildren(box, contentWidth, availableHeight, definiteHeight);
    if (definiteHeight >= 0) contentHeight = definiteHeight;
    contentHeight = clampSize(contentHeight, style.min_height, style.max_height,
                              availableHeight < 0 ? contentHeight : availableHeight, em);
    if (contentHeight < 0) contentHeight = 0;
  }
  /* Replaced elements keep their aspect ratio when only one axis is set. */
  if (box->is_image && forcedContentHeight < 0 && isAuto(style.height) && box->intrinsic_width > 0 &&
      box->intrinsic_height > 0) {
    contentHeight = contentWidth * box->intrinsic_height / box->intrinsic_width;
  }
  box->content_height = contentHeight;
  box->height =
      box->border_top + box->padding_top + contentHeight + box->padding_bottom + box->border_bottom;
  box->baseline = box->height;
  return box->margin_top + box->height + box->margin_bottom;
}

float Layouter::layoutChildren(LayoutBox *block, float contentWidth, float availableHeight,
                               float definiteHeight) const {
  if (block->style->display == Display::Flex)
    return layoutFlex(block, contentWidth, definiteHeight);

  float y = block->content_y;
  float startY = y;
  std::vector<LayoutBox *> inlineRun;
  auto flush = [&]() {
    if (inlineRun.empty()) return;
    y += layoutInlineRun(block, inlineRun, contentWidth, y);
    inlineRun.clear();
  };
  for (const std::unique_ptr<LayoutBox> &child : block->children) {
    Display display = child->display;
    if (display == Display::None) continue;
    if (display == Display::Block || display == Display::ListItem || display == Display::Flex) {
      flush();
      y += layoutBlock(child.get(), block->content_x, y, contentWidth, availableHeight);
    } else {
      inlineRun.push_back(child.get());
    }
  }
  flush();
  return y - startY;
}

}  // namespace layout_flow_detail

float xt_layout_text_width(const std::string &text, const XtStyle &style) {
  return xt_text_measure_width(text, layout_flow_detail::specOf(style));
}

void xt_layout_run(LayoutBox *root, const std::unordered_map<const Node *, XtStyle> &styles,
                   float viewportWidth, float viewportHeight) {
  layout_flow_detail::Layouter layouter;
  layouter.styles = &styles;
  layouter.tree = nullptr;
  layouter.vw = viewportWidth;
  layouter.vh = viewportHeight;
  layouter.layoutBlock(root, 0, 0, viewportWidth, viewportHeight);
}

}  // namespace xtgui
