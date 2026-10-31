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
  if ((box->is_image || box->is_canvas || box->is_inline_svg) && box->intrinsic_width > 0)
    return box->intrinsic_width;
  if (box->is_text) {
    if (style.writing_mode != WritingMode::HorizontalTB) return lineHeightOf(style);
    return textWidth(box->text, style);
  }
  /* A row flex container's intrinsic width is the sum of its items (and gaps);
   * taking the widest child would make a shrink-to-fit toolbar narrower than
   * its buttons and then shrink every button to zero. */
  bool flex_row = style.flex_direction == FlexDirection::Row ||
                  style.flex_direction == FlexDirection::RowReverse;
  /* In a vertical writing mode the flex row axis is vertical, so the intrinsic
   * width is the widest item rather than the sum of the row. */
  if (style.writing_mode != WritingMode::HorizontalTB) flex_row = !flex_row;
  if (style.display == Display::Flex && flex_row) {
    float total = 0;
    int count = 0;
    for (const std::unique_ptr<LayoutBox> &child : box->children) {
      if (child->display == Display::None || isAbsoluteChild(child.get())) continue;
      if (isCollapsibleText(child.get())) continue;
      total += preferredContentWidth(child.get(), available) + horizontalEdges(child.get());
      count++;
    }
    if (count > 1) total += px(style.column_gap, available, style.font_size, 0.0f) * (float)(count - 1);
    return total;
  }
  float best = 0;
  for (const std::unique_ptr<LayoutBox> &child : box->children) {
    if (child->display == Display::None) continue;
    /* Out-of-flow children do not contribute to the parent's intrinsic width. */
    if (isAbsoluteChild(child.get())) continue;
    if (child->is_text) {
      if (child->style->writing_mode != WritingMode::HorizontalTB) {
        best = std::max(best, lineHeightOf(*child->style));
      } else {
        best = std::max(best, textWidth(child->text, *child->style));
      }
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

namespace {
void clearTextFragments(LayoutBox *box) {
  if (box == nullptr) return;
  box->fragments.clear();
  for (const std::unique_ptr<LayoutBox> &child : box->children) clearTextFragments(child.get());
}
}  // namespace

float Layouter::layoutBlock(LayoutBox *box, float x, float y, float availableWidth,
                            float availableHeight, float forcedContentWidth,
                            float forcedContentHeight, bool inFlow) const {
  const XtStyle &style = *box->style;
  /* Flex and grid measure an item before placing it; inline runs append to the
   * text boxes' fragment lists, so drop the previous pass's fragments first. */
  clearTextFragments(box);

  /* A text box is normally laid out by its parent's inline formatting context.
   * Flex/grid items can be text boxes themselves (`<kbd>W</kbd>` with
   * `display:grid`), and `layoutChildren` would otherwise give them no geometry
   * or fragment at all — the glyph then silently disappears from paint. */
  if (box->is_text) {
    if (style.writing_mode != WritingMode::HorizontalTB) {
      return layoutVerticalTextBox(box, x, y, lineHeightOf(style));
    }
    float textW = textWidth(box->text, style);
    float textH = lineHeightOf(style);
    box->x = x;
    box->y = y;
    box->content_x = x;
    box->content_y = y;
    box->width = textW;
    box->content_width = textW;
    box->height = textH;
    box->content_height = textH;
    box->baseline = textH;
    box->fragments.push_back({x, y, textW, textH, box->text});
    return textH;
  }
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
    if (style.box_sizing == BoxSizing::BorderBox) {
      contentWidth -= box->border_left + box->border_right + box->padding_left + box->padding_right;
    }
  }
  contentWidth = clampSize(contentWidth, style.min_width, style.max_width, availableWidth, em);
  if (contentWidth < 0) contentWidth = 0;

  box->x = x + box->margin_left;
  box->y = y;
  box->content_x = box->x + box->border_left + box->padding_left;
  box->content_y = box->y + box->border_top + box->padding_top;
  box->content_width = contentWidth;
  box->width =
      box->border_left + box->padding_left + contentWidth + box->padding_right + box->border_right;
  /* Block-level horizontal auto margins absorb the leftover inline space; two
   * auto margins centre the box. */
  bool autoLeft = style.margin_left_auto;
  bool autoRight = style.margin_right_auto;
  if (!outOfFlow && (autoLeft || autoRight)) {
    float free = availableWidth - box->width - (autoLeft ? 0.0f : box->margin_left) -
                 (autoRight ? 0.0f : box->margin_right);
    if (free < 0.0f) free = 0.0f;
    if (autoLeft && autoRight) {
      box->margin_left = box->margin_right = free / 2.0f;
    } else if (autoLeft) {
      box->margin_left = free;
    } else {
      box->margin_right = free;
    }
    box->x = x + box->margin_left;
    box->content_x = box->x + box->border_left + box->padding_left;
  }

  float definiteHeight = -1;
  if (!isAuto(style.height) && !(style.height.unit == Unit::Percent && availableHeight < 0)) {
    definiteHeight = px(style.height, availableHeight, em, -1);
    if (style.box_sizing == BoxSizing::BorderBox && definiteHeight >= 0) {
      definiteHeight -= box->border_top + box->border_bottom + box->padding_top + box->padding_bottom;
      if (definiteHeight < 0) definiteHeight = 0;
    }
  } else if (outOfFlow && !isAuto(style.top) && !isAuto(style.bottom)) {
    /* An absolutely positioned box with both vertical insets set and `height:
     * auto` fills the containing block (the full-screen overlays rely on
     * `inset: 0`). `layoutBlock` otherwise only knows definite heights from
     * `height`, so derive it here. */
    float top = px(style.top, box->cb_height, em);
    float bottom = px(style.bottom, box->cb_height, em);
    definiteHeight = box->cb_height - top - bottom - verticalEdges(box);
    if (definiteHeight < 0) definiteHeight = 0;
  }
  /* `aspect-ratio` derives the content height from the used width when the
   * height is auto. (Height-to-width derivation is not implemented: no page in
   * the target corpus needs it.) */
  if (definiteHeight < 0 && forcedContentHeight < 0 && style.aspect_ratio > 0.0f) {
    float borderWidth = box->border_left + box->padding_left + contentWidth +
                        box->padding_right + box->border_right;
    float borderHeight = borderWidth / style.aspect_ratio;
    definiteHeight = borderHeight - box->border_top - box->border_bottom - box->padding_top -
                     box->padding_bottom;
    if (definiteHeight < 0) definiteHeight = 0;
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
  /* Replaced elements keep their aspect ratio when only one axis is set. A
   * percentage height in an indefinite containing block behaves like `auto`
   * (CSS 10.5), so it must take this path too: the portrait canvases are
   * `height: 100%` inside a shrink-to-fit frame and were otherwise zero-height
   * and invisible. */
  bool replacedHeightAuto =
      isAuto(style.height) || (style.height.unit == Unit::Percent && availableHeight < 0);
  if ((box->is_image || box->is_canvas || box->is_inline_svg) && forcedContentHeight < 0 &&
      replacedHeightAuto && box->intrinsic_width > 0 && box->intrinsic_height > 0) {
    contentHeight = contentWidth * box->intrinsic_height / box->intrinsic_width;
  }
  box->content_height = contentHeight;
  box->height =
      box->border_top + box->padding_top + contentHeight + box->padding_bottom + box->border_bottom;
  box->border_radius = xt_style_border_radius_px(style.border_radius, box->width, box->height,
                                                 em, rootFontSize, vw, vh);
  box->baseline = box->height;
  /* Now that this box has its final size, its padding box is the containing
   * block its own children resolve against — and only now can an out-of-flow
   * child be placed with `bottom`/`right`. */
  publishContainingBlock(box);
  layoutAbsoluteChildren(box, contentWidth, availableHeight);
  applyPosition(box);
  /* The caller owns the vertical margins: it collapses them and advances the
   * flow itself, so the border-box height is what comes back. */
  (void)inFlow;
  return box->height;
}

bool Layouter::isAbsoluteChild(const LayoutBox *box) {
  /* A text box shares its element's style pointer, but a text node can never be
   * out of flow: treating it as absolute dropped direct text out of an
   * absolutely positioned box (and made a shrink-to-fit width zero). */
  return box != nullptr && !box->is_text && box->style != nullptr &&
         (box->style->position == Position::Absolute || box->style->position == Position::Fixed);
}

void Layouter::layoutAbsoluteChildren(LayoutBox *block, float contentWidth,
                                      float availableHeight) const {
  for (const std::unique_ptr<LayoutBox> &child : block->children) {
    if (child->display == Display::None || !isAbsoluteChild(child.get())) continue;
    /* An absolute box resolves against the nearest *positioned* ancestor, not
     * necessarily its parent: under a static <body> the containing block is the
     * initial containing block (the viewport). Passing the parent directly made
     * `inset:0` hug the body's content height instead of filling the window. */
    setContainingBlock(child.get(), containingBlockAncestor(block));
    layoutBlock(child.get(), block->content_x, block->content_y, contentWidth, availableHeight, -1,
                -1, false);
  }
}

float Layouter::layoutChildren(LayoutBox *block, float contentWidth, float availableHeight,
                               float definiteHeight) const {
  if (block->style->display == Display::Flex)
    return layoutFlex(block, contentWidth, definiteHeight);
  if (block->style->display == Display::Grid)
    return layoutGrid(block, contentWidth, definiteHeight);

  float y = block->content_y;
  float startY = y;
  /* The bottom margin of the last in-flow block, waiting to collapse with the
   * next block's top margin. */
  float pending = 0.0f;
  bool hasBlock = false;
  std::vector<LayoutBox *> inlineRun;
  FloatContext floats;
  auto flush = [&]() {
    if (inlineRun.empty()) return;
    /* Source whitespace between block boxes collapses away (HTML's model), so a
     * run that carries no visible content produces no line at all — and,
     * crucially, does not break margin adjacency with the next block. */
    bool visible = false;
    for (LayoutBox *box : inlineRun) {
      if (!isCollapsibleText(box)) {
        visible = true;
        break;
      }
    }
    if (!visible) {
      /* Source whitespace only: no line, and it does not break margin
       * adjacency with the next block. */
      inlineRun.clear();
      return;
    }
    /* Inline content breaks adjacency: apply the pending margin here instead of
     * letting it collapse into the next block. */
    y += pending;
    pending = 0.0f;
    floats.prune(y);
    y += layoutInlineRun(block, inlineRun, contentWidth, y, &floats);
    inlineRun.clear();
    hasBlock = true;
  };
  for (const std::unique_ptr<LayoutBox> &child : block->children) {
    Display display = child->display;
    if (display == Display::None) continue;
    if (isAbsoluteChild(child.get())) {
      /* Out of flow: `layoutAbsoluteChildren` places it once this block knows
       * its own size. */
      continue;
    }
    const XtStyle *childStyle = child->style;
    if (childStyle != nullptr && childStyle->float_side != FloatSide::None) {
      /* A float interrupts the inline run (content before it is placed first,
       * then the float affects the lines that follow), and takes no flow space
       * of its own. */
      flush();
      resolveEdges(child.get(), contentWidth);
      float edges = horizontalEdges(child.get());
      float available = contentWidth - edges;
      if (available < 0) available = 0;
      float outerWidth;
      if (!isAuto(childStyle->width)) {
        outerWidth = px(childStyle->width, contentWidth, childStyle->font_size, 0.0f) + edges;
      } else {
        outerWidth = preferredContentWidth(child.get(), contentWidth);
        if (outerWidth > available) outerWidth = available;
        outerWidth += edges;
      }
      float contentW = outerWidth - edges;
      if (contentW < 0) contentW = 0;
      float floatX = childStyle->float_side == FloatSide::Left
                         ? block->content_x
                         : block->content_x + contentWidth - outerWidth;
      setContainingBlock(child.get(), containingBlockAncestor(block));
      float height =
          layoutBlock(child.get(), floatX, y, contentWidth, availableHeight, contentW);
      ActiveFloat entry;
      entry.left = childStyle->float_side == FloatSide::Left;
      entry.x = child->x;
      entry.width = child->width + child->margin_left + child->margin_right;
      entry.top = y;
      entry.height = height + child->margin_top + child->margin_bottom;
      floats.floats.push_back(entry);
      continue;
    }
    if (display == Display::Block || display == Display::ListItem || display == Display::Flex ||
        display == Display::Grid || child->style == nullptr) {
      if (childStyle != nullptr && childStyle->inline_level) {
        inlineRun.push_back(child.get());
        continue;
      }
      flush();
      resolveEdges(child.get(), contentWidth);
      /* Adjacent vertical margins collapse: the gap is the collapsed value of
       * the waiting bottom margin and this child's top margin. */
      float top = child->margin_top;
      float gap = hasBlock ? collapseMargins(pending, top) : top;
      float desired = y + gap;
      if (childStyle != nullptr && childStyle->clear != Clear::None) {
        bool clearLeft = childStyle->clear == Clear::Left || childStyle->clear == Clear::Both;
        bool clearRight = childStyle->clear == Clear::Right || childStyle->clear == Clear::Both;
        float clearY = floats.clearY(clearLeft, clearRight, y) + top;
        if (desired < clearY) desired = clearY;
      }
      y = desired;
      setContainingBlock(child.get(), containingBlockAncestor(block));
      y += layoutBlock(child.get(), block->content_x, y, contentWidth, availableHeight);
      pending = child->margin_bottom;
      hasBlock = true;
    } else {
      inlineRun.push_back(child.get());
    }
  }
  flush();
  /* The last bottom margin is part of the content box: it escapes only if the
   * parent has no bottom padding/border (handled by the parent's caller). */
  y += pending;
  /* Grow to contain the floats: a block with only floated children would
   * otherwise collapse to zero height. */
  float floatBottom = floats.maxBottom();
  if (floatBottom > y) y = floatBottom;
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
