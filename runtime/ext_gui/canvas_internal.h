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

/** Fill one closed polygon loop (already in device space) with `paint`,
 * anti-aliased, blending source-over. */
void fill_loop(std::vector<unsigned char> &pixels, int width, int height,
               const std::vector<CanvasPoint> &loop, const CanvasPaint &paint, float alpha,
               bool even_odd = false);

/** Fill several loops as one path with the nonzero winding rule (so a contour
 * inside another punches a hole), or the even-odd rule. */
void fill_loops(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<std::vector<CanvasPoint>> &loops, const CanvasPaint &paint,
                float alpha, bool even_odd = false);

/** Fill a device-space axis-aligned rectangle, anti-aliased. Used by the
 * `fillRect`/`clearRect` fast path when the transform preserves axis
 * alignment. */
void fill_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
               float x1, float y1, const CanvasPaint &paint, float alpha);

/** Clear a device-space axis-aligned rectangle to transparent. */
void clear_rect(std::vector<unsigned char> &pixels, int width, int height, float x0, float y0,
                float x1, float y1);

/** Blit an RGBA8 source image into the destination rectangle using nearest
 * sampling, source-over. Runs in device space (no transform). */
void draw_image(std::vector<unsigned char> &pixels, int width, int height, const Image &source,
                float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                float alpha);

/** Blit an RGBA8 source through a full affine transform. The destination
 * rectangle is mapped to a parallelogram by `transform`; each device pixel is
 * inverse-mapped and sampled from the source rectangle. This is the path that
 * makes rotated/skewed `drawImage` (and perspective-grid canvas textures) draw
 * the image instead of squashing it into the bounding box. */
void draw_image_affine(std::vector<unsigned char> &pixels, int width, int height,
                       const Image &source, float dx, float dy, float dw, float dh, float sx,
                       float sy, float sw, float sh, const CanvasTransform &transform,
                       float alpha);

/** Composite one grayscale glyph bitmap at `(x, baselineY)` with `paint`.
 * `scale_x` < 1 condenses the bitmap horizontally (`fillText`'s `maxWidth`). */
void blit_glyph(std::vector<unsigned char> &pixels, int width, int height,
                const std::vector<unsigned char> &glyph, int glyphWidth, int glyphHeight, int left,
                int top, float x, float baselineY, float scale_x, const CanvasPaint &paint,
                float alpha);

}  // namespace canvas_detail
}  // namespace xtgui

#endif /* XT_GUI_CANVAS_INTERNAL_H */
