/*
 * xbintsc GUI engine — display list (paint).
 *
 * `xt_paint_build` walks the laid-out box tree and produces a painter's-order
 * list of shapes. It is deliberately backend-agnostic: the GPU renderer turns
 * the same list into triangles (rectangles) and glyph quads (text), and
 * diagnostics/tests can inspect it without a GPU.
 *
 * Rectangles, text and images share one painter-order `ops` list, so a
 * `z-index`ed child can paint over a sibling's background and an `overflow`
 * clip applies to every shape in the subtree. Coordinates are viewport-
 * relative. */
#ifndef XT_GUI_PAINT_H
#define XT_GUI_PAINT_H

#include <cstdint>
#include <string>
#include <vector>

#include "css.h"
#include "layout.h"
#include "text.h"
#include "transform.h"

namespace xtgui {

/** A solid, optionally rounded, rectangle. Coordinates are viewport-relative. */
struct PaintRect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  float radius = 0;
  Color color;
  /** `transform` of the box this shape belongs to (layout -> device space). */
  Matrix2D transform;
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
  Matrix2D transform;
};

/** An image to draw, scaled into the given rectangle (viewport-relative).
 * A non-zero `media_id` names a live `<video>` frame owned by the media
 * subsystem; a non-zero `canvas_id` names a live `<canvas>` bitmap owned by the
 * DOM; `src` is then empty and the renderer binds that subsystem's texture.
 * A non-empty `svg` carries the serialized `<svg>` subtree of an inline SVG
 * element, which the renderer rasterises at the drawn device size (there is no
 * file to load). */
struct PaintImage {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  std::string src;
  std::string svg;
  uint64_t media_id = 0;
  uint64_t canvas_id = 0;
  Matrix2D transform;
};

/** The clip rectangle in effect for a painted shape (device-space logical
 * pixels — a transformed subtree contributes the bounding box of its clip).
 * `width <= 0` means "no clip". */
struct PaintClip {
  bool active = false;
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
};

enum class PaintKind { Rect, Text, Image };

/** One entry in painter order: which kind of shape, its index in the matching
 * payload vector, and the clip in effect. */
struct PaintOp {
  PaintKind kind = PaintKind::Rect;
  uint32_t index = 0;
  PaintClip clip;
};

struct DisplayList {
  std::vector<PaintRect> rects;
  std::vector<PaintText> texts;
  std::vector<PaintImage> images;
  /** Shapes in painter order (backgrounds/borders first, then children, then
   * stacked siblings), each with the clip rectangle that bounds it. */
  std::vector<PaintOp> ops;

  void clear() {
    rects.clear();
    texts.clear();
    images.clear();
    ops.clear();
  }
  bool empty() const { return rects.empty() && texts.empty() && images.empty(); }
  /** Human-readable dump, one shape per line, in painter order (diagnostics/tests). */
  std::string dump() const;
};

/** Build the display list for a laid-out tree and append it to `out`. */
void xt_paint_build(const LayoutBox *root, DisplayList &out);

}  // namespace xtgui

#endif /* XT_GUI_PAINT_H */
