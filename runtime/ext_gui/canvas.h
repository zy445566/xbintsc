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
#include <string>
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
};

/** A point in device (canvas) space. */
struct CanvasPoint {
  float x = 0.0f;
  float y = 0.0f;
};

/** One subpath of the current path (device space). */
using CanvasSubpath = std::vector<CanvasPoint>;

class Canvas2D {
 public:
  Canvas2D(int width, int height);
  ~Canvas2D();

  int width() const { return image_.width; }
  int height() const { return image_.height; }
  uint64_t id() const { return id_; }
  /** Bumped by every draw; the renderer re-uploads when it changes. */
  uint64_t generation() const { return generation_; }
  const Image &image() const { return image_; }
  /** Resize the bitmap, discarding its contents (the DOM `canvas.width = …`
   * behaviour). */
  void resize(int width, int height);

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

  /* -- style ------------------------------------------------------------- */
  void set_fill_style(const std::string &css);
  const std::string &fill_style() const { return state_.fill_style; }
  void set_stroke_style(const std::string &css);
  const std::string &stroke_style() const { return state_.stroke_style; }
  void set_line_width(float width) { state_.line_width = width > 0.0f ? width : 0.0f; }
  float line_width() const { return state_.line_width; }
  void set_global_alpha(float alpha);
  float global_alpha() const { return state_.global_alpha; }
  void set_font(const std::string &font);
  const std::string &font() const { return state_.font_string; }
  void set_text_align(const std::string &value);
  const std::string &text_align() const { return state_.text_align_string; }
  void set_text_baseline(const std::string &value);
  const std::string &text_baseline() const { return state_.text_baseline_string; }

  /* -- paths ------------------------------------------------------------- */
  void begin_path();
  void close_path();
  void move_to(float x, float y);
  void line_to(float x, float y);
  void rect(float x, float y, float width, float height);
  void arc(float cx, float cy, float radius, float start, float end, bool counterclockwise);
  void quadratic_curve_to(float cpx, float cpy, float x, float y);
  void bezier_curve_to(float c1x, float c1y, float c2x, float c2y, float x, float y);
  void fill();
  void stroke();

  /* -- rectangles -------------------------------------------------------- */
  void clear_rect(float x, float y, float width, float height);
  void fill_rect(float x, float y, float width, float height);
  void stroke_rect(float x, float y, float width, float height);

  /* -- text -------------------------------------------------------------- */
  float measure_text(const std::string &text) const;
  void fill_text(const std::string &text, float x, float y);
  void stroke_text(const std::string &text, float x, float y);

  /* -- images ------------------------------------------------------------ */
  /** Draw `source` scaled into the destination rectangle, sampling the source
   * rectangle `(sx, sy, sw, sh)`. `source` is RGBA8. */
  void draw_image(const Image &source, float dx, float dy, float dw, float dh, float sx, float sy,
                  float sw, float sh);

  /* -- test hook --------------------------------------------------------- */
  /** Copy an `(x, y, w, h)` region to `out` (RGBA8, row-major). Out-of-bounds
   * pixels are transparent. Returns false when the region is empty. */
  bool get_image_data(int x, int y, int width, int height, std::vector<unsigned char> *out) const;

 private:
  struct State {
    CanvasTransform transform;
    float line_width = 1.0f;
    float global_alpha = 1.0f;
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
  };

  /** Transform a user-space point into device space. */
  void to_device(float x, float y, float *outX, float *outY) const;
  /** The current path transformed into device space. */
  std::vector<CanvasSubpath> device_subpaths() const;
  /** Add one device-space polygon loop to `out`. */
  void flatten_arc(float cx, float cy, float radius, float start, float end, bool ccw,
                   CanvasSubpath *out) const;
  void stroke_subpath(const CanvasSubpath &points, bool closed, const float color[4]);
  void paint_text(const std::string &text, float x, float y, const float color[4], bool stroke);

  Image image_;
  uint64_t id_ = 0;
  uint64_t generation_ = 0;
  State state_;
  std::vector<State> stack_;
  std::vector<CanvasSubpath> path_;
};

/** The canvas with the given process-wide id, or null. Used by the renderer. */
Canvas2D *xt_canvas_by_id(uint64_t id);

}  // namespace xtgui

#endif /* XT_GUI_CANVAS_H */
