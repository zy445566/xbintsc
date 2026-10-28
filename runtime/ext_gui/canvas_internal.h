/*
 * xbintsc GUI engine — canvas rasteriser internals.
 *
 * The public object model lives in `canvas.h`; these functions are the
 * software rasteriser `canvas.cpp` calls. They are deliberately small,
 * self-contained and free of SDL so they can be unit-tested by reasoning about
 * pixels alone.
 */
#ifndef XT_GUI_CANVAS_INTERNAL_H
#define XT_GUI_CANVAS_INTERNAL_H

#include <cstdint>
#include <vector>

#include "canvas.h"

namespace xtgui {
namespace canvas_detail {

/** Source-over blend `color` (0..1) with `coverage` (0..1) into one RGBA8
 * pixel. `alpha` folds in `globalAlpha`. */
void blend_pixel(unsigned char *pixel, const float color[4], float coverage, float alpha);

/** The full `globalCompositeOperation` matrix; `composite` is a
 * `CanvasComposite` value. */
void blend_pixel_mode(unsigned char *pixel, const float color[4], float coverage, float alpha,
                      int composite);

/** One path's anti-aliased coverage, clipped to the canvas bitmap. Only the
 * covered span of each row is stored, so a thin diagonal stroke costs its own
 * pixels instead of its (potentially huge) bounding box. */
struct CoverageGrid {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  /** Concatenated row spans; row `r` owns `[row_offset[r], row_offset[r+1])`. */
  std::vector<float> values;
  /** One past-the-end index per row, plus a final entry (size = rows + 1). */
  std::vector<uint32_t> row_offset;
  /** Device-space x of the first stored coverage value of each row. */
  std::vector<int> row_x;
  bool empty() const { return x1 <= x0 || y1 <= y0; }
};

/** Rasterise `loops` (device space) into a coverage grid. */
void compute_coverage(int width, int height,
                      const std::vector<std::vector<CanvasPoint>> &loops, bool even_odd,
                      CoverageGrid *out);

/** Fill one closed polygon loop (already in device space) with `paint`,
 * anti-aliased, blending source-over. */
void fill_loop(std::vector<unsigned char> &pixels, int width, int height,
               const std::vector<CanvasPoint> &loop, const CanvasPaint &paint, float alpha,
               bool even_odd = false, const CanvasClipMask *clip = nullptr);

/** Fill several loops as one path with the nonzero winding rule (so a contour
 * inside another punches a hole), or the even-odd rule. */
void fill_loops(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<std::vector<CanvasPoint>> &loops, const CanvasPaint &paint,
                float alpha, bool even_odd = false, const CanvasClipMask *clip = nullptr);

/** Fill a device-space axis-aligned rectangle, anti-aliased. Used by the
 * `fillRect`/`clearRect` fast path when the transform preserves axis
 * alignment. */
void fill_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
               float x1, float y1, const CanvasPaint &paint, float alpha,
               const CanvasClipMask *clip = nullptr);

/** Clear a device-space axis-aligned rectangle to transparent. */
void clear_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
                float x1, float y1, const CanvasClipMask *clip = nullptr);

/** Blit an RGBA8 source image into the destination rectangle, source-over.
 * Runs in device space (no transform). `smooth` selects bilinear sampling. */
void draw_image(std::vector<unsigned char> &pixels, int width, int height, const Image &source,
                float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                float alpha, bool smooth, const CanvasClipMask *clip = nullptr);

/** Blit an RGBA8 source through a full affine transform. The destination
 * rectangle is mapped to a parallelogram by `transform`; each device pixel is
 * inverse-mapped and sampled from the source rectangle. This is the path that
 * makes rotated/skewed `drawImage` (and perspective-grid canvas textures) draw
 * the image instead of squashing it into the bounding box. */
void draw_image_affine(std::vector<unsigned char> &pixels, int width, int height,
                       const Image &source, float dx, float dy, float dw, float dh, float sx,
                       float sy, float sw, float sh, const CanvasTransform &transform,
                       float alpha, bool smooth, const CanvasClipMask *clip = nullptr);

/** Composite one grayscale glyph bitmap at `(x, baselineY)` with `paint`.
 * `scale_x` < 1 condenses the bitmap horizontally (`fillText`'s `maxWidth`). */
void blit_glyph(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<unsigned char> &glyph, int glyphWidth, int glyphHeight, int left,
                int top, float x, float baselineY, float scale_x, const CanvasPaint &paint,
                float alpha, const CanvasClipMask *clip = nullptr);

}  // namespace canvas_detail
}  // namespace xtgui

#endif /* XT_GUI_CANVAS_INTERNAL_H */
