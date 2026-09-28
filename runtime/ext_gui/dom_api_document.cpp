/*
 * xbintsc GUI engine — `document` node methods (queries + element creation).
 */

#include "dom_api_internal.h"

#include <cctype>
#include <string>
#include <vector>

namespace domapi {

static const xtgui::Node *find_by_tag(const xtgui::Node *node, const std::string &tag) {
  if (node->isElement() && node->tag == tag) return node;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    if (const xtgui::Node *found = find_by_tag(child.get(), tag)) return found;
  }
  return nullptr;
}

static const xtgui::Node *find_by_id(const xtgui::Node *node, const std::string &id) {
  if (node->isElement()) {
    const std::string *value = node->attr("id");
    if (value != nullptr && *value == id) return node;
  }
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    if (const xtgui::Node *found = find_by_id(child.get(), id)) return found;
  }
  return nullptr;
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(doc_query_selector) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return XT_UNDEFINED;
  return xt_gui_node_handle(win, win->document->querySelector(to_string_value(xt_arg(argc, argv, 0))));
}

METHOD(doc_query_selector_all) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return xt_array_new(0, NULL);
  std::vector<const xtgui::Node *> matches =
      win->document->querySelectorAll(to_string_value(xt_arg(argc, argv, 0)));
  xt_value array = xt_array_new(0, NULL);
  for (const xtgui::Node *node : matches) xt_array_push(array, xt_gui_node_handle(win, node));
  return array;
}

METHOD(doc_get_element_by_id) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return XT_UNDEFINED;
  return xt_gui_node_handle(
      win, find_by_id(win->document->root(), to_string_value(xt_arg(argc, argv, 0))));
}

METHOD(doc_create_element) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return XT_UNDEFINED;
  std::string tag = to_string_value(xt_arg(argc, argv, 0));
  for (char &c : tag) c = (char)std::tolower((unsigned char)c);
  return xt_gui_node_handle(win, win->document->createElement(tag));
}

METHOD(doc_create_text_node) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return XT_UNDEFINED;
  return xt_gui_node_handle(win, win->document->createTextNode(to_string_value(xt_arg(argc, argv, 0))));
}

METHOD(doc_body_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  return xt_gui_node_handle(win, find_by_tag(win->document->root(), "body"));
}

METHOD(doc_document_element_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  return xt_gui_node_handle(win, find_by_tag(win->document->root(), "html"));
}

#undef METHOD

}  // namespace domapi
