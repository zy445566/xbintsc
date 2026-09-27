/*
 * xbintsc GUI engine — display list (paint).
 *
 * `xt_paint_build` walks the laid-out box tree and produces a painter's-order
 * list of shapes. It is deliberately backend-agnostic: the GPU renderer turns
 * the same list into triangles, and diagnostics/tests can inspect it without a
 * GPU. Today it emits backgrounds and borders; text and images arrive with the
 * M4 text stack.
 */
#ifndef XT_GUI_PAINT_H
#define XT_GUI_PAINT_H

#include <string>
#include <vector>

#include "css.h"
#include "layout.h"

namespace xtgui {

/** A solid, optionally rounded, rectangle. Coordinates are viewport-relative. */
struct PaintRect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  float radius = 0;
  Color color;
};

struct DisplayList {
  std::vector<PaintRect> rects;

  void clear() { rects.clear(); }
  bool empty() const { return rects.empty(); }
  /** Human-readable dump, one shape per line (for diagnostics/tests). */
  std::string dump() const;
};

/** Build the display list for a laid-out tree and append it to `out`. */
void xt_paint_build(const LayoutBox *root, DisplayList &out);

}  // namespace xtgui

#endif /* XT_GUI_PAINT_H */
