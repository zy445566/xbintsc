/*
 * xbintsc GUI engine — WebGL 2 uniform-buffer bindings.
 */

#include <cstdint>
#include <string>

#include "dom_api_internal.h"
#include "gui_internal.h"
#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {

METHOD(webgl2_bind_buffer_base) {
  (void)env;
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context == nullptr) return XT_UNDEFINED;
  context->bind_buffer_base(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                            webgl_object_id(xt_arg(argc, argv, 2), "__xt_gl_buffer"), 0, 0);
  return XT_UNDEFINED;
}

METHOD(webgl2_bind_buffer_range) {
  (void)env;
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context == nullptr) return XT_UNDEFINED;
  context->bind_buffer_base(
      webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
      webgl_object_id(xt_arg(argc, argv, 2), "__xt_gl_buffer"),
      (uint32_t)webgl_number(argc, argv, 3, 0.0), (uint32_t)webgl_number(argc, argv, 4, 0.0));
  return XT_UNDEFINED;
}

METHOD(webgl2_uniform_block_binding) {
  (void)env;
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context != nullptr) {
    context->uniform_block_binding(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"),
                                   (uint32_t)webgl_number(argc, argv, 1, 0.0),
                                   webgl_int(argc, argv, 2, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_get_active_uniform_block_parameter) {
  (void)env;
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context == nullptr) return xt_number(0);
  return xt_number(context->uniform_block_parameter(
      webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"),
      (uint32_t)webgl_number(argc, argv, 1, 0.0), webgl_int(argc, argv, 2, 0)));
}

METHOD(webgl2_get_active_uniform_block_name) {
  (void)env;
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context == nullptr) return XT_NULL;
  const std::string name = context->uniform_block_name(
      webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"),
      (uint32_t)webgl_number(argc, argv, 1, 0.0));
  return name.empty() ? XT_NULL : xt_string_from_cstr(name.c_str());
}

METHOD(webgl2_get_indexed_parameter) {
  (void)env;
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context == nullptr) return XT_NULL;
  const int target = webgl_int(argc, argv, 0, 0);
  const int index = webgl_int(argc, argv, 1, 0);
  if (target == GL_UNIFORM_BUFFER) {
    const uint32_t buffer = context->uniform_buffer_binding(index);
    return buffer != 0 ? webgl_make_object(self, "__xt_gl_buffer", buffer, "buffer") : XT_NULL;
  }
  return XT_NULL;
}

}  // namespace domapi

#undef METHOD
