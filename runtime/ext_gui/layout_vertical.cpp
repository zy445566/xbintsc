/*
 * xbintsc GUI engine — vertical writing mode (`writing-mode: vertical-rl|-lr`).
 *
 * This is a focused text layout: characters are placed one per column step,
 * CJK glyphs upright and other scripts rotated 90° at paint time. It covers the
 * decorative vertical labels a page uses; mixed block/inline content in a
 * vertical container still falls back to horizontal layout (documented).
 */

#include "layout_flow_internal.h"

namespace xtgui {
namespace layout_flow_detail {

namespace {

/** Decode one UTF-8 codepoint at `index`, advancing it. */
uint32_t nextCodePoint(const std::string &text, size_t *index) {
  unsigned char first = (unsigned char)text[*index];
  uint32_t value = first;
  size_t length = 1;
  if ((first & 0x80u) == 0) {
    value = first;
  } else if ((first & 0xe0u) == 0xc0u) {
    value = first & 0x1fu;
    length = 2;
  } else if ((first & 0xf0u) == 0xe0u) {
    value = first & 0x0fu;
    length = 3;
  } else {
    value = first & 0x07u;
    length = 4;
  }
  for (size_t i = 1; i < length && *index + i < text.size(); i++) {
    value = (value << 6) | ((unsigned char)text[*index + i] & 0x3fu);
  }
  *index += length;
  return value;
}

/** CJK, kana, Hangul and fullwidth forms are kept upright in vertical text. */
bool isUprightCodePoint(uint32_t value) {
  if (value >= 0x1100 && value <= 0x11ff) return true;
  if (value >= 0x2e80 && value <= 0xa4cf) return true;
  if (value >= 0xac00 && value <= 0xd7af) return true;
  if (value >= 0xf900 && value <= 0xfaff) return true;
  if (value >= 0xfe30 && value <= 0xfe4f) return true;
  if (value >= 0xff00 && value <= 0xffef) return true;
  if (value >= 0x20000) return true;
  return false;
}

/** The column's top-left x for the first column (right edge for `vertical-rl`). */
float firstColumnX(const LayoutBox *container, const XtStyle &style, float contentWidth,
                   float columnWidth) {
  if (style.writing_mode == WritingMode::VerticalLR) return container->content_x;
  float x = container->content_x + contentWidth - columnWidth;
  if (x < container->content_x) x = container->content_x;
  return x;
}

}  // namespace

void Layouter::flattenVerticalText(LayoutBox *box, std::vector<InlineItem> &out) const {
  const XtStyle &style = *box->style;
  const float columnWidth = lineHeightOf(style);
  const std::string &text = box->text;
  size_t index = 0;
  while (index < text.size()) {
    size_t start = index;
    uint32_t code = nextCodePoint(text, &index);
    if (code == '\n') {
      InlineItem br;
      br.box = box;
      br.style = &style;
      br.force_break = true;
      br.vertical = true;
      out.push_back(std::move(br));
      continue;
    }
    InlineItem item;
    item.box = box;
    item.style = &style;
    item.text = text.substr(start, index - start);
    item.is_space = code == ' ' || code == '\t';
    item.vertical = true;
    item.upright = isUprightCodePoint(code);
    item.width = columnWidth;
    item.height = textWidth(item.text, style);
    item.ascent = item.height;
    out.push_back(std::move(item));
  }
}

float Layouter::layoutVerticalRun(LayoutBox *container, const std::vector<InlineItem> &items,
                                  float contentWidth, float y) const {
  const XtStyle &style = *container->style;
  const float columnWidth = lineHeightOf(style);
  const bool rightToLeft = style.writing_mode != WritingMode::VerticalLR;
  float limit = -1.0f;
  if (!isAuto(style.height)) limit = px(style.height, container->content_height, style.font_size, -1.0f);

  float columnX = firstColumnX(container, style, contentWidth, columnWidth);
  float cursorY = y;
  float startY = y;
  float minX = columnX;
  float maxX = columnX + columnWidth;
  float maxY = y;
  bool atColumnStart = true;

  auto newColumn = [&]() {
    columnX += rightToLeft ? -columnWidth : columnWidth;
    cursorY = startY;
    atColumnStart = true;
    if (columnX < minX) minX = columnX;
    if (columnX + columnWidth > maxX) maxX = columnX + columnWidth;
  };

  for (const InlineItem &item : items) {
    if (item.force_break) {
      newColumn();
      continue;
    }
    if (item.is_space && atColumnStart) continue;
    if (limit >= 0.0f && !atColumnStart && cursorY + item.height > startY + limit) newColumn();
    item.box->fragments.push_back(
        {columnX, cursorY, columnWidth, item.height, item.text, true, item.upright});
    cursorY += item.height;
    atColumnStart = false;
    if (cursorY > maxY) maxY = cursorY;
    if (item.width > 0.0f) {
      float right = columnX + columnWidth;
      if (right > maxX) maxX = right;
    }
  }
  (void)minX;
  (void)maxX;
  return maxY - startY;
}

float Layouter::layoutVerticalTextBox(LayoutBox *box, float x, float y, float width) const {
  const XtStyle &style = *box->style;
  const float columnWidth = lineHeightOf(style);
  const bool rightToLeft = style.writing_mode != WritingMode::VerticalLR;
  float limit = -1.0f;
  if (!isAuto(style.height)) limit = px(style.height, -1.0f, style.font_size, -1.0f);

  float columnX = rightToLeft ? x + width - columnWidth : x;
  if (rightToLeft && columnX < x) columnX = x;
  float cursorY = y;
  float startY = y;
  float minX = columnX;
  float maxX = columnX + columnWidth;
  float maxY = y;
  bool atColumnStart = true;

  size_t index = 0;
  const std::string &text = box->text;
  while (index < text.size()) {
    size_t start = index;
    uint32_t code = nextCodePoint(text, &index);
    if (code == '\n') {
      columnX += rightToLeft ? -columnWidth : columnWidth;
      cursorY = startY;
      atColumnStart = true;
      continue;
    }
    std::string glyph = text.substr(start, index - start);
    float advance = textWidth(glyph, style);
    if (limit >= 0.0f && !atColumnStart && cursorY + advance > startY + limit) {
      columnX += rightToLeft ? -columnWidth : columnWidth;
      cursorY = startY;
      atColumnStart = true;
    }
    if (code == ' ' && atColumnStart) continue;
    box->fragments.push_back(
        {columnX, cursorY, columnWidth, advance, glyph, true, isUprightCodePoint(code)});
    cursorY += advance;
    atColumnStart = false;
    if (cursorY > maxY) maxY = cursorY;
  }

  box->x = minX;
  box->y = startY;
  box->width = maxX - minX;
  box->height = maxY - startY;
  box->content_x = box->x;
  box->content_y = box->y;
  box->content_width = box->width;
  box->content_height = box->height;
  box->baseline = box->height;
  return box->height;
}

}  // namespace layout_flow_detail
}  // namespace xtgui
