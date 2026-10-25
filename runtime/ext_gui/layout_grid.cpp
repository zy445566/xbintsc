/*
 * xbintsc GUI engine — CSS Grid formatting context.
 *
 * The engine deliberately implements a focused subset of grid: a track list
 * (`grid-template-columns/rows` with fixed, `auto`, `fr`, `minmax(...)` and
 * `repeat(...)`), `gap`, row-major auto-placement, simple line placement
 * (`grid-column` / `grid-row`), and item alignment (`align/justify-items`,
 * `align/justify-self`, `place-items`). That is enough for application shells
 * such as a HUD with three columns or a centred overlay card, without turning
 * the engine into a full CSS layout implementation.
 */

#include "layout_flow_internal.h"

namespace xtgui {
namespace layout_flow_detail {

namespace {

struct GridPlacement {
  LayoutBox *box = nullptr;
  int col = 0;
  int col_span = 1;
  int row = 0;
  int row_span = 1;
  bool explicit_col = false;
  bool explicit_row = false;
};

int lineToIndex(int line, int line_count) {
  /* CSS grid lines are 1-based; -1 is the last line and -N counts back. */
  if (line > 0) return line - 1;
  if (line < 0) return line_count + line;
  return 0;
}

float trackPx(const Layouter &layouter, const GridTrack &track, bool maximum, float base,
              float em) {
  const Length &length = maximum ? track.max : track.min;
  if (length.unit == Unit::Auto || length.unit == Unit::Invalid || length.unit == Unit::None)
    return -1.0f;
  return layouter.px(length, base, em, 0.0f);
}

/* One free-cell cursor over a dynamically grown occupancy matrix. */
struct Occupancy {
  std::vector<std::vector<bool>> cells;
  int columns = 1;

  void ensure(int row, int col) {
    if ((int)cells.size() <= row) cells.resize((size_t)row + 1);
    for (std::vector<bool> &line : cells)
      if ((int)line.size() < columns) line.resize((size_t)columns, false);
    if ((int)cells[row].size() <= col) cells[row].resize((size_t)col + 1, false);
  }

  bool free(int row, int col, int row_span, int col_span) const {
    for (int r = row; r < row + row_span; r++) {
      if (r >= (int)cells.size()) continue;
      for (int c = col; c < col + col_span; c++) {
        if (c >= (int)cells[r].size()) continue;
        if (cells[r][c]) return false;
      }
    }
    return true;
  }

  void mark(int row, int col, int row_span, int col_span) {
    for (int r = row; r < row + row_span; r++) {
      for (int c = col; c < col + col_span; c++) {
        ensure(r, c);
        cells[r][c] = true;
      }
    }
  }
};

bool isGridItem(const LayoutBox *box) {
  return box != nullptr && box->display != Display::None &&
         box->style != nullptr && !Layouter::isAbsoluteChild(box) &&
         !isCollapsibleText(box);
}

}  // namespace

float Layouter::layoutGrid(LayoutBox *container, float contentWidth,
                           float definiteHeight) const {
  const XtStyle &style = *container->style;
  std::vector<LayoutBox *> children;
  for (const std::unique_ptr<LayoutBox> &child : container->children)
    if (isGridItem(child.get())) children.push_back(child.get());
  if (children.empty()) return 0;

  std::vector<GridTrack> columns = style.grid_columns;
  std::vector<GridTrack> rows = style.grid_rows;
  int columnCount = (int)columns.size();
  if (columnCount < 1) columnCount = 1;
  if ((int)columns.size() < columnCount) columns.push_back(GridTrack{});

  /* Resolve explicit line numbers far enough to know how many columns exist. */
  std::vector<GridPlacement> placements;
  placements.reserve(children.size());
  int maxExplicitColumn = 0;
  int maxExplicitRow = 0;
  for (LayoutBox *box : children) {
    GridPlacement placement;
    placement.box = box;
    const XtStyle &item = *box->style;
    if (item.grid_column_start != 0 || item.grid_column_end != 0) placement.explicit_col = true;
    if (item.grid_row_start != 0 || item.grid_row_end != 0) placement.explicit_row = true;
    int start = lineToIndex(item.grid_column_start, columnCount + 1);
    int end = item.grid_column_end == 0 ? start + 1
                                        : lineToIndex(item.grid_column_end, columnCount + 1);
    if (start < 0) start = 0;
    if (end <= start) end = start + 1;
    placement.col = start;
    placement.col_span = end - start;
    if (placement.explicit_col) maxExplicitColumn = std::max(maxExplicitColumn, end);
    int rowStart = lineToIndex(item.grid_row_start, 1 << 20);
    int rowEnd = item.grid_row_end == 0 ? rowStart + 1 : lineToIndex(item.grid_row_end, 1 << 20);
    if (rowStart < 0) rowStart = 0;
    if (rowEnd <= rowStart) rowEnd = rowStart + 1;
    placement.row = rowStart;
    placement.row_span = rowEnd - rowStart;
    if (placement.explicit_row) maxExplicitRow = std::max(maxExplicitRow, rowEnd);
    placements.push_back(placement);
  }
  if (maxExplicitColumn > columnCount) {
    while ((int)columns.size() < maxExplicitColumn) columns.push_back(GridTrack{});
    columnCount = maxExplicitColumn;
  }

  /* Place explicit items first, then auto-place the rest row-major. */
  Occupancy occupied;
  occupied.columns = columnCount;
  for (GridPlacement &placement : placements) {
    if (!placement.explicit_col && !placement.explicit_row) continue;
    int row = placement.explicit_row ? placement.row : 0;
    int col = placement.explicit_col ? placement.col : 0;
    if (placement.explicit_col && !placement.explicit_row) {
      while (!occupied.free(row, col, placement.row_span, placement.col_span)) row++;
    } else if (!placement.explicit_col && placement.explicit_row) {
      while (!occupied.free(row, col, placement.row_span, placement.col_span)) col++;
    } else {
      while (!occupied.free(row, col, placement.row_span, placement.col_span)) {
        col++;
        if (col + placement.col_span > columnCount) {
          col = 0;
          row++;
        }
      }
    }
    placement.row = row;
    placement.col = col;
    occupied.mark(row, col, placement.row_span, placement.col_span);
  }
  int cursorRow = 0;
  int cursorCol = 0;
  for (GridPlacement &placement : placements) {
    if (placement.explicit_col || placement.explicit_row) continue;
    while (!occupied.free(cursorRow, cursorCol, placement.row_span, placement.col_span)) {
      cursorCol++;
      if (cursorCol + placement.col_span > columnCount) {
        cursorCol = 0;
        cursorRow++;
      }
    }
    placement.row = cursorRow;
    placement.col = cursorCol;
    occupied.mark(cursorRow, cursorCol, placement.row_span, placement.col_span);
    cursorCol += placement.col_span;
    if (cursorCol >= columnCount) {
      cursorCol = 0;
      cursorRow++;
    }
  }

  /* Measure each item once to learn its intrinsic width/height. */
  std::vector<float> naturalWidth(placements.size(), 0.0f);
  std::vector<float> naturalHeight(placements.size(), 0.0f);
  for (size_t i = 0; i < placements.size(); i++) {
    LayoutBox *box = placements[i].box;
    resolveEdges(box, contentWidth);
    float preferred = preferredContentWidth(box, contentWidth);
    if (preferred < 0) preferred = 0;
    naturalWidth[i] = preferred;
  }

  /* Resolve column widths. */
  float gapX = px(style.column_gap, contentWidth, style.font_size, 0.0f);
  if (gapX < 0) gapX = 0;
  float gapY = px(style.row_gap, contentWidth, style.font_size, 0.0f);
  if (gapY < 0) gapY = 0;

  std::vector<float> columnWidth(columns.size(), 0.0f);
  float totalFr = 0;
  for (size_t c = 0; c < columns.size(); c++) {
    totalFr += columns[c].fr;
    float fixed = trackPx(*this, columns[c], true, contentWidth, style.font_size);
    if (columns[c].fr <= 0 && fixed >= 0) columnWidth[c] = fixed;
    if (columns[c].fr <= 0 && fixed < 0) {
      /* Auto track: size to the widest single-column item. */
      float best = 0;
      for (size_t i = 0; i < placements.size(); i++) {
        if (placements[i].col != (int)c || placements[i].col_span != 1) continue;
        LayoutBox *itemBox = placements[i].box;
        float outer = naturalWidth[i] + itemBox->border_left + itemBox->border_right +
                      itemBox->padding_left + itemBox->padding_right + itemBox->margin_left +
                      itemBox->margin_right;
        if (outer > best) best = outer;
      }
      columnWidth[c] = best;
    }
  }
  if (totalFr <= 0 && columns.size() == 1 && columns[0].fr <= 0) {
    columnWidth[0] = contentWidth;
  }
  if (totalFr > 0) {
    float fixedTotal = gapX * (float)(columns.size() - 1);
    for (size_t c = 0; c < columns.size(); c++)
      if (columns[c].fr <= 0) fixedTotal += columnWidth[c];
    float remaining = contentWidth - fixedTotal;
    if (remaining < 0) remaining = 0;
    for (int pass = 0; pass < 2; pass++) {
      float used = 0;
      for (size_t c = 0; c < columns.size(); c++) {
        if (columns[c].fr <= 0) continue;
        float share = remaining * columns[c].fr / totalFr;
        float minimum = trackPx(*this, columns[c], false, contentWidth, style.font_size);
        if (minimum > share) share = minimum;
        columnWidth[c] = share;
        used += share;
      }
      if (used <= remaining + 0.01f) break;
      /* The minimums forced a deficit; shrink the fr share proportionally. */
      float scale = remaining / used;
      for (size_t c = 0; c < columns.size(); c++)
        if (columns[c].fr > 0) columnWidth[c] *= scale;
      break;
    }
  } else {
    /* Fill the container with auto tracks so a single-cell overlay centres in
     * the whole area rather than hugging its content. */
    float used = gapX * (float)(columns.size() - 1);
    for (float width : columnWidth) used += width;
    float extra = contentWidth - used;
    if (extra > 0) {
      int autos = 0;
      for (const GridTrack &track : columns)
        if (track.fr <= 0 && trackPx(*this, track, true, contentWidth, style.font_size) < 0) autos++;
      if (autos > 0)
        for (size_t c = 0; c < columns.size(); c++)
          if (trackPx(*this, columns[c], true, contentWidth, style.font_size) < 0)
            columnWidth[c] += extra / (float)autos;
    }
  }

  /* Rows: resolve explicit tracks and grow implicit ones from item heights. */
  int rowCount = maxExplicitRow;
  for (const GridPlacement &placement : placements)
    rowCount = std::max(rowCount, placement.row + placement.row_span);
  if (rowCount < 1) rowCount = 1;
  while ((int)rows.size() < rowCount) rows.push_back(GridTrack{});
  std::vector<float> rowHeight(rows.size(), 0.0f);
  for (size_t r = 0; r < rows.size(); r++) {
    float fixed = trackPx(*this, rows[r], true, contentWidth, style.font_size);
    if (fixed >= 0) rowHeight[r] = fixed;
  }

  /* Lay each item out at its column width to learn its height. */
  std::vector<float> itemHeight(placements.size(), 0.0f);
  for (size_t i = 0; i < placements.size(); i++) {
    GridPlacement &placement = placements[i];
    LayoutBox *box = placement.box;
    float cellWidth = 0;
    for (int c = placement.col; c < placement.col + placement.col_span && c < columnCount; c++)
      cellWidth += columnWidth[(size_t)c];
    cellWidth += gapX * (float)(placement.col_span - 1);
    float availableWidth = cellWidth - box->margin_left - box->margin_right;
    if (availableWidth < 0) availableWidth = 0;
    float contentAvail = availableWidth - box->border_left - box->border_right -
                         box->padding_left - box->padding_right;
    if (contentAvail < 0) contentAvail = 0;
    bool stretch = true;
    AlignItems justify = box->style->has_justify_self ? box->style->justify_self : style.justify_items;
    stretch = justify == AlignItems::Stretch;
    float forced = stretch ? -1.0f : naturalWidth[i];
    if (forced > contentAvail) forced = contentAvail;
    setContainingBlock(box, containingBlockAncestor(box));
    layoutBlock(box, container->content_x, container->content_y, availableWidth, -1, forced, -1,
                true);
    itemHeight[i] = box->height + box->margin_top + box->margin_bottom;
  }
  for (size_t i = 0; i < placements.size(); i++) {
    GridPlacement &placement = placements[i];
    if (trackPx(*this, rows[(size_t)placement.row], true, contentWidth, style.font_size) >= 0)
      continue;
    for (int r = placement.row; r < placement.row + placement.row_span && r < (int)rowHeight.size();
         r++) {
      if (itemHeight[i] > rowHeight[(size_t)r]) rowHeight[(size_t)r] = itemHeight[i];
    }
  }

  float totalHeight = gapY * (float)(rows.size() - 1);
  for (float value : rowHeight) totalHeight += value;
  if (rows.size() == 1 && definiteHeight >= 0 && definiteHeight > totalHeight)
    totalHeight = definiteHeight;
  if (rows.size() == 1 && definiteHeight >= 0) rowHeight[0] = totalHeight;

  /* Place the items. */
  std::vector<float> columnX(columnWidth.size(), 0.0f);
  float x = container->content_x;
  for (size_t c = 0; c < columnWidth.size(); c++) {
    columnX[c] = x;
    x += columnWidth[c] + gapX;
  }
  std::vector<float> rowY(rowHeight.size(), 0.0f);
  float y = container->content_y;
  for (size_t r = 0; r < rowHeight.size(); r++) {
    rowY[r] = y;
    y += rowHeight[r] + gapY;
  }

  for (size_t i = 0; i < placements.size(); i++) {
    GridPlacement &placement = placements[i];
    LayoutBox *box = placement.box;
    float cellX = columnX[(size_t)placement.col];
    float cellY = rowY[(size_t)placement.row];
    float cellW = 0;
    for (int c = placement.col; c < placement.col + placement.col_span && c < columnCount; c++)
      cellW += columnWidth[(size_t)c];
    cellW += gapX * (float)(placement.col_span - 1);
    float cellH = 0;
    for (int r = placement.row; r < placement.row + placement.row_span && r < (int)rowHeight.size();
         r++)
      cellH += rowHeight[(size_t)r];
    cellH += gapY * (float)(placement.row_span - 1);

    float availW = cellW - box->margin_left - box->margin_right;
    float availH = cellH - box->margin_top - box->margin_bottom;
    if (availW < 0) availW = 0;
    if (availH < 0) availH = 0;

    AlignItems justify = box->style->has_justify_self ? box->style->justify_self : style.justify_items;
    AlignItems align = box->style->has_align_self ? box->style->align_self : style.align_items;
    bool stretchW = justify == AlignItems::Stretch;
    bool stretchH = align == AlignItems::Stretch && isAuto(box->style->height);

    float forcedW = stretchW ? -1.0f : naturalWidth[i];
    float forcedH = -1.0f;
    if (stretchH && cellH > 0) {
      float contentH = availH - box->border_top - box->border_bottom - box->padding_top -
                       box->padding_bottom;
      if (contentH < 0) contentH = 0;
      forcedH = contentH;
    }
    setContainingBlock(box, containingBlockAncestor(box));
    layoutBlock(box, cellX, cellY, availW, -1, forcedW, forcedH, true);

    float dx = 0;
    if (justify == AlignItems::Center) dx = (availW - box->width) / 2.0f;
    else if (justify == AlignItems::FlexEnd) dx = availW - box->width;
    float dy = box->margin_top;
    if (align == AlignItems::Center) dy = box->margin_top + (availH - box->height) / 2.0f;
    else if (align == AlignItems::FlexEnd) dy = cellH - box->margin_bottom - box->height;
    if (stretchH) dy = box->margin_top;
    translate(box, dx, dy);
  }

  return totalHeight;
}

}  // namespace layout_flow_detail
}  // namespace xtgui
