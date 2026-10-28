/*
 * xbintsc GUI engine — `createImageBitmap`.
 *
 * The browser entry point returns a promise (as it does on the web), so code
 * that writes `const bitmap = await createImageBitmap(canvas)` works unchanged.
 * The pixels are kept in `image_bitmap.cpp`; `texImage2D` reads the same
 * registry through the bitmap's `__xt_bitmap` id.
 */

#include "gui_engine.h"
#include "dom_api_internal.h"

#include <cstring>
#include <string>
#include <vector>

#include "image_bitmap.h"
#include "canvas.h"
#include "webgl.h"

namespace {

void flip_rows(std::vector<unsigned char> *pixels, int width, int height) {
  const size_t stride = (size_t)width * 4;
  std::vector<unsigned char> row(stride);
  for (int y = 0; y < height / 2; y++) {
    unsigned char *top = pixels->data() + (size_t)y * stride;
    unsigned char *bottom = pixels->data() + (size_t)(height - 1 - y) * stride;
    memcpy(row.data(), top, stride);
    memcpy(top, bottom, stride);
    memcpy(bottom, row.data(), stride);
  }
}

bool source_pixels(xt_value source, xtgui::Image *out) {
  /* ImageData / another bitmap-like object: width, height, data. */
  xt_value width_value = xt_object_get_cstr(source, "width");
  xt_value height_value = xt_object_get_cstr(source, "height");
  xt_value data = xt_object_get_cstr(source, "data");
  if (XT_IS_OBJECT(data) && XT_IS_NUMBER(width_value) && XT_IS_NUMBER(height_value)) {
    const int width = (int)xt_to_number(width_value);
    const int height = (int)xt_to_number(height_value);
    const std::vector<uint8_t> bytes = domapi::webgl_bytes(data);
    if (width <= 0 || height <= 0 || bytes.size() < (size_t)width * height * 4) return false;
    out->width = width;
    out->height = height;
    out->pixels.assign(bytes.begin(), bytes.begin() + (size_t)width * height * 4);
    return true;
  }

  if (XT_IS_OBJECT(source)) {
    xt_value canvas_id = xt_object_get_cstr(source, "__xt_gui_canvas");
    if (XT_IS_NUMBER(canvas_id)) {
      xtgui::Canvas2D *canvas = xtgui::xt_canvas_by_id((uint64_t)xt_to_number(canvas_id));
      if (canvas == nullptr || canvas->image().width <= 0) return false;
      *out = canvas->image();
      return true;
    }
  }

  XtGuiWindow *win = nullptr;
  xtgui::Node *node = domapi::resolve_node(source, &win);
  if (node == nullptr || !node->isElement()) return false;
  if (node->isTag("img")) {
    const std::string *src = node->attr("src");
    const xtgui::Image *image = src != nullptr ? xtgui::xt_image_load(*src) : nullptr;
    if (image == nullptr) return false;
    *out = *image;
    return true;
  }
  if (node->isTag("canvas")) {
    if (xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node)) {
      *out = canvas->image();
      return true;
    }
    if (xtgui::WebGLContext *context = xt_gui_webgl_find(win, node)) {
      out->width = context->width();
      out->height = context->height();
      out->pixels.resize((size_t)out->width * out->height * 4);
      if (!context->read_pixels(0, 0, out->width, out->height, out->pixels.data())) {
        return false;
      }
      /* readPixels is GL-ordered (bottom-up); bitmaps are stored top-down. */
      flip_rows(&out->pixels, out->width, out->height);
      return true;
    }
    return false;
  }
  if (node->isTag("video")) {
    xtgui::MediaPlayer *player = xt_gui_media_player(win, node);
    const xtgui::Image *frame = player != nullptr ? player->frame() : nullptr;
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return false;
    *out = *frame;
    return true;
  }
  return false;
}

xt_value bitmap_object(const xtgui::Image &image) {
  const uint64_t id = xtgui::xt_image_bitmap_add(image);
  xt_value name = xt_string_from_cstr("ImageBitmap");
  xt_value constructor = xt_browser_type(1, &name);
  xt_value prototype = xt_object_get_cstr(constructor, "prototype");
  xt_value bitmap = XT_IS_OBJECT(prototype) ? xt_object_new_with_proto(prototype)
                                            : xt_object_new();
  xt_object_set(bitmap, xt_string_from_cstr("width"), xt_number(image.width));
  xt_object_set(bitmap, xt_string_from_cstr("height"), xt_number(image.height));
  xt_object_set(bitmap, xt_string_from_cstr("__xt_bitmap"), xt_number((double)id));
  return bitmap;
}

}  // namespace

namespace {

xt_value create_bitmap(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_reject(xt_string_from_cstr("TypeError"));
  xtgui::Image image;
  if (!source_pixels(xt_arg(argc, argv, 0), &image)) {
    return xt_promise_reject(xt_string_from_cstr("InvalidStateError"));
  }
  return xt_promise_resolve(bitmap_object(image));
}

/** The closure ABI the runtime calls for a first-class function value. */
xt_value create_bitmap_call(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return create_bitmap(argc, argv);
}

xt_value g_create_bitmap = XT_UNDEFINED;

}  // namespace

/* `createImageBitmap` is a *global value*: the compiler calls this getter once
 * and stores the returned closure, which is then called like any function. */
xt_value xt_browser_create_image_bitmap(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (!XT_IS_OBJECT(g_create_bitmap)) {
    g_create_bitmap = xt_closure_new((void *)create_bitmap_call, 0, nullptr);
    xt_gc_add_root(&g_create_bitmap);
  }
  return g_create_bitmap;
}

