/*
 * xbintsc GUI engine — CSS `outline` painting.
 *
 * Outline is a focus ring: drawn outside the border box, unaffected by layout,
 * with an optional offset. The engine has one radius, so the four edge
 * rectangles are square; the ring still follows the box's `border-radius` only
 * approximately (documented).
 */

#include "paint_internal.h"

namespace xtgui {
namespace paint_detail {

void addOutline(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                const FilterChain &chain, const PaintClip &clip, float viewportWidth,
                float viewportHeight) {
  if (style.outline_style == BorderStyle::None || !style.has_outline_color) return;
  float width = resolveLength(style.outline_width, box.width, style.font_size, viewportWidth,
                              viewportHeight);
  if (width <= 0.0f) return;
  float offset = resolveLength(style.outline_offset, box.width, style.font_size, viewportWidth,
                               viewportHeight);
  float x = box.x - offset;
  float y = box.y - offset;
  float outer_width = box.width + offset * 2.0f;
  float outer_height = box.height + offset * 2.0f;
  if (outer_width <= 0.0f || outer_height <= 0.0f) return;
  Color color = filterColor(chain, style.outline_color);
  if (color.a <= 0.0f) return;

  addRectFiltered(out, x, y, outer_width, width, 0.0f, color, clip, box.world, chain);
  addRectFiltered(out, x, y + outer_height - width, outer_width, width, 0.0f, color, clip,
                  box.world, chain);
  float side_height = outer_height - width * 2.0f;
  if (side_height > 0.0f) {
    addRectFiltered(out, x, y + width, width, side_height, 0.0f, color, clip, box.world, chain);
    addRectFiltered(out, x + outer_width - width, y + width, width, side_height, 0.0f, color, clip,
                    box.world, chain);
  }
}

}  // namespace paint_detail
}  // namespace xtgui
