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
    /* Out-of-flow children do not contribute to the parent's intrinsic width. */
    if (isAbsoluteChild(child.get())) continue;
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

bool Layouter::establishesContainingBlock(const LayoutBox *box) {
  return box->style != nullptr && box->style->position != Position::Static;
}

const LayoutBox *Layouter::containingBlockAncestor(const LayoutBox *box) const {
  /* Absolute boxes resolve against the padding box of the nearest ancestor with
   * `position != static`; when there is none, the caller falls back to the
   * initial containing block (the viewport). */
  for (const LayoutBox *ancestor = box; ancestor != nullptr; ancestor = ancestor->parent) {
    if (establishesContainingBlock(ancestor)) return ancestor;
  }
  return nullptr;
}

void Layouter::setContainingBlock(LayoutBox *box, const LayoutBox *ancestor) const {
  if (ancestor != nullptr) {
    /* The padding box of the ancestor. Its height is only known once the
     * ancestor's own layout has finished, which is why `publishContainingBlock`
     * is called again at the end of `layoutBlock` for the box that owns it. */
    box->cb_x = ancestor->content_x - ancestor->padding_left;
    box->cb_y = ancestor->content_y - ancestor->padding_top;
    box->cb_width = ancestor->padding_left + ancestor->content_width + ancestor->padding_right;
    box->cb_height = ancestor->padding_top + ancestor->content_height + ancestor->padding_bottom;
    return;
  }
  /* No positioned ancestor: the initial containing block is the viewport. */
  box->cb_x = 0;
  box->cb_y = 0;
  box->cb_width = vw;
  box->cb_height = vh;
}

void Layouter::publishContainingBlock(LayoutBox *box) const {
  if (!establishesContainingBlock(box)) return;
  for (const std::unique_ptr<LayoutBox> &child : box->children) setContainingBlock(child.get(), box);
}

void Layouter::applyPosition(LayoutBox *box) const {
  if (box->style == nullptr) return;
  const XtStyle &style = *box->style;
  if (style.position == Position::Static) return;
  float em = style.font_size;

  if (style.position == Position::Relative) {
    /* Relative positioning offsets the box (and its subtree) without changing
     * the space it occupies in the flow. */
    float dx = 0;
    float dy = 0;
    if (!isAuto(style.left)) dx = px(style.left, box->cb_width, em);
    else if (!isAuto(style.right)) dx = -px(style.right, box->cb_width, em);
    if (!isAuto(style.top)) dy = px(style.top, box->cb_height, em);
    else if (!isAuto(style.bottom)) dy = -px(style.bottom, box->cb_height, em);
    if (dx != 0 || dy != 0) translate(box, dx, dy);
    return;
  }

  /* Absolute (and fixed, which resolves against the viewport): the static
   * position stands in for every `auto` inset. */
  float x = box->x;
  float y = box->y;
  if (!isAuto(style.left)) x = box->cb_x + px(style.left, box->cb_width, em) + box->margin_left;
  else if (!isAuto(style.right)) {
    x = box->cb_x + box->cb_width - px(style.right, box->cb_width, em) - box->margin_right - box->width;
  }
  if (!isAuto(style.top)) y = box->cb_y + px(style.top, box->cb_height, em) + box->margin_top;
  else if (!isAuto(style.bottom)) {
    y = box->cb_y + box->cb_height - px(style.bottom, box->cb_height, em) - box->margin_bottom - box->height;
  }
  translate(box, x - box->x, y - box->y);
}

float Layouter::layoutBlock(LayoutBox *box, float x, float y, float availableWidth,
                            float availableHeight, float forcedContentWidth,
                            float forcedContentHeight, bool inFlow) const {
  const XtStyle &style = *box->style;
  float em = style.font_size;
  resolveEdges(box, availableWidth);

  float contentWidth;
  bool outOfFlow = style.position == Position::Absolute || style.position == Position::Fixed;
  if (forcedContentWidth >= 0) {
    contentWidth = forcedContentWidth;
  } else if (isAuto(style.width)) {
    if (outOfFlow && !isAuto(style.left) && !isAuto(style.right)) {
      /* Both horizontal insets are set, so `auto` fills the space between them
       * (the padding box of the containing block). */
      contentWidth = box->cb_width - horizontalEdges(box) - px(style.left, box->cb_width, em) -
                     px(style.right, box->cb_width, em);
      if (contentWidth < 0) contentWidth = 0;
    } else if (outOfFlow) {
      /* An out-of-flow box does not fill its containing block: an `auto` width
       * is shrink-to-fit — its preferred width, capped by the space available
       * (the containing block's width, not the parent's content box, because an
       * absolute box is laid out against the former). */
      float available = box->cb_width - horizontalEdges(box);
      float preferred = preferredContentWidth(box, availableWidth);
      contentWidth = preferred < available ? preferred : available;
      if (contentWidth < 0) contentWidth = 0;
    } else {
      contentWidth = availableWidth - horizontalEdges(box);
    }
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
  /* Now that this box has its final size, its padding box is the containing
   * block its own children resolve against — and only now can an out-of-flow
   * child be placed with `bottom`/`right`. */
  publishContainingBlock(box);
  layoutAbsoluteChildren(box, contentWidth, availableHeight);
  applyPosition(box);
  /* An absolutely positioned box consumes no space in its parent's flow. */
  if (!inFlow) return 0;
  return box->margin_top + box->height + box->margin_bottom;
}

bool Layouter::isAbsoluteChild(const LayoutBox *box) {
  return box->style != nullptr && (box->style->position == Position::Absolute ||
                                   box->style->position == Position::Fixed);
}

void Layouter::layoutAbsoluteChildren(LayoutBox *block, float contentWidth,
                                      float availableHeight) const {
  for (const std::unique_ptr<LayoutBox> &child : block->children) {
    if (child->display == Display::None || !isAbsoluteChild(child.get())) continue;
    setContainingBlock(child.get(), block);
    layoutBlock(child.get(), block->content_x, block->content_y, contentWidth, availableHeight, -1,
                -1, false);
  }
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
    /* Source whitespace between block boxes collapses away (HTML's model), so a
     * run that carries no visible content produces no line at all. */
    bool visible = false;
    for (LayoutBox *box : inlineRun) {
      if (!isCollapsibleText(box)) {
        visible = true;
        break;
      }
    }
    if (visible) y += layoutInlineRun(block, inlineRun, contentWidth, y);
    inlineRun.clear();
  };
  for (const std::unique_ptr<LayoutBox> &child : block->children) {
    Display display = child->display;
    if (display == Display::None) continue;
    if (isAbsoluteChild(child.get())) {
      /* Out of flow: `layoutAbsoluteChildren` places it once this block knows
       * its own size. */
      continue;
    }
    if (display == Display::Block || display == Display::ListItem || display == Display::Flex ||
        child->style == nullptr) {
      flush();
      setContainingBlock(child.get(), containingBlockAncestor(block));
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
