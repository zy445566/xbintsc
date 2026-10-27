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
  if (win == nullptr || argc < 1) return XT_UNDEFINED;
  std::string selector = to_string_value(xt_arg(argc, argv, 0));
  const xtgui::Node *node =
      win->document != nullptr ? win->document->querySelector(selector) : nullptr;
  if (node != nullptr) return xt_gui_node_handle(win, node);
  /* A page that is already parsed has no such element: answer undefined, like
   * the browser. Only a query made *before* `loadHTML` earns a pending handle
   * that binds once the tree exists. */
  if (win != nullptr && !win->html.empty()) return XT_UNDEFINED;
  return pending_handle(win, pending_by_selector, selector);
}

/* -- pending lookup helpers (see `dom_api.cpp`) --------------------------- */

xtgui::Node *document_find_by_id(XtGuiWindow *win, const std::string &id) {
  if (win == nullptr || win->document == nullptr) return nullptr;
  return const_cast<xtgui::Node *>(find_by_id(win->document->root(), id));
}

xtgui::Node *document_query_selector(XtGuiWindow *win, const std::string &selector) {
  if (win == nullptr || win->document == nullptr) return nullptr;
  return const_cast<xtgui::Node *>(win->document->querySelector(selector));
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
  if (win == nullptr || argc < 1) return XT_UNDEFINED;
  std::string id = to_string_value(xt_arg(argc, argv, 0));
  const xtgui::Node *node =
      win->document != nullptr ? find_by_id(win->document->root(), id) : nullptr;
  if (node != nullptr) return xt_gui_node_handle(win, node);
  if (win != nullptr && !win->html.empty()) return XT_UNDEFINED;
  /* The page has not been loaded yet (a module queried the id while it
   * initialized): answer with a pending handle that binds to the real node once
   * the document is parsed. */
  return pending_handle(win, pending_by_id, id);
}

METHOD(doc_create_element) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return XT_UNDEFINED;
  std::string tag = to_string_value(xt_arg(argc, argv, 0));
  for (char &c : tag) c = (char)std::tolower((unsigned char)c);
  return xt_gui_node_handle(win, win->document->createElement(tag));
}

/** `createElementNS(ns, name)`: the namespace is recorded (so `namespaceURI`
 * answers it) and the qualified name's prefix is preserved. HTML names are
 * lowercased, but names in another namespace keep their case — SVG is
 * case-sensitive (`linearGradient`, `clipPath`). */
METHOD(doc_create_element_ns) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || argc < 2) return XT_UNDEFINED;
  std::string ns = to_string_value(xt_arg(argc, argv, 0));
  std::string name = to_string_value(xt_arg(argc, argv, 1));
  std::string prefix;
  size_t colon = name.rfind(':');
  if (colon != std::string::npos) {
    prefix = name.substr(0, colon);
    name = name.substr(colon + 1);
  }
  const bool html = ns.empty() || ns == "http://www.w3.org/1999/xhtml";
  if (html) {
    for (char &c : name) c = (char)std::tolower((unsigned char)c);
  }
  xtgui::Node *node = win->document->createElement(name);
  if (node == nullptr) return XT_UNDEFINED;
  node->ns = ns;
  node->ns_prefix = prefix;
  return xt_gui_node_handle(win, node);
}

/** `document.hidden` / `visibilityState`: a window in the main loop is always
 * visible, and reporting hidden would make a page pause its own loop. */
METHOD(doc_hidden_get) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_FALSE;
}

METHOD(doc_visibility_state_get) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_string_from_cstr("visible");
}

/** `document.readyState`: the page is fully parsed before any script runs. */
METHOD(doc_ready_state_get) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_string_from_cstr("complete");
}

METHOD(doc_title_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr) return xt_string_from_cstr("");
  const xtgui::Node *title = find_by_tag(win->document->root(), "title");
  std::string text;
  if (title != nullptr) {
    for (const std::unique_ptr<xtgui::Node> &child : title->children) {
      if (!child->isElement()) text += child->text;
    }
  }
  return xt_string_from_cstr(text.c_str());
}

/** `document.title = text`: replace the text of the document's `<title>`.
 * A page without one gets a `<title>` (and a `<head>`, if the parser did not
 * create one), so the assignment is not silently lost. */
METHOD(doc_title_set) {
  (void)env;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  std::string text;
  if (argc >= 1) text = to_string_value(xt_arg(argc, argv, 0));
  xtgui::Node *root = const_cast<xtgui::Node *>(win->document->root());
  xtgui::Node *title = const_cast<xtgui::Node *>(find_by_tag(root, "title"));
  if (title == nullptr) {
    xtgui::Node *head = const_cast<xtgui::Node *>(find_by_tag(root, "head"));
    if (head == nullptr) {
      head = win->document->createElement("head");
      if (head == nullptr) return XT_UNDEFINED;
      /* `<head>` belongs before the body, not after it. */
      xtgui::Node *body = const_cast<xtgui::Node *>(find_by_tag(root, "body"));
      win->document->insertBefore(root, head, body);
    }
    title = win->document->createElement("title");
    if (title == nullptr) return XT_UNDEFINED;
    win->document->appendChild(head, title);
  }
  title->children.clear();
  if (!text.empty()) title->addText(text);
  mark_dirty(win);
  return XT_UNDEFINED;
}

METHOD(doc_default_view_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = resolve_window(self);
  return win != nullptr ? win->object : XT_UNDEFINED;
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
