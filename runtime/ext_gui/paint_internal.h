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
  /** Product of the CSS `opacity` values on the ancestor chain (including the
   * element itself). It multiplies every painted colour's alpha, which is the
   * per-shape approximation of group opacity. */
  float opacity = 1.0f;
  struct Shadow {
    float dx = 0.0f;
    float dy = 0.0f;
    float blur = 0.0f;
    Color color;
  };
  std::vector<Shadow> shadows;

  bool empty() const { return ops.empty() && blur <= 0.0f && shadows.empty() && opacity >= 1.0f; }
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

/** Append the `outline` edge rectangles (after the border, before content). */
void addOutline(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                const FilterChain &chain, const PaintClip &clip, float viewportWidth,
                float viewportHeight);

/** Append the drop-shadow silhouettes of a box (once, before its content). */
void addBoxDropShadows(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                       const FilterChain &chain, const PaintClip &clip);

/** Append the `box-shadow` layers of a box. Outer shadows paint behind the
 * background; inset shadows paint above it (call with `inset = true` for the
 * latter). Declaration order is topmost first, so the list is reversed. */
void addBoxShadows(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                   const FilterChain &chain, const PaintClip &clip, bool inset,
                   float viewportWidth, float viewportHeight);

/** Rasterise (or fetch from the bounded cache) the blurred glyph mask of a
 * `text-shadow` layer. Returns null when the run has no visible glyphs. */
std::shared_ptr<const PaintBitmap> textShadowMask(const std::string &text, const FontSpec &font,
                                                  float blur);

/** Append a bitmap-backed image op (a rasterised shadow) with `color` as its
 * tint. The bitmap now owns its pixels; the renderer uploads it once. */
void addBitmapImage(DisplayList &out, std::shared_ptr<const PaintBitmap> bitmap, float x, float y,
                    const Color &color, const Matrix2D &transform, const PaintClip &clip);

/** Resolve and append a `backdrop-filter` op for a box (before its
 * background). Returns false when the filter list has no visible effect. */
bool addBackdropFilter(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                       const PaintClip &clip, float viewportWidth, float viewportHeight);

/** Resolve `clip-path` into a device-space triangulated polygon. */
int addClipPathForBox(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                      float viewportWidth, float viewportHeight);

/** Push `index` onto a clip chain (at most two survive). */
PaintClip addClipPath(PaintClip clip, int index);

}  // namespace paint_detail
}  // namespace xtgui

#endif /* XT_GUI_PAINT_INTERNAL_H */
