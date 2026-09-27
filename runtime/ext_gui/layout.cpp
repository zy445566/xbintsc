/*
 * xbintsc GUI engine — layout implementation.
 *
 * The algorithm is intentionally a pragmatic subset:
 *   - Block flow: consecutive inline-level children form an anonymous inline
 *     formatting context; block-level children are stacked vertically.
 *   - Inline flow: greedy, word-based line breaking with `text-align`; text
 *     metrics are approximated (real shaping arrives with M4).
 *   - Flexbox: single-line row/column with grow/shrink, gap, justify-content
 *     and align-items.
 * Margins do not collapse yet, and only static positioning is honoured.
 */

#include "layout.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace xtgui {
namespace {

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

float toPx(const Length &length, float base, float em, float root, float vw, float vh, float fallback) {
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

bool isAuto(const Length &length) {
  return length.unit == Unit::Auto || length.unit == Unit::Invalid || length.unit == Unit::None;
}

/* Per-character advance as a fraction of the font size; deliberately crude but
 * deterministic. Replaced by real shaping in M4. */
float charAdvance(unsigned char c) {
  if (c < 0x20 || c == 0x7f) return 0.0f;
  if (c >= 0x80) return 0.55f;
  char ch = (char)c;
  if (ch == ' ') return 0.28f;
  if (std::strchr("ilj|.,:;'!`", ch) != nullptr) return 0.26f;
  if (std::strchr("mwMW@%", ch) != nullptr) return 0.85f;
  if (ch >= '0' && ch <= '9') return 0.56f;
  if (ch >= 'A' && ch <= 'Z') return 0.64f;
  if (std::strchr("()[]{}<>/\\\"", ch) != nullptr) return 0.36f;
  return 0.52f;
}

float lineHeightOf(const XtStyle &style) {
  if (style.line_height.unit == Unit::Number) {
    return style.line_height.value > 0 ? style.line_height.value * style.font_size : style.font_size * 1.2f;
  }
  if (style.line_height.unit == Unit::Px) return style.line_height.value;
  return style.font_size * 1.2f;
}

float ascentOf(const XtStyle &style) { return style.font_size * 0.8f; }

struct Layouter {
  const std::unordered_map<const Node *, XtStyle> *styles = nullptr;
  LayoutTree *tree = nullptr;
  float vw = 0;
  float vh = 0;
  float rootFontSize = 16.0f;

  const XtStyle *styleOf(const Node *node) const {
    auto it = styles->find(node);
    return it == styles->end() ? nullptr : &it->second;
  }

  float px(const Length &length, float base, float em, float fallback = 0.0f) const {
    return toPx(length, base, em, rootFontSize, vw, vh, fallback);
  }

  float clampSize(float value, const Length &minLength, const Length &maxLength, float base,
                  float em) const {
    if (!isAuto(minLength)) value = std::max(value, px(minLength, base, em, value));
    if (!isAuto(maxLength)) value = std::min(value, px(maxLength, base, em, value));
    return value;
  }

  float textWidth(const std::string &text, const XtStyle &style) const {
    bool monospace = style.font_family.find("mono") != std::string::npos;
    float factor = monospace ? 0.6f : 1.0f;
    float width = 0;
    for (char c : text) width += charAdvance((unsigned char)c) * factor;
    return width * style.font_size;
  }

  float horizontalEdges(const LayoutBox *box) const {
    return box->margin_left + box->margin_right + box->border_left + box->border_right +
           box->padding_left + box->padding_right;
  }

  float verticalEdges(const LayoutBox *box) const {
    return box->margin_top + box->margin_bottom + box->border_top + box->border_bottom +
           box->padding_top + box->padding_bottom;
  }

  float preferredContentWidth(const LayoutBox *box, float available) const {
    const XtStyle &style = *box->style;
    if (!isAuto(style.width)) return px(style.width, available, style.font_size, 0.0f);
    float best = 0;
    for (const std::unique_ptr<LayoutBox> &child : box->children) {
      if (child->display == Display::None) continue;
      if (child->is_text) {
        best = std::max(best, textWidth(child->text, *child->style));
      } else {
        best = std::max(best, preferredContentWidth(child.get(), available) + horizontalEdges(child.get()));
      }
    }
    return best;
  }

  void translate(LayoutBox *box, float dx, float dy) const {
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

  void resolveEdges(LayoutBox *box, float containingWidth) const {
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

  /** Lay out `box` at (x, y) and return its outer height (margins included). */
  float layoutBlock(LayoutBox *box, float x, float y, float availableWidth, float availableHeight,
                    float forcedContentWidth = -1, float forcedContentHeight = -1) const {
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
    box->width = box->border_left + box->padding_left + contentWidth + box->padding_right + box->border_right;

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
    box->content_height = contentHeight;
    box->height = box->border_top + box->padding_top + contentHeight + box->padding_bottom + box->border_bottom;
    box->baseline = box->height;
    return box->margin_top + box->height + box->margin_bottom;
  }

  /** Lay out the children of `block`; returns the used content height. */
  float layoutChildren(LayoutBox *block, float contentWidth, float availableHeight,
                       float definiteHeight) const {
    if (block->style->display == Display::Flex) return layoutFlex(block, contentWidth, definiteHeight);

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

  void flattenInline(LayoutBox *box, float contentWidth, std::vector<InlineItem> &out) const {
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

  void computeInlineGeometry(LayoutBox *box) const {
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
      if (child->width <= 0 && child->height <= 0 && !child->is_text && child->children.empty()) continue;
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

  /** Lay out an inline formatting context; returns its consumed height. */
  float layoutInlineRun(LayoutBox *container, const std::vector<LayoutBox *> &run, float contentWidth,
                        float y) const {
    std::vector<InlineItem> items;
    for (LayoutBox *box : run) flattenInline(box, contentWidth, items);
    if (items.empty()) return 0;

    std::vector<std::vector<InlineItem *>> lines;
    std::vector<InlineItem *> line;
    float lineWidth = 0;
    auto pushLine = [&]() {
      while (!line.empty() && line.back()->is_space) {
        lineWidth -= line.back()->width;
        line.pop_back();
      }
      if (!line.empty()) lines.push_back(line);
      line.clear();
      lineWidth = 0;
    };
    for (InlineItem &item : items) {
      if (item.force_break) {
        pushLine();
        continue;
      }
      if (item.is_space && line.empty()) continue;  // no leading space on a line
      if (lineWidth + item.width > contentWidth && !line.empty()) pushLine();
      if (item.is_space && line.empty()) continue;
      line.push_back(&item);
      lineWidth += item.width;
    }
    pushLine();

    const XtStyle &containerStyle = *container->style;
    float cursorY = y;
    for (size_t index = 0; index < lines.size(); index++) {
      std::vector<InlineItem *> &current = lines[index];
      float width = 0;
      float ascent = 0;
      float descent = 0;
      for (InlineItem *item : current) {
        width += item->width;
        ascent = std::max(ascent, item->ascent);
        descent = std::max(descent, item->height - item->ascent);
      }
      float height = ascent + descent;
      float x = container->content_x;
      if (containerStyle.text_align == TextAlign::Center) {
        x += (contentWidth - width) / 2.0f;
      } else if (containerStyle.text_align == TextAlign::End) {
        x += contentWidth - width;
      }
      for (InlineItem *item : current) {
        if (item->atomic) {
          float dx = x + item->box->margin_left - item->box->x;
          float dy = cursorY + item->box->margin_top - item->box->y;
          translate(item->box, dx, dy);
        } else {
          item->box->fragments.push_back({x, cursorY, item->width, height});
        }
        x += item->width;
      }
      cursorY += height;
    }
    for (LayoutBox *box : run) computeInlineGeometry(box);
    return cursorY - y;
  }

  float layoutFlex(LayoutBox *container, float contentWidth, float definiteHeight) const {
    const XtStyle &containerStyle = *container->style;
    bool isRow = containerStyle.flex_direction == FlexDirection::Row ||
                 containerStyle.flex_direction == FlexDirection::RowReverse;
    bool reverse = containerStyle.flex_direction == FlexDirection::RowReverse ||
                   containerStyle.flex_direction == FlexDirection::ColumnReverse;
    float gap = px(containerStyle.gap, contentWidth, containerStyle.font_size, 0.0f);

    std::vector<LayoutBox *> items;
    for (const std::unique_ptr<LayoutBox> &child : container->children) {
      if (child->display != Display::None) items.push_back(child.get());
    }
    if (items.empty()) return 0;
    size_t count = items.size();

    if (isRow) {
      float mainSize = contentWidth;
      std::vector<float> outer(count, 0.0f);
      std::vector<float> mainSizes(count, 0.0f);
      float sumBase = 0;
      for (size_t i = 0; i < count; i++) {
        const XtStyle &style = *items[i]->style;
        float basis;
        if (isAuto(style.flex_basis)) {
          if (!isAuto(style.width)) basis = px(style.width, mainSize, style.font_size, 0.0f);
          else basis = preferredContentWidth(items[i], mainSize);
        } else {
          basis = px(style.flex_basis, mainSize, style.font_size, 0.0f);
        }
        resolveEdges(items[i], mainSize);
        float edges = horizontalEdges(items[i]);
        outer[i] = basis + edges;
        mainSizes[i] = outer[i];
        sumBase += outer[i];
      }
      float free = mainSize - sumBase - gap * (float)(count - 1);
      if (free > 0) {
        float totalGrow = 0;
        for (LayoutBox *item : items) totalGrow += item->style->flex_grow;
        if (totalGrow > 0) {
          for (size_t i = 0; i < count; i++) mainSizes[i] += free * items[i]->style->flex_grow / totalGrow;
        }
      } else if (free < 0) {
        float totalShrink = 0;
        for (size_t i = 0; i < count; i++) totalShrink += items[i]->style->flex_shrink * outer[i];
        if (totalShrink > 0) {
          for (size_t i = 0; i < count; i++) {
            mainSizes[i] += free * (items[i]->style->flex_shrink * outer[i]) / totalShrink;
            mainSizes[i] = std::max(0.0f, mainSizes[i]);
          }
        }
      }

      /* Cross sizes: lay out once to measure, then apply cross alignment. */
      float crossSize = definiteHeight >= 0 ? definiteHeight : 0;
      std::vector<float> cross(count, 0.0f);
      for (size_t i = 0; i < count; i++) {
        float contentW = mainSizes[i] - horizontalEdges(items[i]);
        if (contentW < 0) contentW = 0;
        layoutBlock(items[i], container->content_x, container->content_y, contentWidth, definiteHeight,
                    contentW);
        cross[i] = verticalEdges(items[i]) + items[i]->height;
        if (definiteHeight < 0) crossSize = std::max(crossSize, cross[i]);
      }
      if (containerStyle.align_items == AlignItems::Stretch && definiteHeight >= 0) {
        for (size_t i = 0; i < count; i++) {
          if (isAuto(items[i]->style->height)) {
            float contentH = crossSize - verticalEdges(items[i]);
            if (contentH < 0) contentH = 0;
            float contentW = mainSizes[i] - horizontalEdges(items[i]);
            layoutBlock(items[i], container->content_x, container->content_y, contentWidth, definiteHeight,
                        contentW, contentH);
          }
        }
      }

      /* justify-content: place the main axis. */
      float freeSpace = mainSize - (sumOf(mainSizes) + gap * (float)(count - 1));
      float offset = 0;
      float between = gap;
      applyJustify(containerStyle.justify_content, freeSpace, count, &offset, &between);

      float cursor = container->content_x + offset;
      for (size_t k = 0; k < count; k++) {
        size_t i = reverse ? (count - 1 - k) : k;
        float crossOffset = crossOffsetFor(containerStyle.align_items, crossSize, cross[i]);
        float dx = cursor - items[i]->x;
        float dy = container->content_y + crossOffset - items[i]->y;
        translate(items[i], dx, dy);
        cursor += mainSizes[i] + between;
      }
      return crossSize;
    }

    /* Column flex. */
    float mainSize = definiteHeight >= 0 ? definiteHeight : -1;
    std::vector<float> outer(count, 0.0f);
    float sumBase = 0;
    for (size_t i = 0; i < count; i++) {
      layoutBlock(items[i], container->content_x, container->content_y, contentWidth, definiteHeight);
      outer[i] = verticalEdges(items[i]) + items[i]->height;
      sumBase += outer[i];
    }
    std::vector<float> mainSizes = outer;
    if (mainSize >= 0) {
      float free = mainSize - sumBase - gap * (float)(count - 1);
      if (free > 0) {
        float totalGrow = 0;
        for (LayoutBox *item : items) totalGrow += item->style->flex_grow;
        if (totalGrow > 0) {
          for (size_t i = 0; i < count; i++) {
            mainSizes[i] += free * items[i]->style->flex_grow / totalGrow;
            float contentH = mainSizes[i] - verticalEdges(items[i]);
            if (contentH < 0) contentH = 0;
            layoutBlock(items[i], container->content_x, container->content_y, contentWidth, definiteHeight, -1,
                        contentH);
          }
        }
      }
    } else {
      mainSize = sumBase + gap * (float)(count - 1);
    }
    float freeSpace = mainSize - (sumOf(mainSizes) + gap * (float)(count - 1));
    float offset = 0;
    float between = gap;
    applyJustify(containerStyle.justify_content, freeSpace, count, &offset, &between);

    float cursor = container->content_y + offset;
    for (size_t k = 0; k < count; k++) {
      size_t i = reverse ? (count - 1 - k) : k;
      float dx = container->content_x - items[i]->x;
      float dy = cursor - items[i]->y;
      translate(items[i], dx, dy);
      cursor += mainSizes[i] + between;
    }
    return mainSize;
  }

  static float sumOf(const std::vector<float> &values) {
    float total = 0;
    for (float value : values) total += value;
    return total;
  }

  static void applyJustify(JustifyContent justify, float freeSpace, size_t count, float *offset,
                           float *between) {
    if (count == 0) return;
    if (freeSpace < 0) freeSpace = 0;
    switch (justify) {
      case JustifyContent::FlexEnd:
        *offset = freeSpace;
        break;
      case JustifyContent::Center:
        *offset = freeSpace / 2.0f;
        break;
      case JustifyContent::SpaceBetween:
        if (count > 1) *between += freeSpace / (float)(count - 1);
        break;
      case JustifyContent::SpaceAround:
        *between += freeSpace / (float)count;
        *offset = freeSpace / (2.0f * (float)count);
        break;
      case JustifyContent::SpaceEvenly:
        *between += freeSpace / (float)(count + 1);
        *offset = freeSpace / (float)(count + 1);
        break;
      default:
        break;
    }
  }

  static float crossOffsetFor(AlignItems align, float crossSize, float itemCross) {
    switch (align) {
      case AlignItems::FlexEnd:
        return crossSize - itemCross;
      case AlignItems::Center:
        return (crossSize - itemCross) / 2.0f;
      case AlignItems::Baseline:
        return crossSize - itemCross;
      default:
        return 0.0f;
    }
  }
};

std::unique_ptr<LayoutBox> buildBoxTree(const Node *node,
                                        const std::unordered_map<const Node *, XtStyle> &styles,
                                        const XtStyle *inherited) {
  if (node->isText()) {
    auto box = std::make_unique<LayoutBox>();
    box->node = node;
    box->is_text = true;
    box->text = node->text;
    box->style = inherited;
    box->display = Display::Inline;
    return box;
  }
  auto it = styles.find(node);
  const XtStyle *style = it == styles.end() ? inherited : &it->second;
  if (style != nullptr && style->display == Display::None) return nullptr;

  auto box = std::make_unique<LayoutBox>();
  box->node = node;
  box->style = style;
  box->display = style != nullptr ? style->display : Display::Block;
  for (const std::unique_ptr<Node> &child : node->children) {
    std::unique_ptr<LayoutBox> childBox = buildBoxTree(child.get(), styles, style);
    if (childBox != nullptr) {
      childBox->parent = box.get();
      box->children.push_back(std::move(childBox));
    }
  }
  return box;
}

std::string dumpBox(const LayoutBox *box, int depth) {
  std::string out(depth * 2, ' ');
  if (box->is_text) {
    out += "#text";
  } else if (box->node != nullptr && box->node->isElement()) {
    out += "<" + box->node->tag + ">";
  } else {
    out += "#root";
  }
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), " x=%.1f y=%.1f w=%.1f h=%.1f\n", box->x, box->y, box->width,
                box->height);
  out += buffer;
  for (const std::unique_ptr<LayoutBox> &child : box->children) out += dumpBox(child.get(), depth + 1);
  return out;
}

}  // namespace

float xt_layout_text_width(const std::string &text, const XtStyle &style) {
  bool monospace = style.font_family.find("mono") != std::string::npos;
  float factor = monospace ? 0.6f : 1.0f;
  float width = 0;
  for (char c : text) width += charAdvance((unsigned char)c) * factor;
  return width * style.font_size;
}

void LayoutTree::compute(const Node *root, const std::unordered_map<const Node *, XtStyle> &styles,
                         float viewportWidth, float viewportHeight) {
  root_.reset();
  index_.clear();
  root_style_ = xt_style_initial();
  root_style_.display = Display::Block;

  Layouter layouter;
  layouter.styles = &styles;
  layouter.tree = this;
  layouter.vw = viewportWidth;
  layouter.vh = viewportHeight;

  root_ = std::make_unique<LayoutBox>();
  root_->node = root;
  root_->style = &root_style_;
  root_->display = Display::Block;
  if (root != nullptr) {
    if (root->type == NodeType::Document) {
      for (const std::unique_ptr<Node> &child : root->children) {
        std::unique_ptr<LayoutBox> box = buildBoxTree(child.get(), styles, &root_style_);
        if (box != nullptr) {
          box->parent = root_.get();
          root_->children.push_back(std::move(box));
        }
      }
    } else {
      std::unique_ptr<LayoutBox> box = buildBoxTree(root, styles, &root_style_);
      if (box != nullptr) {
        box->parent = root_.get();
        root_->children.push_back(std::move(box));
      }
    }
  }
  layouter.layoutBlock(root_.get(), 0, 0, viewportWidth, viewportHeight);

  std::vector<const LayoutBox *> stack;
  stack.push_back(root_.get());
  while (!stack.empty()) {
    const LayoutBox *box = stack.back();
    stack.pop_back();
    if (box->node != nullptr) index_[box->node] = box;
    for (const std::unique_ptr<LayoutBox> &child : box->children) stack.push_back(child.get());
  }
}

const LayoutBox *LayoutTree::find(const Node *node) const {
  auto it = index_.find(node);
  return it == index_.end() ? nullptr : it->second;
}

std::string LayoutTree::dump() const {
  return root_ == nullptr ? std::string() : dumpBox(root_.get(), 0);
}

}  // namespace xtgui
