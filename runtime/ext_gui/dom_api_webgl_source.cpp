/*
 * xbintsc GUI engine — WebGL pixel sources.
 *
 * Decodes the things `texImage2D` accepts (`<img>`, `<canvas>`, `<video>`,
 * `ImageData` and `ImageBitmap`) into tightly packed RGBA8, and converts the
 * RGBA/RGB/LUMINANCE families to that layout.
 */

#include "dom_api_internal.h"

#include <cstring>
#include <string>
#include <vector>

#include "dom_api_webgl_rt.h"
#include "image.h"
#include "image_bitmap.h"
#include "webgl.h"

namespace domapi {
namespace {

void flip_rows(std::vector<uint8_t> *pixels, int width, int height) {
  const size_t stride = (size_t)width * 4;
  std::vector<uint8_t> row(stride);
  for (int y = 0; y < height / 2; y++) {
    uint8_t *top = pixels->data() + (size_t)y * stride;
    uint8_t *bottom = pixels->data() + (size_t)(height - 1 - y) * stride;
    memcpy(row.data(), top, stride);
    memcpy(top, bottom, stride);
    memcpy(bottom, row.data(), stride);
  }
}

}  // namespace

void webgl_convert_pixels(const uint8_t *source, int count, int format, bool premultiply,
                    std::vector<uint8_t> *out) {
  out->assign((size_t)count * 4, 255);
  for (int i = 0; i < count; i++) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    switch (format) {
      case GL_RGBA:
        r = source[i * 4 + 0];
        g = source[i * 4 + 1];
        b = source[i * 4 + 2];
        a = source[i * 4 + 3];
        break;
      case GL_RGB:
        r = source[i * 3 + 0];
        g = source[i * 3 + 1];
        b = source[i * 3 + 2];
        break;
      case GL_LUMINANCE_ALPHA:
        r = g = b = source[i * 2 + 0];
        a = source[i * 2 + 1];
        break;
      case GL_ALPHA:
        a = source[i];
        r = g = b = 0;
        break;
      case GL_LUMINANCE:
      default:
        r = g = b = source[i];
        break;
    }
    if (premultiply) {
      r = (uint8_t)((r * a + 127) / 255);
      g = (uint8_t)((g * a + 127) / 255);
      b = (uint8_t)((b * a + 127) / 255);
    }
    (*out)[(size_t)i * 4 + 0] = r;
    (*out)[(size_t)i * 4 + 1] = g;
    (*out)[(size_t)i * 4 + 2] = b;
    (*out)[(size_t)i * 4 + 3] = a;
  }
}


/** Decode a DOM source into top-down RGBA8. */
bool webgl_read_source(xt_value source, int *width, int *height, std::vector<uint8_t> *rgba,
                 bool premultiply) {
  if (XT_IS_OBJECT(source)) {
    xt_value bitmap_id = xt_object_get_cstr(source, "__xt_bitmap");
    if (XT_IS_NUMBER(bitmap_id)) {
      const xtgui::Image *bitmap =
          xtgui::xt_image_bitmap_get((uint64_t)xt_to_number(bitmap_id));
      if (bitmap == nullptr || bitmap->width <= 0 || bitmap->height <= 0) return false;
      *width = bitmap->width;
      *height = bitmap->height;
      if (premultiply) {
        webgl_convert_pixels(bitmap->pixels.data(), bitmap->width * bitmap->height, GL_RGBA, true,
                       rgba);
      } else {
        *rgba = bitmap->pixels;
      }
      return true;
    }
  }
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(source, &win);
  const xtgui::Image *image = nullptr;
  std::vector<uint8_t> owned;
  if (node != nullptr && node->isElement() && node->isTag("img")) {
    const std::string *src = node->attr("src");
    if (src != nullptr) image = xtgui::xt_image_load(*src);
  } else if (node != nullptr && node->isElement() && node->isTag("video")) {
    xtgui::MediaPlayer *player = xt_gui_media_player(win, node);
    if (player != nullptr) image = player->frame();
  } else if (node != nullptr && node->isElement() && node->isTag("canvas")) {
    if (xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node)) {
      image = &canvas->image();
    } else if (xtgui::WebGLContext *context = xt_gui_webgl_find(win, node)) {
      owned.resize((size_t)context->width() * context->height() * 4);
      if (!context->read_pixels(0, 0, context->width(), context->height(), owned.data())) {
        return false;
      }
      /* `readPixels` is GL-ordered (bottom-up); textures are stored top-down. */
      flip_rows(&owned, context->width(), context->height());
      *width = context->width();
      *height = context->height();
      rgba->swap(owned);
      return true;
    }
  } else if (XT_IS_OBJECT(source) && !xt_is_array_buffer(source)) {
    xt_value data = xt_object_get_cstr(source, "data");
    xt_value width_value = xt_object_get_cstr(source, "width");
    xt_value height_value = xt_object_get_cstr(source, "height");
    if (XT_IS_OBJECT(data) && XT_IS_NUMBER(width_value) && XT_IS_NUMBER(height_value)) {
      const int w = (int)xt_to_number(width_value);
      const int h = (int)xt_to_number(height_value);
      const std::vector<uint8_t> bytes = webgl_bytes(data);
      if (w <= 0 || h <= 0 || bytes.size() < (size_t)w * h * 4) return false;
      webgl_convert_pixels(bytes.data(), w * h, GL_RGBA, premultiply, rgba);
      *width = w;
      *height = h;
      return true;
    }
    /* `new Image()` is a plain object with a `src`. */
    xt_value src = xt_object_get_cstr(source, "src");
    if (XT_IS_STRING(src)) {
      const char *path = xt_string_data(xt_to_string(src));
      if (path != nullptr && path[0] != '\0') image = xtgui::xt_image_load(path);
    }
  }
  if (image == nullptr || image->width <= 0 || image->height <= 0) return false;
  *width = image->width;
  *height = image->height;
  if (premultiply) {
    webgl_convert_pixels(image->pixels.data(), image->width * image->height, GL_RGBA, true, rgba);
  } else {
    *rgba = image->pixels;
  }
  return true;
}


}  // namespace domapi
