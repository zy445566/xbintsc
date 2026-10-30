/*
 * xbintsc GUI engine — optional image/SVG decoding table.
 *
 * `<img>`, CSS `background-image`, `drawImage` and inline `<svg>` all need
 * stb_image/NanoSVG, which together are a noticeable part of a DOM-only binary.
 * The `images` link feature installs this table from a bridge; without it, the
 * core's image/SVG entry points are no-ops and the page simply draws nothing for
 * those sources (the element and its layout still exist).
 */
#ifndef XT_GUI_IMAGE_API_H
#define XT_GUI_IMAGE_API_H

#include <cstddef>
#include <string>

#include "image.h"

struct XtImageApi {
  const xtgui::Image *(*load)(const std::string &path, int target_width, int target_height);
  bool (*size)(const std::string &path, int *width, int *height);
  bool (*decode)(const unsigned char *data, size_t size, xtgui::Image *out);
  bool (*is_vector)(const std::string &path);
  void (*shutdown)();
  bool (*svg_size)(const std::string &path, int *width, int *height);
  bool (*svg_render)(const std::string &path, int width, int height, xtgui::Image *out);
  bool (*svg_markup_size)(const std::string &markup, int *width, int *height);
  bool (*svg_markup_render)(const std::string &markup, int width, int height, xtgui::Image *out);
  int (*svg_markup_shapes)(const std::string &markup);
  bool (*inline_is_root)(const void *node);
  std::string (*inline_serialize)(const void *node);
  /** Set the window icon from the embedded PNG (a no-op without an icon). */
  void (*apply_icon)(void *window);
};

/** Install the table. Called by the `images` feature bridge before `main`. */
void xt_image_api_install(const XtImageApi *api);

/** The installed table, or null when the `images` feature was not linked. */
const XtImageApi *xt_image_api();

#endif /* XT_GUI_IMAGE_API_H */
