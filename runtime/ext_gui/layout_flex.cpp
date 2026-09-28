/*
 * xbintsc GUI engine — flex formatting context.
 *
 * Sizes flex items along the main axis (grow/shrink/basis), resolves the cross
 * axis (align-items) and places them (justify-content), reusing the block and
 * inline helpers from `layout_flow.cpp` / `layout_inline.cpp`.
 */

#include "layout_flow_internal.h"

namespace xtgui {
namespace layout_flow_detail {

float Layouter::layoutFlex(LayoutBox *container, float contentWidth, float definiteHeight) const {
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
        for (size_t i = 0; i < count; i++)
          mainSizes[i] += free * items[i]->style->flex_grow / totalGrow;
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
      layoutBlock(items[i], container->content_x, container->content_y, contentWidth,
                  definiteHeight, contentW);
      cross[i] = verticalEdges(items[i]) + items[i]->height;
      if (definiteHeight < 0) crossSize = std::max(crossSize, cross[i]);
    }
    if (containerStyle.align_items == AlignItems::Stretch && definiteHeight >= 0) {
      for (size_t i = 0; i < count; i++) {
        if (isAuto(items[i]->style->height)) {
          float contentH = crossSize - verticalEdges(items[i]);
          if (contentH < 0) contentH = 0;
          float contentW = mainSizes[i] - horizontalEdges(items[i]);
          layoutBlock(items[i], container->content_x, container->content_y, contentWidth,
                      definiteHeight, contentW, contentH);
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
          layoutBlock(items[i], container->content_x, container->content_y, contentWidth,
                      definiteHeight, -1, contentH);
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
