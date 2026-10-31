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
#include <memory>
#include <string>
#include <vector>

#include "css.h"
#include "layout.h"
#include "text.h"
#include "transform.h"

namespace xtgui {

/** One resolved colour stop of a gradient. `offset` is a fraction of the
 * gradient line and non-decreasing, as in CSS. */
struct GradientStop {
  float offset = 0.0f;
  Color color;
};

/** A radial `background-image` layer resolved by the paint pass: the renderer
 * rasterises it into one RGBA texture and draws that as a single image quad.
 *
 * Nesting translucent bands instead composites every band's alpha once per band
 * — each band covers the whole interior — so a vignette whose stops fade to
 * `transparent` turns into an opaque disc that hides the scene behind it.
 * Resolving the ramp per pixel composites it exactly once. */
struct PaintGradient {
  std::vector<GradientStop> stops;
  /** Centre in box-local logical pixels; the drawn quad spans the whole box. */
  float center_x = 0.0f;
  float center_y = 0.0f;
  float radius_x = 0.0f;
  float radius_y = 0.0f;
  /** `repeating-radial-gradient()`: the stop pattern restarts every `period`
   * (the last stop's offset) instead of clamping at the outer ellipse. */
  bool repeating = false;
  float period = 1.0f;
  /** Identity of the ramp (stops and geometry) for the renderer's texture cache. */
  std::string key;
};

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
  /** Set on a `text-shadow` copy (diagnostics only; the renderer draws it like
   * any other glyph run). */
  bool shadow = false;
};

/** A CPU-rasterised RGBA bitmap carried by a display-list image op. Shadows are
 * painted as one pre-blurred texture rather than a stack of offset copies: a
 * separable Gaussian on the mask is the CSS blur, and the shader only has to
 * composite one quad. `scale` is bitmap pixels per logical pixel. */
struct PaintBitmap {
  int width = 0;
  int height = 0;
  float scale = 1.0f;
  /** Logical offset of the bitmap's top-left from the shape's origin (a text
   * run's start/baseline, or a box's top-left). */
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  std::string key;
  std::vector<unsigned char> pixels;
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
  /** Modulating colour for the image shader (white normally; a filter's
   * multiplicative effect may tint it). */
  Color tint{1.0f, 1.0f, 1.0f, 1.0f};
  /** A CPU-rasterised shadow mask (white RGB, coverage in alpha) drawn with
   * `tint` as its colour. */
  std::shared_ptr<const PaintBitmap> bitmap;
  /** A `background-image` radial gradient: the renderer rasterises it into the
   * quad instead of loading `src`/`svg`. Its stops already carry the colour
   * functions of the filter chain; `tint` still carries brightness/opacity. */
  std::shared_ptr<const PaintGradient> gradient;
};

/** One triangle of a clip path in device-space logical pixels. The paint pass
 * triangulates (possibly concave) polygons once so the renderer can clip
 * convex shape/glyph quads by a simple half-plane test per triangle. */
struct ClipTriangle {
  float x[3] = {0, 0, 0};
  float y[3] = {0, 0, 0};
};

/** A resolved `clip-path` polygon plus its triangulation. */
struct ClipPathGeometry {
  std::vector<float> points;  // x, y pairs (diagnostics)
  std::vector<ClipTriangle> triangles;
};

/** The clip rectangle in effect for a painted shape (device-space logical
 * pixels — a transformed subtree contributes the bounding box of its clip).
 * `width <= 0` means "no clip". `paths` index `DisplayList::clip_paths`; at most
 * two nested `clip-path` shapes are kept (CSS would intersect every ancestor;
 * deeper nesting is rare and the outermost two are the ones a UI uses). */
struct PaintClip {
  bool active = false;
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  int path_count = 0;
  int paths[2] = {-1, -1};
};

enum class PaintKind { Rect, Text, Image, LayerBegin, LayerEnd, Backdrop };

/** One `mix-blend-mode` compositing group. The ops between its `LayerBegin`
 * and `LayerEnd` markers are rendered to an offscreen texture and composited
 * over the backdrop as a group, so overlapping children blend once. */
struct PaintLayer {
  MixBlendMode blend = MixBlendMode::Normal;
};

/** One `backdrop-filter` application: the renderer snapshots the scene painted
 * so far, applies the Gaussian blur / colour matrix to it, and composites the
 * result back only inside the element's rounded border box. */
struct PaintBackdrop {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  float radius = 0;
  /** Standard deviation of the Gaussian in logical pixels (0 = no blur). */
  float blur = 0;
  /** Combined colour filter, row-major 3x3 plus a per-channel offset. */
  float color[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  float offset[3] = {0, 0, 0};
  float alpha = 1;
};

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
  /** Device-space clip-path polygons shared by an element's ops. */
  std::vector<ClipPathGeometry> clip_paths;
  /** `mix-blend-mode` groups referenced by `LayerBegin` ops. */
  std::vector<PaintLayer> layers;
  /** `backdrop-filter` regions referenced by `Backdrop` ops. */
  std::vector<PaintBackdrop> backdrops;

  void clear() {
    rects.clear();
    texts.clear();
    images.clear();
    ops.clear();
    clip_paths.clear();
    layers.clear();
    backdrops.clear();
  }
  bool empty() const { return rects.empty() && texts.empty() && images.empty(); }
  /** Human-readable dump, one shape per line, in painter order (diagnostics/tests). */
  std::string dump() const;
};

namespace paint_detail {

/** Interpolate a gradient's colour at `t` (a fraction of the gradient line; any
 * value when `repeating`). Stops interpolate in premultiplied alpha, as CSS
 * does, so a `transparent` stop does not darken the ramp towards an opaque
 * colour; the result is converted back to straight alpha, which is what both the
 * band and the texture paths blend. */
Color gradientColorAt(const std::vector<GradientStop> &stops, bool repeating, float period,
                      float t);

}  // namespace paint_detail

/** Build the display list for a laid-out tree and append it to `out`. The
 * viewport size resolves `vw`/`vh` units inside `clip-path` and shadows; pass 0
 * to take it from the root box's containing block. */
void xt_paint_build(const LayoutBox *root, DisplayList &out, float viewportWidth = 0.0f,
                    float viewportHeight = 0.0f);

/** Shift every shape and clip in `list` by `(dx, dy)`. Used for page scroll:
 * the layout stays at document coordinates and painting moves the viewport. */
void xt_paint_translate(DisplayList &list, float dx, float dy);

}  // namespace xtgui

#endif /* XT_GUI_PAINT_H */
