/*
 * xbintsc GUI engine — lazy construction of the DOM prototypes (node,
 * document, event, classList, style).
 */

#include "dom_api_internal.h"

namespace domapi {

static void define_method(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

static void define_getter(xt_value proto, const char *name, void *fn) {
  xt_object_define_getter(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

static void define_setter(xt_value proto, const char *name, void *fn) {
  xt_object_define_setter(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

xt_value node_proto() {
  xt_gc_add_root(&g_node_proto);
  if (XT_IS_OBJECT(g_node_proto)) return g_node_proto;
  xt_value proto = xt_object_new();
  /* Getters. */
  define_getter(proto, "tagName", (void *)node_tag_name_get);
  define_getter(proto, "nodeName", (void *)node_tag_name_get);
  define_getter(proto, "nodeType", (void *)node_node_type_get);
  define_getter(proto, "id", (void *)node_id_get);
  define_getter(proto, "className", (void *)node_class_name_get);
  define_getter(proto, "textContent", (void *)node_text_content_get);
  define_getter(proto, "innerText", (void *)node_text_content_get);
  define_getter(proto, "innerHTML", (void *)node_inner_html_get);
  define_getter(proto, "parentNode", (void *)node_parent_node_get);
  define_getter(proto, "parentElement", (void *)node_parent_element_get);
  define_getter(proto, "children", (void *)node_children_get);
  define_getter(proto, "childNodes", (void *)node_child_nodes_get);
  define_getter(proto, "childElementCount", (void *)node_child_element_count_get);
  define_getter(proto, "firstElementChild", (void *)node_first_element_child_get);
  define_getter(proto, "lastElementChild", (void *)node_last_element_child_get);
  define_getter(proto, "nextElementSibling", (void *)node_next_element_sibling_get);
  define_getter(proto, "previousElementSibling", (void *)node_previous_element_sibling_get);
  define_getter(proto, "isConnected", (void *)node_is_connected_get);
  define_getter(proto, "classList", (void *)node_class_list_get);
  define_getter(proto, "style", (void *)node_style_get);
  define_getter(proto, "offsetWidth", (void *)node_offset_width_get);
  define_getter(proto, "offsetHeight", (void *)node_offset_height_get);
  /* Media element properties (`<video>`/`<audio>`; harmless on any element). */
  define_getter(proto, "paused", (void *)node_paused_get);
  define_getter(proto, "ended", (void *)node_ended_get);
  define_getter(proto, "duration", (void *)node_duration_get);
  define_getter(proto, "currentTime", (void *)node_current_time_get);
  define_getter(proto, "volume", (void *)node_volume_get);
  define_getter(proto, "muted", (void *)node_muted_get);
  define_getter(proto, "loop", (void *)node_loop_get);
  define_getter(proto, "autoplay", (void *)node_autoplay_get);
  define_getter(proto, "videoWidth", (void *)node_video_width_get);
  define_getter(proto, "videoHeight", (void *)node_video_height_get);
  define_getter(proto, "readyState", (void *)node_ready_state_get);
  define_getter(proto, "src", (void *)node_media_src_get);
  /* `<canvas>` element properties (`width`/`height` also address the attribute
   * on other elements). */
  define_getter(proto, "width", (void *)node_width_get);
  define_getter(proto, "height", (void *)node_height_get);
  /* Setters. */
  define_setter(proto, "id", (void *)node_id_set);
  define_setter(proto, "className", (void *)node_class_name_set);
  define_setter(proto, "textContent", (void *)node_text_content_set);
  define_setter(proto, "innerText", (void *)node_text_content_set);
  define_setter(proto, "innerHTML", (void *)node_inner_html_set);
  define_setter(proto, "currentTime", (void *)node_current_time_set);
  define_setter(proto, "volume", (void *)node_volume_set);
  define_setter(proto, "muted", (void *)node_muted_set);
  define_setter(proto, "loop", (void *)node_loop_set);
  define_setter(proto, "autoplay", (void *)node_autoplay_set);
  define_setter(proto, "src", (void *)node_media_src_set);
  define_setter(proto, "width", (void *)node_width_set);
  define_setter(proto, "height", (void *)node_height_set);
  /* Methods. */
  define_method(proto, "getAttribute", (void *)node_get_attribute);
  define_method(proto, "setAttribute", (void *)node_set_attribute);
  define_method(proto, "removeAttribute", (void *)node_remove_attribute);
  define_method(proto, "hasAttribute", (void *)node_has_attribute);
  define_method(proto, "querySelector", (void *)node_query_selector);
  define_method(proto, "querySelectorAll", (void *)node_query_selector_all);
  define_method(proto, "matches", (void *)node_matches);
  define_method(proto, "appendChild", (void *)node_append_child);
  define_method(proto, "insertBefore", (void *)node_insert_before);
  define_method(proto, "removeChild", (void *)node_remove_child);
  define_method(proto, "replaceChild", (void *)node_replace_child);
  define_method(proto, "remove", (void *)node_remove);
  define_method(proto, "cloneNode", (void *)node_clone_node);
  define_method(proto, "getBoundingClientRect", (void *)node_get_bounding_rect);
  define_method(proto, "contains", (void *)node_contains);
  define_method(proto, "focus", (void *)node_focus);
  define_method(proto, "blur", (void *)node_blur);
  define_method(proto, "click", (void *)node_click);
  define_method(proto, "addEventListener", (void *)node_add_event_listener);
  define_method(proto, "removeEventListener", (void *)node_remove_event_listener);
  define_method(proto, "dispatchEvent", (void *)node_dispatch_event);
  define_method(proto, "play", (void *)node_play);
  define_method(proto, "pause", (void *)node_pause);
  define_method(proto, "load", (void *)node_load);
  define_method(proto, "getContext", (void *)node_get_context);
  g_node_proto = proto;
  return proto;
}

xt_value document_proto() {
  xt_gc_add_root(&g_document_proto);
  if (XT_IS_OBJECT(g_document_proto)) return g_document_proto;
  xt_value proto = xt_object_new_with_proto(node_proto());
  define_getter(proto, "body", (void *)doc_body_get);
  define_getter(proto, "documentElement", (void *)doc_document_element_get);
  define_method(proto, "querySelector", (void *)doc_query_selector);
  define_method(proto, "querySelectorAll", (void *)doc_query_selector_all);
  define_method(proto, "getElementById", (void *)doc_get_element_by_id);
  define_method(proto, "createElement", (void *)doc_create_element);
  define_method(proto, "createTextNode", (void *)doc_create_text_node);
  /* `document.addEventListener` / element listener plumbing come from the node
   * prototype and operate on the document root node. */
  define_method(proto, "addEventListener", (void *)node_add_event_listener);
  define_method(proto, "removeEventListener", (void *)node_remove_event_listener);
  define_method(proto, "dispatchEvent", (void *)node_dispatch_event);
  g_document_proto = proto;
  return proto;
}

xt_value event_proto() {
  xt_gc_add_root(&g_event_proto);
  if (XT_IS_OBJECT(g_event_proto)) return g_event_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "preventDefault", (void *)event_prevent_default);
  define_method(proto, "stopPropagation", (void *)event_stop_propagation);
  define_method(proto, "stopImmediatePropagation", (void *)event_stop_immediate_propagation);
  g_event_proto = proto;
  return proto;
}

xt_value class_list_proto() {
  xt_gc_add_root(&g_class_list_proto);
  if (XT_IS_OBJECT(g_class_list_proto)) return g_class_list_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "add", (void *)class_list_add);
  define_method(proto, "remove", (void *)class_list_remove);
  define_method(proto, "toggle", (void *)class_list_toggle);
  define_method(proto, "contains", (void *)class_list_contains);
  g_class_list_proto = proto;
  return proto;
}

xt_value style_proto() {
  xt_gc_add_root(&g_style_proto);
  if (XT_IS_OBJECT(g_style_proto)) return g_style_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "setProperty", (void *)style_set_property);
  define_method(proto, "getPropertyValue", (void *)style_get_property_value);
  define_method(proto, "removeProperty", (void *)style_remove_property);
  g_style_proto = proto;
  return proto;
}

xt_value canvas_proto() {
  xt_gc_add_root(&g_canvas_proto);
  if (XT_IS_OBJECT(g_canvas_proto)) return g_canvas_proto;
  xt_value proto = xt_object_new();
  /* Properties. */
  define_getter(proto, "canvas", (void *)canvas_get_canvas);
  define_getter(proto, "fillStyle", (void *)canvas_fill_style_get);
  define_setter(proto, "fillStyle", (void *)canvas_fill_style_set);
  define_getter(proto, "strokeStyle", (void *)canvas_stroke_style_get);
  define_setter(proto, "strokeStyle", (void *)canvas_stroke_style_set);
  define_getter(proto, "lineWidth", (void *)canvas_line_width_get);
  define_setter(proto, "lineWidth", (void *)canvas_line_width_set);
  define_getter(proto, "globalAlpha", (void *)canvas_global_alpha_get);
  define_setter(proto, "globalAlpha", (void *)canvas_global_alpha_set);
  define_getter(proto, "font", (void *)canvas_font_get);
  define_setter(proto, "font", (void *)canvas_font_set);
  define_getter(proto, "textAlign", (void *)canvas_text_align_get);
  define_setter(proto, "textAlign", (void *)canvas_text_align_set);
  define_getter(proto, "textBaseline", (void *)canvas_text_baseline_get);
  define_setter(proto, "textBaseline", (void *)canvas_text_baseline_set);
  define_getter(proto, "lineCap", (void *)canvas_line_cap_get);
  define_setter(proto, "lineCap", (void *)canvas_line_cap_set);
  define_getter(proto, "lineJoin", (void *)canvas_line_join_get);
  define_setter(proto, "lineJoin", (void *)canvas_line_join_set);
  define_getter(proto, "miterLimit", (void *)canvas_miter_limit_get);
  define_setter(proto, "miterLimit", (void *)canvas_miter_limit_set);
  define_getter(proto, "lineDashOffset", (void *)canvas_line_dash_offset_get);
  define_setter(proto, "lineDashOffset", (void *)canvas_line_dash_offset_set);
  /* Methods. */
  define_method(proto, "save", (void *)canvas_save);
  define_method(proto, "restore", (void *)canvas_restore);
  define_method(proto, "translate", (void *)canvas_translate);
  define_method(proto, "rotate", (void *)canvas_rotate);
  define_method(proto, "scale", (void *)canvas_scale);
  define_method(proto, "transform", (void *)canvas_transform);
  define_method(proto, "setTransform", (void *)canvas_set_transform);
  define_method(proto, "resetTransform", (void *)canvas_reset_transform);
  define_method(proto, "beginPath", (void *)canvas_begin_path);
  define_method(proto, "closePath", (void *)canvas_close_path);
  define_method(proto, "moveTo", (void *)canvas_move_to);
  define_method(proto, "lineTo", (void *)canvas_line_to);
  define_method(proto, "rect", (void *)canvas_rect);
  define_method(proto, "roundRect", (void *)canvas_round_rect);
  define_method(proto, "arc", (void *)canvas_arc);
  define_method(proto, "ellipse", (void *)canvas_ellipse);
  define_method(proto, "quadraticCurveTo", (void *)canvas_quadratic_curve_to);
  define_method(proto, "bezierCurveTo", (void *)canvas_bezier_curve_to);
  define_method(proto, "fill", (void *)canvas_fill);
  define_method(proto, "stroke", (void *)canvas_stroke);
  define_method(proto, "clearRect", (void *)canvas_clear_rect);
  define_method(proto, "fillRect", (void *)canvas_fill_rect);
  define_method(proto, "strokeRect", (void *)canvas_stroke_rect);
  define_method(proto, "measureText", (void *)canvas_measure_text);
  define_method(proto, "fillText", (void *)canvas_fill_text);
  define_method(proto, "strokeText", (void *)canvas_stroke_text);
  define_method(proto, "drawImage", (void *)canvas_draw_image);
  define_method(proto, "getImageData", (void *)canvas_get_image_data);
  define_method(proto, "setLineDash", (void *)canvas_line_dash_set);
  define_method(proto, "getLineDash", (void *)canvas_line_dash_get);
  define_method(proto, "getTransform", (void *)canvas_get_transform);
  define_method(proto, "createLinearGradient", (void *)canvas_create_linear_gradient);
  define_method(proto, "createRadialGradient", (void *)canvas_create_radial_gradient);
  g_canvas_proto = proto;
  return proto;
}

xt_value canvas_gradient_proto() {
  xt_gc_add_root(&g_canvas_gradient_proto);
  if (XT_IS_OBJECT(g_canvas_gradient_proto)) return g_canvas_gradient_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "addColorStop", (void *)canvas_gradient_add_color_stop);
  g_canvas_gradient_proto = proto;
  return proto;
}

}  // namespace domapi
