/*
 * xbintsc GUI engine — WebGL buffer, vertex-attribute and draw bindings.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_from_context(self); }

uint32_t buffer_argument(xt_value value) { return webgl_object_id(value, "__xt_gl_buffer"); }

}  // namespace

METHOD(webgl_create_buffer) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_buffer", context->create_buffer(), "buffer");
}

METHOD(webgl_delete_buffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_buffer(buffer_argument(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl_bind_buffer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_buffer(webgl_int(argc, argv, 0, 0), buffer_argument(xt_arg(argc, argv, 1)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_buffer_data) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int target = webgl_int(argc, argv, 0, 0);
  const uint32_t buffer = context->bound_buffer(target);
  if (buffer == 0) return XT_UNDEFINED;
  const xt_value data = xt_arg(argc, argv, 1);
  const int usage = webgl_int(argc, argv, 2, GL_STATIC_DRAW);
  if (XT_IS_OBJECT(data)) {
    const std::vector<uint8_t> bytes = webgl_bytes(data);
    context->buffer_data(buffer, bytes.empty() ? nullptr : bytes.data(), (uint32_t)bytes.size(),
                         usage);
  } else {
    const uint32_t size = (uint32_t)xt_to_number(data);
    context->buffer_data(buffer, nullptr, size, usage);
  }
  return XT_UNDEFINED;
}

METHOD(webgl_buffer_sub_data) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int target = webgl_int(argc, argv, 0, 0);
  const uint32_t buffer = context->bound_buffer(target);
  const uint32_t offset = (uint32_t)webgl_number(argc, argv, 1, 0.0);
  const std::vector<uint8_t> bytes = webgl_bytes(xt_arg(argc, argv, 2));
  if (buffer != 0) context->buffer_sub_data(buffer, offset, bytes.data(), (uint32_t)bytes.size());
  return XT_UNDEFINED;
}

METHOD(webgl_get_buffer_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const int target = webgl_int(argc, argv, 0, 0);
  const int pname = webgl_int(argc, argv, 1, 0);
  const uint32_t buffer = context->bound_buffer(target);
  if (pname == GL_BUFFER_SIZE) return xt_number((double)context->buffer_size(buffer));
  return xt_number(0);
}

METHOD(webgl_enable_vertex_attrib_array) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->enable_attrib(webgl_int(argc, argv, 0, 0), true);
  return XT_UNDEFINED;
}

METHOD(webgl_disable_vertex_attrib_array) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->enable_attrib(webgl_int(argc, argv, 0, 0), false);
  return XT_UNDEFINED;
}

METHOD(webgl_vertex_attrib_pointer) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->attrib_pointer(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 4),
                            webgl_int(argc, argv, 2, GL_FLOAT), value_truthy(xt_arg(argc, argv, 3)),
                            webgl_int(argc, argv, 4, 0), webgl_int(argc, argv, 5, 0));
  }
  return XT_UNDEFINED;
}

namespace {

void attrib_value(xt_value self, int32_t argc, xt_value *argv, int components) {
  xtgui::WebGLContext *context = webgl_from_context(self);
  if (context == nullptr) return;
  float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  for (int i = 0; i < components; i++) values[i] = (float)webgl_number(argc, argv, i + 1, 0.0);
  if (components < 4) values[3] = 1.0f;
  context->attrib_value(webgl_int(argc, argv, 0, 0), components, values);
}

}  // namespace

METHOD(webgl_vertex_attrib1f) {
  attrib_value(self, argc, argv, 1);
  return XT_UNDEFINED;
}
METHOD(webgl_vertex_attrib2f) {
  attrib_value(self, argc, argv, 2);
  return XT_UNDEFINED;
}
METHOD(webgl_vertex_attrib3f) {
  attrib_value(self, argc, argv, 3);
  return XT_UNDEFINED;
}
METHOD(webgl_vertex_attrib4f) {
  attrib_value(self, argc, argv, 4);
  return XT_UNDEFINED;
}
METHOD(webgl_vertex_attrib1fv) {
  attrib_value(self, argc, argv, 1);
  return XT_UNDEFINED;
}
METHOD(webgl_vertex_attrib2fv) {
  attrib_value(self, argc, argv, 2);
  return XT_UNDEFINED;
}
METHOD(webgl_vertex_attrib3fv) {
  attrib_value(self, argc, argv, 3);
  return XT_UNDEFINED;
}
METHOD(webgl_vertex_attrib4fv) {
  attrib_value(self, argc, argv, 4);
  return XT_UNDEFINED;
}

METHOD(webgl_get_vertex_attrib) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  const int index = webgl_int(argc, argv, 0, -1);
  const int pname = webgl_int(argc, argv, 1, 0);
  if (index < 0 || index >= 16) return XT_NULL;
  int size = 4;
  int type = GL_FLOAT;
  int stride = 0;
  bool normalized = false;
  float current[4] = {0, 0, 0, 1};
  context->attrib_state(index, &size, &type, &stride, &normalized, current);
  if (pname == GL_CURRENT_VERTEX_ATTRIB) {
    xt_value values = xt_array_new(0, nullptr);
    for (float value : current) xt_array_push(values, xt_number(value));
    return values;
  }
  if (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED) return xt_bool(context->attrib_enabled(index));
  if (pname == GL_VERTEX_ATTRIB_ARRAY_SIZE) return xt_number(size);
  if (pname == GL_VERTEX_ATTRIB_ARRAY_TYPE) return xt_number(type);
  if (pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE) return xt_number(stride);
  if (pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED) return xt_bool(normalized);
  if (pname == GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING) {
    const uint32_t buffer = context->attrib_buffer(index);
    return buffer != 0 ? webgl_make_object(self, "__xt_gl_buffer", buffer, "buffer") : XT_NULL;
  }
  return xt_number(0);
}

METHOD(webgl_draw_arrays) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->draw_arrays(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                         webgl_int(argc, argv, 2, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_draw_elements) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->draw_elements(webgl_int(argc, argv, 0, 0), webgl_int(argc, argv, 1, 0),
                           webgl_int(argc, argv, 2, 0), webgl_int(argc, argv, 3, 0));
  }
  return XT_UNDEFINED;
}

}  // namespace domapi
