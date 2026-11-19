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

namespace domapi {
namespace {

/** `framebufferTexture2D`/`framebufferRenderbuffer` name a *target*: a
 * `READ_FRAMEBUFFER` argument modifies the read framebuffer, everything else
 * the draw framebuffer. Resolution code (three.js) binds the multisampled
 * source as READ and the resolve destination as DRAW and then attaches to each
 * in turn; ignoring the target attached the multisampled renderbuffer to the
 * destination and left it multisampled, which the GPU cannot blit into. */
uint32_t attachment_framebuffer(xtgui::WebGLContext *context, int target) {
  return target == GL_READ_FRAMEBUFFER ? context->bound_read_framebuffer_id()
                                       : context->bound_framebuffer_id();
}

}  // namespace
}  // namespace domapi

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
    /* WebGL's first argument is the target (`FRAMEBUFFER`, `DRAW_FRAMEBUFFER`
     * or `READ_FRAMEBUFFER`), not the object: the texture attaches to that
     * framebuffer. */
    context->framebuffer_texture(
        domapi::attachment_framebuffer(context, webgl_int(argc, argv, 0, GL_FRAMEBUFFER)),
        texture_argument(xt_arg(argc, argv, 3)),
        webgl_int(argc, argv, 1, GL_COLOR_ATTACHMENT0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_check_framebuffer_status) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_number(GL_FRAMEBUFFER_COMPLETE);
  const uint32_t framebuffer =
      domapi::attachment_framebuffer(context, webgl_int(argc, argv, 0, GL_FRAMEBUFFER));
  if (framebuffer == 0) return xt_number(GL_FRAMEBUFFER_COMPLETE);
  return xt_number(context->framebuffer_status(framebuffer));
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
    /* renderbufferStorage(target, internalformat, width, height) */
    context->renderbuffer_storage(context->bound_renderbuffer_id(),
                                  webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 0),
                                  webgl_int(argc, argv, 1, GL_RGBA8), 1);
  }
  return XT_UNDEFINED;
}

METHOD(webgl_renderbuffer_storage_multisample) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->renderbuffer_storage(context->bound_renderbuffer_id(),
                                  webgl_int(argc, argv, 3, 0), webgl_int(argc, argv, 4, 0),
                                  webgl_int(argc, argv, 2, GL_RGBA8),
                                  webgl_int(argc, argv, 1, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_framebuffer_renderbuffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->framebuffer_renderbuffer(
        domapi::attachment_framebuffer(context, webgl_int(argc, argv, 0, GL_FRAMEBUFFER)),
        webgl_int(argc, argv, 1, GL_COLOR_ATTACHMENT0),
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
  if (pname == GL_RENDERBUFFER_INTERNAL_FORMAT) return xt_number(renderbuffer->internalformat);
  if (pname == GL_RENDERBUFFER_SAMPLES) {
    /* A single-sampled renderbuffer reports 0 samples, like a browser. */
    return xt_number(renderbuffer->samples > 1 ? renderbuffer->samples : 0);
  }
  if (renderbuffer->depth) {
    if (pname == GL_RENDERBUFFER_DEPTH_SIZE) {
      return xt_number(renderbuffer->internalformat == GL_DEPTH_COMPONENT16 ? 16 : 24);
    }
    if (pname == GL_RENDERBUFFER_STENCIL_SIZE) {
      return xt_number(renderbuffer->internalformat == GL_STENCIL_INDEX8 ||
                               renderbuffer->internalformat == GL_DEPTH_STENCIL ||
                               renderbuffer->internalformat == GL_DEPTH24_STENCIL8 ||
                               renderbuffer->internalformat == GL_DEPTH32F_STENCIL8
                           ? 8
                           : 0);
    }
    return xt_number(0);
  }
  if (pname == GL_RENDERBUFFER_RED_SIZE || pname == GL_RENDERBUFFER_GREEN_SIZE ||
      pname == GL_RENDERBUFFER_BLUE_SIZE || pname == GL_RENDERBUFFER_ALPHA_SIZE) {
    return xt_number(8);
  }
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
