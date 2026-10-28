/*
 * xbintsc GUI engine — Canvas2D text drawing.
 *
 * `fillText`/`strokeText` shape a run with HarfBuzz and blit FreeType glyph
 * coverage through the rasteriser. The state and path live in `canvas.h`; this
 * unit only owns the text entry points and the optional `maxWidth` condense.
 */

#include "canvas.h"

#include <cmath>
#include <string>
#include <vector>

#include "canvas_internal.h"
#include "perf.h"

namespace xtgui {

float Canvas2D::measure_text(const std::string &text) const {
  return xt_text_measure_width(text, state_.font);
}

void Canvas2D::paint_text(const std::string &text, float x, float y, const CanvasPaint &paint,
                          bool stroke, float max_width) {
  Font *font = xt_text_resolve(state_.font);
  if (font == nullptr) return;
  /* Layout and paint share the bounded shaping cache: a label redrawn every
   * frame is shaped once, not once per frame. `maxWidth` only condenses the
   * drawn run, so it does not affect the shaped result. */
  const ShapedRun *run = xt_text_shape_cached(text, font);
  if (run == nullptr) return;
  const std::vector<ShapedGlyph> &glyphs = run->glyphs;
  float width = run->width;
  float ascent = 0.0f, descent = 0.0f, lineHeight = 0.0f;
  xt_text_metrics(state_.font, &ascent, &descent, &lineHeight);

  /* The optional `maxWidth` condenses the run horizontally instead of clipping
   * it: advances and glyph bitmaps are squeezed by the same factor. */
  float horizontal = 1.0f;
  if (max_width > 0.0f && width > max_width && width > 0.0001f) {
    horizontal = max_width / width;
  }

  float originX = x;
  if (state_.text_align == 1) {
    originX -= width * horizontal * 0.5f;
  } else if (state_.text_align == 2) {
    originX -= width * horizontal;
  }
  float originY = y;
  switch (state_.text_baseline) {
    case 1:
      originY += ascent;
      break;
    case 2:
      originY += (ascent - descent) * 0.5f;
      break;
    case 3:
      originY -= descent;
      break;
    case 4:
      originY += ascent * 0.8f;
      break;
    case 5:
      originY -= descent;
      break;
    default:
      break;
  }

  float scale = state_.transform.scale();
  if (scale <= 0.0001f) scale = 1.0f;
  bool scaled = std::fabs(scale - 1.0f) > 0.01f;

  /* Scaling a face is a cached lookup, but it was still one path key + hash
   * per glyph. There are at most a handful of faces in a run, so remember the
   * sized face for the ones this call touches. */
  Font *scaled_from[8] = {nullptr};
  Font *scaled_to[8] = {nullptr};
  int scaled_count = 0;
  auto sized_face = [&](Font *face) -> Font * {
    for (int i = 0; i < scaled_count; i++) {
      if (scaled_from[i] == face) return scaled_to[i];
    }
    Font *sized = xt_text_scale(face, state_.font.pixel_size * scale);
    if (sized == nullptr) sized = face;
    if (scaled_count < 8) {
      scaled_from[scaled_count] = face;
      scaled_to[scaled_count] = sized;
      scaled_count += 1;
    }
    return sized;
  };

  float penX = 0.0f;
  for (const ShapedGlyph &glyph : glyphs) {
    float userX = originX + (penX + glyph.x_offset) * horizontal;
    float userY = originY + glyph.y_offset;
    float deviceX = 0.0f, deviceY = 0.0f;
    to_device(userX, userY, &deviceX, &deviceY);
    Font *face = glyph.font != nullptr ? glyph.font : font;
    if (scaled) face = sized_face(face);
    const GlyphImage *image = xt_text_glyph_cached(face, glyph.glyph);
    if (image != nullptr && image->width > 0 && image->height > 0) {
      const float left = deviceX + (float)image->left * horizontal;
      const float top = deviceY - (float)image->top;
      mark_dirty(left, top, left + (float)image->width * horizontal,
                 top + (float)image->height);
      canvas_detail::blit_glyph(image_.pixels, image_.width, image_.height, image->pixels,
                                image->width, image->height, image->left, image->top, deviceX,
                                deviceY, horizontal, paint, state_.global_alpha, clip_mask());
    }
    penX += glyph.x_advance;
  }
  (void)stroke;
  generation_++;
}

void Canvas2D::fill_text(const std::string &text, float x, float y, float max_width) {
  if (begin_shadow_layer()) {
    fill_text(text, x, y, max_width);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  paint_text(text, x, y, fill_paint(), false, max_width);
}

void Canvas2D::stroke_text(const std::string &text, float x, float y, float max_width) {
  if (begin_shadow_layer()) {
    stroke_text(text, x, y, max_width);
    end_shadow_layer();
    return;
  }
  g_perf_frame.canvasDraws += 1;
  paint_text(text, x, y, stroke_paint(), true, max_width);
}

}  // namespace xtgui
