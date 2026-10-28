/*
 * xbintsc GUI engine — `<canvas>` 2D drawing.
 *
 * A `Canvas2D` owns one RGBA8 bitmap and the 2D drawing state (fill/stroke
 * style, line width, transform, text settings). It is the engine-side object
 * behind `canvas.getContext("2d")`; the DOM bindings live in
 * `dom_api_canvas.cpp` and the pixels reach the screen through the same GPU
 * texture path as images and video (`renderer_image.cpp`).
 *
 * The rasteriser is deliberately small and self-written (no Skia/Cairo): it
 * fills rectangles, polygons and flattened arcs with sub-scanline coverage
 * anti-aliasing, and blits FreeType glyph bitmaps for `fillText`. The pieces
 * live in `canvas_raster.cpp`; this header only exposes the object model.
 *
 * Canvas pixels are *not* touched by the garbage collector: a canvas is owned
 * by the window (keyed by its DOM node) and released with the document.
 */
#ifndef XT_GUI_CANVAS_H
#define XT_GUI_CANVAS_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "image.h"
#include "text.h"

namespace xtgui {

/** A 2D affine transform, laid out like the DOM `DOMMatrix` (a b c d e f). */
struct CanvasTransform {
  float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;

  void apply(float x, float y, float *outX, float *outY) const {
    *outX = a * x + c * y + e;
    *outY = b * x + d * y + f;
  }
  /** True when the transform keeps axis-aligned rectangles axis aligned (no
   * rotation or skew), which lets the rasteriser use its fast rectangle path. */
  bool axis_aligned() const { return b == 0.0f && c == 0.0f; }
  /** Approximate uniform scale, used to size stroked lines and text. */
  float scale() const;
  /** The inverse transform (device -> user space); identity when singular. */
  CanvasTransform inverted() const;
};

/** A point in device (canvas) space. */
struct CanvasPoint {
  float x = 0.0f;
  float y = 0.0f;
};

/** One clip region: per-pixel coverage (0 = fully clipped, 255 = kept). A clip
 * is baked into device space when `clip()` runs, exactly like a browser. */
struct CanvasClipMask {
  std::vector<uint8_t> data;
  int width = 0;
  int height = 0;
  uint8_t at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return data[(size_t)y * (size_t)width + (size_t)x];
  }
};

/** One subpath of the current path (device space). */
using CanvasSubpath = std::vector<CanvasPoint>;

/**
 * A `CanvasGradient` handle: the colour stops plus the geometric definition of
 * a linear or radial gradient. Gradients are owned by the canvas that created
 * them, so a stale context handle resolves to no gradient instead of a dangling
 * pointer. The DOM layer copies only the id around; the stops stay live, which
 * is what makes `addColorStop` after `fillStyle = gradient` affect later fills.
 */
class CanvasGradient {
 public:
  enum class Type { Linear, Radial };

  CanvasGradient(uint64_t id, Type type, float x0, float y0, float x1, float y1, float r0,
                 float r1);

  uint64_t id() const { return id_; }
  Type type() const { return type_; }

  /** Add a colour stop (offset clamped to 0..1, stops kept sorted by offset). */
  void add_color_stop(float offset, const float color[4]);
  /** Sample the gradient at a user-space point. A gradient with no stops is
   * transparent black, so this always fills `out`. */
  bool color_at(float x, float y, float out[4]) const;

 private:
  struct Stop {
    float offset = 0.0f;
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  };

  uint64_t id_ = 0;
  Type type_ = Type::Linear;
  float x0_ = 0.0f, y0_ = 0.0f, x1_ = 0.0f, y1_ = 0.0f;
  float r0_ = 0.0f, r1_ = 0.0f;
  std::vector<Stop> stops_;
};

class CanvasGradient;
class CanvasPattern;

/** `globalCompositeOperation` values, in the order the DOM layer maps names. */
enum CanvasComposite {
  kCanvasCompositeSourceOver = 0,
  kCanvasCompositeSourceIn,
  kCanvasCompositeSourceOut,
  kCanvasCompositeSourceAtop,
  kCanvasCompositeDestinationOver,
  kCanvasCompositeDestinationIn,
  kCanvasCompositeDestinationOut,
  kCanvasCompositeDestinationAtop,
  kCanvasCompositeLighter,
  kCanvasCompositeCopy,
  kCanvasCompositeXor,
  kCanvasCompositeMultiply,
  kCanvasCompositeScreen,
  kCanvasCompositeOverlay,
  kCanvasCompositeDarken,
  kCanvasCompositeLighten,
  kCanvasCompositeColorDodge,
  kCanvasCompositeColorBurn,
  kCanvasCompositeHardLight,
  kCanvasCompositeSoftLight,
  kCanvasCompositeDifference,
  kCanvasCompositeExclusion,
  kCanvasCompositeHue,
  kCanvasCompositeSaturation,
  kCanvasCompositeColor,
  kCanvasCompositeLuminosity,
};

/** What a fill or stroke paints with: a solid colour, a gradient sampled through
 * the inverse of the current transform, or a repeating image pattern. `color`
 * points into the canvas state and stays valid for one draw call. */
struct CanvasPaint {
  const float *color = nullptr;
  const CanvasGradient *gradient = nullptr;
  const CanvasPattern *pattern = nullptr;
  CanvasTransform inverse;
  /** `globalCompositeOperation` as the `CanvasComposite` enum value. */
  int composite = 0;
};

/**
 * A `CanvasPattern`: an image tiled in user space with one of the four
 * repetition modes. Owned by the canvas that created it, like a gradient, so a
 * stale handle resolves to no pattern instead of a dangling pointer.
 */
class CanvasPattern {
 public:
  enum class Repeat { Both, X, Y, None };

  CanvasPattern(uint64_t id, const Image &image, Repeat repeat);
  uint64_t id() const { return id_; }
  Repeat repeat() const { return repeat_; }
  /** Pattern colour at a user-space point; false when the point falls outside a
   * non-repeating pattern (the caller then paints nothing). */
  bool color_at(float x, float y, float out[4]) const;

 private:
  uint64_t id_ = 0;
  Image image_;
  Repeat repeat_ = Repeat::Both;
};

class Canvas2D {
 public:
  Canvas2D(int width, int height);
  ~Canvas2D();

  int width() const { return image_.width; }
  int height() const { return image_.height; }
  uint64_t id() const { return id_; }
  /** Bumped by every draw; the renderer re-uploads when it changes. */
  uint64_t generation() const { return generation_; }
  /** Device-space bounds of the pixels changed since the renderer last uploaded
   * them. Empty means nothing visible changed (a no-op or off-canvas draw), so
   * the renderer can skip the upload entirely. */
  struct CanvasDirtyRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
  };
  const CanvasDirtyRect &pending_dirty() const { return dirty_; }
  void clear_dirty() { dirty_ = CanvasDirtyRect(); }
  const Image &image() const { return image_; }
  /** Resize the bitmap, discarding its contents (the DOM `canvas.width = …`
   * behaviour). */
  void resize(int width, int height);
  /** `getContextAttributes().alpha`: an opaque canvas clears to opaque black
   * instead of transparent, as the WebGL/2D context attribute specifies. */
  void set_alpha(bool alpha);
  bool alpha() const { return alpha_; }

  /* -- state ------------------------------------------------------------- */
  void save();
  void restore();

  /* -- transform --------------------------------------------------------- */
  void translate(float x, float y);
  void rotate(float radians);
  void scale(float sx, float sy);
  void transform(float a, float b, float c, float d, float e, float f);
  void set_transform(float a, float b, float c, float d, float e, float f);
  void reset_transform() { state_.transform = CanvasTransform(); }
  const CanvasTransform &current_transform() const { return state_.transform; }

  /* -- style ------------------------------------------------------------- */
  void set_fill_style(const std::string &css);
  const std::string &fill_style() const { return state_.fill_style; }
  void set_stroke_style(const std::string &css);
  const std::string &stroke_style() const { return state_.stroke_style; }
  /** Use a gradient created by this canvas (`0` restores the solid colour). */
  void set_fill_gradient(uint64_t id);
  void set_stroke_gradient(uint64_t id);
  uint64_t fill_gradient_id() const { return state_.fill_gradient; }
  uint64_t stroke_gradient_id() const { return state_.stroke_gradient; }
  /** Use a pattern created by this canvas (`0` restores the solid colour). */
  void set_fill_pattern(uint64_t id);
  void set_stroke_pattern(uint64_t id);
  uint64_t fill_pattern_id() const { return state_.fill_pattern; }
  uint64_t stroke_pattern_id() const { return state_.stroke_pattern; }
  void set_line_width(float width) { state_.line_width = width > 0.0f ? width : 0.0f; }
  float line_width() const { return state_.line_width; }
  void set_line_cap(const std::string &value);
  const std::string &line_cap() const { return state_.line_cap_string; }
  void set_line_join(const std::string &value);
  const std::string &line_join() const { return state_.line_join_string; }
  void set_miter_limit(float limit);
  float miter_limit() const { return state_.miter_limit; }
  void set_line_dash(const std::vector<float> &segments);
  const std::vector<float> &line_dash() const { return state_.line_dash; }
  void set_line_dash_offset(float offset) { state_.line_dash_offset = offset; }
  float line_dash_offset() const { return state_.line_dash_offset; }
  void set_global_alpha(float alpha);
  float global_alpha() const { return state_.global_alpha; }
  void set_global_composite_operation(const std::string &value);
  const std::string &global_composite_operation() const {
    return state_.global_composite_string;
  }
  void set_shadow_color(const std::string &css);
  const std::string &shadow_color() const { return state_.shadow_color_string; }
  void set_shadow_blur(float blur);
  float shadow_blur() const { return state_.shadow_blur; }
  void set_shadow_offset_x(float offset) { state_.shadow_offset_x = offset; }
  float shadow_offset_x() const { return state_.shadow_offset_x; }
  void set_shadow_offset_y(float offset) { state_.shadow_offset_y = offset; }
  float shadow_offset_y() const { return state_.shadow_offset_y; }
  void set_image_smoothing_enabled(bool enabled) { state_.image_smoothing = enabled; }
  bool image_smoothing_enabled() const { return state_.image_smoothing; }
  void set_image_smoothing_quality(const std::string &value);
  const std::string &image_smoothing_quality() const {
    return state_.image_smoothing_quality;
  }
  void set_font(const std::string &font);
  const std::string &font() const { return state_.font_string; }
  void set_text_align(const std::string &value);
  const std::string &text_align() const { return state_.text_align_string; }
  void set_text_baseline(const std::string &value);
  const std::string &text_baseline() const { return state_.text_baseline_string; }

  /* -- gradients --------------------------------------------------------- */
  CanvasGradient *create_linear_gradient(float x0, float y0, float x1, float y1);
  CanvasGradient *create_radial_gradient(float x0, float y0, float r0, float x1, float y1,
                                         float r1);
  CanvasGradient *find_gradient(uint64_t id) const;

  /* -- patterns ---------------------------------------------------------- */
  /** Create a tiled pattern from `image`; the canvas owns it. */
  CanvasPattern *create_pattern(const Image &image, CanvasPattern::Repeat repeat);
  CanvasPattern *find_pattern(uint64_t id) const;

  /* -- paths ------------------------------------------------------------- */
  void begin_path();
  void close_path();
  void move_to(float x, float y);
  void line_to(float x, float y);
  void rect(float x, float y, float width, float height);
  /** Four corner radii, clockwise from the top-left (CSS/`roundRect` order). */
  void round_rect(float x, float y, float width, float height, float radius_tl, float radius_tr,
                  float radius_br, float radius_bl);
  void arc(float cx, float cy, float radius, float start, float end, bool counterclockwise);
  void ellipse(float cx, float cy, float rx, float ry, float rotation, float start, float end,
               bool counterclockwise);
  void quadratic_curve_to(float cpx, float cpy, float x, float y);
  void bezier_curve_to(float c1x, float c1y, float c2x, float c2y, float x, float y);
  /** Fill the current path (`even_odd` selects the even-odd rule). */
  void fill(bool even_odd = false);
  void stroke();
  /** Fill/stroke a path supplied by the caller (`Path2D`); the points are user
   * space and are transformed with the current state, like the current path. */
  void fill_paths(const std::vector<CanvasSubpath> &paths, bool even_odd);
  void stroke_paths(const std::vector<CanvasSubpath> &paths);
  /** Intersect the clip region with the current path (or a `Path2D`). */
  void clip(bool even_odd = false);
  void clip_paths(const std::vector<CanvasSubpath> &paths, bool even_odd);
  /** Hit testing in the current user space. */
  bool is_point_in_path(float x, float y, bool even_odd = false) const;
  bool is_point_in_paths(const std::vector<CanvasSubpath> &paths, float x, float y,
                         bool even_odd) const;
  bool is_point_in_stroke(float x, float y) const;
  bool is_point_in_stroke_paths(const std::vector<CanvasSubpath> &paths, float x, float y) const;

  /* -- rectangles -------------------------------------------------------- */
  void clear_rect(float x, float y, float width, float height);
  void fill_rect(float x, float y, float width, float height);
  void stroke_rect(float x, float y, float width, float height);

  /* -- text -------------------------------------------------------------- */
  float measure_text(const std::string &text) const;
  /** Draw text; a positive `max_width` condenses it horizontally so the run is
   * at most that wide, as the canvas `fillText`/`strokeText` fourth argument
   * requires. */
  void fill_text(const std::string &text, float x, float y, float max_width = 0.0f);
  void stroke_text(const std::string &text, float x, float y, float max_width = 0.0f);

  /* -- images ------------------------------------------------------------ */
  /** Draw `source` scaled into the destination rectangle, sampling the source
   * rectangle `(sx, sy, sw, sh)`. `source` is RGBA8. */
  void draw_image(const Image &source, float dx, float dy, float dw, float dh, float sx, float sy,
                  float sw, float sh);

  /* -- pixel writes and export ------------------------------------------- */
  /** `putImageData`: copy RGBA bytes straight into the bitmap (no transform,
   * blending or globalAlpha), clipped to the canvas and the optional dirty
   * rectangle. */
  void set_image_data(const std::vector<unsigned char> &rgba, int data_width, int data_height,
                      int dx, int dy, int dirty_x, int dirty_y, int dirty_width, int dirty_height);
  /** Encode the bitmap as a PNG (used by `toDataURL`/`toBlob`). */
  bool encode_png(std::vector<unsigned char> *out) const;

  /* -- test hook --------------------------------------------------------- */
  /** Copy an `(x, y, w, h)` region to `out` (RGBA8, row-major). Out-of-bounds
   * pixels are transparent. Returns false when the region is empty. */
  bool get_image_data(int x, int y, int width, int height, std::vector<unsigned char> *out) const;

 private:
  struct State {
    CanvasTransform transform;
    float line_width = 1.0f;
    float global_alpha = 1.0f;
    int line_cap = 0;        // 0 butt, 1 round, 2 square
    int line_join = 0;       // 0 miter, 1 round, 2 bevel
    float miter_limit = 10.0f;
    std::vector<float> line_dash;
    float line_dash_offset = 0.0f;
    FontSpec font;
    std::string font_string = "10px sans-serif";
    int text_align = 0;      // 0 start/left, 1 center, 2 right/end
    int text_baseline = 0;   // 0 alphabetic, 1 top, 2 middle, 3 bottom, 4 hanging, 5 ideographic
    std::string text_align_string = "start";
    std::string text_baseline_string = "alphabetic";
    std::string fill_style = "#000000";
    std::string stroke_style = "#000000";
    float fill_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float stroke_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    uint64_t fill_gradient = 0;
    uint64_t stroke_gradient = 0;
    uint64_t fill_pattern = 0;
    uint64_t stroke_pattern = 0;
    std::string line_cap_string = "butt";
    std::string line_join_string = "miter";
    int global_composite = kCanvasCompositeSourceOver;
    std::string global_composite_string = "source-over";
    float shadow_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::string shadow_color_string = "rgba(0, 0, 0, 0)";
    float shadow_blur = 0.0f;
    float shadow_offset_x = 0.0f;
    float shadow_offset_y = 0.0f;
    bool image_smoothing = true;
    std::string image_smoothing_quality = "low";
    /** Current clip region; `save`/`restore` share it (a later `clip` installs a
     * new mask instead of mutating the saved one). */
    std::shared_ptr<CanvasClipMask> clip;
  };

  /** Transform a user-space point into device space. */
  void to_device(float x, float y, float *outX, float *outY) const;
  /** Union a device-space rectangle into the pending upload region. */
  void mark_dirty(float x0, float y0, float x1, float y1);
  /** Union the bounds of a device-space path (grown by `expand`) into the
   * pending upload region. */
  void mark_path_dirty(const std::vector<CanvasSubpath> &subpaths, float expand);
  void mark_all_dirty();
  /** How far a stroke can reach outside its centre line (half width, miter
   * limit and a pixel of anti-aliasing slack). */
  float stroke_extent() const;
  /** The current path transformed into device space. */
  std::vector<CanvasSubpath> device_subpaths() const;
  /** Any user-space path transformed into device space. */
  std::vector<CanvasSubpath> device_subpaths_of(const std::vector<CanvasSubpath> &paths) const;
  /** Add one device-space polygon loop to `out`. */
  void flatten_arc(float cx, float cy, float radius, float start, float end, bool ccw,
                   CanvasSubpath *out) const;
  /** Append a flattened arc to the current subpath, moving/line-to its start as
   * the canvas path rules require. Shared by `arc` and `ellipse`. */
  void append_path_points(const CanvasSubpath &points);
  /** Paint descriptors for the current state. */
  CanvasPaint fill_paint() const;
  CanvasPaint stroke_paint() const;
  CanvasPaint make_paint(uint64_t gradient_id, uint64_t pattern_id, const float color[4]) const;
  void stroke_subpath(const CanvasSubpath &points, bool closed, const CanvasPaint &paint);
  /** Stroke one polyline with caps and joins; `half` is half the device-space
   * line width. Used by `stroke_subpath` for both solid and dashed paths. */
  void stroke_polyline(const CanvasSubpath &points, bool closed, float half,
                       const CanvasPaint &paint);
  /** Split `points` into the "on" pieces of the current dash pattern. */
  void dash_polyline(const CanvasSubpath &points, bool closed,
                     std::vector<CanvasSubpath> *out) const;
  void paint_text(const std::string &text, float x, float y, const CanvasPaint &paint,
                  bool stroke, float max_width);
  /** True when the current state paints a shadow for a drawing operation. */
  bool shadow_active() const;
  /** Redirect the next draw into a scratch layer so its alpha can be turned
   * into a shadow. Returns false while already inside the shadow layer (or when
   * no shadow is active), in which case the caller draws directly. */
  bool begin_shadow_layer();
  void end_shadow_layer();
  /** The current clip coverage, or null when the canvas is unclipped. */
  const CanvasClipMask *clip_mask() const;
  void composite_layer(const Image &layer);
  void composite_shadow_layer(const Image &layer);

  Image image_;
  /** Scratch bitmap a draw is rendered into while its shadow is built. */
  Image shadow_layer_;
  bool in_shadow_layer_ = false;
  bool alpha_ = true;
  uint64_t id_ = 0;
  uint64_t generation_ = 0;
  CanvasDirtyRect dirty_;
  State state_;
  std::vector<State> stack_;
  std::vector<CanvasSubpath> path_;
  std::unordered_map<uint64_t, std::unique_ptr<CanvasGradient>> gradients_;
  std::unordered_map<uint64_t, std::unique_ptr<CanvasPattern>> patterns_;
};

/** The canvas with the given process-wide id, or null. Used by the renderer. */
Canvas2D *xt_canvas_by_id(uint64_t id);

}  // namespace xtgui

#endif /* XT_GUI_CANVAS_H */
