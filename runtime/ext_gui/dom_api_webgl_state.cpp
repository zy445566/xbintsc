/*
 * xbintsc GUI engine — WebGL fixed-function state and queries.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_live_context(self); }

/** Fire a `webglcontextlost`/`webglcontextrestored` event on the canvas that
 * owns the context. `self` is the context or extension handle. */
void dispatch_context_event(xt_value self, const char *type) {
  XtGuiWindow *win = nullptr;
  xtgui::Node *node =
      resolve_node(xt_object_get_cstr(self, "__xt_gl_node"), &win);
  if (win != nullptr && node != nullptr) xt_gui_dispatch_to_node(win, node, type);
}

xt_value number_array(const double *values, int count) {
  xt_value array = xt_array_new(0, nullptr);
  for (int i = 0; i < count; i++) xt_array_push(array, xt_number(values[i]));
  return array;
}

xt_value object_for(xt_value self, uint32_t id, const char *key, const char *kind) {
  return webgl_make_object(self, key, id, kind);
}

}  // namespace

METHOD(webgl_lose_context);
METHOD(webgl_restore_context);

void webgl_unpack_flags(xt_value context, bool *flip_y, bool *premultiply) {
  *flip_y = value_truthy(xt_object_get_cstr(context, "__xt_unpack_flip_y"));
  *premultiply = value_truthy(xt_object_get_cstr(context, "__xt_unpack_premultiply"));
}

METHOD(webgl_enable) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->enable(webgl_int(argc, argv, 0, 0));
  return XT_UNDEFINED;
}

METHOD(webgl_disable) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->disable(webgl_int(argc, argv, 0, 0));
  return XT_UNDEFINED;
}

METHOD(webgl_is_enabled) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  return xt_bool(context != nullptr && context->is_enabled(webgl_int(argc, argv, 0, 0)));
}

METHOD(webgl_clear) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->clear(webgl_int(argc, argv, 0, 0));
  return XT_UNDEFINED;
}

METHOD(webgl_clear_color) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->clear_color((float)webgl_number(argc, argv, 0, 0.0),
                         (float)webgl_number(argc, argv, 1, 0.0),
                         (float)webgl_number(argc, argv, 2, 0.0),
                         (float)webgl_number(argc, argv, 3, 0.0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_clear_depth) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->clear_depth((float)webgl_number(argc, argv, 0, 1.0));
  return XT_UNDEFINED;
}

METHOD(webgl_clear_stencil) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->clear_stencil(webgl_int(argc, argv, 0, 0));
  return XT_UNDEFINED;
}

METHOD(webgl_color_mask) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->color_mask(value_truthy(xt_arg(argc, argv, 0)),
                        value_truthy(xt_arg(argc, argv, 1)),
                        value_truthy(xt_arg(argc, argv, 2)),
                        value_truthy(xt_arg(argc, argv, 3)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_depth_func) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->depth_func(webgl_int(argc, argv, 0, GL_LESS));
  return XT_UNDEFINED;
}

METHOD(webgl_depth_mask) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->depth_mask(value_truthy(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl_depth_range) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl_blend_color) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->blend_color((float)webgl_number(argc, argv, 0, 0.0),
                         (float)webgl_number(argc, argv, 1, 0.0),
                         (float)webgl_number(argc, argv, 2, 0.0),
                         (float)webgl_number(argc, argv, 3, 0.0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_blend_equation) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->blend_equation(webgl_int(argc, argv, 0, GL_FUNC_ADD));
  return XT_UNDEFINED;
}

METHOD(webgl_blend_equation_separate) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->blend_equation_separate(webgl_int(argc, argv, 0, GL_FUNC_ADD),
                                     webgl_int(argc, argv, 1, GL_FUNC_ADD));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_blend_func) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->blend_func(webgl_int(argc, argv, 0, GL_ONE), webgl_int(argc, argv, 1, GL_ZERO));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_blend_func_separate) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->blend_func_separate(webgl_int(argc, argv, 0, GL_ONE),
                                 webgl_int(argc, argv, 1, GL_ZERO),
                                 webgl_int(argc, argv, 2, GL_ONE),
                                 webgl_int(argc, argv, 3, GL_ZERO));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_cull_face) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->cull_face(webgl_int(argc, argv, 0, GL_BACK));
  return XT_UNDEFINED;
}

METHOD(webgl_front_face) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->front_face(webgl_int(argc, argv, 0, GL_CCW));
  return XT_UNDEFINED;
}

METHOD(webgl_viewport) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->viewport(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                      webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_scissor) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->scissor(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                     webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_finish) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->finish();
  return XT_UNDEFINED;
}

METHOD(webgl_flush) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl_hint) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl_line_width) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl_polygon_offset) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl_sample_coverage) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl_pixel_storei) {
  (void)env;
  const int pname = webgl_int(argc, argv, 0, 0);
  const bool value = webgl_int(argc, argv, 1, 0) != 0;
  if (pname == 0x9240) {  // UNPACK_FLIP_Y_WEBGL
    xt_object_set(self, xt_string_from_cstr("__xt_unpack_flip_y"), xt_bool(value));
  } else if (pname == 0x9241) {  // UNPACK_PREMULTIPLY_ALPHA_WEBGL
    xt_object_set(self, xt_string_from_cstr("__xt_unpack_premultiply"), xt_bool(value));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_get_error) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context != nullptr && context->lost()) return xt_number(GL_CONTEXT_LOST_WEBGL);
  return xt_number(context != nullptr ? context->error() : GL_NO_ERROR);
}

METHOD(webgl_get_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  const int pname = webgl_int(argc, argv, 0, 0);
  if (context == nullptr) return XT_NULL;
  switch (pname) {
    case GL_VIEWPORT: {
      int box[4] = {0, 0, 0, 0};
      context->viewport_box(box);
      const double values[4] = {(double)box[0], (double)box[1], (double)box[2], (double)box[3]};
      return number_array(values, 4);
    }
    case GL_SCISSOR_BOX: {
      int box[4] = {0, 0, 0, 0};
      context->scissor_box(box);
      const double values[4] = {(double)box[0], (double)box[1], (double)box[2], (double)box[3]};
      return number_array(values, 4);
    }
    case GL_STENCIL_FUNC:
      return xt_number(context->stencil_face(GL_FRONT).func);
    case GL_STENCIL_REF:
      return xt_number(context->stencil_face(GL_FRONT).ref);
    case GL_STENCIL_VALUE_MASK:
      return xt_number((double)context->stencil_face(GL_FRONT).value_mask);
    case GL_STENCIL_WRITEMASK:
      return xt_number((double)context->stencil_face(GL_FRONT).write_mask);
    case GL_STENCIL_FAIL:
      return xt_number(context->stencil_face(GL_FRONT).fail);
    case GL_STENCIL_PASS_DEPTH_FAIL:
      return xt_number(context->stencil_face(GL_FRONT).zfail);
    case GL_STENCIL_PASS_DEPTH_PASS:
      return xt_number(context->stencil_face(GL_FRONT).zpass);
    case GL_STENCIL_BACK_FUNC:
      return xt_number(context->stencil_face(GL_BACK).func);
    case GL_STENCIL_BACK_REF:
      return xt_number(context->stencil_face(GL_BACK).ref);
    case GL_STENCIL_BACK_VALUE_MASK:
      return xt_number((double)context->stencil_face(GL_BACK).value_mask);
    case GL_STENCIL_BACK_WRITEMASK:
      return xt_number((double)context->stencil_face(GL_BACK).write_mask);
    case GL_STENCIL_BACK_FAIL:
      return xt_number(context->stencil_face(GL_BACK).fail);
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
      return xt_number(context->stencil_face(GL_BACK).zfail);
    case GL_STENCIL_BACK_PASS_DEPTH_PASS:
      return xt_number(context->stencil_face(GL_BACK).zpass);
    case GL_COLOR_CLEAR_VALUE: {
      const double values[4] = {0, 0, 0, 0};
      return number_array(values, 4);
    }
    case GL_DEPTH_RANGE: {
      const double values[2] = {0, 1};
      return number_array(values, 2);
    }
    case GL_BLEND_COLOR: {
      float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      context->blend_color_value(values);
      const double out[4] = {values[0], values[1], values[2], values[3]};
      return number_array(out, 4);
    }
    case GL_ALIASED_LINE_WIDTH_RANGE:
    case GL_ALIASED_POINT_SIZE_RANGE: {
      const double values[2] = {1, 1};
      return number_array(values, 2);
    }
    case GL_MAX_VIEWPORT_DIMS: {
      const double values[2] = {8192, 8192};
      return number_array(values, 2);
    }
    case GL_ARRAY_BUFFER_BINDING:
      return object_for(self, context->bound_buffer(GL_ARRAY_BUFFER), "__xt_gl_buffer", "buffer");
    case GL_ELEMENT_ARRAY_BUFFER_BINDING:
      return object_for(self, context->bound_buffer(GL_ELEMENT_ARRAY_BUFFER), "__xt_gl_buffer",
                        "buffer");
    case GL_CURRENT_PROGRAM: {
      const uint32_t program = context->current_program_id();
      return program != 0
                 ? webgl_make_object(self, "__xt_gl_program", program, "program")
                 : XT_NULL;
    }
    case GL_TEXTURE_BINDING_2D: {
      const uint32_t texture = context->bound_texture(0);
      return texture != 0
                 ? webgl_make_object(self, "__xt_gl_texture", texture, "texture")
                 : XT_NULL;
    }
    case GL_FRAMEBUFFER_BINDING: {
      const uint32_t framebuffer = context->bound_framebuffer_id();
      return framebuffer != 0
                 ? webgl_make_object(self, "__xt_gl_framebuffer", framebuffer, "framebuffer")
                 : XT_NULL;
    }
    case GL_READ_FRAMEBUFFER_BINDING: {
      const uint32_t framebuffer = context->bound_read_framebuffer_id();
      return framebuffer != 0
                 ? webgl_make_object(self, "__xt_gl_framebuffer", framebuffer, "framebuffer")
                 : XT_NULL;
    }
    case GL_RENDERBUFFER_BINDING: {
      const uint32_t renderbuffer = context->bound_renderbuffer_id();
      return renderbuffer != 0
                 ? webgl_make_object(self, "__xt_gl_renderbuffer", renderbuffer, "renderbuffer")
                 : XT_NULL;
    }
    case GL_VENDOR:
    case GL_RENDERER:
    case GL_VERSION:
    case GL_SHADING_LANGUAGE_VERSION:
      return xt_string_from_cstr(context->string_parameter(pname).c_str());
    case GL_UNPACK_FLIP_Y_WEBGL:
      return xt_bool(value_truthy(xt_object_get_cstr(self, "__xt_unpack_flip_y")));
    case GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL:
      return xt_bool(value_truthy(xt_object_get_cstr(self, "__xt_unpack_premultiply")));
    default:
      return xt_number(context->parameter(pname));
  }
}

METHOD(webgl_get_supported_extensions) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value list = xt_array_new(0, nullptr);
  xt_array_push(list, xt_string_from_cstr("OES_element_index_uint"));
  xt_array_push(list, xt_string_from_cstr("OES_standard_derivatives"));
  xt_array_push(list, xt_string_from_cstr("OES_texture_float"));
  xt_array_push(list, xt_string_from_cstr("OES_texture_half_float"));
  xt_array_push(list, xt_string_from_cstr("WEBGL_depth_texture"));
  xt_array_push(list, xt_string_from_cstr("EXT_color_buffer_float"));
  xt_array_push(list, xt_string_from_cstr("EXT_color_buffer_half_float"));
  xt_array_push(list, xt_string_from_cstr("WEBGL_lose_context"));
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context != nullptr && context->supports_texture_format(0x83F1)) {
    xt_array_push(list, xt_string_from_cstr("WEBGL_compressed_texture_s3tc"));
  }
  if (context != nullptr && context->supports_texture_format(0x8C4D)) {
    xt_array_push(list, xt_string_from_cstr("WEBGL_compressed_texture_s3tc_srgb"));
  }
  if (context != nullptr && context->supports_texture_format(0x93B0)) {
    xt_array_push(list, xt_string_from_cstr("WEBGL_compressed_texture_astc"));
  }
  return list;
}

METHOD(webgl_get_extension) {
  (void)env;
  const std::string name = webgl_string(argc, argv, 0);
  if (name.rfind("WEBGL_compressed_texture_", 0) == 0) {
    xt_value extension = webgl_compressed_extension(name, webgl_from_context(self));
    if (!XT_IS_UNDEFINED(extension)) return extension;
  }
  if (name == "OES_element_index_uint" || name == "OES_standard_derivatives" ||
      name == "OES_texture_float" || name == "OES_texture_half_float" ||
      name == "WEBGL_depth_texture" || name == "EXT_color_buffer_float" ||
      name == "EXT_color_buffer_half_float") {
    return xt_object_new();
  }
  if (name == "WEBGL_lose_context") {
    xt_value object = xt_object_new();
    /* The closures receive this object as `this`, so carry the context id and
     * the canvas handle over from the context that produced the extension. */
    xt_object_set(object, xt_string_from_cstr("__xt_gl_context"),
                  xt_object_get_cstr(self, "__xt_gl_context"));
    xt_object_set(object, xt_string_from_cstr("__xt_gl_node"),
                  xt_object_get_cstr(self, "__xt_gl_node"));
    xt_object_set(object, xt_string_from_cstr("loseContext"),
                  xt_closure_new((void *)webgl_lose_context, 0, nullptr));
    xt_object_set(object, xt_string_from_cstr("restoreContext"),
                  xt_closure_new((void *)webgl_restore_context, 0, nullptr));
    return object;
  }
  return XT_NULL;
}

METHOD(webgl_get_context_attributes) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value attributes = xt_object_new();
  xt_object_set(attributes, xt_string_from_cstr("alpha"), XT_TRUE);
  xt_object_set(attributes, xt_string_from_cstr("depth"), XT_TRUE);
  xt_object_set(attributes, xt_string_from_cstr("stencil"), XT_TRUE);
  xt_object_set(attributes, xt_string_from_cstr("antialias"), XT_FALSE);
  xt_object_set(attributes, xt_string_from_cstr("premultipliedAlpha"), XT_TRUE);
  xt_object_set(attributes, xt_string_from_cstr("preserveDrawingBuffer"), XT_TRUE);
  xt_object_set(attributes, xt_string_from_cstr("desynchronized"), XT_FALSE);
  xt_object_set(attributes, xt_string_from_cstr("powerPreference"),
                xt_string_from_cstr("default"));
  return attributes;
}

METHOD(webgl_is_context_lost) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = webgl_from_context(self);
  return xt_bool(context == nullptr || context->lost());
}

/* `WEBGL_lose_context.loseContext()` / `restoreContext()`: the simulator drops
 * every object and reports `CONTEXT_LOST_WEBGL` until the restore, so a page
 * exercises the same loss/restore path it would take on a real GPU reset. */
METHOD(webgl_lose_context) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context == nullptr || context->lost()) return XT_UNDEFINED;
  context->lose_context();
  dispatch_context_event(self, "webglcontextlost");
  return XT_UNDEFINED;
}

METHOD(webgl_restore_context) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context == nullptr || !context->lost()) return XT_UNDEFINED;
  context->restore_context();
  if (!context->lost()) dispatch_context_event(self, "webglcontextrestored");
  return XT_UNDEFINED;
}

/* Stencil test/update state. SDL_GPU has one stencil reference per draw and no
 * per-pipeline compare/write masks; the entry points store WebGL's state, the
 * pipeline carries the compare function and ops, and `SDL_SetGPUStencilReference`
 * supplies the reference. A write mask of 0 is emulated as KEEP. */
METHOD(webgl_stencil_func) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->stencil_func(webgl_int(argc, argv, 0, GL_ALWAYS), webgl_int(argc, argv, 1, 0),
                          (uint32_t)webgl_number(argc, argv, 2, 0xFFFFFFFF));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_stencil_func_separate) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    const int face = webgl_int(argc, argv, 0, GL_FRONT_AND_BACK);
    context->stencil_func_separate(face, webgl_int(argc, argv, 1, GL_ALWAYS),
                                   webgl_int(argc, argv, 2, 0),
                                   (uint32_t)webgl_number(argc, argv, 3, 0xFFFFFFFF));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_stencil_op) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->stencil_op(webgl_int(argc, argv, 0, GL_KEEP), webgl_int(argc, argv, 1, GL_KEEP),
                        webgl_int(argc, argv, 2, GL_KEEP));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_stencil_op_separate) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    const int face = webgl_int(argc, argv, 0, GL_FRONT_AND_BACK);
    context->stencil_op_separate(face, webgl_int(argc, argv, 1, GL_KEEP),
                                 webgl_int(argc, argv, 2, GL_KEEP),
                                 webgl_int(argc, argv, 3, GL_KEEP));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_stencil_mask) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->stencil_mask((uint32_t)webgl_number(argc, argv, 0, 0xFFFFFFFF));
  return XT_UNDEFINED;
}

METHOD(webgl_stencil_mask_separate) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->stencil_mask_separate(webgl_int(argc, argv, 0, GL_FRONT_AND_BACK),
                                   (uint32_t)webgl_number(argc, argv, 1, 0xFFFFFFFF));
  }
  return XT_UNDEFINED;
}

}  // namespace domapi
