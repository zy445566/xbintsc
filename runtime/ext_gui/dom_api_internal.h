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

xt_value node_proto();
xt_value document_proto();
xt_value event_proto();
xt_value class_list_proto();
xt_value style_proto();

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

/* -- method definitions (uniform `(self, env, argc, argv)` ABI) ----------- */

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

/* document */
METHOD(doc_query_selector);
METHOD(doc_query_selector_all);
METHOD(doc_get_element_by_id);
METHOD(doc_create_element);
METHOD(doc_create_text_node);
METHOD(doc_body_get);
METHOD(doc_document_element_get);

/* node getters */
METHOD(node_tag_name_get);
METHOD(node_node_type_get);
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

/* event methods */
METHOD(event_prevent_default);
METHOD(event_stop_propagation);
METHOD(event_stop_immediate_propagation);

/* classList methods */
METHOD(class_list_add);
METHOD(class_list_remove);
METHOD(class_list_toggle);
METHOD(class_list_contains);

/* style methods */
METHOD(style_set_property);
METHOD(style_get_property_value);
METHOD(style_remove_property);

#undef METHOD

}  // namespace domapi

#endif /* XT_GUI_DOM_API_INTERNAL_H */
