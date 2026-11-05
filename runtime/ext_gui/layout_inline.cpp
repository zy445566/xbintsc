/*
 * xbintsc GUI engine — inline formatting context.
 *
 * Flattens inline subtrees into text/atomic items, breaks them into lines and
 * writes the resulting visual text fragments onto the boxes (`layout_flow.cpp`
 * owns the surrounding block/flow logic).
 */

#include "layout_flow_internal.h"

#include <utility>

namespace xtgui {
namespace layout_flow_detail {

void Layouter::flattenInline(LayoutBox *box, float contentWidth,
                             std::vector<InlineItem> &out) const {
  const XtStyle &style = *box->style;
  if (box->is_text) {
    if (style.writing_mode != WritingMode::HorizontalTB) {
      flattenVerticalText(box, out);
      return;
    }
    float height = lineHeightOf(style);
    float ascent = ascentOf(style);
    const std::string &text = box->text;
    size_t i = 0;
    while (i < text.size()) {
      if (text[i] == '\n' || text[i] == '\r') {
        /* `nowrap` collapses a source newline to a space; the other modes keep
         * the engine's existing forced break (notably `pre`, which must). */
        bool preserveNewline = style.white_space != WhiteSpace::NoWrap;
        InlineItem item;
        item.box = box;
        item.style = &style;
        if (preserveNewline) {
          item.force_break = true;
        } else {
          item.text = " ";
          item.is_space = true;
          item.width = textWidth(" ", style);
          item.height = height;
          item.ascent = ascent;
        }
        out.push_back(std::move(item));
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
  if (box->display == Display::InlineBlock || style.inline_level ||
      style.writing_mode != WritingMode::HorizontalTB) {
    float available = contentWidth;
    float intrinsic = preferredContentWidth(box, available);
    setContainingBlock(box, containingBlockAncestor(box));
    /* An explicit width must go through the normal used-width path so
     * `box-sizing: border-box` can subtract padding and borders. Only an `auto`
     * width needs the shrink-to-fit content width forced here. */
    layoutBlock(box, 0, 0, available, -1, isAuto(style.width) ? intrinsic : -1.0f);
    InlineItem item;
    item.box = box;
    item.style = &style;
    item.atomic = true;
    item.width = box->margin_left + box->width + box->margin_right;
    item.height = box->margin_top + box->height + box->margin_bottom;
    float shift = 0.0f;
    if (!isAuto(style.vertical_align)) {
      shift = px(style.vertical_align, available, style.font_size, 0.0f);
    }
    /* A positive `vertical-align` raises the atomic box above the baseline;
     * the line box keeps its own ascent so the shift is purely visual. */
    item.shift = shift;
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
  /* An atomic inline box already has geometry from its own formatting context;
   * replacing it with the union of its descendants would shrink an inline-grid
   * to its text (and an inline-block to its content). */
  if (!box->is_text && box->style != nullptr &&
      (box->display == Display::InlineBlock || box->style->inline_level)) {
    return;
  }
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
  if (containerStyle.writing_mode != WritingMode::HorizontalTB) {
    float height = layoutVerticalRun(container, items, contentWidth, y);
    for (LayoutBox *box : run) computeInlineGeometry(box);
    return height;
  }
  /* `nowrap`/`pre` keep the run on one line (a preserved newline still breaks
   * it); without that, `text-overflow: ellipsis` would never see an overflowing
   * line on ordinary prose. */
  const bool wrap = containerStyle.white_space == WhiteSpace::Normal ||
                    containerStyle.white_space == WhiteSpace::PreWrap ||
                    containerStyle.white_space == WhiteSpace::PreLine;
  const bool ellipsis = containerStyle.text_overflow == TextOverflow::Ellipsis &&
                        containerStyle.overflow != Overflow::Visible;
  float cursorY = y;
  std::vector<InlineItem *> line;
  float lineWidth = 0;
  float lineAscent = 0;
  float lineDescent = 0;

  /* Byte length of the longest UTF-8 prefix of `text` that fits in `budget`
   * when measured with `style`, without splitting a multi-byte code point. */
  auto fittingPrefix = [&](const std::string &text, const XtStyle &style, float budget) {
    size_t best = 0;
    for (size_t i = 0; i < text.size();) {
      size_t next = i + 1;
      while (next < text.size() && ((unsigned char)text[next] & 0xC0) == 0x80) next++;
      if (textWidth(text.substr(0, next), style) > budget + 0.001f) break;
      best = next;
      i = next;
    }
    return best;
  };

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
    if (ellipsis && lineWidth > available + 0.001f) {
      /* Replace the part of the line that does not fit with one ellipsis. The
       * ellipsis gets the style of the run it terminates, so a smaller/larger
       * inline span keeps its own metrics. */
      const std::string dots = "\xE2\x80\xA6";  // U+2026 HORIZONTAL ELLIPSIS
      float dotsWidth = textWidth(dots, containerStyle);
      float budget = available - dotsWidth;
      if (budget < 0.0f) budget = 0.0f;
      std::vector<InlineItem *> kept;
      float used = 0.0f;
      for (InlineItem *item : line) {
        if (used + item->width <= budget + 0.001f) {
          kept.push_back(item);
          used += item->width;
          continue;
        }
        if (item->atomic) {
          /* An atomic inline box cannot carry the ellipsis itself; append it to
           * the last text run when one still fits, otherwise drop the box. */
          if (!kept.empty() && !kept.back()->atomic && !kept.back()->is_space) {
            kept.back()->text += dots;
            kept.back()->width += dotsWidth;
          }
          break;
        }
        size_t prefix = fittingPrefix(item->text, *item->style, budget - used);
        std::string text = item->text.substr(0, prefix);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
        item->text = text + dots;
        item->width = textWidth(text, *item->style) + dotsWidth;
        kept.push_back(item);
        break;
      }
      line = std::move(kept);
      lineWidth = 0.0f;
      for (InlineItem *item : line) lineWidth += item->width;
      if (line.empty()) return;
    }
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
          float dy = cursorY + item->box->margin_top - item->box->y - item->shift;
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

  for (size_t index = 0; index < items.size(); index++) {
    InlineItem &item = items[index];
    if (item.force_break) {
      finalize();
      continue;
    }
    if (item.is_space && line.empty()) continue;  // no leading space on a line
    /* A `break-word`/`anywhere` word that does not fit is split at the
     * largest prefix the remaining line can hold; the remainder is processed
     * again on the next line. Without this a long token overflowed the box. */
    for (;;) {
      /* Break against the band the line would occupy, then place it. */
      float pendingHeight = std::max(lineAscent + lineDescent, item.height);
      float x0 = 0;
      float x1 = 0;
      band(cursorY, pendingHeight, &x0, &x1);
      float available = x1 - x0;
      bool overflows = lineWidth + item.width > available + 0.001f;
      if (wrap && overflows) {
        bool breakWord = item.style != nullptr &&
                         item.style->overflow_wrap != OverflowWrap::Normal;
        if (breakWord && !item.atomic && !item.is_space && item.text.size() > 1) {
          float room = available - lineWidth;
          size_t prefix = fittingPrefix(item.text, *item.style, room);
          if (prefix > 0 && prefix < item.text.size()) {
            InlineItem head = item;
            head.text = item.text.substr(0, prefix);
            head.width = textWidth(head.text, *item.style);
            line.push_back(&head);
            lineWidth += head.width;
            lineAscent = std::max(lineAscent, head.ascent);
            lineDescent = std::max(lineDescent, head.height - head.ascent);
            finalize();
            item.text = item.text.substr(prefix);
            item.width = textWidth(item.text, *item.style);
            continue;
          }
        }
        if (!line.empty()) finalize();
      }
      if (item.is_space && line.empty()) break;
      line.push_back(&item);
      lineWidth += item.width;
      lineAscent = std::max(lineAscent, item.ascent);
      lineDescent = std::max(lineDescent, item.height - item.ascent);
      break;
    }
  }
  finalize();

  for (LayoutBox *box : run) computeInlineGeometry(box);
  return cursorY - y;
}

}  // namespace layout_flow_detail
}  // namespace xtgui
