/*
 * xbintsc GUI engine — WebGL 2 multisample textures and format queries.
 *
 * Kept apart from `dom_api_webgl2.cpp` so that file stays within the
 * repository's source-length budget; the methods are registered on the same
 * WebGL 2 prototype.
 */

#include "dom_api_internal.h"

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_live_context(self); }

}  // namespace

METHOD(webgl2_tex_storage_2d_multisample) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture();
  const int internalformat = webgl_int(argc, argv, 2, 0x8058);  // GL_RGBA8
  context->tex_storage_2d_multisample(texture, webgl_int(argc, argv, 1, 0),
                                      xtgui::webgl_storage_format(internalformat),
                                      webgl_int(argc, argv, 3, 0),
                                      webgl_int(argc, argv, 4, 0));
  return XT_UNDEFINED;
}

METHOD(webgl2_tex_image_2d_multisample) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture();
  const int internalformat = webgl_int(argc, argv, 2, 0x8058);  // GL_RGBA8
  context->tex_storage_2d_multisample(texture, webgl_int(argc, argv, 1, 0),
                                      xtgui::webgl_storage_format(internalformat),
                                      webgl_int(argc, argv, 3, 0),
                                      webgl_int(argc, argv, 4, 0));
  return XT_UNDEFINED;
}

METHOD(webgl2_get_internalformat_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  xt_value list = xt_array_new(0, nullptr);
  if (context == nullptr) return list;
  const int pname = webgl_int(argc, argv, 2, 0);
  if (pname != GL_SAMPLES) return list;
  const int internalformat = webgl_int(argc, argv, 1, 0x8058);  // GL_RGBA8
  if (context->max_samples() <= 0 ||
      xtgui::webgl_compressed_format(internalformat) != SDL_GPU_TEXTUREFORMAT_INVALID) {
    return list;
  }
  xt_array_push(list, xt_number((double)context->max_samples()));
  return list;
}

}  // namespace domapi
