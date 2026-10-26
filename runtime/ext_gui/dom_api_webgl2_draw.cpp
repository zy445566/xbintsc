/*
 * xbintsc GUI engine — WebGL 2 draw-side bindings.
 *
 * Vertex-array objects, instanced draws, integer attributes, unsigned integer
 * uniforms and sampler objects. The heavy lifting lives in `webgl_draw.cpp` /
 * `webgl_webgl2.cpp`; these are the DOM-facing wrappers.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_live_context(self); }

uint32_t vertex_array_argument(xt_value value) {
  return webgl_object_id(value, "__xt_gl_vertex_array");
}

uint32_t sampler_argument(xt_value value) {
  return webgl_object_id(value, "__xt_gl_sampler");
}

}  // namespace

/* -- vertex arrays ------------------------------------------------------- */

METHOD(webgl2_create_vertex_array) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_vertex_array", context->create_vertex_array(),
                           "vertexarray");
}

METHOD(webgl2_delete_vertex_array) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_vertex_array(vertex_array_argument(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl2_bind_vertex_array) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_vertex_array(vertex_array_argument(xt_arg(argc, argv, 0)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_is_vertex_array) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  return xt_bool(context != nullptr &&
                 context->is_vertex_array(vertex_array_argument(xt_arg(argc, argv, 0))));
}

/* -- integer attributes and instancing ----------------------------------- */

METHOD(webgl2_vertex_attrib_ipointer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->attrib_ipointer(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 4),
                             webgl_int(argc, argv, 2, GL_INT), webgl_int(argc, argv, 3, 0),
                             webgl_int(argc, argv, 4, 0));
  }
  return XT_UNDEFINED;
}

namespace {

void attrib_integer(xt_value self, int32_t argc, xt_value *argv, bool is_unsigned) {
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context == nullptr) return;
  float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  const std::vector<double> numbers = webgl_numbers(xt_arg(argc, argv, 1));
  if (numbers.size() >= 4) {
    for (int i = 0; i < 4; i++) values[i] = (float)numbers[(size_t)i];
  } else {
    for (int i = 0; i < 4; i++) values[i] = (float)webgl_number(argc, argv, i + 1, 0.0);
  }
  (void)is_unsigned;
  context->attrib_value(webgl_int(argc, argv, 0, 0), 4, values);
}

}  // namespace

METHOD(webgl2_vertex_attrib_i4i) {
  attrib_integer(self, argc, argv, false);
  return XT_UNDEFINED;
}

METHOD(webgl2_vertex_attrib_i4iv) {
  attrib_integer(self, argc, argv, false);
  return XT_UNDEFINED;
}

METHOD(webgl2_vertex_attrib_i4ui) {
  attrib_integer(self, argc, argv, true);
  return XT_UNDEFINED;
}

METHOD(webgl2_vertex_attrib_i4uiv) {
  attrib_integer(self, argc, argv, true);
  return XT_UNDEFINED;
}

METHOD(webgl2_vertex_attrib_divisor) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->attrib_divisor(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_draw_arrays_instanced) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->draw_arrays_instanced(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                                   webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 1));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_draw_elements_instanced) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->draw_elements_instanced(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                                     webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 0),
                                     webgl_int(argc, argv, 4, 1));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_draw_range_elements) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->draw_range_elements(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                                 webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 0),
                                 webgl_int(argc, argv, 4, 0), webgl_int(argc, argv, 5, 0));
  }
  return XT_UNDEFINED;
}

/* -- unsigned integer uniforms ------------------------------------------ */

namespace {

void uniform_unsigned(xt_value self, int32_t argc, xt_value *argv, int components, bool vector) {
  xtgui::WebGLContext *context = webgl_live_context(self);
  if (context == nullptr) return;
  xt_value handle = xt_arg(argc, argv, 0);
  if (!XT_IS_OBJECT(handle)) return;
  xt_value program_value = xt_object_get_cstr(handle, "__xt_gl_program");
  xt_value location_value = xt_object_get_cstr(handle, "__xt_gl_location");
  if (!XT_IS_NUMBER(program_value) || !XT_IS_NUMBER(location_value)) return;
  const uint32_t program = (uint32_t)xt_to_number(program_value);
  const int location = (int)xt_to_number(location_value);
  if (vector) {
    const std::vector<double> numbers =
        webgl_numbers(argc > 1 ? xt_arg(argc, argv, 1) : XT_UNDEFINED);
    if (numbers.empty()) return;
    std::vector<int> values(numbers.size());
    for (size_t i = 0; i < numbers.size(); i++) values[i] = (int)(unsigned int)numbers[i];
    context->uniform_ints(program, location, components, values.data(),
                          (int)(numbers.size() / components));
    return;
  }
  int values[4] = {0, 0, 0, 0};
  for (int i = 0; i < components; i++) {
    values[i] = (int)(unsigned int)webgl_number(argc, argv, i + 1, 0.0);
  }
  context->uniform_ints(program, location, components, values, 1);
}

}  // namespace

METHOD(webgl2_uniform1ui) {
  uniform_unsigned(self, argc, argv, 1, false);
  return XT_UNDEFINED;
}

METHOD(webgl2_uniform2ui) {
  uniform_unsigned(self, argc, argv, 2, false);
  return XT_UNDEFINED;
}

METHOD(webgl2_uniform3ui) {
  uniform_unsigned(self, argc, argv, 3, false);
  return XT_UNDEFINED;
}

METHOD(webgl2_uniform4ui) {
  uniform_unsigned(self, argc, argv, 4, false);
  return XT_UNDEFINED;
}

/* -- sampler objects ----------------------------------------------------- */

METHOD(webgl2_create_sampler) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_sampler", context->create_sampler(), "sampler");
}

METHOD(webgl2_delete_sampler) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_sampler(sampler_argument(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl2_bind_sampler) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_sampler(webgl_int(argc, argv, 0, 0), sampler_argument(xt_arg(argc, argv, 1)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_sampler_parameteri) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->sampler_parameter(sampler_argument(xt_arg(argc, argv, 0)),
                               webgl_int(argc, argv, 1, 0), webgl_int(argc, argv, 2, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_sampler_parameterf) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->sampler_parameter(sampler_argument(xt_arg(argc, argv, 0)),
                               webgl_int(argc, argv, 1, 0),
                               (int)webgl_number(argc, argv, 2, 0.0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_get_sampler_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return xt_number(context->sampler_parameter_value(sampler_argument(xt_arg(argc, argv, 0)),
                                                    webgl_int(argc, argv, 1, 0)));
}

}  // namespace domapi
