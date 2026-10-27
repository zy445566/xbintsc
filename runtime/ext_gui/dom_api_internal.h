/*
 * xbintsc GUI engine — shared declarations for the DOM object model
 * translation units (`dom_api.cpp`, `dom_api_events.cpp`,
 * `dom_api_document.cpp`, `dom_api_node.cpp`, `dom_api_node_read.cpp`,
 * `dom_api_style.cpp`, `dom_api_proto.cpp`).
 *
 * The prototypes and the helpers that more than one unit needs live in
 * `xtgui`-adjacent namespace `domapi` so the three public entry points
 * (`xt_gui_node_handle` and friends, declared in `gui_engine.h`) can stay at
 * global scope.
 */
#ifndef XT_GUI_DOM_API_INTERNAL_H
#define XT_GUI_DOM_API_INTERNAL_H

#include <cstdint>
#include <string>
#include <vector>

#include "gui_engine.h"

namespace domapi {

/* -- prototypes ----------------------------------------------------------- */

extern xt_value g_node_proto;
extern xt_value g_document_proto;
extern xt_value g_event_proto;
extern xt_value g_class_list_proto;
extern xt_value g_style_proto;
extern xt_value g_computed_style_proto;
extern xt_value g_canvas_proto;
extern xt_value g_canvas_gradient_proto;
extern xt_value g_webgl_proto;
extern xt_value g_webgl2_proto;
extern xt_value g_webgl_buffer_proto;
extern xt_value g_webgl_texture_proto;
extern xt_value g_webgl_shader_proto;
extern xt_value g_webgl_program_proto;
extern xt_value g_webgl_framebuffer_proto;
extern xt_value g_webgl_renderbuffer_proto;
extern xt_value g_webgl_uniform_proto;
extern xt_value g_webgl_vertex_array_proto;
extern xt_value g_webgl_sampler_proto;
extern xt_value g_webgl_query_proto;
extern xt_value g_webgl_sync_proto;
extern xt_value g_webgl_transform_feedback_proto;

xt_value node_proto();
xt_value document_proto();
xt_value event_proto();
xt_value class_list_proto();
xt_value style_proto();
xt_value computed_style_proto();
xt_value canvas_proto();
xt_value canvas_gradient_proto();
xt_value webgl_proto();
xt_value webgl2_proto();
/** The prototype a WebGL object of `kind` inherits (`buffer`, `texture`, …),
 * so `buffer instanceof WebGLBuffer` holds. */
xt_value webgl_object_proto(const std::string &kind);

/** Create/return the 2D context for a pending `<canvas>` handle (before load). */
xt_value xt_canvas_pending_context(xt_value self, XtGuiWindow *win);

/* -- Path2D --------------------------------------------------------------- */

/** True when `value` is a `Path2D` handle. */
bool path2d_is(xt_value value);
/** The user-space subpaths a `Path2D` handle holds. */
std::vector<xtgui::CanvasSubpath> path2d_subpaths(xt_value value);
/** `new Path2D()` / `new Path2D(other)`. */
xt_value path2d_create(xt_value source);
xt_value path2d_proto();

/* -- canvas helpers shared by `dom_api_canvas*.cpp` ----------------------- */

/** The engine canvas behind a 2D context handle, or null when stale/foreign. */
xtgui::Canvas2D *canvas_from_context(xt_value self);
double canvas_number_arg(int32_t argc, xt_value *argv, int32_t index, double fallback);
std::string canvas_string_arg(int32_t argc, xt_value *argv, int32_t index);
/** The cached `CanvasGradient` handle for `id` on `canvas`; cached on the
 * context so `ctx.fillStyle === gradient` holds after `fillStyle = gradient`. */
xt_value canvas_gradient_handle(xt_value context, const xtgui::Canvas2D *canvas, uint64_t id);
/** The gradient id stored on a handle, or 0 when it is not a gradient for
 * `canvas` (stale or created by another canvas). */
uint64_t canvas_gradient_id(xt_value handle, const xtgui::Canvas2D *canvas);

/* -- WebGL helpers shared by `dom_api_webgl*.cpp` ------------------------- */

/** The engine WebGL context behind a context handle, or null. */
xtgui::WebGLContext *webgl_from_context(xt_value self);
/** Like `webgl_from_context`, but null while the context is lost: every GL
 * entry point except `getError`/`isContextLost` is a no-op in that state. */
xtgui::WebGLContext *webgl_live_context(xt_value self);
/** The numeric context id stored on a context handle (0 when absent). */
uint64_t webgl_context_id(xt_value self);
/** Create the JavaScript handle for a canvas's WebGL context. */
xt_value webgl_context_handle(xt_value element, xtgui::WebGLContext *context);
/** The id stored on an object handle under `key`, or 0. */
uint32_t webgl_object_id(xt_value handle, const char *key);
/** Build an object handle (`{__xt_gl_context, __xt_gl_<key>}`). */
xt_value webgl_make_object(xt_value context, const char *key, uint32_t id,
                           const std::string &kind);
double webgl_number(int32_t argc, xt_value *argv, int32_t index, double fallback);
int webgl_int(int32_t argc, xt_value *argv, int32_t index, int fallback);
std::string webgl_string(int32_t argc, xt_value *argv, int32_t index);
/** Read an array/typed array of numbers. */
std::vector<double> webgl_numbers(xt_value value);
/** Read the raw bytes of a typed array / ArrayBuffer (or a byte array). */
std::vector<uint8_t> webgl_bytes(xt_value value);
/** Store `count` bytes into a typed array / array handle. */
void webgl_write_bytes(xt_value target, const uint8_t *bytes, size_t count);
/** Convert a source pixel array (RGBA/RGB/LUMINANCE/…) to RGBA8. */
void webgl_convert_pixels(const uint8_t *source, int count, int format, bool premultiply,
                          std::vector<uint8_t> *out);
/** Decode a `texImage2D` source (`<img>`, `<canvas>`, `<video>`, `ImageData`,
 * `ImageBitmap`) into top-down RGBA8. */
bool webgl_read_source(xt_value source, int *width, int *height,
                       std::vector<uint8_t> *rgba, bool premultiply);
/** Read the `UNPACK_FLIP_Y_WEBGL` / `UNPACK_PREMULTIPLY_ALPHA_WEBGL` flags set
 * through `pixelStorei`. */
void webgl_unpack_flags(xt_value context, bool *flip_y, bool *premultiply);
/** Extension object for a compressed-texture family (null when unsupported,
 * undefined when the name is not one of those extensions). */
xt_value webgl_compressed_extension(const std::string &name, xtgui::WebGLContext *context);

/** How a pending handle names the node it will bind to. */
enum PendingKind { pending_by_id = 1, pending_by_selector = 2 };

/** A handle for a node the document does not have yet: it binds on first use
 * after `loadHTML` has parsed the page (`resolve_node` does the lookup). */
xt_value pending_handle(XtGuiWindow *win, int kind, const std::string &key);
/** The first element with `id` / matching `selector`, or null. */
xtgui::Node *document_find_by_id(XtGuiWindow *win, const std::string &id);
xtgui::Node *document_query_selector(XtGuiWindow *win, const std::string &selector);

/* -- cross-unit helpers --------------------------------------------------- */

std::string to_string_value(xt_value value);
bool value_truthy(xt_value value);
XtGuiWindow *resolve_window(xt_value self);
xtgui::Node *resolve_node(xt_value self, XtGuiWindow **winOut);
void mark_dirty(XtGuiWindow *win);
void set_attribute_raw(xtgui::Node *node, const std::string &name, const std::string &value);
void remove_attribute_raw(xtgui::Node *node, const std::string &name);
void set_attribute(XtGuiWindow *win, xtgui::Node *node, const std::string &name,
                   const std::string &value);
std::string class_name_of(const xtgui::Node *node);
void set_class_name(XtGuiWindow *win, xtgui::Node *node, const std::string &value);
void remove_listener(XtGuiWindow *win, xtgui::Node *node, const std::string &type, xt_value fn,
                     bool capture);
/** Forget the handles of `node` and its descendants, then free the subtree from
 * the document's detached pool. Use this only for nodes the script can no longer
 * reach (the children `innerHTML = …` replaces, the node `replaceChild` drops):
 * an existing handle then resolves to "stale" and no-ops, exactly like a handle
 * to a node the document already removed and released. */
void discard_subtree(XtGuiWindow *win, xtgui::Node *node);

/* -- method definitions (uniform `(self, env, argc, argv)` ABI) ----------- */

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

/* document */
METHOD(doc_query_selector);
METHOD(doc_query_selector_all);
METHOD(doc_get_element_by_id);
METHOD(doc_create_element);
METHOD(doc_create_element_ns);
METHOD(doc_hidden_get);
METHOD(doc_visibility_state_get);
METHOD(doc_ready_state_get);
METHOD(doc_has_focus);
METHOD(doc_title_get);
METHOD(doc_title_set);
METHOD(doc_default_view_get);
METHOD(doc_create_text_node);
METHOD(doc_body_get);
METHOD(doc_document_element_get);

/* node getters */
METHOD(node_tag_name_get);
METHOD(node_node_type_get);
METHOD(node_namespace_uri_get);
METHOD(node_local_name_get);
METHOD(node_prefix_get);
METHOD(node_id_get);
METHOD(node_id_set);
METHOD(node_class_name_get);
METHOD(node_class_name_set);
METHOD(node_text_content_get);
METHOD(node_text_content_set);
METHOD(node_inner_html_get);
METHOD(node_inner_html_set);
METHOD(node_parent_node_get);
METHOD(node_parent_element_get);
METHOD(node_children_get);
METHOD(node_child_nodes_get);
METHOD(node_child_element_count_get);
METHOD(node_first_element_child_get);
METHOD(node_last_element_child_get);
METHOD(node_next_element_sibling_get);
METHOD(node_previous_element_sibling_get);
METHOD(node_is_connected_get);
METHOD(node_class_list_get);
METHOD(node_style_get);

/* node methods */
METHOD(node_get_attribute);
METHOD(node_set_attribute);
METHOD(node_remove_attribute);
METHOD(node_has_attribute);
METHOD(node_query_selector);
METHOD(node_query_selector_all);
METHOD(node_matches);
METHOD(node_append_child);
METHOD(node_append);
METHOD(node_prepend);
METHOD(node_replace_children);
METHOD(node_insert_adjacent_html);
METHOD(node_insert_before);
METHOD(node_remove_child);
METHOD(node_replace_child);
METHOD(node_remove);
METHOD(node_clone_node);
METHOD(node_get_bounding_rect);
METHOD(node_offset_width_get);
METHOD(node_offset_height_get);
METHOD(node_contains);
METHOD(node_focus);
METHOD(node_blur);
METHOD(node_click);
METHOD(node_add_event_listener);
METHOD(node_remove_event_listener);
METHOD(node_dispatch_event);

/* event construction (a detached `Event`/`CustomEvent` handle) */
xt_value event_construct(xt_value type, xt_value init);

/* event methods */
METHOD(event_prevent_default);
METHOD(event_stop_propagation);
METHOD(event_stop_immediate_propagation);

/* media (`<video>`/`<audio>`) */
METHOD(node_play);
METHOD(node_pause);
METHOD(node_load);
METHOD(node_paused_get);
METHOD(node_ended_get);
METHOD(node_current_time_get);
METHOD(node_current_time_set);
METHOD(node_duration_get);
METHOD(node_volume_get);
METHOD(node_volume_set);
METHOD(node_muted_get);
METHOD(node_muted_set);
METHOD(node_loop_get);
METHOD(node_loop_set);
METHOD(node_autoplay_get);
METHOD(node_autoplay_set);
METHOD(node_video_width_get);
METHOD(node_video_height_get);
METHOD(node_ready_state_get);
METHOD(node_media_src_get);
METHOD(node_media_src_set);

/* canvas (`<canvas>` element + 2D context) */
METHOD(node_get_context);
METHOD(node_width_get);
METHOD(node_width_set);
METHOD(node_height_get);
METHOD(node_height_set);
METHOD(canvas_get_canvas);
METHOD(canvas_fill_style_get);
METHOD(canvas_fill_style_set);
METHOD(canvas_stroke_style_get);
METHOD(canvas_stroke_style_set);
METHOD(canvas_line_width_get);
METHOD(canvas_line_width_set);
METHOD(canvas_global_alpha_get);
METHOD(canvas_global_alpha_set);
METHOD(canvas_font_get);
METHOD(canvas_font_set);
METHOD(canvas_text_align_get);
METHOD(canvas_text_align_set);
METHOD(canvas_text_baseline_get);
METHOD(canvas_text_baseline_set);
METHOD(canvas_save);
METHOD(canvas_restore);
METHOD(canvas_translate);
METHOD(canvas_rotate);
METHOD(canvas_scale);
METHOD(canvas_transform);
METHOD(canvas_set_transform);
METHOD(canvas_reset_transform);
METHOD(canvas_begin_path);
METHOD(canvas_close_path);
METHOD(canvas_move_to);
METHOD(canvas_line_to);
METHOD(canvas_rect);
METHOD(canvas_arc);
METHOD(canvas_quadratic_curve_to);
METHOD(canvas_bezier_curve_to);
METHOD(canvas_fill);
METHOD(canvas_stroke);
METHOD(canvas_clear_rect);
METHOD(canvas_fill_rect);
METHOD(canvas_stroke_rect);
METHOD(canvas_measure_text);METHOD(canvas_fill_text);
METHOD(canvas_stroke_text);
METHOD(canvas_draw_image);
METHOD(canvas_get_image_data);
METHOD(canvas_get_context_attributes);
METHOD(canvas_put_image_data);
METHOD(canvas_create_image_data);
METHOD(canvas_create_pattern);
METHOD(canvas_to_data_url);
METHOD(canvas_to_blob);
/** The cached `CanvasPattern` handle for `id` on `canvas`. */
xt_value canvas_pattern_handle(xt_value context, const xtgui::Canvas2D *canvas, uint64_t id);
/** The pattern id stored on a handle, or 0 when it is not a pattern for
 * `canvas` (stale or created by another canvas). */
uint64_t canvas_pattern_id(xt_value handle, const xtgui::Canvas2D *canvas);
METHOD(canvas_ellipse);
METHOD(canvas_round_rect);
METHOD(canvas_line_cap_get);
METHOD(canvas_line_cap_set);
METHOD(canvas_line_join_get);
METHOD(canvas_line_join_set);
METHOD(canvas_miter_limit_get);
METHOD(canvas_miter_limit_set);
METHOD(canvas_line_dash_get);
METHOD(canvas_line_dash_set);
METHOD(canvas_line_dash_offset_get);
METHOD(canvas_line_dash_offset_set);
METHOD(canvas_get_transform);
METHOD(canvas_create_linear_gradient);
METHOD(canvas_create_radial_gradient);
METHOD(canvas_gradient_add_color_stop);

/* classList methods */
METHOD(class_list_add);
METHOD(class_list_remove);
METHOD(class_list_toggle);
METHOD(class_list_contains);

/* style methods */
METHOD(style_set_property);
METHOD(style_get_property_value);
METHOD(style_get_property_priority);
METHOD(style_remove_property);
METHOD(style_css_text_get);
METHOD(style_css_text_set);

/* computed style (`getComputedStyle`) */
METHOD(computed_style_get_property_value);
METHOD(computed_style_get_property_priority);
/** Build the computed-style handle for an element (and optional pseudo). */
xt_value computed_style_handle(xt_value element, xt_value pseudo);

/* WebGL context: state and queries */
METHOD(webgl_canvas_get);
METHOD(webgl_drawing_buffer_width_get);
METHOD(webgl_drawing_buffer_height_get);
METHOD(webgl_enable);
METHOD(webgl_disable);
METHOD(webgl_is_enabled);
METHOD(webgl_clear);
METHOD(webgl_clear_color);
METHOD(webgl_clear_depth);
METHOD(webgl_clear_stencil);
METHOD(webgl_color_mask);
METHOD(webgl_depth_func);
METHOD(webgl_depth_mask);
METHOD(webgl_depth_range);
METHOD(webgl_blend_color);
METHOD(webgl_blend_equation);
METHOD(webgl_blend_equation_separate);
METHOD(webgl_blend_func);
METHOD(webgl_blend_func_separate);
METHOD(webgl_cull_face);
METHOD(webgl_front_face);
METHOD(webgl_viewport);
METHOD(webgl_scissor);
METHOD(webgl_finish);
METHOD(webgl_flush);
METHOD(webgl_hint);
METHOD(webgl_line_width);
METHOD(webgl_polygon_offset);
METHOD(webgl_sample_coverage);
METHOD(webgl_pixel_storei);
METHOD(webgl_get_error);
METHOD(webgl_get_parameter);
METHOD(webgl_get_supported_extensions);
METHOD(webgl_get_extension);
METHOD(webgl_get_context_attributes);
METHOD(webgl_is_context_lost);
METHOD(webgl_stencil_func);
METHOD(webgl_stencil_func_separate);
METHOD(webgl_stencil_op);
METHOD(webgl_stencil_op_separate);
METHOD(webgl_stencil_mask);
METHOD(webgl_stencil_mask_separate);
METHOD(webgl_get_shader_precision_format);

/* WebGL shaders, programs and uniforms */
METHOD(webgl_create_shader);
METHOD(webgl_delete_shader);
METHOD(webgl_shader_source);
METHOD(webgl_compile_shader);
METHOD(webgl_get_shader_parameter);
METHOD(webgl_get_shader_info_log);
METHOD(webgl_get_shader_source);
METHOD(webgl_create_program);
METHOD(webgl_delete_program);
METHOD(webgl_attach_shader);
METHOD(webgl_detach_shader);
METHOD(webgl_link_program);
METHOD(webgl_use_program);
METHOD(webgl_get_program_parameter);
METHOD(webgl_get_program_info_log);
METHOD(webgl_get_attached_shaders);
METHOD(webgl_validate_program);
METHOD(webgl_get_active_uniform);
METHOD(webgl_get_active_attrib);
METHOD(webgl_get_uniform_location);
METHOD(webgl_get_attrib_location);
METHOD(webgl_bind_attrib_location);
METHOD(webgl_get_uniform);
METHOD(webgl_uniform1f);
METHOD(webgl_uniform2f);
METHOD(webgl_uniform3f);
METHOD(webgl_uniform4f);
METHOD(webgl_uniform1i);
METHOD(webgl_uniform2i);
METHOD(webgl_uniform3i);
METHOD(webgl_uniform4i);
METHOD(webgl_uniform1fv);
METHOD(webgl_uniform2fv);
METHOD(webgl_uniform3fv);
METHOD(webgl_uniform4fv);
METHOD(webgl_uniform1iv);
METHOD(webgl_uniform2iv);
METHOD(webgl_uniform3iv);
METHOD(webgl_uniform4iv);
METHOD(webgl_uniform_matrix2fv);
METHOD(webgl_uniform_matrix3fv);
METHOD(webgl_uniform_matrix4fv);

/* WebGL buffers, vertex attributes and draws */
METHOD(webgl_create_buffer);
METHOD(webgl_delete_buffer);
METHOD(webgl_bind_buffer);
METHOD(webgl_buffer_data);
METHOD(webgl_buffer_sub_data);
METHOD(webgl_get_buffer_parameter);
METHOD(webgl_enable_vertex_attrib_array);
METHOD(webgl_disable_vertex_attrib_array);
METHOD(webgl_vertex_attrib_pointer);
METHOD(webgl_vertex_attrib1f);
METHOD(webgl_vertex_attrib2f);
METHOD(webgl_vertex_attrib3f);
METHOD(webgl_vertex_attrib4f);
METHOD(webgl_vertex_attrib1fv);
METHOD(webgl_vertex_attrib2fv);
METHOD(webgl_vertex_attrib3fv);
METHOD(webgl_vertex_attrib4fv);
METHOD(webgl_get_vertex_attrib);
METHOD(webgl_draw_arrays);
METHOD(webgl_draw_elements);

/* WebGL textures, framebuffers and readback */
METHOD(webgl_create_texture);
METHOD(webgl_delete_texture);
METHOD(webgl_bind_texture);
METHOD(webgl_active_texture);
METHOD(webgl_tex_parameteri);
METHOD(webgl_tex_parameterf);
METHOD(webgl_get_tex_parameter);
METHOD(webgl_tex_image_2d);
METHOD(webgl_tex_sub_image_2d);
METHOD(webgl_compressed_tex_image_2d);
METHOD(webgl_compressed_tex_sub_image_2d);
METHOD(webgl_generate_mipmap);
METHOD(webgl_create_framebuffer);
METHOD(webgl_delete_framebuffer);
METHOD(webgl_bind_framebuffer);
METHOD(webgl_framebuffer_texture_2d);
METHOD(webgl_check_framebuffer_status);
METHOD(webgl_create_renderbuffer);
METHOD(webgl_delete_renderbuffer);
METHOD(webgl_bind_renderbuffer);
METHOD(webgl_renderbuffer_storage);
METHOD(webgl_framebuffer_renderbuffer);
METHOD(webgl_get_renderbuffer_parameter);
METHOD(webgl_read_pixels);

/* WebGL 2 context extensions */
METHOD(webgl2_create_vertex_array);
METHOD(webgl2_delete_vertex_array);
METHOD(webgl2_bind_vertex_array);
METHOD(webgl2_is_vertex_array);
METHOD(webgl2_vertex_attrib_ipointer);
METHOD(webgl2_vertex_attrib_i4i);
METHOD(webgl2_vertex_attrib_i4iv);
METHOD(webgl2_vertex_attrib_i4ui);
METHOD(webgl2_vertex_attrib_i4uiv);
METHOD(webgl2_vertex_attrib_divisor);
METHOD(webgl2_draw_arrays_instanced);
METHOD(webgl2_draw_elements_instanced);
METHOD(webgl2_draw_range_elements);
METHOD(webgl2_uniform1ui);
METHOD(webgl2_uniform2ui);
METHOD(webgl2_uniform3ui);
METHOD(webgl2_uniform4ui);
METHOD(webgl2_create_sampler);
METHOD(webgl2_delete_sampler);
METHOD(webgl2_bind_sampler);
METHOD(webgl2_sampler_parameteri);
METHOD(webgl2_sampler_parameterf);
METHOD(webgl2_get_sampler_parameter);
METHOD(webgl2_create_query);
METHOD(webgl2_delete_query);
METHOD(webgl2_begin_query);
METHOD(webgl2_end_query);
METHOD(webgl2_get_query);
METHOD(webgl2_get_query_parameter);
METHOD(webgl2_create_transform_feedback);
METHOD(webgl2_delete_transform_feedback);
METHOD(webgl2_bind_transform_feedback);
METHOD(webgl2_begin_transform_feedback);
METHOD(webgl2_end_transform_feedback);
METHOD(webgl2_pause_transform_feedback);
METHOD(webgl2_resume_transform_feedback);
METHOD(webgl2_transform_feedback_varyings);
METHOD(webgl2_get_transform_feedback_varying);
METHOD(webgl2_fence_sync);
METHOD(webgl2_delete_sync);
METHOD(webgl2_client_wait_sync);
METHOD(webgl2_wait_sync);
METHOD(webgl2_get_sync_parameter);
METHOD(webgl2_bind_buffer_base);
METHOD(webgl2_bind_buffer_range);
METHOD(webgl2_get_indexed_parameter);
METHOD(webgl2_copy_buffer_sub_data);
METHOD(webgl2_get_buffer_sub_data);
METHOD(webgl2_get_uniform_indices);
METHOD(webgl2_get_active_uniforms);
METHOD(webgl2_get_uniform_block_index);
METHOD(webgl2_get_active_uniform_block_parameter);
METHOD(webgl2_get_active_uniform_block_name);
METHOD(webgl2_uniform_block_binding);
METHOD(webgl2_drawing_buffer_storage);
METHOD(webgl2_tex_storage_2d);
METHOD(webgl2_tex_storage_3d);
METHOD(webgl2_tex_image_3d);
METHOD(webgl2_tex_sub_image_3d);
METHOD(webgl2_compressed_tex_image2d);
METHOD(webgl2_compressed_tex_sub_image2d);
METHOD(webgl2_compressed_tex_image3d);
METHOD(webgl2_compressed_tex_sub_image3d);
METHOD(webgl2_clear_buffer_fi);
METHOD(webgl2_clear_buffer_fv);
METHOD(webgl2_clear_buffer_iv);
METHOD(webgl2_clear_buffer_uiv);
METHOD(webgl2_draw_buffers);
METHOD(webgl2_read_buffer);
METHOD(webgl2_blit_framebuffer);
METHOD(webgl2_framebuffer_texture_layer);
METHOD(webgl2_invalidate_framebuffer);
METHOD(webgl2_invalidate_sub_framebuffer);
METHOD(webgl2_get_internalformat_parameter);
METHOD(webgl2_get_frag_data_location);

#undef METHOD

}  // namespace domapi

#endif /* XT_GUI_DOM_API_INTERNAL_H */
