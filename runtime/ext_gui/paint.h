/*
 * xbintsc GUI engine — display list (paint).
 *
 * `xt_paint_build` walks the laid-out box tree and produces a painter's-order
 * list of shapes. It is deliberately backend-agnostic: the GPU renderer turns
 * the same list into triangles (rectangles) and glyph quads (text), and
 * diagnostics/tests can inspect it without a GPU.
 *
 * Rectangles and text are kept in separate lists: backgrounds/borders are drawn
 * first, then all text on top. That matches almost every static UI and lets the
 * renderer use one draw call per list instead of switching pipelines per shape.
 */
#ifndef XT_GUI_PAINT_H
#define XT_GUI_PAINT_H

#include <string>
#include <vector>

#include "css.h"
#include "layout.h"
#include "text.h"

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

/** A run of text to draw at `x`/`baseline` (viewport-relative). */
struct PaintText {
  float x = 0;
  float baseline = 0;
  float width = 0;
  float height = 0;
  Color color;
  std::string text;
  FontSpec font;
};

/** An image to draw, scaled into the given rectangle (viewport-relative). */
struct PaintImage {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  std::string src;
};

struct DisplayList {
  std::vector<PaintRect> rects;
  std::vector<PaintText> texts;
  std::vector<PaintImage> images;

  void clear() {
    rects.clear();
    texts.clear();
    images.clear();
  }
  bool empty() const { return rects.empty() && texts.empty() && images.empty(); }
  /** Human-readable dump, one shape per line (for diagnostics/tests). */
  std::string dump() const;
};

/** Build the display list for a laid-out tree and append it to `out`. */
void xt_paint_build(const LayoutBox *root, DisplayList &out);

}  // namespace xtgui

#endif /* XT_GUI_PAINT_H */
