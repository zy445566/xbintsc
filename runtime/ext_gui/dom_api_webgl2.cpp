/*
 * xbintsc GUI engine — the WebGL 2 context prototype and its less-used API.
 *
 * The prototype inherits the WebGL 1 one, so every WebGL 1 method and constant
 * is available on a WebGL 2 context. This unit adds the WebGL 2 constants and
 * the parts of the API whose backend is either a no-op (queries, fences,
 * transform feedback, uniform blocks) or a simple clear/copy (clearBuffer*,
 * copyBufferSubData, getBufferSubData, texStorage2D).
 */

#include "dom_api_internal.h"

#include <cstring>
#include <string>
#include <vector>

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

struct MethodEntry {
  const char *name;
  void *fn;
};

const MethodEntry kMethods[] = {
    {"createVertexArray", (void *)webgl2_create_vertex_array},
    {"deleteVertexArray", (void *)webgl2_delete_vertex_array},
    {"bindVertexArray", (void *)webgl2_bind_vertex_array},
    {"isVertexArray", (void *)webgl2_is_vertex_array},
    {"vertexAttribIPointer", (void *)webgl2_vertex_attrib_ipointer},
    {"vertexAttribI4i", (void *)webgl2_vertex_attrib_i4i},
    {"vertexAttribI4iv", (void *)webgl2_vertex_attrib_i4iv},
    {"vertexAttribI4ui", (void *)webgl2_vertex_attrib_i4ui},
    {"vertexAttribI4uiv", (void *)webgl2_vertex_attrib_i4uiv},
    {"vertexAttribDivisor", (void *)webgl2_vertex_attrib_divisor},
    {"drawArraysInstanced", (void *)webgl2_draw_arrays_instanced},
    {"drawElementsInstanced", (void *)webgl2_draw_elements_instanced},
    {"drawRangeElements", (void *)webgl2_draw_range_elements},
    {"uniform1ui", (void *)webgl2_uniform1ui},
    {"uniform2ui", (void *)webgl2_uniform2ui},
    {"uniform3ui", (void *)webgl2_uniform3ui},
    {"uniform4ui", (void *)webgl2_uniform4ui},
    {"createSampler", (void *)webgl2_create_sampler},
    {"deleteSampler", (void *)webgl2_delete_sampler},
    {"bindSampler", (void *)webgl2_bind_sampler},
    {"samplerParameteri", (void *)webgl2_sampler_parameteri},
    {"samplerParameterf", (void *)webgl2_sampler_parameterf},
    {"getSamplerParameter", (void *)webgl2_get_sampler_parameter},
    {"createQuery", (void *)webgl2_create_query},
    {"deleteQuery", (void *)webgl2_delete_query},
    {"beginQuery", (void *)webgl2_begin_query},
    {"endQuery", (void *)webgl2_end_query},
    {"getQuery", (void *)webgl2_get_query},
    {"getQueryParameter", (void *)webgl2_get_query_parameter},
    {"createTransformFeedback", (void *)webgl2_create_transform_feedback},
    {"deleteTransformFeedback", (void *)webgl2_delete_transform_feedback},
    {"bindTransformFeedback", (void *)webgl2_bind_transform_feedback},
    {"beginTransformFeedback", (void *)webgl2_begin_transform_feedback},
    {"endTransformFeedback", (void *)webgl2_end_transform_feedback},
    {"pauseTransformFeedback", (void *)webgl2_pause_transform_feedback},
    {"resumeTransformFeedback", (void *)webgl2_resume_transform_feedback},
    {"transformFeedbackVaryings", (void *)webgl2_transform_feedback_varyings},
    {"getTransformFeedbackVarying", (void *)webgl2_get_transform_feedback_varying},
    {"fenceSync", (void *)webgl2_fence_sync},
    {"deleteSync", (void *)webgl2_delete_sync},
    {"clientWaitSync", (void *)webgl2_client_wait_sync},
    {"waitSync", (void *)webgl2_wait_sync},
    {"getSyncParameter", (void *)webgl2_get_sync_parameter},
    {"bindBufferBase", (void *)webgl2_bind_buffer_base},
    {"bindBufferRange", (void *)webgl2_bind_buffer_range},
    {"getIndexedParameter", (void *)webgl2_get_indexed_parameter},
    {"copyBufferSubData", (void *)webgl2_copy_buffer_sub_data},
    {"getBufferSubData", (void *)webgl2_get_buffer_sub_data},
    {"getUniformIndices", (void *)webgl2_get_uniform_indices},
    {"getActiveUniforms", (void *)webgl2_get_active_uniforms},
    {"getUniformBlockIndex", (void *)webgl2_get_uniform_block_index},
    {"getActiveUniformBlockParameter", (void *)webgl2_get_active_uniform_block_parameter},
    {"getActiveUniformBlockName", (void *)webgl2_get_active_uniform_block_name},
    {"uniformBlockBinding", (void *)webgl2_uniform_block_binding},
    {"drawingBufferStorage", (void *)webgl2_drawing_buffer_storage},
    {"texStorage2D", (void *)webgl2_tex_storage_2d},
    {"texStorage3D", (void *)webgl2_tex_storage_3d},
    {"texImage3D", (void *)webgl2_tex_image_3d},
    {"texSubImage3D", (void *)webgl2_tex_sub_image_3d},
    {"compressedTexImage2D", (void *)webgl_compressed_tex_image_2d},
    {"compressedTexSubImage2D", (void *)webgl_compressed_tex_sub_image_2d},
    {"compressedTexImage3D", (void *)webgl2_compressed_tex_image3d},
    {"compressedTexSubImage3D", (void *)webgl2_compressed_tex_sub_image3d},
    {"clearBufferfi", (void *)webgl2_clear_buffer_fi},
    {"clearBufferfv", (void *)webgl2_clear_buffer_fv},
    {"clearBufferiv", (void *)webgl2_clear_buffer_iv},
    {"clearBufferuiv", (void *)webgl2_clear_buffer_uiv},
    {"drawBuffers", (void *)webgl2_draw_buffers},
    {"readBuffer", (void *)webgl2_read_buffer},
    {"blitFramebuffer", (void *)webgl2_blit_framebuffer},
    {"framebufferTextureLayer", (void *)webgl2_framebuffer_texture_layer},
    {"invalidateFramebuffer", (void *)webgl2_invalidate_framebuffer},
    {"invalidateSubFramebuffer", (void *)webgl2_invalidate_sub_framebuffer},
    {"getInternalformatParameter", (void *)webgl2_get_internalformat_parameter},
    {"getFragDataLocation", (void *)webgl2_get_frag_data_location},
};

void define_method(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

xtgui::WebGLContext *context_of(xt_value self) { return webgl_live_context(self); }

xt_value uint_array(const unsigned int *values, int count) {
  xt_value array = xt_array_new(0, nullptr);
  for (int i = 0; i < count; i++) xt_array_push(array, xt_number((double)values[i]));
  return array;
}

}  // namespace

xt_value webgl2_proto() {
  xt_gc_add_root(&g_webgl2_proto);
  if (XT_IS_OBJECT(g_webgl2_proto)) return g_webgl2_proto;
  /* WebGL 2 shares the WebGL 1 surface but not its prototype: in a browser
   * `gl2 instanceof WebGLRenderingContext` is false, which is how three.js and
   * other libraries detect WebGL 2. Copy the WebGL 1 members instead of
   * inheriting the WebGL 1 prototype. */
  xt_value webgl1 = webgl_proto();
  xt_value proto = xt_object_new();
  xt_value keys = xt_object_keys(webgl1);
  const double count = xt_to_number(xt_array_length(keys));
  for (int32_t i = 0; i < (int32_t)count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    const char *name = xt_string_data(key);
    if (name == nullptr) continue;
    /* `constructor` gets the WebGL 2 name below. The three host getters are
     * accessors, not values: reading them here would run them against the
     * WebGL 1 prototype and copy `undefined`. Reinstall them as accessors. */
    if (strcmp(name, "constructor") == 0 || strcmp(name, "canvas") == 0 ||
        strcmp(name, "drawingBufferWidth") == 0 || strcmp(name, "drawingBufferHeight") == 0) {
      continue;
    }
    xt_object_set(proto, key, xt_object_get(webgl1, key));
  }
  xt_object_define_getter(proto, xt_string_from_cstr("canvas"),
                          xt_closure_new((void *)webgl_canvas_get, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("drawingBufferWidth"),
                          xt_closure_new((void *)webgl_drawing_buffer_width_get, 0, nullptr));
  xt_object_define_getter(proto, xt_string_from_cstr("drawingBufferHeight"),
                          xt_closure_new((void *)webgl_drawing_buffer_height_get, 0, nullptr));
#define XT_WEBGL2_CONSTANT(name, value) \
  xt_object_set(proto, xt_string_from_cstr(#name), xt_number((double)(value)));
  XT_WEBGL2_CONSTANTS(XT_WEBGL2_CONSTANT)
#undef XT_WEBGL2_CONSTANT
  for (const MethodEntry &method : kMethods) define_method(proto, method.name, method.fn);
  {
    xt_value constructor = xt_object_new();
    xt_object_set(constructor, xt_string_from_cstr("name"),
                  xt_string_from_cstr("WebGL2RenderingContext"));
    xt_object_set(proto, xt_string_from_cstr("constructor"), constructor);
  }
  g_webgl2_proto = proto;
  return proto;
}

/* -- queries ------------------------------------------------------------- */

METHOD(webgl2_create_query) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_query", context->create_query(), "query");
}

METHOD(webgl2_delete_query) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_query(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_query"));
  return XT_UNDEFINED;
}

METHOD(webgl2_begin_query) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl2_end_query) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl2_get_query) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_NULL;
}

METHOD(webgl2_get_query_parameter) {
  (void)env;
  const int pname = webgl_int(argc, argv, 1, 0);
  if (pname == GL_QUERY_RESULT_AVAILABLE) return XT_TRUE;
  if (pname == GL_QUERY_RESULT) return xt_number(0);
  return XT_NULL;
}

/* -- transform feedback -------------------------------------------------- */

METHOD(webgl2_create_transform_feedback) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_NULL;
}

#define WEBGL2_NOOP(name)                     \
  METHOD(name) {                              \
    (void)self;                               \
    (void)env;                                \
    (void)argc;                               \
    (void)argv;                               \
    return XT_UNDEFINED;                      \
  }

WEBGL2_NOOP(webgl2_delete_transform_feedback)
WEBGL2_NOOP(webgl2_bind_transform_feedback)
WEBGL2_NOOP(webgl2_begin_transform_feedback)
WEBGL2_NOOP(webgl2_end_transform_feedback)
WEBGL2_NOOP(webgl2_pause_transform_feedback)
WEBGL2_NOOP(webgl2_resume_transform_feedback)
WEBGL2_NOOP(webgl2_transform_feedback_varyings)
WEBGL2_NOOP(webgl2_invalidate_framebuffer)
WEBGL2_NOOP(webgl2_invalidate_sub_framebuffer)

METHOD(webgl2_blit_framebuffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int source[4] = {webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                         webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 0)};
  const int destination[4] = {webgl_int(argc, argv, 4, 0), webgl_int(argc, argv, 5, 0),
                              webgl_int(argc, argv, 6, 0), webgl_int(argc, argv, 7, 0)};
  context->blit_framebuffer(source, destination, webgl_int(argc, argv, 8, GL_COLOR_BUFFER_BIT),
                            webgl_int(argc, argv, 9, GL_NEAREST));
  return XT_UNDEFINED;
}

METHOD(webgl2_tex_sub_image_3d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0 || webgl_int(argc, argv, 1, 0) != 0) return XT_UNDEFINED;
  const int x = webgl_int(argc, argv, 2, 0);
  const int y = webgl_int(argc, argv, 3, 0);
  const int z = webgl_int(argc, argv, 4, 0);
  const int width = webgl_int(argc, argv, 5, 0);
  const int height = webgl_int(argc, argv, 6, 0);
  const int depth = webgl_int(argc, argv, 7, 0);
  const int format = webgl_int(argc, argv, 8, GL_RGBA);
  const int type = webgl_int(argc, argv, 9, GL_UNSIGNED_BYTE);
  if (type != GL_UNSIGNED_BYTE) {
    context->set_error(GL_INVALID_ENUM);
    return XT_UNDEFINED;
  }
  int channels = 1;
  if (format == GL_RGBA) channels = 4;
  else if (format == GL_RGB) channels = 3;
  else if (format == GL_LUMINANCE_ALPHA) channels = 2;
  const std::vector<uint8_t> bytes = webgl_bytes(xt_arg(argc, argv, 10));
  const size_t count = (size_t)width * height * depth;
  if (bytes.size() < count * (size_t)channels) {
    context->set_error(GL_INVALID_VALUE);
    return XT_UNDEFINED;
  }
  std::vector<uint8_t> rgba;
  webgl_convert_pixels(bytes.data(), (int)count, format, false, &rgba);
  context->tex_sub_image_3d(texture, x, y, z, width, height, depth, rgba.data());
  return XT_UNDEFINED;
}

METHOD(webgl2_framebuffer_texture_layer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  context->framebuffer_texture_layer(
      context->bound_framebuffer_id(),
      webgl_object_id(xt_arg(argc, argv, 2), "__xt_gl_texture"),
      webgl_int(argc, argv, 1, GL_COLOR_ATTACHMENT0), webgl_int(argc, argv, 4, 0));
  return XT_UNDEFINED;
}

METHOD(webgl2_get_transform_feedback_varying) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_NULL;
}

/* -- fences -------------------------------------------------------------- */

METHOD(webgl2_fence_sync) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_sync", context->create_sync(), "sync");
}

METHOD(webgl2_delete_sync) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_sync(webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_sync"));
  return XT_UNDEFINED;
}

METHOD(webgl2_client_wait_sync) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(GL_ALREADY_SIGNALED);
}

METHOD(webgl2_wait_sync) {
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

METHOD(webgl2_get_sync_parameter) {
  (void)env;
  const int pname = webgl_int(argc, argv, 1, 0);
  if (pname == GL_SYNC_STATUS) return xt_number(GL_SIGNALED);
  return XT_NULL;
}

/* -- uniform blocks and indexed queries ---------------------------------- */


METHOD(webgl2_get_uniform_indices) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_array_new(0, nullptr);
}

METHOD(webgl2_get_active_uniforms) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_array_new(0, nullptr);
}

METHOD(webgl2_get_uniform_block_index) {
  (void)env;
  const xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return xt_number(4294967295.0);
  const uint32_t program = webgl_object_id(xt_arg(argc, argv, 0), "__xt_gl_program");
  const uint32_t index =
      context->uniform_block_index(program, webgl_string(argc, argv, 1));
  return xt_number((double)index);
}



/* -- buffers ------------------------------------------------------------- */

METHOD(webgl2_copy_buffer_sub_data) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->copy_buffer_sub_data(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                                  (uint32_t)webgl_number(argc, argv, 2, 0.0),
                                  (uint32_t)webgl_number(argc, argv, 3, 0.0),
                                  (uint32_t)webgl_number(argc, argv, 4, 0.0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_get_buffer_sub_data) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int target = webgl_int(argc, argv, 0, 0);
  const uint32_t offset = (uint32_t)webgl_number(argc, argv, 1, 0.0);
  xt_value destination = xt_arg(argc, argv, 2);
  const uint32_t destination_offset = (uint32_t)webgl_number(argc, argv, 3, 0.0);
  uint32_t length = (uint32_t)webgl_number(argc, argv, 4, 0.0);
  std::vector<uint8_t> existing = webgl_bytes(destination);
  if (destination_offset > existing.size()) return XT_UNDEFINED;
  if (length == 0) length = (uint32_t)(existing.size() - destination_offset);
  std::vector<uint8_t> data(length);
  if (!context->get_buffer_sub_data(target, offset, data.data(), length)) return XT_UNDEFINED;
  if (destination_offset + length > existing.size()) existing.resize(destination_offset + length);
  memcpy(existing.data() + destination_offset, data.data(), length);
  webgl_write_bytes(destination, existing.data(), existing.size());
  return XT_UNDEFINED;
}

/* -- textures ------------------------------------------------------------ */

METHOD(webgl2_drawing_buffer_storage) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->drawing_buffer_storage(webgl_int(argc, argv, 0, 0),
                                    webgl_int(argc, argv, 1, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_tex_storage_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    const uint32_t texture = context->bound_texture(0);
    const int internalformat = webgl_int(argc, argv, 2, 0x8058);  // GL_RGBA8
    context->tex_storage_format(texture, webgl_int(argc, argv, 1, 1),
                                webgl_int(argc, argv, 3, 0), webgl_int(argc, argv, 4, 0),
                                xtgui::webgl_storage_format(internalformat));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_tex_storage_3d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int internalformat = webgl_int(argc, argv, 2, 0x8058);  // GL_RGBA8
  context->tex_storage_3d(context->bound_texture(0), webgl_int(argc, argv, 1, 1),
                          webgl_int(argc, argv, 3, 0), webgl_int(argc, argv, 4, 0),
                          webgl_int(argc, argv, 5, 0),
                          xtgui::webgl_storage_format(internalformat),
                          webgl_int(argc, argv, 0, GL_TEXTURE_2D_ARRAY));
  return XT_UNDEFINED;
}

METHOD(webgl2_tex_image_3d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0) return XT_UNDEFINED;
  if (webgl_int(argc, argv, 1, 0) != 0) return XT_UNDEFINED;
  const int internalformat = webgl_int(argc, argv, 2, GL_RGBA);
  const int width = webgl_int(argc, argv, 3, 0);
  const int height = webgl_int(argc, argv, 4, 0);
  const int depth = webgl_int(argc, argv, 5, 0);
  const int format = webgl_int(argc, argv, 7, GL_RGBA);
  const int type = webgl_int(argc, argv, 8, GL_UNSIGNED_BYTE);
  if (type != GL_UNSIGNED_BYTE || width <= 0 || height <= 0 || depth <= 0) {
    context->set_error(GL_INVALID_VALUE);
    return XT_UNDEFINED;
  }
  int channels = 1;
  if (format == GL_RGBA) channels = 4;
  else if (format == GL_RGB) channels = 3;
  else if (format == GL_LUMINANCE_ALPHA) channels = 2;
  const std::vector<uint8_t> bytes = webgl_bytes(xt_arg(argc, argv, 9));
  const size_t count = (size_t)width * height * depth;
  std::vector<uint8_t> rgba;
  if (bytes.empty()) {
    rgba.assign(count * 4, 0);
  } else if (bytes.size() < count * (size_t)channels) {
    context->set_error(GL_INVALID_VALUE);
    return XT_UNDEFINED;
  } else {
    webgl_convert_pixels(bytes.data(), (int)count, format, false, &rgba);
  }
  context->tex_image_3d(texture, width, height, depth,
                        xtgui::webgl_storage_format(internalformat), rgba.data(),
                        webgl_int(argc, argv, 0, GL_TEXTURE_2D_ARRAY));
  return XT_UNDEFINED;
}

WEBGL2_NOOP(webgl2_compressed_tex_image3d)
WEBGL2_NOOP(webgl2_compressed_tex_sub_image3d)

/* -- clears and draw buffers --------------------------------------------- */

METHOD(webgl2_clear_buffer_fv) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const std::vector<double> values = webgl_numbers(xt_arg(argc, argv, 2));
  float floats[4] = {0, 0, 0, 1};
  for (size_t i = 0; i < values.size() && i < 4; i++) floats[i] = (float)values[i];
  context->clear_buffer_float(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0), floats,
                              (int)values.size());
  return XT_UNDEFINED;
}

METHOD(webgl2_clear_buffer_iv) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const std::vector<double> values = webgl_numbers(xt_arg(argc, argv, 2));
  int ints[4] = {0, 0, 0, 1};
  for (size_t i = 0; i < values.size() && i < 4; i++) ints[i] = (int)values[i];
  context->clear_buffer_int(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0), ints,
                            (int)values.size());
  return XT_UNDEFINED;
}

METHOD(webgl2_clear_buffer_uiv) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const std::vector<double> values = webgl_numbers(xt_arg(argc, argv, 2));
  unsigned int uints[4] = {0, 0, 0, 1};
  for (size_t i = 0; i < values.size() && i < 4; i++) uints[i] = (unsigned int)values[i];
  context->clear_buffer_uint(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0), uints,
                             (int)values.size());
  return XT_UNDEFINED;
}

METHOD(webgl2_clear_buffer_fi) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->clear_buffer_fi(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                             (float)webgl_number(argc, argv, 2, 1.0),
                             webgl_int(argc, argv, 3, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl2_draw_buffers) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const xt_value list = xt_arg(argc, argv, 0);
  std::vector<double> values = webgl_numbers(list);
  if (values.empty()) {
    /* `webgl_numbers` only walks typed arrays; plain WebGL argument arrays
     * (`drawBuffers([gl.COLOR_ATTACHMENT0, ...])`) need the element accessor. */
    const double length = xt_to_number(xt_get(list, xt_string_from_cstr("length")));
    if (length > 0.0 && length <= 16.0) {
      for (int i = 0; i < (int)length; i++) {
        values.push_back(xt_to_number(xt_get(list, xt_number((double)i))));
      }
    }
  }
  std::vector<int> buffers(values.size());
  for (size_t i = 0; i < values.size(); i++) buffers[i] = (int)values[i];
  context->draw_buffers((int)buffers.size(), buffers.data());
  return XT_UNDEFINED;
}

METHOD(webgl2_read_buffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->read_buffer(webgl_int(argc, argv, 0, GL_BACK));
  return XT_UNDEFINED;
}

/* -- misc ---------------------------------------------------------------- */

METHOD(webgl2_get_internalformat_parameter) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_array_new(0, nullptr);
}

METHOD(webgl2_get_frag_data_location) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(-1);
}

}  // namespace domapi
