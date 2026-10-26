/*
 * xbintsc GUI engine — shared paint helpers.
 *
 * `paint.cpp` owns the tree walk; the background, effect and clip-path
 * translation units fill in the shapes. They all append through the small
 * `addRect` / `addText` / `addImage` helpers here so painter order and the
 * active clip stay consistent.
 */
#ifndef XT_GUI_PAINT_INTERNAL_H
#define XT_GUI_PAINT_INTERNAL_H

#include <string>
#include <vector>

#include "paint.h"
#include "style.h"

namespace xtgui {
namespace paint_detail {

/** A resolved chain of `filter` functions. `filter` applies to the element and
 * its subtree, so it accumulates down the tree. Only the colour functions are
 * group-accurate; blur and drop-shadow are approximated per shape (see the
 * docs). */
struct FilterChain {
  std::vector<FilterOp> ops;  // colour functions (and any unknown no-ops)
  float blur = 0.0f;          // resolved px
  struct Shadow {
    float dx = 0.0f;
    float dy = 0.0f;
    float blur = 0.0f;
    Color color;
  };
  std::vector<Shadow> shadows;

  bool empty() const { return ops.empty() && blur <= 0.0f && shadows.empty(); }
};

/** Append `filters` (declared on a box with `style`) to `parent`. */
FilterChain extendFilter(const FilterChain &parent, const std::vector<FilterOp> &filters,
                         const XtStyle &style, float viewportWidth, float viewportHeight);

/** Apply the colour half of a filter chain. */
Color filterColor(const FilterChain &chain, Color color);

/** The multiplicative tint an image shader can carry (brightness/opacity). */
Color imageTint(const FilterChain &chain);

float resolveLength(const Length &length, float base, float em, float viewportWidth,
                    float viewportHeight);

/** Append one shape; skips empty geometry and fully transparent colours. */
void addRect(DisplayList &out, float x, float y, float width, float height, float radius,
             const Color &color, const PaintClip &clip, const Matrix2D &transform);
/** Solid rectangle with the chain's blur spread into offset copies. */
void addRectFiltered(DisplayList &out, float x, float y, float width, float height, float radius,
                     const Color &color, const PaintClip &clip, const Matrix2D &transform,
                     const FilterChain &chain);
/** Add a text run (and its shadows/blur copies) for one laid-out fragment. */
void addTextRun(DisplayList &out, float x, float baseline, float width, float height,
                const std::string &text, const FontSpec &font, const Color &color,
                const Matrix2D &transform, const PaintClip &clip, const XtStyle &style,
                const FilterChain &chain, float viewportWidth, float viewportHeight);
void addImage(DisplayList &out, PaintImage image, const PaintClip &clip, const FilterChain &chain);

/** Paint the `background-color` and every `background-image` layer. */
void paintBackgrounds(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                      const FilterChain &chain, const PaintClip &clip, float viewportWidth,
                      float viewportHeight);

/** Append the drop-shadow silhouettes of a box (once, before its content). */
void addBoxDropShadows(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                       const FilterChain &chain, const PaintClip &clip);

/** Resolve `clip-path` into a device-space triangulated polygon. */
int addClipPathForBox(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                      float viewportWidth, float viewportHeight);

/** Push `index` onto a clip chain (at most two survive). */
PaintClip addClipPath(PaintClip clip, int index);

}  // namespace paint_detail
}  // namespace xtgui

#endif /* XT_GUI_PAINT_INTERNAL_H */
