/*
 * xbintsc GUI engine — flex formatting context.
 *
 * Sizes flex items along the main axis (grow/shrink/basis), wraps them into
 * lines when `flex-wrap` allows it, resolves the cross axis (align-items /
 * align-content) and places them (justify-content), reusing the block and
 * inline helpers from `layout_flow.cpp` / `layout_inline.cpp`.
 */

#include "layout_flow_internal.h"

namespace xtgui {
namespace layout_flow_detail {

/** Give the line's free main-axis space to `auto` margins (CSS flexbox). Returns
 * true when any item had one; the caller then skips `justify-content`. */
static bool applyAutoMainMargins(const std::vector<LayoutBox *> &items,
                                 const std::vector<size_t> &line, bool row, float freeSpace) {
  int count = 0;
  for (size_t i : line) {
    const XtStyle &style = *items[i]->style;
    if (row) {
      if (style.margin_left_auto) count++;
      if (style.margin_right_auto) count++;
    } else {
      if (style.margin_top_auto) count++;
      if (style.margin_bottom_auto) count++;
    }
  }
  if (count == 0) return false;
  if (freeSpace < 0.0f) freeSpace = 0.0f;
  float share = freeSpace / (float)count;
  for (size_t i : line) {
    const XtStyle &style = *items[i]->style;
    if (row) {
      if (style.margin_left_auto) items[i]->margin_left = share;
      if (style.margin_right_auto) items[i]->margin_right = share;
    } else {
      if (style.margin_top_auto) items[i]->margin_top = share;
      if (style.margin_bottom_auto) items[i]->margin_bottom = share;
    }
  }
  return true;
}

/** Partition `basis` (outer main sizes) into lines, breaking before an item
 * that no longer fits on the current line. A single line is produced when
 * `wrap` is false. */
static std::vector<std::vector<size_t>> breakIntoLines(const std::vector<float> &basis,
                                                       float mainSize, float gap, bool wrap) {
  std::vector<std::vector<size_t>> lines;
  std::vector<size_t> current;
  float used = 0;
  for (size_t i = 0; i < basis.size(); i++) {
    float add = basis[i] + (current.empty() ? 0.0f : gap);
    if (wrap && !current.empty() && used + add > mainSize + 0.001f) {
      lines.push_back(current);
      current.clear();
      used = 0;
      add = basis[i];
    }
    current.push_back(i);
    used += add;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

float Layouter::layoutFlex(LayoutBox *container, float contentWidth, float definiteHeight) const {
  const XtStyle &containerStyle = *container->style;
  /* In a vertical writing mode the inline axis is vertical, so a `row` flex
   * container stacks its items top-to-bottom (and `column` lays them out
   * horizontally). */
  FlexDirection direction = containerStyle.flex_direction;
  if (containerStyle.writing_mode != WritingMode::HorizontalTB) {
    switch (direction) {
      case FlexDirection::Row: direction = FlexDirection::Column; break;
      case FlexDirection::RowReverse: direction = FlexDirection::ColumnReverse; break;
      case FlexDirection::Column: direction = FlexDirection::Row; break;
      case FlexDirection::ColumnReverse: direction = FlexDirection::RowReverse; break;
    }
  }
  bool isRow = direction == FlexDirection::Row || direction == FlexDirection::RowReverse;

  /* An absolutely positioned child is out of flow: it takes no part in the flex
   * algorithm and is placed against the container's padding box instead. */
  std::vector<LayoutBox *> items;
  for (const std::unique_ptr<LayoutBox> &child : container->children) {
    if (child->display == Display::None || isAbsoluteChild(child.get())) continue;
    /* Whitespace-only text (source indentation) does not become a flex item. */
    if (isCollapsibleText(child.get())) continue;
    items.push_back(child.get());
  }
  if (items.empty()) return 0;

  float mainGap =
      px(isRow ? containerStyle.column_gap : containerStyle.row_gap, contentWidth,
         containerStyle.font_size, 0.0f);
  float crossGap =
      px(isRow ? containerStyle.row_gap : containerStyle.column_gap, contentWidth,
         containerStyle.font_size, 0.0f);
  if (isRow)
    return layoutFlexRow(container, items, contentWidth, definiteHeight, mainGap, crossGap,
                         direction);
  return layoutFlexColumn(container, items, contentWidth, definiteHeight, mainGap, crossGap,
                          direction);
}

float Layouter::layoutFlexRow(LayoutBox *container, const std::vector<LayoutBox *> &items,
                              float contentWidth, float definiteHeight, float mainGap,
                              float crossGap, FlexDirection direction) const {
  const XtStyle &containerStyle = *container->style;
  bool reverse = direction == FlexDirection::RowReverse;
  bool wrap = containerStyle.flex_wrap != FlexWrap::NoWrap;
  bool wrapReverse = containerStyle.flex_wrap == FlexWrap::WrapReverse;
  size_t count = items.size();
  float mainSize = contentWidth;

  /* Main-axis basis: flex-basis, then width, then preferred content width. */
  std::vector<float> basis(count, 0.0f);
  for (size_t i = 0; i < count; i++) {
    const XtStyle &style = *items[i]->style;
    float base;
    if (isAuto(style.flex_basis)) {
      if (!isAuto(style.width)) base = px(style.width, mainSize, style.font_size, 0.0f);
      else base = preferredContentWidth(items[i], mainSize);
    } else {
      base = px(style.flex_basis, mainSize, style.font_size, 0.0f);
    }
    resolveEdges(items[i], mainSize);
    basis[i] = base + horizontalEdges(items[i]);
  }

  std::vector<std::vector<size_t>> lines = breakIntoLines(basis, mainSize, mainGap, wrap);
  std::vector<float> mainSizes(count, 0.0f);
  std::vector<float> cross(count, 0.0f);
  std::vector<float> lineCross(lines.size(), 0.0f);

  for (size_t li = 0; li < lines.size(); li++) {
    const std::vector<size_t> &line = lines[li];
    float sumBase = 0;
    for (size_t i : line) sumBase += basis[i];
    for (size_t i : line) mainSizes[i] = basis[i];
    float free = mainSize - sumBase - mainGap * (float)(line.size() - 1);
    if (free > 0) {
      float totalGrow = 0;
      for (size_t i : line) totalGrow += items[i]->style->flex_grow;
      if (totalGrow > 0)
        for (size_t i : line) mainSizes[i] += free * items[i]->style->flex_grow / totalGrow;
    } else if (free < 0) {
      float totalShrink = 0;
      for (size_t i : line) totalShrink += items[i]->style->flex_shrink * basis[i];
      if (totalShrink > 0)
        for (size_t i : line) {
          mainSizes[i] += free * (items[i]->style->flex_shrink * basis[i]) / totalShrink;
          if (mainSizes[i] < 0) mainSizes[i] = 0;
        }
    }
    /* Measure each item's cross size by laying it out at its used main size. */
    for (size_t i : line) {
      float contentW = mainSizes[i] - horizontalEdges(items[i]);
      if (contentW < 0) contentW = 0;
      setContainingBlock(items[i], containingBlockAncestor(items[i]));
      layoutBlock(items[i], container->content_x, container->content_y, contentWidth, definiteHeight,
                  contentW);
      cross[i] = verticalEdges(items[i]) + items[i]->height;
      if (cross[i] > lineCross[li]) lineCross[li] = cross[i];
    }
  }

  /* Cross-axis sizing and align-content. A single line takes the container's
   * definite cross size (or its content size); multiple lines share any extra
   * space according to align-content. */
  float crossContainer;
  float lineOffset = 0.0f;
  float lineStep = crossGap;
  if (lines.size() == 1) {
    crossContainer = definiteHeight >= 0 ? definiteHeight : lineCross[0];
    lineCross[0] = crossContainer;
  } else {
    float total = crossGap * (float)(lines.size() - 1);
    for (float value : lineCross) total += value;
    crossContainer = definiteHeight >= 0 ? definiteHeight : total;
    float remaining = crossContainer - total;
    if (remaining < 0) remaining = 0;
    switch (containerStyle.align_content) {
      case AlignContent::Stretch:
        for (float &value : lineCross) value += remaining / (float)lines.size();
        break;
      case AlignContent::FlexEnd:
        lineOffset = remaining;
        break;
      case AlignContent::Center:
        lineOffset = remaining / 2.0f;
        break;
      case AlignContent::SpaceBetween:
        if (lines.size() > 1) lineStep += remaining / (float)(lines.size() - 1);
        break;
      case AlignContent::SpaceAround:
        lineStep += remaining / (float)lines.size();
        lineOffset = remaining / (2.0f * (float)lines.size());
        break;
      default:
        break;
    }
  }

  for (size_t li = 0; li < lines.size(); li++) {
    const std::vector<size_t> &line = lines[li];
    float lineStart = wrapReverse ? crossContainer - lineOffset - lineCross[li] : lineOffset;
    float sumMain = 0;
    for (size_t i : line) sumMain += mainSizes[i];
    float freeSpace = mainSize - (sumMain + mainGap * (float)(line.size() - 1));
    float offset = 0;
    float between = mainGap;
    if (applyAutoMainMargins(items, line, true, freeSpace)) freeSpace = 0.0f;
    applyJustify(containerStyle.justify_content, freeSpace, line.size(), &offset, &between);
    float cursor = container->content_x + offset;
    for (size_t k = 0; k < line.size(); k++) {
      size_t i = reverse ? line[line.size() - 1 - k] : line[k];
      bool stretch = containerStyle.align_items == AlignItems::Stretch &&
                     isAuto(items[i]->style->height) &&
                     (definiteHeight >= 0 || lines.size() > 1);
      if (stretch) {
        float contentH = lineCross[li] - verticalEdges(items[i]);
        if (contentH < 0) contentH = 0;
        float contentW = mainSizes[i] - horizontalEdges(items[i]);
        layoutBlock(items[i], container->content_x, container->content_y, contentWidth,
                    definiteHeight, contentW, contentH);
        cross[i] = lineCross[li];
      }
      float crossOffset = crossOffsetFor(containerStyle.align_items, lineCross[li], cross[i]);
      float dx = cursor + items[i]->margin_left - items[i]->x;
      float dy =
          container->content_y + lineStart + crossOffset + items[i]->margin_top - items[i]->y;
      translate(items[i], dx, dy);
      float auto_extra = 0.0f;
      if (items[i]->style->margin_left_auto) auto_extra += items[i]->margin_left;
      if (items[i]->style->margin_right_auto) auto_extra += items[i]->margin_right;
      cursor += mainSizes[i] + between + auto_extra;
    }
    lineOffset += lineCross[li] + lineStep;
  }
  return crossContainer;
}

float Layouter::layoutFlexColumn(LayoutBox *container, const std::vector<LayoutBox *> &items,
                                 float contentWidth, float definiteHeight, float mainGap,
                                 float crossGap, FlexDirection direction) const {
  const XtStyle &containerStyle = *container->style;
  bool reverse = direction == FlexDirection::ColumnReverse;
  bool wrap = containerStyle.flex_wrap != FlexWrap::NoWrap && definiteHeight >= 0;
  bool wrapReverse = containerStyle.flex_wrap == FlexWrap::WrapReverse;
  size_t count = items.size();

  /* Column items have to be laid out to learn their main (height) size. */
  std::vector<float> outer(count, 0.0f);
  for (size_t i = 0; i < count; i++) {
    setContainingBlock(items[i], containingBlockAncestor(items[i]));
    layoutBlock(items[i], container->content_x, container->content_y, contentWidth, definiteHeight);
    outer[i] = verticalEdges(items[i]) + items[i]->height;
  }

  if (!wrap) {
    float mainSize = definiteHeight >= 0 ? definiteHeight : -1;
    std::vector<float> mainSizes = outer;
    if (mainSize >= 0) {
      float free = mainSize - sumOf(outer) - mainGap * (float)(count - 1);
      if (free > 0) {
        float totalGrow = 0;
        for (LayoutBox *item : items) totalGrow += item->style->flex_grow;
        if (totalGrow > 0) {
          for (size_t i = 0; i < count; i++) {
            mainSizes[i] += free * items[i]->style->flex_grow / totalGrow;
            float contentH = mainSizes[i] - verticalEdges(items[i]);
            if (contentH < 0) contentH = 0;
            layoutBlock(items[i], container->content_x, container->content_y, contentWidth,
                        definiteHeight, -1, contentH);
          }
        }
      }
    } else {
      mainSize = sumOf(outer) + mainGap * (float)(count - 1);
    }
    float freeSpace = mainSize - (sumOf(mainSizes) + mainGap * (float)(count - 1));
    float offset = 0;
    float between = mainGap;
    std::vector<size_t> all(count);
    for (size_t i = 0; i < count; i++) all[i] = i;
    if (applyAutoMainMargins(items, all, false, freeSpace)) freeSpace = 0.0f;
    applyJustify(containerStyle.justify_content, freeSpace, count, &offset, &between);
    float cursor = container->content_y + offset;
    for (size_t k = 0; k < count; k++) {
      size_t i = reverse ? (count - 1 - k) : k;
      float dx = container->content_x + items[i]->margin_left - items[i]->x;
      float dy = cursor + items[i]->margin_top - items[i]->y;
      translate(items[i], dx, dy);
      float auto_extra = 0.0f;
      if (items[i]->style->margin_top_auto) auto_extra += items[i]->margin_top;
      if (items[i]->style->margin_bottom_auto) auto_extra += items[i]->margin_bottom;
      cursor += mainSizes[i] + between + auto_extra;
    }
    return mainSize;
  }

  /* Wrapped columns: lines run top-to-bottom and are placed side by side. */
  std::vector<std::vector<size_t>> lines = breakIntoLines(outer, definiteHeight, mainGap, true);
  std::vector<float> mainSizes(count, 0.0f);
  std::vector<float> lineCross(lines.size(), 0.0f);
  for (size_t li = 0; li < lines.size(); li++) {
    const std::vector<size_t> &line = lines[li];
    float sumBase = 0;
    for (size_t i : line) sumBase += outer[i];
    for (size_t i : line) mainSizes[i] = outer[i];
    float free = definiteHeight - sumBase - mainGap * (float)(line.size() - 1);
    if (free > 0) {
      float totalGrow = 0;
      for (size_t i : line) totalGrow += items[i]->style->flex_grow;
      if (totalGrow > 0)
        for (size_t i : line) mainSizes[i] += free * items[i]->style->flex_grow / totalGrow;
    }
    for (size_t i : line) {
      float contentH = mainSizes[i] - verticalEdges(items[i]);
      if (contentH < 0) contentH = 0;
      layoutBlock(items[i], container->content_x, container->content_y, contentWidth,
                  definiteHeight, -1, contentH);
      float crossSize = horizontalEdges(items[i]) + items[i]->width;
      if (crossSize > lineCross[li]) lineCross[li] = crossSize;
    }
  }
  float total = crossGap * (float)(lines.size() - 1);
  for (float value : lineCross) total += value;
  float lineOffset = 0.0f;
  float lineStep = crossGap;
  float remaining = contentWidth - total;
  if (remaining < 0) remaining = 0;
  switch (containerStyle.align_content) {
    case AlignContent::Stretch:
      for (float &value : lineCross) value += remaining / (float)lines.size();
      break;
    case AlignContent::FlexEnd:
      lineOffset = remaining;
      break;
    case AlignContent::Center:
      lineOffset = remaining / 2.0f;
      break;
    case AlignContent::SpaceBetween:
      if (lines.size() > 1) lineStep += remaining / (float)(lines.size() - 1);
      break;
    case AlignContent::SpaceAround:
      lineStep += remaining / (float)lines.size();
      lineOffset = remaining / (2.0f * (float)lines.size());
      break;
    default:
      break;
  }
  for (size_t li = 0; li < lines.size(); li++) {
    const std::vector<size_t> &line = lines[li];
    float lineStart = wrapReverse ? contentWidth - lineOffset - lineCross[li] : lineOffset;
    float sumMain = 0;
    for (size_t i : line) sumMain += mainSizes[i];
    float freeSpace = definiteHeight - (sumMain + mainGap * (float)(line.size() - 1));
    float offset = 0;
    float between = mainGap;
    if (applyAutoMainMargins(items, line, false, freeSpace)) freeSpace = 0.0f;
    applyJustify(containerStyle.justify_content, freeSpace, line.size(), &offset, &between);
    float cursor = container->content_y + offset;
    for (size_t k = 0; k < line.size(); k++) {
      size_t i = reverse ? line[line.size() - 1 - k] : line[k];
      float dx = container->content_x + lineStart + items[i]->margin_left - items[i]->x;
      float dy = cursor + items[i]->margin_top - items[i]->y;
      translate(items[i], dx, dy);
      float auto_extra = 0.0f;
      if (items[i]->style->margin_top_auto) auto_extra += items[i]->margin_top;
      if (items[i]->style->margin_bottom_auto) auto_extra += items[i]->margin_bottom;
      cursor += mainSizes[i] + between + auto_extra;
    }
    lineOffset += lineCross[li] + lineStep;
  }
  return contentWidth;
}

float Layouter::sumOf(const std::vector<float> &values) {
  float total = 0;
  for (float value : values) total += value;
  return total;
}

void Layouter::applyJustify(JustifyContent justify, float freeSpace, size_t count, float *offset,
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

float Layouter::crossOffsetFor(AlignItems align, float crossSize, float itemCross) {
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

}  // namespace layout_flow_detail
}  // namespace xtgui
