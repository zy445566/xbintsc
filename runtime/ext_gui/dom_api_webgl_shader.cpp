/*
 * xbintsc GUI engine — WebGL shader, program and uniform bindings.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_from_context(self); }

xt_value shader_argument(xt_value value) {
  return webgl_object_id(value, "__xt_gl_shader");
}

/** Resolve a `WebGLUniformLocation` handle into its program and table index. */
bool location_of(xt_value handle, uint32_t *program, int *location) {
  if (!XT_IS_OBJECT(handle)) return false;
  xt_value program_value = xt_object_get_cstr(handle, "__xt_gl_program");
  xt_value location_value = xt_object_get_cstr(handle, "__xt_gl_location");
  if (!XT_IS_NUMBER(program_value) || !XT_IS_NUMBER(location_value)) return false;
  *program = (uint32_t)xt_to_number(program_value);
  *location = (int)xt_to_number(location_value);
  return true;
}

int attribute_type(const std::string &type) {
  if (type == "float") return GL_FLOAT;
  if (type == "vec2") return GL_FLOAT_VEC2;
  if (type == "vec3") return GL_FLOAT_VEC3;
  if (type == "vec4") return GL_FLOAT_VEC4;
  if (type == "mat2") return GL_FLOAT_MAT2;
  if (type == "mat3") return GL_FLOAT_MAT3;
  if (type == "mat4") return GL_FLOAT_MAT4;
  if (type == "int") return GL_INT;
  if (type == "ivec2") return GL_INT_VEC2;
  if (type == "ivec3") return GL_INT_VEC3;
  if (type == "ivec4") return GL_INT_VEC4;
  return GL_FLOAT;
}

xt_value uniform_values_object(xtgui::WebGLContext *context, uint32_t program, int location) {
  std::vector<double> values;
  if (!context->uniform_values(program, location, &values)) return XT_NULL;
  xt_value array = xt_array_new(0, nullptr);
  for (double value : values) xt_array_push(array, xt_number(value));
  return array;
}

}  // namespace

METHOD(webgl_create_shader) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const uint32_t id = context->create_shader(webgl_int(argc, argv, 0, 0));
  return webgl_make_object(self, "__xt_gl_shader", id, "shader");
}

METHOD(webgl_delete_shader) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_shader(shader_argument(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl_shader_source) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->shader_source(shader_argument(xt_arg(argc, argv, 0)),
                           webgl_string(argc, argv, 1));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_compile_shader) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->compile_shader(shader_argument(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl_get_shader_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const uint32_t shader = shader_argument(xt_arg(argc, argv, 0));
  const int pname = webgl_int(argc, argv, 1, 0);
  xtgui::WebGLShader *record = context->shader(shader);
  if (record == nullptr) return XT_NULL;
  if (pname == GL_COMPILE_STATUS) return xt_bool(context->shader_compiled(shader));
  if (pname == GL_DELETE_STATUS) return XT_FALSE;
  if (pname == GL_SHADER_TYPE) return xt_number(record->type);
  return XT_NULL;
}

METHOD(webgl_get_shader_info_log) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_string_from_cstr("");
  return xt_string_from_cstr(context->shader_info_log(shader_argument(xt_arg(argc, argv, 0))).c_str());
}

METHOD(webgl_get_shader_source) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_string_from_cstr("");
  return xt_string_from_cstr(
      context->shader_source_text(shader_argument(xt_arg(argc, argv, 0))).c_str());
}

METHOD(webgl_create_program) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_program", context->create_program(), "program");
}

METHOD(webgl_delete_program) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->delete_program(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_attach_shader) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->attach_shader(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"),
                           shader_argument(xt_arg(argc, argv, 1)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_detach_shader) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->detach_shader(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"),
                           shader_argument(xt_arg(argc, argv, 1)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_link_program) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->link_program(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_use_program) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->use_program(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_get_program_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const uint32_t program = webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program");
  const int pname = webgl_int(argc, argv, 1, 0);
  if (pname == GL_LINK_STATUS) return xt_bool(context->program_linked(program));
  if (pname == GL_VALIDATE_STATUS) return xt_bool(context->program_linked(program));
  if (pname == GL_DELETE_STATUS) return XT_FALSE;
  if (pname == GL_ATTACHED_SHADERS) {
    return xt_number((double)context->attached_shaders(program).size());
  }
  if (pname == GL_ACTIVE_UNIFORMS) return xt_number((double)context->active_uniform_count(program));
  if (pname == GL_ACTIVE_ATTRIBUTES) return xt_number((double)context->active_attrib_count(program));
  return xt_number(0);
}

METHOD(webgl_get_program_info_log) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_string_from_cstr("");
  return xt_string_from_cstr(
      context->program_info_log(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program")).c_str());
}

METHOD(webgl_get_attached_shaders) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  xt_value result = xt_array_new(0, nullptr);
  if (context == nullptr) return result;
  const uint32_t program = webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program");
  for (uint32_t shader : context->attached_shaders(program)) {
    xt_array_push(result, webgl_make_object(self, "__xt_gl_shader", shader, "shader"));
  }
  return result;
}

METHOD(webgl_validate_program) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl_get_active_uniform) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const uint32_t program = webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program");
  const int index = webgl_int(argc, argv, 1, -1);
  const xtgui::WebGLUniform *uniform = context->active_uniform(program, index);
  if (uniform == nullptr) return XT_NULL;
  xt_value info = xt_object_new_with_proto(webgl_object_proto("info"));
  std::string name = uniform->name;
  if (uniform->layout.is_array) name += "[0]";
  xt_object_set(info, xt_string_from_cstr("name"), xt_string_from_cstr(name.c_str()));
  xt_object_set(info, xt_string_from_cstr("size"),
                xt_number((double)(uniform->layout.is_array ? uniform->layout.array_size : 1)));
  xt_object_set(info, xt_string_from_cstr("type"), xt_number((double)uniform->layout.gl_type));
  return info;
}

METHOD(webgl_get_active_attrib) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const uint32_t program = webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program");
  const int index = webgl_int(argc, argv, 1, -1);
  const xtgui::GlslUniformDecl *attrib = context->active_attrib(program, index);
  if (attrib == nullptr) return XT_NULL;
  xt_value info = xt_object_new_with_proto(webgl_object_proto("info"));
  xt_object_set(info, xt_string_from_cstr("name"), xt_string_from_cstr(attrib->name.c_str()));
  xt_object_set(info, xt_string_from_cstr("size"), xt_number(1));
  xt_object_set(info, xt_string_from_cstr("type"), xt_number(attribute_type(attrib->type)));
  return info;
}

METHOD(webgl_get_uniform_location) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const uint32_t program = webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program");
  const int location = context->uniform_location(program, webgl_string(argc, argv, 1));
  if (location < 0) return XT_NULL;
  xt_value handle = webgl_make_object(self, "__xt_gl_program", program, "uniform");
  xt_object_set(handle, xt_string_from_cstr("__xt_gl_location"), xt_number((double)location));
  return handle;
}

METHOD(webgl_get_attrib_location) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_number(-1);
  const uint32_t program = webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program");
  return xt_number((double)context->attrib_location(program, webgl_string(argc, argv, 1)));
}

METHOD(webgl_bind_attrib_location) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_attrib_location(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program"),
                                  webgl_int(argc, argv, 1, 0), webgl_string(argc, argv, 2));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_get_uniform) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  uint32_t program = 0;
  int location = 0;
  /* `getUniform(program, location)` — the context object is `this`, so the
   * program is the first argument and the location the second. */
  if (!location_of(xt_arg(argc, argv, 1), &program, &location)) return XT_NULL;
  return uniform_values_object(context, program, location);
}

namespace {

void uniform_scalars(xt_value self, int32_t argc, xt_value *argv, int components, bool integer) {
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context == nullptr) return;
  uint32_t program = 0;
  int location = 0;
  if (!location_of(xt_arg(argc, argv, 0), &program, &location)) return;
  if (integer) {
    int values[4] = {0, 0, 0, 0};
    for (int i = 0; i < components; i++) values[i] = webgl_int(argc, argv, i + 1, 0);
    context->uniform_ints(program, location, components, values, 1);
  } else {
    double values[4] = {0, 0, 0, 0};
    for (int i = 0; i < components; i++) values[i] = webgl_number(argc, argv, i + 1, 0.0);
    context->uniform_floats(program, location, components, values, 1);
  }
}

void uniform_vectors(xt_value self, int32_t argc, xt_value *argv, int components, bool integer) {
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context == nullptr) return;
  uint32_t program = 0;
  int location = 0;
  if (!location_of(xt_arg(argc, argv, 0), &program, &location)) return;
  const std::vector<double> numbers =
      webgl_numbers(argc > 1 ? xt_arg(argc, argv, 1) : XT_UNDEFINED);
  if (numbers.empty()) return;
  const int count = (int)(numbers.size() / components);
  if (integer) {
    std::vector<int> values(numbers.size());
    for (size_t i = 0; i < numbers.size(); i++) values[i] = (int)numbers[i];
    context->uniform_ints(program, location, components, values.data(), count);
  } else {
    context->uniform_floats(program, location, components, numbers.data(), count);
  }
}

}  // namespace

METHOD(webgl_uniform1f) {
  uniform_scalars(self, argc, argv, 1, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform2f) {
  uniform_scalars(self, argc, argv, 2, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform3f) {
  uniform_scalars(self, argc, argv, 3, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform4f) {
  uniform_scalars(self, argc, argv, 4, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform1i) {
  uniform_scalars(self, argc, argv, 1, true);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform2i) {
  uniform_scalars(self, argc, argv, 2, true);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform3i) {
  uniform_scalars(self, argc, argv, 3, true);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform4i) {
  uniform_scalars(self, argc, argv, 4, true);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform1fv) {
  uniform_vectors(self, argc, argv, 1, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform2fv) {
  uniform_vectors(self, argc, argv, 2, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform3fv) {
  uniform_vectors(self, argc, argv, 3, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform4fv) {
  uniform_vectors(self, argc, argv, 4, false);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform1iv) {
  uniform_vectors(self, argc, argv, 1, true);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform2iv) {
  uniform_vectors(self, argc, argv, 2, true);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform3iv) {
  uniform_vectors(self, argc, argv, 3, true);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform4iv) {
  uniform_vectors(self, argc, argv, 4, true);
  return XT_UNDEFINED;
}

namespace {

void uniform_matrix(xt_value self, int32_t argc, xt_value *argv, int dimensions) {
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context == nullptr) return;
  uint32_t program = 0;
  int location = 0;
  if (!location_of(xt_arg(argc, argv, 0), &program, &location)) return;
  const std::vector<double> values =
      webgl_numbers(argc > 2 ? xt_arg(argc, argv, 2) : XT_UNDEFINED);
  if (values.empty()) return;
  context->uniform_matrix(program, location, dimensions, values.data());
}

}  // namespace

METHOD(webgl_uniform_matrix2fv) {
  uniform_matrix(self, argc, argv, 2);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform_matrix3fv) {
  uniform_matrix(self, argc, argv, 3);
  return XT_UNDEFINED;
}
METHOD(webgl_uniform_matrix4fv) {
  uniform_matrix(self, argc, argv, 4);
  return XT_UNDEFINED;
}

METHOD(webgl_get_shader_precision_format) {
  (void)env;
  (void)argc;
  (void)argv;
  /* Every stage runs at highp on the Vulkan backend; a positive precision is
   * what capability probes (three.js among them) need to avoid dropping to
   * mediump. */
  xt_value result = xt_object_new();
  xt_object_set(result, xt_string_from_cstr("rangeMin"), xt_number(127));
  xt_object_set(result, xt_string_from_cstr("rangeMax"), xt_number(127));
  xt_object_set(result, xt_string_from_cstr("precision"), xt_number(23));
  return result;
}

}  // namespace domapi
