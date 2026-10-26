/*
 * xbintsc GUI engine — WebGL texture, framebuffer and readback bindings.
 *
 * `texImage2D` accepts every source a WebGL 1 page uses: a raw pixel array, an
 * `<img>`/`<canvas>` element and an `ImageData`-shaped object. Whatever the
 * source, it is converted to RGBA8 (SDL_GPU's sampler format) on the CPU and
 * uploaded to the GPU; `UNPACK_FLIP_Y_WEBGL` and
 * `UNPACK_PREMULTIPLY_ALPHA_WEBGL` are honoured here.
 */

#include "dom_api_internal.h"

#include <cstring>
#include <string>
#include <vector>

#include "canvas.h"
#include "dom_api_webgl_rt.h"
#include "image.h"
#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_from_context(self); }

uint32_t texture_argument(xt_value value) { return webgl_object_id(value, "__xt_gl_texture"); }

int bytes_per_pixel(int format) {
  if (format == GL_RGBA) return 4;
  if (format == GL_RGB) return 3;
  if (format == GL_LUMINANCE_ALPHA) return 2;
  return 1;
}

void convert_pixels(const uint8_t *source, int count, int format, bool premultiply,
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

/** Decode a DOM source into top-down RGBA8. */
bool read_source(xt_value source, int *width, int *height, std::vector<uint8_t> *rgba,
                 bool premultiply) {
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(source, &win);
  const xtgui::Image *image = nullptr;
  std::vector<uint8_t> owned;
  if (node != nullptr && node->isElement() && node->isTag("img")) {
    const std::string *src = node->attr("src");
    if (src != nullptr) image = xtgui::xt_image_load(*src);
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
      convert_pixels(bytes.data(), w * h, GL_RGBA, premultiply, rgba);
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
    convert_pixels(image->pixels.data(), image->width * image->height, GL_RGBA, true, rgba);
  } else {
    *rgba = image->pixels;
  }
  return true;
}

}  // namespace

/* -- texture objects ----------------------------------------------------- */

METHOD(webgl_create_texture) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_texture", context->create_texture(), "texture");
}

METHOD(webgl_delete_texture) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_texture(texture_argument(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl_bind_texture) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_texture(webgl_int(argc, argv, 0, GL_TEXTURE_2D),
                          texture_argument(xt_arg(argc, argv, 1)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_active_texture) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->active_texture(webgl_int(argc, argv, 0, GL_TEXTURE0));
  return XT_UNDEFINED;
}

METHOD(webgl_tex_parameteri) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    const uint32_t texture = context->bound_texture(0);
    context->tex_parameter(texture, webgl_int(argc, argv, 1, 0), webgl_int(argc, argv, 2, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_tex_parameterf) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    const uint32_t texture = context->bound_texture(0);
    context->tex_parameter(texture, webgl_int(argc, argv, 1, 0),
                           (int)webgl_number(argc, argv, 2, 0.0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_get_tex_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return xt_number(context->tex_parameter_value(context->bound_texture(0),
                                                webgl_int(argc, argv, 1, 0)));
}

METHOD(webgl_tex_image_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int target = webgl_int(argc, argv, 0, GL_TEXTURE_2D);
  if (target != GL_TEXTURE_2D) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0) {
    context->set_error(GL_INVALID_OPERATION);
    return XT_UNDEFINED;
  }
  const int level = webgl_int(argc, argv, 1, 0);
  if (level != 0) return XT_UNDEFINED;  // mip levels beyond 0 are not uploaded
  bool flip_y = false;
  bool premultiply = false;
  webgl_unpack_flags(self, &flip_y, &premultiply);

  int width = 0;
  int height = 0;
  int format = GL_RGBA;
  std::vector<uint8_t> rgba;
  if (argc >= 9) {
    width = webgl_int(argc, argv, 3, 0);
    height = webgl_int(argc, argv, 4, 0);
    format = webgl_int(argc, argv, 6, GL_RGBA);
    const int type = webgl_int(argc, argv, 7, GL_UNSIGNED_BYTE);
    if (type != GL_UNSIGNED_BYTE) return XT_UNDEFINED;
    const std::vector<uint8_t> pixels = webgl_bytes(xt_arg(argc, argv, 8));
    if (pixels.empty()) {
      rgba.assign((size_t)width * height * 4, 0);
    } else if (pixels.size() < (size_t)width * height * bytes_per_pixel(format)) {
      context->set_error(GL_INVALID_VALUE);
      return XT_UNDEFINED;
    } else {
      convert_pixels(pixels.data(), width * height, format, premultiply, &rgba);
    }
  } else {
    if (!read_source(xt_arg(argc, argv, 5), &width, &height, &rgba, premultiply)) {
      context->set_error(GL_INVALID_VALUE);
      return XT_UNDEFINED;
    }
    format = GL_RGBA;
  }
  if (width <= 0 || height <= 0) {
    context->set_error(GL_INVALID_VALUE);
    return XT_UNDEFINED;
  }
  if (flip_y) flip_rows(&rgba, width, height);
  context->tex_image_rgba(texture, width, height, rgba.data());
  return XT_UNDEFINED;
}

METHOD(webgl_tex_sub_image_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0) return XT_UNDEFINED;
  const int level = webgl_int(argc, argv, 1, 0);
  if (level != 0) return XT_UNDEFINED;
  bool flip_y = false;
  bool premultiply = false;
  webgl_unpack_flags(self, &flip_y, &premultiply);
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int format = GL_RGBA;
  std::vector<uint8_t> rgba;
  if (argc >= 9) {
    x = webgl_int(argc, argv, 2, 0);
    y = webgl_int(argc, argv, 3, 0);
    width = webgl_int(argc, argv, 4, 0);
    height = webgl_int(argc, argv, 5, 0);
    format = webgl_int(argc, argv, 6, GL_RGBA);
    const std::vector<uint8_t> pixels = webgl_bytes(xt_arg(argc, argv, 8));
    if (pixels.size() < (size_t)width * height * bytes_per_pixel(format)) return XT_UNDEFINED;
    convert_pixels(pixels.data(), width * height, format, premultiply, &rgba);
  } else {
    int source_width = 0;
    int source_height = 0;
    if (!read_source(xt_arg(argc, argv, 6), &source_width, &source_height, &rgba, premultiply)) {
      return XT_UNDEFINED;
    }
    width = source_width;
    height = source_height;
  }
  if (flip_y) flip_rows(&rgba, width, height);
  context->tex_sub_image_rgba(texture, x, y, width, height, rgba.data());
  return XT_UNDEFINED;
}

METHOD(webgl_generate_mipmap) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->generate_mipmap(context->bound_texture(0));
  return XT_UNDEFINED;
}

/* -- framebuffers and renderbuffers -------------------------------------- */

METHOD(webgl_create_framebuffer) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_framebuffer", context->create_framebuffer(),
                           "framebuffer");
}

METHOD(webgl_delete_framebuffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->delete_framebuffer(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_framebuffer"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_bind_framebuffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_framebuffer(webgl_object_id(xt_arg(argc, argv, 1), "__xt_gl_framebuffer"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_framebuffer_texture_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->framebuffer_texture(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_framebuffer"),
                                 texture_argument(xt_arg(argc, argv, 3)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_check_framebuffer_status) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_number(GL_FRAMEBUFFER_COMPLETE);
  const uint32_t framebuffer = context->bound_framebuffer_id();
  if (framebuffer == 0) return xt_number(GL_FRAMEBUFFER_COMPLETE);
  return xt_number(context->framebuffer_status(framebuffer) ? GL_FRAMEBUFFER_COMPLETE
                                                            : GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
}

METHOD(webgl_create_renderbuffer) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_renderbuffer", context->create_renderbuffer(),
                           "renderbuffer");
}

METHOD(webgl_delete_renderbuffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->delete_renderbuffer(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_renderbuffer"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_bind_renderbuffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_renderbuffer(webgl_object_id(xt_arg(argc, argv, 1), "__xt_gl_renderbuffer"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_renderbuffer_storage) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->renderbuffer_storage(context->bound_renderbuffer_id(), webgl_int(argc, argv, 3, 0),
                                  webgl_int(argc, argv, 4, 0), false);
  }
  return XT_UNDEFINED;
}

METHOD(webgl_framebuffer_renderbuffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->framebuffer_renderbuffer(
        webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_framebuffer"),
        webgl_object_id(xt_arg(argc, argv, 3), "__xt_gl_renderbuffer"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_get_renderbuffer_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_number(0);
  xtgui::WebGLRenderbuffer *renderbuffer = context->renderbuffer(context->bound_renderbuffer_id());
  if (renderbuffer == nullptr) return xt_number(0);
  const int pname = webgl_int(argc, argv, 1, 0);
  if (pname == GL_RENDERBUFFER_WIDTH) return xt_number(renderbuffer->width);
  if (pname == GL_RENDERBUFFER_HEIGHT) return xt_number(renderbuffer->height);
  return xt_number(0);
}

METHOD(webgl_read_pixels) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int x = webgl_int(argc, argv, 0, 0);
  const int y = webgl_int(argc, argv, 1, 0);
  const int width = webgl_int(argc, argv, 2, 0);
  const int height = webgl_int(argc, argv, 3, 0);
  const int format = webgl_int(argc, argv, 4, GL_RGBA);
  const int type = webgl_int(argc, argv, 5, GL_UNSIGNED_BYTE);
  if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) return XT_UNDEFINED;
  std::vector<uint8_t> pixels((size_t)width * height * 4);
  if (!context->read_pixels(x, y, width, height, pixels.data())) return XT_UNDEFINED;
  webgl_write_bytes(xt_arg(argc, argv, 6), pixels.data(), pixels.size());
  return XT_UNDEFINED;
}

}  // namespace domapi
