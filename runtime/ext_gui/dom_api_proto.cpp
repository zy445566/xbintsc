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
  /* Setters. */
  define_setter(proto, "id", (void *)node_id_set);
  define_setter(proto, "className", (void *)node_class_name_set);
  define_setter(proto, "textContent", (void *)node_text_content_set);
  define_setter(proto, "innerText", (void *)node_text_content_set);
  define_setter(proto, "innerHTML", (void *)node_inner_html_set);
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
  g_node_proto = proto;
  return proto;
}

xt_value document_proto() {
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
  if (XT_IS_OBJECT(g_event_proto)) return g_event_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "preventDefault", (void *)event_prevent_default);
  define_method(proto, "stopPropagation", (void *)event_stop_propagation);
  define_method(proto, "stopImmediatePropagation", (void *)event_stop_immediate_propagation);
  g_event_proto = proto;
  return proto;
}

xt_value class_list_proto() {
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
  if (XT_IS_OBJECT(g_style_proto)) return g_style_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "setProperty", (void *)style_set_property);
  define_method(proto, "getPropertyValue", (void *)style_get_property_value);
  define_method(proto, "removeProperty", (void *)style_remove_property);
  g_style_proto = proto;
  return proto;
}

}  // namespace domapi
