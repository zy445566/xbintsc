/*
 * xbintsc GUI engine — `images` link feature implementation.
 *
 * The only core-side place that references stb_image/NanoSVG and the inline-SVG
 * serializer. `image_bridge.c` (linked only when the feature is active) calls
 * `xt_image_feature_register`, which installs `XtImageApi`; without it the core
 * draws no images or SVG and those members stay out of the binary.
 */

#include "image_api.h"

#include <SDL3/SDL.h>

#include <cstring>

#include "inline_svg.h"
#include "svg.h"

/* Embedded by the driver's icon object; a zero size means "no icon". */
extern "C" {
extern const unsigned char xt_app_icon_data[];
extern const unsigned long long xt_app_icon_size;
extern const char xt_app_icon_format[];
}

namespace {

const xtgui::Image *load(const std::string &path, int target_width, int target_height) {
  return xtgui::xt_image_load(path, target_width, target_height);
}

bool size(const std::string &path, int *width, int *height) {
  return xtgui::xt_image_size(path, width, height);
}

bool decode(const unsigned char *data, size_t size, xtgui::Image *out) {
  return xtgui::xt_image_decode(data, size, out);
}

bool is_vector(const std::string &path) { return xtgui::xt_image_is_vector(path); }

void shutdown() { xtgui::xt_image_shutdown(); }

bool svg_size(const std::string &path, int *width, int *height) {
  return xtgui::xt_svg_size(path, width, height);
}

bool svg_render(const std::string &path, int width, int height, xtgui::Image *out) {
  return xtgui::xt_svg_render(path, width, height, out);
}

bool svg_markup_size(const std::string &markup, int *width, int *height) {
  return xtgui::xt_svg_markup_size(markup, width, height);
}

bool svg_markup_render(const std::string &markup, int width, int height, xtgui::Image *out) {
  return xtgui::xt_svg_markup_render(markup, width, height, out);
}

int svg_markup_shapes(const std::string &markup) {
  return xtgui::xt_svg_markup_shapes(markup);
}

bool inline_is_root(const void *node) {
  return xtgui::xt_inline_svg_is_root(static_cast<const xtgui::Node *>(node));
}

std::string inline_serialize(const void *node) {
  return xtgui::xt_inline_svg_serialize(static_cast<const xtgui::Node *>(node));
}

void apply_icon(void *window) {
  SDL_Window *sdl_window = static_cast<SDL_Window *>(window);
  const size_t size = (size_t)xt_app_icon_size;
  if (sdl_window == nullptr || size == 0) return;
  if (strcmp(xt_app_icon_format, "png") != 0) return;
  xtgui::Image image;
  if (!xtgui::xt_image_decode(xt_app_icon_data, size, &image)) return;
  if (image.width <= 0 || image.height <= 0) return;
  SDL_Surface *surface = SDL_CreateSurfaceFrom(
      image.width, image.height, SDL_PIXELFORMAT_RGBA32, image.pixels.data(), image.width * 4);
  if (surface == nullptr) return;
  SDL_SetWindowIcon(sdl_window, surface);
  SDL_DestroySurface(surface);
}

const XtImageApi kImageApi = {
    load,
    size,
    decode,
    is_vector,
    shutdown,
    svg_size,
    svg_render,
    svg_markup_size,
    svg_markup_render,
    svg_markup_shapes,
    inline_is_root,
    inline_serialize,
    apply_icon,
};

}  // namespace

extern "C" void xt_image_feature_register(void) { xt_image_api_install(&kImageApi); }
