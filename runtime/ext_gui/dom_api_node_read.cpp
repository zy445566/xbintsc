/*
 * xbintsc GUI engine — element handle getters/setters (`tagName`, `id`,
 * `className`, `textContent`, tree navigation, `classList`, `style`, ...).
 */

#include "dom_api_internal.h"

#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace domapi {

static std::string to_upper(std::string text) {
  for (char &c : text) c = (char)std::toupper((unsigned char)c);
  return text;
}

static void collect_text(const xtgui::Node *node, std::string &out) {
  if (node->isText()) {
    out += node->text;
    return;
  }
  for (const std::unique_ptr<xtgui::Node> &child : node->children) collect_text(child.get(), out);
}

/** Forget the handles of `node` and every descendant: they are about to be
 * freed, and a slot left behind would dangle. */
static void forget_subtree(XtGuiWindow *win, const xtgui::Node *node) {
  xt_gui_node_handle_forget(win, node);
  for (const std::unique_ptr<xtgui::Node> &child : node->children) forget_subtree(win, child.get());
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(node_tag_name_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  return xt_string_from_cstr(to_upper(node->tag).c_str());
}

METHOD(node_node_type_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr) return XT_UNDEFINED;
  if (node->isElement()) return xt_number(1);
  if (node->isText()) return xt_number(3);
  return xt_number(9);
}

METHOD(node_id_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement()) return xt_string_from_cstr("");
  const std::string *value = node->attr("id");
  return xt_string_from_cstr(value != nullptr ? value->c_str() : "");
}

METHOD(node_id_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  set_attribute(win, node, "id", argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "");
  return XT_UNDEFINED;
}

METHOD(node_class_name_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement()) return xt_string_from_cstr("");
  return xt_string_from_cstr(class_name_of(node).c_str());
}

METHOD(node_class_name_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  set_class_name(win, node, argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "");
  return XT_UNDEFINED;
}

METHOD(node_text_content_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr) return XT_UNDEFINED;
  std::string out;
  collect_text(node, out);
  return xt_string_from_cstr(out.c_str());
}

METHOD(node_text_content_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  std::string text = argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "";
  std::vector<xtgui::Node *> existing;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) existing.push_back(child.get());
  for (xtgui::Node *child : existing) win->document->removeChild(node, child);
  for (xtgui::Node *child : existing) forget_subtree(win, child);
  if (!text.empty()) node->addText(text);
  mark_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_inner_html_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr) return XT_UNDEFINED;
  std::string out;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    out += xtgui::xt_dom_to_string(child.get());
  }
  return xt_string_from_cstr(out.c_str());
}

METHOD(node_inner_html_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  std::string html = argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "";
  std::vector<xtgui::Node *> existing;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) existing.push_back(child.get());
  for (xtgui::Node *child : existing) win->document->removeChild(node, child);
  /* The old children are dropped here (only `removeChild` keeps a detached node
   * alive), so forget them *and everything below them* before they are freed:
   * a leftover slot would be a dangling pointer for the next handle lookup. */
  for (xtgui::Node *child : existing) forget_subtree(win, child);
  std::unique_ptr<xtgui::Node> fragment = xtgui::xt_html_parse(html);
  for (std::unique_ptr<xtgui::Node> &child : fragment->children) node->append(std::move(child));
  mark_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_parent_node_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || node->parent == nullptr || node->parent->isDocument()) return XT_UNDEFINED;
  return xt_gui_node_handle(win, node->parent);
}

METHOD(node_parent_element_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || node->parent == nullptr || !node->parent->isElement()) return XT_UNDEFINED;
  return xt_gui_node_handle(win, node->parent);
}

METHOD(node_children_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xt_value array = xt_array_new(0, NULL);
  if (node == nullptr) return array;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    if (child->isElement()) xt_array_push(array, xt_gui_node_handle(win, child.get()));
  }
  return array;
}

METHOD(node_child_nodes_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xt_value array = xt_array_new(0, NULL);
  if (node == nullptr) return array;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    xt_array_push(array, xt_gui_node_handle(win, child.get()));
  }
  return array;
}

METHOD(node_child_element_count_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr) return xt_number(0);
  double count = 0;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    if (child->isElement()) count += 1;
  }
  return xt_number(count);
}

METHOD(node_first_element_child_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr) return XT_UNDEFINED;
  return xt_gui_node_handle(win, node->firstElementChild());
}

METHOD(node_last_element_child_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr) return XT_UNDEFINED;
  for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
    if ((*it)->isElement()) return xt_gui_node_handle(win, it->get());
  }
  return XT_UNDEFINED;
}

METHOD(node_next_element_sibling_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || node->parent == nullptr) return XT_UNDEFINED;
  bool found = false;
  for (const std::unique_ptr<xtgui::Node> &sibling : node->parent->children) {
    if (sibling.get() == node) {
      found = true;
      continue;
    }
    if (found && sibling->isElement()) return xt_gui_node_handle(win, sibling.get());
  }
  return XT_UNDEFINED;
}

METHOD(node_previous_element_sibling_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || node->parent == nullptr) return XT_UNDEFINED;
  xtgui::Node *previous = nullptr;
  for (const std::unique_ptr<xtgui::Node> &sibling : node->parent->children) {
    if (sibling.get() == node) break;
    if (sibling->isElement()) previous = sibling.get();
  }
  return xt_gui_node_handle(win, previous);
}

METHOD(node_is_connected_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr) return XT_FALSE;
  return win->document->root()->contains(node) ? XT_TRUE : XT_FALSE;
}

METHOD(node_class_list_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_class_list");
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value list = xt_object_new_with_proto(class_list_proto());
  xt_object_set(list, xt_string_from_cstr("__xt_gui_node"), self);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_class_list"), list);
  return list;
}

METHOD(node_style_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_style");
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value style = xt_object_new_with_proto(style_proto());
  xt_object_set(style, xt_string_from_cstr("__xt_gui_node"), self);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_style"), style);
  return style;
}

#undef METHOD

}  // namespace domapi
