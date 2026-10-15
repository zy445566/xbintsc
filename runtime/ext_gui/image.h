/*
 * xbintsc GUI engine — image decoding (stb_image wrapper).
 *
 * Only the decode step lives here; uploading the pixels to a GPU texture is
 * the renderer's job. Decoded images are cached by path so repeated paints do
 * not re-decode. This is one of the few third-party, *low-level* libraries the
 * engine is allowed to depend on (see doc/gui.md).
 */
#ifndef XT_GUI_IMAGE_H
#define XT_GUI_IMAGE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xtgui {

/** A decoded image: tightly packed RGBA8, row-major (`width * height * 4`). */
struct Image {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
};

/** Decode an image file (PNG/JPEG/BMP/GIF/TGA or SVG) to RGBA8. Returns
 * nullptr on failure. Results (including failures) are cached by path. A
 * `file://` prefix and URL percent-encoding are handled.
 *
 * `target_width`/`target_height` are the pixel size the image will be drawn at.
 * Raster formats ignore them (they decode at their intrinsic resolution), but
 * SVG is a vector format and is rasterised at (approximately) that size —
 * preserving its aspect ratio — so a scaled `<img src="logo.svg">` stays crisp
 * instead of being a magnified bitmap. Pass `<= 0` (the default) to use the intrinsic size. The cache is keyed by the
 * resulting pixel size for SVG and by path for raster formats. */
const Image *xt_image_load(const std::string &path, int target_width = 0, int target_height = 0);

/** Read just the intrinsic size from the file header, without decoding. For an
 * SVG this parses the document (the size comes from width/height or the
 * viewBox). Returns false when the file cannot be read/parsed. Cached. */
bool xt_image_size(const std::string &path, int *width, int *height);

/** Decode an in-memory buffer to RGBA8 (e.g. a future `data:` URI). */
bool xt_image_decode(const unsigned char *data, size_t size, Image *out);

/** True when `path` is a vector source (SVG) that is rasterised on demand
 * rather than decoded as a bitmap. The renderer uses this to key vector
 * textures by pixel size. */
bool xt_image_is_vector(const std::string &path);

void xt_image_shutdown();

}  // namespace xtgui

#endif /* XT_GUI_IMAGE_H */
