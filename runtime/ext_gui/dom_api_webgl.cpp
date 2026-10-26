/*
 * xbintsc GUI engine — the WebGL context object and its value helpers.
 *
 * `canvas.getContext("webgl")` creates an `xtgui::WebGLContext` (see
 * `webgl.h`) and hands the page a handle whose prototype carries the WebGL 1.0
 * method table and every GL enum constant. Shader/program/buffer/texture
 * handles are plain objects holding an integer id, so a deleted object becomes
 * a no-op instead of a dangling pointer.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "webgl.h"
#include "dom_api_webgl_rt.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

/* The runtime's typed-array accessors are declared in `rt_internal.h`, which is
 * a C-only header; `dom_api_webgl_rt.h` re-declares the handful used here with
 * C linkage. */

namespace domapi {

xt_value g_webgl_proto = XT_UNDEFINED;
xt_value g_webgl_buffer_proto = XT_UNDEFINED;
xt_value g_webgl_texture_proto = XT_UNDEFINED;
xt_value g_webgl_shader_proto = XT_UNDEFINED;
xt_value g_webgl_program_proto = XT_UNDEFINED;
xt_value g_webgl_framebuffer_proto = XT_UNDEFINED;
xt_value g_webgl_renderbuffer_proto = XT_UNDEFINED;
xt_value g_webgl_uniform_proto = XT_UNDEFINED;
xt_value g_webgl_info_proto = XT_UNDEFINED;

namespace {

struct MethodEntry {
  const char *name;
  void *fn;
};

const MethodEntry kMethods[] = {
    {"enable", (void *)webgl_enable},
    {"disable", (void *)webgl_disable},
    {"isEnabled", (void *)webgl_is_enabled},
    {"clear", (void *)webgl_clear},
    {"clearColor", (void *)webgl_clear_color},
    {"clearDepth", (void *)webgl_clear_depth},
    {"clearStencil", (void *)webgl_clear_stencil},
    {"colorMask", (void *)webgl_color_mask},
    {"depthFunc", (void *)webgl_depth_func},
    {"depthMask", (void *)webgl_depth_mask},
    {"depthRange", (void *)webgl_depth_range},
    {"blendColor", (void *)webgl_blend_color},
    {"blendEquation", (void *)webgl_blend_equation},
    {"blendEquationSeparate", (void *)webgl_blend_equation_separate},
    {"blendFunc", (void *)webgl_blend_func},
    {"blendFuncSeparate", (void *)webgl_blend_func_separate},
    {"cullFace", (void *)webgl_cull_face},
    {"frontFace", (void *)webgl_front_face},
    {"viewport", (void *)webgl_viewport},
    {"scissor", (void *)webgl_scissor},
    {"finish", (void *)webgl_finish},
    {"flush", (void *)webgl_flush},
    {"hint", (void *)webgl_hint},
    {"lineWidth", (void *)webgl_line_width},
    {"polygonOffset", (void *)webgl_polygon_offset},
    {"sampleCoverage", (void *)webgl_sample_coverage},
    {"pixelStorei", (void *)webgl_pixel_storei},
    {"getError", (void *)webgl_get_error},
    {"getParameter", (void *)webgl_get_parameter},
    {"getSupportedExtensions", (void *)webgl_get_supported_extensions},
    {"getExtension", (void *)webgl_get_extension},
    {"getContextAttributes", (void *)webgl_get_context_attributes},
    {"isContextLost", (void *)webgl_is_context_lost},
    {"stencilFunc", (void *)webgl_stencil_func},
    {"stencilFuncSeparate", (void *)webgl_stencil_func_separate},
    {"stencilOp", (void *)webgl_stencil_op},
    {"stencilOpSeparate", (void *)webgl_stencil_op_separate},
    {"stencilMask", (void *)webgl_stencil_mask},
    {"stencilMaskSeparate", (void *)webgl_stencil_mask_separate},
    {"getShaderPrecisionFormat", (void *)webgl_get_shader_precision_format},
    {"createShader", (void *)webgl_create_shader},
    {"deleteShader", (void *)webgl_delete_shader},
    {"shaderSource", (void *)webgl_shader_source},
    {"compileShader", (void *)webgl_compile_shader},
    {"getShaderParameter", (void *)webgl_get_shader_parameter},
    {"getShaderInfoLog", (void *)webgl_get_shader_info_log},
    {"getShaderSource", (void *)webgl_get_shader_source},
    {"createProgram", (void *)webgl_create_program},
    {"deleteProgram", (void *)webgl_delete_program},
    {"attachShader", (void *)webgl_attach_shader},
    {"detachShader", (void *)webgl_detach_shader},
    {"linkProgram", (void *)webgl_link_program},
    {"useProgram", (void *)webgl_use_program},
    {"getProgramParameter", (void *)webgl_get_program_parameter},
    {"getProgramInfoLog", (void *)webgl_get_program_info_log},
    {"getAttachedShaders", (void *)webgl_get_attached_shaders},
    {"validateProgram", (void *)webgl_validate_program},
    {"getActiveUniform", (void *)webgl_get_active_uniform},
    {"getActiveAttrib", (void *)webgl_get_active_attrib},
    {"getUniformLocation", (void *)webgl_get_uniform_location},
    {"getAttribLocation", (void *)webgl_get_attrib_location},
    {"bindAttribLocation", (void *)webgl_bind_attrib_location},
    {"getUniform", (void *)webgl_get_uniform},
    {"uniform1f", (void *)webgl_uniform1f},
    {"uniform2f", (void *)webgl_uniform2f},
    {"uniform3f", (void *)webgl_uniform3f},
    {"uniform4f", (void *)webgl_uniform4f},
    {"uniform1i", (void *)webgl_uniform1i},
    {"uniform2i", (void *)webgl_uniform2i},
    {"uniform3i", (void *)webgl_uniform3i},
    {"uniform4i", (void *)webgl_uniform4i},
    {"uniform1fv", (void *)webgl_uniform1fv},
    {"uniform2fv", (void *)webgl_uniform2fv},
    {"uniform3fv", (void *)webgl_uniform3fv},
    {"uniform4fv", (void *)webgl_uniform4fv},
    {"uniform1iv", (void *)webgl_uniform1iv},
    {"uniform2iv", (void *)webgl_uniform2iv},
    {"uniform3iv", (void *)webgl_uniform3iv},
    {"uniform4iv", (void *)webgl_uniform4iv},
    {"uniformMatrix2fv", (void *)webgl_uniform_matrix2fv},
    {"uniformMatrix3fv", (void *)webgl_uniform_matrix3fv},
    {"uniformMatrix4fv", (void *)webgl_uniform_matrix4fv},
    {"createBuffer", (void *)webgl_create_buffer},
    {"deleteBuffer", (void *)webgl_delete_buffer},
    {"bindBuffer", (void *)webgl_bind_buffer},
    {"bufferData", (void *)webgl_buffer_data},
    {"bufferSubData", (void *)webgl_buffer_sub_data},
    {"getBufferParameter", (void *)webgl_get_buffer_parameter},
    {"enableVertexAttribArray", (void *)webgl_enable_vertex_attrib_array},
    {"disableVertexAttribArray", (void *)webgl_disable_vertex_attrib_array},
    {"vertexAttribPointer", (void *)webgl_vertex_attrib_pointer},
    {"vertexAttrib1f", (void *)webgl_vertex_attrib1f},
    {"vertexAttrib2f", (void *)webgl_vertex_attrib2f},
    {"vertexAttrib3f", (void *)webgl_vertex_attrib3f},
    {"vertexAttrib4f", (void *)webgl_vertex_attrib4f},
    {"vertexAttrib1fv", (void *)webgl_vertex_attrib1fv},
    {"vertexAttrib2fv", (void *)webgl_vertex_attrib2fv},
    {"vertexAttrib3fv", (void *)webgl_vertex_attrib3fv},
    {"vertexAttrib4fv", (void *)webgl_vertex_attrib4fv},
    {"getVertexAttrib", (void *)webgl_get_vertex_attrib},
    {"drawArrays", (void *)webgl_draw_arrays},
    {"drawElements", (void *)webgl_draw_elements},
    {"createTexture", (void *)webgl_create_texture},
    {"deleteTexture", (void *)webgl_delete_texture},
    {"bindTexture", (void *)webgl_bind_texture},
    {"activeTexture", (void *)webgl_active_texture},
    {"texParameteri", (void *)webgl_tex_parameteri},
    {"texParameterf", (void *)webgl_tex_parameterf},
    {"getTexParameter", (void *)webgl_get_tex_parameter},
    {"texImage2D", (void *)webgl_tex_image_2d},
    {"texSubImage2D", (void *)webgl_tex_sub_image_2d},
    {"generateMipmap", (void *)webgl_generate_mipmap},
    {"createFramebuffer", (void *)webgl_create_framebuffer},
    {"deleteFramebuffer", (void *)webgl_delete_framebuffer},
    {"bindFramebuffer", (void *)webgl_bind_framebuffer},
    {"framebufferTexture2D", (void *)webgl_framebuffer_texture_2d},
    {"checkFramebufferStatus", (void *)webgl_check_framebuffer_status},
    {"createRenderbuffer", (void *)webgl_create_renderbuffer},
    {"deleteRenderbuffer", (void *)webgl_delete_renderbuffer},
    {"bindRenderbuffer", (void *)webgl_bind_renderbuffer},
    {"renderbufferStorage", (void *)webgl_renderbuffer_storage},
    {"framebufferRenderbuffer", (void *)webgl_framebuffer_renderbuffer},
    {"getRenderbufferParameter", (void *)webgl_get_renderbuffer_parameter},
    {"readPixels", (void *)webgl_read_pixels},
};

void define_method(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

void define_getter(xt_value proto, const char *name, void *fn) {
  xt_object_define_getter(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

xt_value *proto_root(const std::string &kind) {
  if (kind == "buffer") return &g_webgl_buffer_proto;
  if (kind == "texture") return &g_webgl_texture_proto;
  if (kind == "shader") return &g_webgl_shader_proto;
  if (kind == "program") return &g_webgl_program_proto;
  if (kind == "framebuffer") return &g_webgl_framebuffer_proto;
  if (kind == "renderbuffer") return &g_webgl_renderbuffer_proto;
  if (kind == "uniform") return &g_webgl_uniform_proto;
  return &g_webgl_info_proto;
}

}  // namespace

xt_value webgl_object_proto(const std::string &kind) {
  xt_value *slot = proto_root(kind);
  xt_gc_add_root(slot);
  if (XT_IS_OBJECT(*slot)) return *slot;
  xt_value proto = xt_object_new();
  *slot = proto;
  return proto;
}

xt_value webgl_proto() {
  xt_gc_add_root(&g_webgl_proto);
  if (XT_IS_OBJECT(g_webgl_proto)) return g_webgl_proto;
  xt_value proto = xt_object_new();
#define XT_WEBGL_CONSTANT(name, value) \
  xt_object_set(proto, xt_string_from_cstr(#name), xt_number((double)(value)));
  XT_WEBGL_CONSTANTS(XT_WEBGL_CONSTANT)
#undef XT_WEBGL_CONSTANT
  for (const MethodEntry &method : kMethods) define_method(proto, method.name, method.fn);
  define_getter(proto, "canvas", (void *)webgl_canvas_get);
  define_getter(proto, "drawingBufferWidth", (void *)webgl_drawing_buffer_width_get);
  define_getter(proto, "drawingBufferHeight", (void *)webgl_drawing_buffer_height_get);
  g_webgl_proto = proto;
  return proto;
}

/* -- handles and context lookup ------------------------------------------ */

uint64_t webgl_context_id(xt_value self) {
  xt_value value = xt_object_get_cstr(self, "__xt_gl_context");
  if (!XT_IS_NUMBER(value)) return 0;
  return (uint64_t)xt_to_number(value);
}

xtgui::WebGLContext *webgl_from_context(xt_value self) {
  const uint64_t id = webgl_context_id(self);
  return id != 0 ? xtgui::xt_webgl_by_id(id) : nullptr;
}

xt_value webgl_context_handle(xt_value element, xtgui::WebGLContext *context) {
  xt_value cached = xt_object_get_cstr(element, "__xt_gui_gl");
  if (XT_IS_OBJECT(cached)) return cached;
  if (context == nullptr) return XT_NULL;
  xt_value handle = xt_object_new_with_proto(webgl_proto());
  xt_object_set(handle, xt_string_from_cstr("__xt_gl_context"),
                xt_number((double)context->id()));
  xt_object_set(handle, xt_string_from_cstr("__xt_gl_node"), element);
  xt_object_set(element, xt_string_from_cstr("__xt_gui_gl"), handle);
  return handle;
}

uint32_t webgl_object_id(xt_value handle, const char *key) {
  if (!XT_IS_OBJECT(handle)) return 0;
  xt_value value = xt_object_get_cstr(handle, key);
  if (!XT_IS_NUMBER(value)) return 0;
  return (uint32_t)xt_to_number(value);
}

xt_value webgl_make_object(xt_value context, const char *key, uint32_t id,
                           const std::string &kind) {
  if (id == 0) return XT_NULL;
  xt_value handle = xt_object_new_with_proto(webgl_object_proto(kind));
  xt_object_set(handle, xt_string_from_cstr("__xt_gl_context"),
                xt_object_get_cstr(context, "__xt_gl_context"));
  xt_object_set(handle, xt_string_from_cstr(key), xt_number((double)id));
  xt_object_set(handle, xt_string_from_cstr("__xt_gl_owner"), context);
  return handle;
}

/* -- argument helpers ---------------------------------------------------- */

double webgl_number(int32_t argc, xt_value *argv, int32_t index, double fallback) {
  if (index >= argc) return fallback;
  return xt_to_number(xt_arg(argc, argv, index));
}

int webgl_int(int32_t argc, xt_value *argv, int32_t index, int fallback) {
  return (int)webgl_number(argc, argv, index, (double)fallback);
}

std::string webgl_string(int32_t argc, xt_value *argv, int32_t index) {
  if (index >= argc) return std::string();
  return to_string_value(xt_arg(argc, argv, index));
}

std::vector<double> webgl_numbers(xt_value value) {
  std::vector<double> out;
  if (!XT_IS_OBJECT(value)) return out;
  const int kind = xt_typed_array_kind_of(value);
  const double length = xt_to_number(xt_get(value, xt_string_from_cstr("length")));
  if (!(length > 0.0) || length > 100000000.0) return out;
  out.reserve((size_t)length);
  for (int i = 0; i < (int)length; i++) {
    if (kind >= 0) {
      out.push_back(xt_to_number(xt_typed_array_read(value, (uint32_t)i)));
    } else {
      out.push_back(xt_to_number(xt_get(value, xt_number((double)i))));
    }
  }
  return out;
}

std::vector<uint8_t> webgl_bytes(xt_value value) {
  std::vector<uint8_t> out;
  if (!XT_IS_OBJECT(value)) return out;
  const int kind = xt_typed_array_kind_of(value);
  if (kind >= 0) {
    xt_value buffer = xt_typed_array_buffer_of(value);
    const uint32_t offset = xt_typed_array_byte_offset_of(value);
    const double length = xt_to_number(xt_get(value, xt_string_from_cstr("byteLength")));
    if (!(length > 0.0) || !xt_is_array_buffer(buffer)) return out;
    out.resize((size_t)length);
    for (uint32_t i = 0; i < (uint32_t)length; i++) {
      out[i] = xt_array_buffer_get_byte(buffer, offset + i);
    }
    return out;
  }
  if (xt_is_array_buffer(value)) {
    const uint32_t length = xt_array_buffer_byte_length(value);
    out.resize(length);
    for (uint32_t i = 0; i < length; i++) out[i] = xt_array_buffer_get_byte(value, i);
    return out;
  }
  const std::vector<double> numbers = webgl_numbers(value);
  out.resize(numbers.size());
  for (size_t i = 0; i < numbers.size(); i++) {
    double number = numbers[i];
    if (!(number > 0.0)) number = 0.0;
    if (number > 255.0) number = 255.0;
    out[i] = (uint8_t)number;
  }
  return out;
}

void webgl_write_bytes(xt_value target, const uint8_t *bytes, size_t count) {
  if (!XT_IS_OBJECT(target) || bytes == nullptr) return;
  const int kind = xt_typed_array_kind_of(target);
  for (size_t i = 0; i < count; i++) {
    if (kind >= 0) {
      xt_typed_array_write(target, (uint32_t)i, xt_number((double)bytes[i]));
    } else {
      xt_set(target, xt_number((double)i), xt_number((double)bytes[i]));
    }
  }
}

/* -- context handle members ---------------------------------------------- */

METHOD(webgl_canvas_get) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_gl_node");
}

METHOD(webgl_drawing_buffer_width_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = webgl_from_context(self);
  return xt_number(context != nullptr ? context->width() : 0);
}

METHOD(webgl_drawing_buffer_height_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = webgl_from_context(self);
  return xt_number(context != nullptr ? context->height() : 0);
}

}  // namespace domapi
