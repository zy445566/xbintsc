/*
 * xbintsc GUI engine — WebGL framebuffer, renderbuffer and readback bindings.
 *
 * Textures live in `dom_api_webgl_texture.cpp`; this unit owns the objects that
 * render into them, plus `readPixels`.
 */

#include "dom_api_internal.h"

#include <cstring>
#include <string>
#include <vector>

#include "dom_api_webgl_rt.h"
#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_live_context(self); }

uint32_t texture_argument(xt_value value) { return webgl_object_id(value, "__xt_gl_texture"); }

}  // namespace

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
    context->bind_framebuffer(webgl_int(argc, argv, 0, GL_FRAMEBUFFER),
                              webgl_object_id(xt_arg(argc, argv, 1), "__xt_gl_framebuffer"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_framebuffer_texture_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    /* WebGL's first argument is the target (`FRAMEBUFFER`), not the object:
     * the texture attaches to the currently bound framebuffer. */
    context->framebuffer_texture(context->bound_framebuffer_id(),
                                 texture_argument(xt_arg(argc, argv, 3)),
                                 webgl_int(argc, argv, 1, GL_COLOR_ATTACHMENT0));
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
        context->bound_framebuffer_id(),
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
