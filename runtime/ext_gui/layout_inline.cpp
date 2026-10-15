/*
 * xbintsc GUI engine — inline formatting context.
 *
 * Flattens inline subtrees into text/atomic items, breaks them into lines and
 * writes the resulting visual text fragments onto the boxes (`layout_flow.cpp`
 * owns the surrounding block/flow logic).
 */

#include "layout_flow_internal.h"

namespace xtgui {
namespace layout_flow_detail {

void Layouter::flattenInline(LayoutBox *box, float contentWidth,
                             std::vector<InlineItem> &out) const {
  const XtStyle &style = *box->style;
  if (box->is_text) {
    float height = lineHeightOf(style);
    float ascent = ascentOf(style);
    const std::string &text = box->text;
    size_t i = 0;
    while (i < text.size()) {
      if (text[i] == '\n') {
        InlineItem br;
        br.box = box;
        br.style = &style;
        br.force_break = true;
        out.push_back(std::move(br));
        i++;
        continue;
      }
      bool space = text[i] == ' ' || text[i] == '\t';
      size_t j = i;
      while (j < text.size() && text[j] != '\n' && (text[j] == ' ' || text[j] == '\t') == space) j++;
      InlineItem item;
      item.box = box;
      item.style = &style;
      item.text = text.substr(i, j - i);
      item.is_space = space;
      item.width = textWidth(item.text, style);
      item.height = height;
      item.ascent = ascent;
      out.push_back(std::move(item));
      i = j;
    }
    return;
  }
  if (box->display == Display::InlineBlock) {
    float available = contentWidth;
    float intrinsic = preferredContentWidth(box, available);
    setContainingBlock(box, containingBlockAncestor(box));
    layoutBlock(box, 0, 0, available, -1, intrinsic);
    InlineItem item;
    item.box = box;
    item.style = &style;
    item.atomic = true;
    item.width = box->margin_left + box->width + box->margin_right;
    item.height = box->margin_top + box->height + box->margin_bottom;
    item.ascent = item.height;
    out.push_back(std::move(item));
    return;
  }
  /* Inline element: flatten its subtree. */
  for (const std::unique_ptr<LayoutBox> &child : box->children) {
    if (child->display == Display::None) continue;
    flattenInline(child.get(), contentWidth, out);
  }
}

void Layouter::computeInlineGeometry(LayoutBox *box) const {
  if (box->is_text) {
    if (box->fragments.empty()) return;
    float minx = kHuge, miny = kHuge, maxx = -kHuge, maxy = -kHuge;
    for (const TextFragment &fragment : box->fragments) {
      minx = std::min(minx, fragment.x);
      miny = std::min(miny, fragment.y);
      maxx = std::max(maxx, fragment.x + fragment.width);
      maxy = std::max(maxy, fragment.y + fragment.height);
    }
    box->x = minx;
    box->y = miny;
    box->width = maxx - minx;
    box->height = maxy - miny;
    box->content_x = minx;
    box->content_y = miny;
    box->content_width = box->width;
    box->content_height = box->height;
    return;
  }
  float minx = kHuge, miny = kHuge, maxx = -kHuge, maxy = -kHuge;
  bool any = false;
  for (const std::unique_ptr<LayoutBox> &child : box->children) {
    if (child->display == Display::None) continue;
    computeInlineGeometry(child.get());
    if (child->is_text && child->fragments.empty()) continue;
    if (child->width <= 0 && child->height <= 0 && !child->is_text && child->children.empty())
      continue;
    any = true;
    minx = std::min(minx, child->x);
    miny = std::min(miny, child->y);
    maxx = std::max(maxx, child->x + child->width);
    maxy = std::max(maxy, child->y + child->height);
  }
  if (!any) return;
  box->x = minx;
  box->y = miny;
  box->width = maxx - minx;
  box->height = maxy - miny;
  box->content_x = minx;
  box->content_y = miny;
  box->content_width = box->width;
  box->content_height = box->height;
}

float Layouter::layoutInlineRun(LayoutBox *container, const std::vector<LayoutBox *> &run,
                                float contentWidth, float y, const FloatContext *floats) const {
  std::vector<InlineItem> items;
  for (LayoutBox *box : run) flattenInline(box, contentWidth, items);
  if (items.empty()) return 0;

  const XtStyle &containerStyle = *container->style;
  float cursorY = y;
  std::vector<InlineItem *> line;
  float lineWidth = 0;
  float lineAscent = 0;
  float lineDescent = 0;

  /* Horizontal band left free by the floats overlapping [top, top + height). */
  auto band = [&](float top, float height, float *x0, float *x1) {
    *x0 = container->content_x;
    *x1 = container->content_x + contentWidth;
    if (floats != nullptr && height > 0) {
      *x0 = floats->leftEdge(*x0, top, top + height);
      *x1 = floats->rightEdge(*x1, top, top + height);
    }
    if (*x1 < *x0) *x1 = *x0;
  };

  auto finalize = [&]() {
    while (!line.empty() && line.back()->is_space) {
      lineWidth -= line.back()->width;
      line.pop_back();
    }
    if (line.empty()) return;
    float height = lineAscent + lineDescent;
    float x0 = 0;
    float x1 = 0;
    band(cursorY, height, &x0, &x1);
    float available = x1 - x0;
    float x = x0;
    if (containerStyle.text_align == TextAlign::Center) {
      x += (available - lineWidth) / 2.0f;
    } else if (containerStyle.text_align == TextAlign::End) {
      x += available - lineWidth;
    }
    for (InlineItem *item : line) {
      if (item->atomic) {
        /* An out-of-flow item was already placed against its containing block by
         * `layoutBlock`, so it must not be moved back onto the line. */
        if (!isAbsoluteChild(item->box)) {
          float dx = x + item->box->margin_left - item->box->x;
          float dy = cursorY + item->box->margin_top - item->box->y;
          translate(item->box, dx, dy);
          /* `position: relative` on an inline-block shifts it after line
           * placement, so the line box keeps the space it reserved. */
          applyPosition(item->box);
        }
      } else {
        item->box->fragments.push_back({x, cursorY, item->width, height, item->text});
      }
      x += item->width;
    }
    cursorY += height;
    line.clear();
    lineWidth = 0;
    lineAscent = 0;
    lineDescent = 0;
  };

  for (InlineItem &item : items) {
    if (item.force_break) {
      finalize();
      continue;
    }
    if (item.is_space && line.empty()) continue;  // no leading space on a line
    /* Break against the band the line would occupy, then place it. */
    float pendingHeight = std::max(lineAscent + lineDescent, item.height);
    float x0 = 0;
    float x1 = 0;
    band(cursorY, pendingHeight, &x0, &x1);
    float available = x1 - x0;
    if (lineWidth + item.width > available && !line.empty()) finalize();
    if (item.is_space && line.empty()) continue;
    line.push_back(&item);
    lineWidth += item.width;
    lineAscent = std::max(lineAscent, item.ascent);
    lineDescent = std::max(lineDescent, item.height - item.ascent);
  }
  finalize();

  for (LayoutBox *box : run) computeInlineGeometry(box);
  return cursorY - y;
}

}  // namespace layout_flow_detail
}  // namespace xtgui
