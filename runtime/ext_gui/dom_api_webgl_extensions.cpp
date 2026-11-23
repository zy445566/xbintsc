/*
 * xbintsc GUI engine — WebGL extension entry points.
 *
 * `getSupportedExtensions` / `getExtension` decide what a capability probe sees;
 * the compressed-texture families are built by `dom_api_webgl_compressed.cpp`.
 * `WEBGL_lose_context` lives here too because its two methods are the only
 * extension that carries behaviour. `KHR_parallel_shader_compile` publishes
 * only `COMPLETION_STATUS_KHR`: translation is synchronous, so the completion
 * query is always true (`dom_api_webgl_shader.cpp`).
 */

#include "dom_api_internal.h"

#include <string>

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

/** Fire a `webglcontextlost`/`webglcontextrestored` event on the canvas that
 * owns the context. `self` is the context or extension handle. */
void dispatch_context_event(xt_value self, const char *type) {
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(xt_object_get_cstr(self, "__xt_gl_node"), &win);
  if (win != nullptr && node != nullptr) xt_gui_dispatch_to_node(win, node, type);
}

}  // namespace

METHOD(webgl_lose_context);
METHOD(webgl_restore_context);

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
  /* Compilation is synchronous, so the completion query is always true; the
   * extension is still advertised because three.js (and other loaders) warn
   * when a probe finds it missing. */
  xt_array_push(list, xt_string_from_cstr("KHR_parallel_shader_compile"));
  xt_array_push(list, xt_string_from_cstr("EXT_texture_filter_anisotropic"));
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
  if (name == "EXT_texture_filter_anisotropic") {
    xt_value e = xt_object_new();
    xt_object_set(e, xt_string_from_cstr("MAX_TEXTURE_MAX_ANISOTROPY_EXT"), xt_number(0x84FF));
    xt_object_set(e, xt_string_from_cstr("TEXTURE_MAX_ANISOTROPY_EXT"), xt_number(0x84FE));
    return e;
  }
  if (name == "KHR_parallel_shader_compile") {
    xt_value e = xt_object_new();
    xt_object_set(e, xt_string_from_cstr("COMPLETION_STATUS_KHR"),
                  xt_number(xtgui::kWebGLCompletionStatusKHR));
    return e;
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

}  // namespace domapi
