/*
 * xbintsc GUI engine — the Canvas2D image APIs (`putImageData`,
 * `createImageData`, `createPattern`, `toDataURL`, `toBlob`).
 *
 * `ImageData` is the same `{ width, height, data }` shape `getImageData` already
 * returns (a byte array), patterns are canvas-owned handles like gradients, and
 * `toDataURL`/`toBlob` export the bitmap as a PNG data URL / callback result.
 */

#include "dom_api_internal.h"

#include <cmath>
#include <string>
#include <vector>

#include "canvas.h"
#include "image.h"

namespace domapi {

namespace {

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

/** Read an `ImageData`-shaped value into width/height/RGBA bytes. */
bool image_data_arg(xt_value value, int *width, int *height, std::vector<unsigned char> *bytes) {
  if (!XT_IS_OBJECT(value)) return false;
  xt_value w = xt_object_get_cstr(value, "width");
  xt_value h = xt_object_get_cstr(value, "height");
  if (!XT_IS_NUMBER(w) || !XT_IS_NUMBER(h)) return false;
  *width = (int)xt_to_number(w);
  *height = (int)xt_to_number(h);
  if (*width <= 0 || *height <= 0) return false;
  xt_value data = xt_object_get_cstr(value, "data");
  if (!XT_IS_ARRAY(data)) return false;
  int32_t count = (int32_t)xt_to_number(xt_array_length(data));
  if (count < *width * *height * 4) return false;
  bytes->resize((size_t)count);
  for (int32_t i = 0; i < count; i++) {
    (*bytes)[(size_t)i] = (unsigned char)xt_to_number(xt_array_get(data, xt_number((double)i)));
  }
  return true;
}

xt_value make_image_data(int width, int height, const std::vector<unsigned char> &bytes) {
  xt_value result = xt_object_new();
  xt_value data = xt_array_new(0, nullptr);
  for (unsigned char byte : bytes) xt_array_push(data, xt_number((double)byte));
  xt_object_set(result, xt_string_from_cstr("width"), xt_number((double)width));
  xt_object_set(result, xt_string_from_cstr("height"), xt_number((double)height));
  xt_object_set(result, xt_string_from_cstr("data"), data);
  return result;
}

/** Decode a `createPattern` source: an `<img>` or another `<canvas>`. */
bool source_image(XtGuiWindow *win, xt_value value, xtgui::Image *out) {
  xtgui::Node *node = resolve_node(value, &win);
  if (node == nullptr || !node->isElement()) return false;
  if (node->isTag("img")) {
    const std::string *src = node->attr("src");
    if (src == nullptr) return false;
    const xtgui::Image *image = xtgui::xt_image_load(*src);
    if (image == nullptr || image->width <= 0 || image->height <= 0) return false;
    *out = *image;
    return true;
  }
  xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
  if (canvas == nullptr) return false;
  *out = canvas->image();
  return out->width > 0 && out->height > 0;
}

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<unsigned char> &bytes) {
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  size_t i = 0;
  while (i + 2 < bytes.size()) {
    uint32_t triple = ((uint32_t)bytes[i] << 16) | ((uint32_t)bytes[i + 1] << 8) | bytes[i + 2];
    out.push_back(kBase64[(triple >> 18) & 0x3F]);
    out.push_back(kBase64[(triple >> 12) & 0x3F]);
    out.push_back(kBase64[(triple >> 6) & 0x3F]);
    out.push_back(kBase64[triple & 0x3F]);
    i += 3;
  }
  if (i < bytes.size()) {
    uint32_t triple = (uint32_t)bytes[i] << 16;
    bool two = i + 1 < bytes.size();
    if (two) triple |= (uint32_t)bytes[i + 1] << 8;
    out.push_back(kBase64[(triple >> 18) & 0x3F]);
    out.push_back(kBase64[(triple >> 12) & 0x3F]);
    out.push_back(two ? kBase64[(triple >> 6) & 0x3F] : '=');
    out.push_back('=');
  }
  return out;
}

xt_value blob_callback(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  xt_value fn = xt_closure_env(env, 0);
  xt_value blob = xt_closure_env(env, 1);
  xt_value args[1] = {blob};
  return xt_call_with_this(fn, XT_UNDEFINED, 1, args);
}

}  // namespace

/** The canvas behind a context handle or a `<canvas>` element handle. */
xtgui::Canvas2D *canvas_or_element(xt_value self) {
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas != nullptr) return canvas;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr) return nullptr;
  return xt_gui_canvas_find(win, node);
}

xt_value canvas_pattern_handle(xt_value context, const xtgui::Canvas2D *canvas, uint64_t id) {
  const std::string key = "__xt_gui_pattern_" + std::to_string(id);
  xt_value cached = xt_object_get_cstr(context, key.c_str());
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value handle = xt_object_new_with_proto(xt_object_prototype());
  if (canvas != nullptr) {
    xt_object_set(handle, xt_string_from_cstr("__xt_gui_canvas"), xt_number((double)canvas->id()));
  }
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_pattern"), xt_number((double)id));
  xt_object_set(context, xt_string_from_cstr(key.c_str()), handle);
  return handle;
}

uint64_t canvas_pattern_id(xt_value handle, const xtgui::Canvas2D *canvas) {
  if (canvas == nullptr || !XT_IS_OBJECT(handle)) return 0;
  xt_value canvasId = xt_object_get_cstr(handle, "__xt_gui_canvas");
  xt_value patternId = xt_object_get_cstr(handle, "__xt_gui_pattern");
  if (!XT_IS_NUMBER(canvasId) || !XT_IS_NUMBER(patternId)) return 0;
  if ((uint64_t)xt_to_number(canvasId) != canvas->id()) return 0;
  uint64_t id = (uint64_t)xt_to_number(patternId);
  return canvas->find_pattern(id) == nullptr ? 0 : id;
}

METHOD(canvas_put_image_data) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr || argc < 3) return XT_UNDEFINED;
  int width = 0;
  int height = 0;
  std::vector<unsigned char> bytes;
  if (!image_data_arg(xt_arg(argc, argv, 0), &width, &height, &bytes)) return XT_UNDEFINED;
  int dx = (int)canvas_number_arg(argc, argv, 1, 0.0);
  int dy = (int)canvas_number_arg(argc, argv, 2, 0.0);
  int dirty_x = (int)canvas_number_arg(argc, argv, 3, 0.0);
  int dirty_y = (int)canvas_number_arg(argc, argv, 4, 0.0);
  int dirty_width = (int)canvas_number_arg(argc, argv, 5, 0.0);
  int dirty_height = (int)canvas_number_arg(argc, argv, 6, 0.0);
  canvas->set_image_data(bytes, width, height, dx, dy, dirty_x, dirty_y, dirty_width,
                         dirty_height);
  return XT_UNDEFINED;
}

METHOD(canvas_create_image_data) {
  (void)env;
  int width = 0;
  int height = 0;
  if (argc >= 2) {
    width = (int)canvas_number_arg(argc, argv, 0, 0.0);
    height = (int)canvas_number_arg(argc, argv, 1, 0.0);
  } else if (argc >= 1) {
    int source_width = 0;
    int source_height = 0;
    std::vector<unsigned char> bytes;
    if (image_data_arg(xt_arg(argc, argv, 0), &source_width, &source_height, &bytes)) {
      width = source_width;
      height = source_height;
    }
  }
  if (width <= 0 || height <= 0) return XT_UNDEFINED;
  return make_image_data(width, height, std::vector<unsigned char>((size_t)width * height * 4, 0));
}

METHOD(canvas_create_pattern) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_from_context(self);
  if (canvas == nullptr || argc < 1) return XT_NULL;
  XtGuiWindow *win = nullptr;
  resolve_node(xt_object_get_cstr(self, "__xt_gui_node"), &win);
  xtgui::Image source;
  if (win == nullptr || !source_image(win, xt_arg(argc, argv, 0), &source)) return XT_NULL;
  std::string repetition = argc >= 2 ? canvas_string_arg(argc, argv, 1) : "repeat";
  xtgui::CanvasPattern::Repeat repeat = xtgui::CanvasPattern::Repeat::Both;
  if (repetition == "repeat-x") repeat = xtgui::CanvasPattern::Repeat::X;
  else if (repetition == "repeat-y") repeat = xtgui::CanvasPattern::Repeat::Y;
  else if (repetition == "no-repeat") repeat = xtgui::CanvasPattern::Repeat::None;
  xtgui::CanvasPattern *pattern = canvas->create_pattern(source, repeat);
  return canvas_pattern_handle(self, canvas, pattern->id());
}

/** `toDataURL(type?)`: a PNG data URL (the engine has one encoder; a browser
 * also falls back to PNG for an unsupported type). */
METHOD(canvas_to_data_url) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Canvas2D *canvas = canvas_or_element(self);
  if (canvas == nullptr) return xt_string_from_cstr("data:,");
  std::vector<unsigned char> png;
  if (!canvas->encode_png(&png)) return xt_string_from_cstr("data:,");
  std::string url = "data:image/png;base64," + base64_encode(png);
  return xt_string_from_cstr(url.c_str());
}

/** `toBlob(callback, type?)`: build a Blob-shaped result and deliver it from a
 * microtask, as the browser does. The bytes are on `__xt_gui_blob_data`; there
 * is no `URL.createObjectURL` in this runtime. */
METHOD(canvas_to_blob) {
  (void)env;
  xtgui::Canvas2D *canvas = canvas_or_element(self);
  xt_value callback = argc >= 1 ? xt_arg(argc, argv, 0) : XT_UNDEFINED;
  if (canvas == nullptr || !XT_IS_FUNCTION(callback)) return XT_UNDEFINED;
  std::vector<unsigned char> png;
  if (!canvas->encode_png(&png)) return XT_UNDEFINED;
  xt_value data = xt_array_new(0, nullptr);
  for (unsigned char byte : png) xt_array_push(data, xt_number((double)byte));
  xt_value blob = xt_object_new();
  xt_object_set(blob, xt_string_from_cstr("type"), xt_string_from_cstr("image/png"));
  xt_object_set(blob, xt_string_from_cstr("size"), xt_number((double)png.size()));
  xt_object_set(blob, xt_string_from_cstr("__xt_gui_blob_data"), data);
  xt_value captured[2] = {callback, blob};
  xt_value task = xt_closure_new((void *)blob_callback, 2, captured);
  xt_value args[1] = {task};
  xt_queue_microtask(1, args);
  return XT_UNDEFINED;
}

#undef METHOD

}  // namespace domapi
