/*
 * xbintsc GUI engine — SVG decoding (NanoSVG wrapper).
 *
 * An SVG file is not a raster, so `stb_image` cannot decode it. This module
 * sits next to `image.{h,cpp}` and turns an SVG file into the same RGBA8
 * `Image` the rest of the image pipeline already consumes: parse once (cached
 * per path) and rasterise into a texture-sized buffer on demand. The parse and
 * the anti-aliased scanline rasteriser are the vendored NanoSVG library
 * (`nanosvg.h` + `nanosvgrast.h`) — the same class of low-level, third-party
 * image library `stb_image` is (see doc/gui.md).
 *
 * Only *files* are handled: `<img src="logo.svg">`. Inline `<svg>` trees in a
 * document are still ordinary unknown elements to the HTML parser and generate
 * no boxes.
 */
#ifndef XT_GUI_SVG_H
#define XT_GUI_SVG_H

#include <string>

#include "image.h"

namespace xtgui {

/** True when `path` names an SVG file: a `.svg` extension (case-insensitive),
 * or a small prefix of the contents that looks like an SVG root element. */
bool xt_svg_is_svg(const std::string &path);

/** Read the intrinsic size from an SVG file without rasterising it. Returns
 * false when the file is missing or cannot be parsed. Cached per path. */
bool xt_svg_size(const std::string &path, int *width, int *height);

/** Rasterise `path` to RGBA8. `width`/`height` are the requested pixel size;
 * when either is `<= 0` the intrinsic SVG size is used. The viewBox is scaled
 * uniformly at the larger of the two axis scales (preserving the SVG's aspect
 * ratio, so the caller's quad may stretch it exactly as it would a bitmap) and
 * clamped so neither axis exceeds 8192px. Returns false on parse/rasteriser
 * failure. The parse is cached per path; each rasterisation is fresh. */
bool xt_svg_render(const std::string &path, int width, int height, Image *out);

/** Drop the parsed-document cache (called on engine shutdown). */
void xt_svg_shutdown();

}  // namespace xtgui

#endif /* XT_GUI_SVG_H */
