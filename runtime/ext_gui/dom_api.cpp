/*
 * xbintsc GUI engine — the DOM object model exposed to TypeScript.
 *
 * Node/document handles are plain runtime objects with a shared prototype,
 * exactly like the window handles in `window.cpp`. Each handle carries hidden
 * properties:
 *
 *   __xt_gui_win   the owning window object (resolved with xt_gui_window_from_this)
 *   __xt_gui_gen   the document generation it was created for
 *   __xt_gui_node  an index into the window's node table (element handles)
 *   __xt_gui_doc   true for the singleton `document` handle
 *
 * A DOM mutation marks the document dirty; the frame loop restyles/relayouts
 * before the next paint.
 */

#include "dom_api.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

static xt_value g_node_proto = XT_UNDEFINED;
static xt_value g_document_proto = XT_UNDEFINED;
static xt_value g_event_proto = XT_UNDEFINED;
static xt_value g_class_list_proto = XT_UNDEFINED;
static xt_value g_style_proto = XT_UNDEFINED;

static xt_value node_proto();
static xt_value document_proto();
static xt_value event_proto();
static xt_value class_list_proto();
static xt_value style_proto();

/* -- small helpers -------------------------------------------------------- */

static const char *to_cstr(xt_value value, const char *fallback = "") {
  const char *data = xt_string_data(xt_to_string(value));
  return data != nullptr ? data : fallback;
}

static std::string to_string_value(xt_value value) { return std::string(to_cstr(value)); }

static std::string to_upper(std::string text) {
  for (char &c : text) c = (char)std::toupper((unsigned char)c);
  return text;
}

static bool value_truthy(xt_value value) { return xt_truthy(value) != 0; }

static void define_method(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

static void define_getter(xt_value proto, const char *name, void *fn) {
  xt_object_define_getter(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

static void define_setter(xt_value proto, const char *name, void *fn) {
  xt_object_define_setter(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

/* -- handle plumbing ------------------------------------------------------ */

static XtGuiWindow *resolve_window(xt_value self) {
  return xt_gui_window_from_this(xt_object_get_cstr(self, "__xt_gui_win"));
}

/** Resolve a node handle to its node, or NULL when it is stale/foreign. */
static xtgui::Node *resolve_node(xt_value self, XtGuiWindow **winOut) {
  XtGuiWindow *win = resolve_window(self);
  if (winOut != nullptr) *winOut = win;
  if (win == nullptr || win->document == nullptr) return nullptr;
  xt_value gen = xt_object_get_cstr(self, "__xt_gui_gen");
  if (!XT_IS_NUMBER(gen) || xt_to_number(gen) != win->doc_generation) return nullptr;
  if (value_truthy(xt_object_get_cstr(self, "__xt_gui_doc"))) {
    return const_cast<xtgui::Node *>(win->document->root());
  }
  xt_value index = xt_object_get_cstr(self, "__xt_gui_node");
  if (!XT_IS_NUMBER(index)) return nullptr;
  int slot = (int)xt_to_number(index);
  if (slot < 0 || slot >= (int)win->node_order.size()) return nullptr;
  return win->node_order[(size_t)slot];
}

xt_value xt_gui_node_handle(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return XT_UNDEFINED;
  auto cached = win->node_handles.find(node);
  if (cached != win->node_handles.end()) return cached->second;
  int slot;
  auto found = win->node_index.find(node);
  if (found != win->node_index.end()) {
    slot = found->second;
  } else {
    slot = (int)win->node_order.size();
    win->node_order.push_back(const_cast<xtgui::Node *>(node));
    win->node_index[node] = slot;
  }
  xt_value handle = xt_object_new_with_proto(node_proto());
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_win"), win->object);
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_gen"), xt_number(win->doc_generation));
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_node"), xt_number((double)slot));
  win->node_handles[node] = handle;
  return handle;
}

xt_value xt_gui_document_handle(XtGuiWindow *win) {
  if (win == nullptr) return XT_UNDEFINED;
  if (!XT_IS_OBJECT(win->document_object)) {
    xt_value handle = xt_object_new_with_proto(document_proto());
    xt_object_set(handle, xt_string_from_cstr("__xt_gui_win"), win->object);
    xt_object_set(handle, xt_string_from_cstr("__xt_gui_doc"), XT_TRUE);
    win->document_object = handle;
  }
  xt_object_set(win->document_object, xt_string_from_cstr("__xt_gui_gen"),
                xt_number(win->doc_generation));
  return win->document_object;
}

void xt_gui_handles_reset(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->doc_generation += 1.0;
  win->node_order.clear();
  win->node_index.clear();
  win->node_handles.clear();
  win->node_listeners.clear();
  win->struct_dirty = 0;
  if (XT_IS_OBJECT(win->document_object)) {
    xt_object_set(win->document_object, xt_string_from_cstr("__xt_gui_gen"),
                  xt_number(win->doc_generation));
  }
}

static void mark_dirty(XtGuiWindow *win) {
  if (win != nullptr && win->document != nullptr) win->document->invalidate();
  if (win != nullptr) win->struct_dirty = 1;
}

/* -- attribute / class / text helpers ------------------------------------- */

static void set_attribute_raw(xtgui::Node *node, const std::string &name, const std::string &value) {
  for (xtgui::Attr &attr : node->attrs) {
    if (attr.name == name) {
      attr.value = value;
      return;
    }
  }
  node->attrs.push_back({name, value});
}

static void remove_attribute_raw(xtgui::Node *node, const std::string &name) {
  for (auto it = node->attrs.begin(); it != node->attrs.end(); ++it) {
    if (it->name == name) {
      node->attrs.erase(it);
      return;
    }
  }
}

static void set_attribute(XtGuiWindow *win, xtgui::Node *node, const std::string &name,
                          const std::string &value) {
  set_attribute_raw(node, name, value);
  mark_dirty(win);
}

static void collect_text(const xtgui::Node *node, std::string &out) {
  if (node->isText()) {
    out += node->text;
    return;
  }
  for (const std::unique_ptr<xtgui::Node> &child : node->children) collect_text(child.get(), out);
}

static std::vector<std::string> split_classes(const std::string &text) {
  std::vector<std::string> out;
  size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && std::isspace((unsigned char)text[index])) index++;
    size_t start = index;
    while (index < text.size() && !std::isspace((unsigned char)text[index])) index++;
    if (index > start) out.push_back(text.substr(start, index - start));
  }
  return out;
}

static std::string join_classes(const std::vector<std::string> &names) {
  std::string out;
  for (size_t i = 0; i < names.size(); i++) {
    if (i > 0) out += " ";
    out += names[i];
  }
  return out;
}

static std::string class_name_of(const xtgui::Node *node) {
  const std::string *value = node->attr("class");
  return value != nullptr ? *value : std::string();
}

static void set_class_name(XtGuiWindow *win, xtgui::Node *node, const std::string &value) {
  if (value.empty()) {
    remove_attribute_raw(node, "class");
    mark_dirty(win);
  } else {
    set_attribute(win, node, "class", value);
  }
}

/* -- inline style helpers ------------------------------------------------- */

static std::string inline_style_of(const xtgui::Node *node) {
  const std::string *value = node->attr("style");
  return value != nullptr ? *value : std::string();
}

static void write_declarations(xtgui::Node *node, const std::vector<xtgui::Declaration> &decls) {
  std::string out;
  for (const xtgui::Declaration &decl : decls) {
    if (!out.empty()) out += ";";
    out += decl.property + ":" + decl.value + (decl.important ? " !important" : "");
  }
  if (out.empty()) {
    remove_attribute_raw(node, "style");
  } else {
    set_attribute_raw(node, "style", out);
  }
}

static void set_style_property(XtGuiWindow *win, xtgui::Node *node, const std::string &name,
                               const std::string &value) {
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  bool found = false;
  for (xtgui::Declaration &decl : decls) {
    if (decl.property == name) {
      decl.value = value;
      decl.important = false;
      found = true;
    }
  }
  if (!found) decls.push_back({name, value, false});
  write_declarations(node, decls);
  mark_dirty(win);
}

static std::string get_style_property(const xtgui::Node *node, const std::string &name) {
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  std::string value;
  for (const xtgui::Declaration &decl : decls) {
    if (decl.property == name) value = decl.value;
  }
  return value;
}

static void remove_style_property(XtGuiWindow *win, xtgui::Node *node, const std::string &name) {
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  std::vector<xtgui::Declaration> kept;
  for (const xtgui::Declaration &decl : decls) {
    if (decl.property != name) kept.push_back(decl);
  }
  write_declarations(node, kept);
  mark_dirty(win);
}

/* -- queries -------------------------------------------------------------- */

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

static void collect_matches(const xtgui::Node *node,
                            const std::vector<xtgui::ComplexSelector> &selectors,
                            std::vector<const xtgui::Node *> &out) {
  if (node->isElement()) {
    for (const xtgui::ComplexSelector &selector : selectors) {
      if (xtgui::xt_css_match(node, selector)) {
        out.push_back(node);
        break;
      }
    }
  }
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    collect_matches(child.get(), selectors, out);
  }
}

/* -- event machinery ------------------------------------------------------ */

static xt_value make_dom_event(XtGuiWindow *win, const char *type, const xtgui::Node *target,
                               xt_value legacy) {
  xt_value event = xt_object_new_with_proto(event_proto());
  xt_object_set(event, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  xt_object_set(event, xt_string_from_cstr("target"), xt_gui_node_handle(win, target));
  xt_object_set(event, xt_string_from_cstr("defaultPrevented"), XT_FALSE);
  xt_object_set(event, xt_string_from_cstr("bubbles"), XT_TRUE);
  if (XT_IS_OBJECT(legacy)) {
    static const char *const fields[] = {
        "x",     "y",    "clientX", "clientY", "button", "clicks",
        "deltaX", "deltaY", "key",  "code",    "repeat", "ctrl",
        "shift", "alt",  "meta",
    };
    for (const char *field : fields) {
      xt_value value = xt_object_get_cstr(legacy, field);
      if (!XT_IS_UNDEFINED(value)) xt_object_set(event, xt_string_from_cstr(field), value);
    }
  }
  return event;
}

static void remove_listener(XtGuiWindow *win, xtgui::Node *node, const std::string &type,
                            xt_value fn, bool capture) {
  auto found = win->node_listeners.find(node);
  if (found == win->node_listeners.end()) return;
  std::vector<XtGuiNodeListener> &list = found->second;
  for (auto it = list.begin(); it != list.end(); ++it) {
    if (it->type == type && it->capture == capture && xt_truthy(xt_seq(it->fn, fn))) {
      list.erase(it);
      return;
    }
  }
}

static bool call_node_listeners(XtGuiWindow *win, xtgui::Node *node, const char *type,
                                xt_value event, bool capture) {
  auto found = win->node_listeners.find(node);
  if (found == win->node_listeners.end()) return false;
  std::vector<XtGuiNodeListener> snapshot = found->second;
  for (const XtGuiNodeListener &listener : snapshot) {
    if (listener.type != type || listener.capture != capture) continue;
    if (listener.once) remove_listener(win, node, listener.type, listener.fn, listener.capture);
    xt_value self = xt_gui_node_handle(win, node);
    xt_object_set(event, xt_string_from_cstr("currentTarget"), self);
    xt_value args[1] = {event};
    xt_call_with_this(listener.fn, self, 1, args);
    if (value_truthy(xt_object_get_cstr(event, "__stopImmediate"))) return true;
    if (value_truthy(xt_object_get_cstr(event, "__stop"))) return true;
  }
  return value_truthy(xt_object_get_cstr(event, "__stop"));
}

void xt_gui_emit_dom_event(XtGuiWindow *win, const char *type, const xtgui::Node *target,
                           xt_value legacy) {
  if (win == nullptr || win->document == nullptr || target == nullptr) return;
  if (win->node_listeners.empty()) return;
  std::vector<xtgui::Node *> path;
  for (xtgui::Node *node = const_cast<xtgui::Node *>(target); node != nullptr; node = node->parent) {
    path.push_back(node);
  }
  xt_value event = make_dom_event(win, type, target, legacy);
  /* Capture phase: root -> target. */
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    if (call_node_listeners(win, *it, type, event, true)) return;
  }
  /* Bubble phase: target -> root. */
  for (auto it = path.begin(); it != path.end(); ++it) {
    if (call_node_listeners(win, *it, type, event, false)) return;
  }
}

void xt_gui_dispatch_to_node(XtGuiWindow *win, const xtgui::Node *target, const char *type) {
  if (win == nullptr || target == nullptr) return;
  xt_value legacy = xt_object_new();
  xt_object_set(legacy, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  std::string describe = xtgui::xt_dom_describe(target);
  xt_object_set(legacy, xt_string_from_cstr("target"), xt_string_from_cstr(describe.c_str()));
  xt_gui_emit_dom_event(win, type, target, legacy);
}

/* -- document methods ----------------------------------------------------- */

#define METHOD(name) static xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

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

/* -- node getters --------------------------------------------------------- */

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

/* -- node methods --------------------------------------------------------- */

METHOD(node_get_attribute) {
  (void)env;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement() || argc < 1) return XT_UNDEFINED;
  const std::string *value = node->attr(to_string_value(xt_arg(argc, argv, 0)));
  if (value == nullptr) return XT_UNDEFINED;
  return xt_string_from_cstr(value->c_str());
}

METHOD(node_set_attribute) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement() || argc < 2) return XT_UNDEFINED;
  set_attribute(win, node, to_string_value(xt_arg(argc, argv, 0)),
                to_string_value(xt_arg(argc, argv, 1)));
  return XT_UNDEFINED;
}

METHOD(node_remove_attribute) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement() || argc < 1) return XT_UNDEFINED;
  remove_attribute_raw(node, to_string_value(xt_arg(argc, argv, 0)));
  mark_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_has_attribute) {
  (void)env;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement() || argc < 1) return XT_FALSE;
  return node->attr(to_string_value(xt_arg(argc, argv, 0))) != nullptr ? XT_TRUE : XT_FALSE;
}

METHOD(node_query_selector) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || argc < 1) return XT_UNDEFINED;
  std::vector<xtgui::ComplexSelector> selectors =
      xtgui::xt_css_parse_selector_list(to_string_value(xt_arg(argc, argv, 0)));
  if (selectors.empty()) return XT_UNDEFINED;
  std::vector<const xtgui::Node *> matches;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    collect_matches(child.get(), selectors, matches);
  }
  return matches.empty() ? XT_UNDEFINED : xt_gui_node_handle(win, matches.front());
}

METHOD(node_query_selector_all) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xt_value array = xt_array_new(0, NULL);
  if (node == nullptr || argc < 1) return array;
  std::vector<xtgui::ComplexSelector> selectors =
      xtgui::xt_css_parse_selector_list(to_string_value(xt_arg(argc, argv, 0)));
  std::vector<const xtgui::Node *> matches;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    collect_matches(child.get(), selectors, matches);
  }
  for (const xtgui::Node *match : matches) xt_array_push(array, xt_gui_node_handle(win, match));
  return array;
}

METHOD(node_matches) {
  (void)env;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement() || argc < 1) return XT_FALSE;
  std::vector<xtgui::ComplexSelector> selectors =
      xtgui::xt_css_parse_selector_list(to_string_value(xt_arg(argc, argv, 0)));
  for (const xtgui::ComplexSelector &selector : selectors) {
    if (xtgui::xt_css_match(node, selector)) return XT_TRUE;
  }
  return XT_FALSE;
}

METHOD(node_append_child) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || argc < 1) return XT_UNDEFINED;
  xtgui::Node *child = resolve_node(xt_arg(argc, argv, 0), nullptr);
  if (child == nullptr) return XT_UNDEFINED;
  return xt_gui_node_handle(win, win->document->appendChild(node, child));
}

METHOD(node_insert_before) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || argc < 1) return XT_UNDEFINED;
  xtgui::Node *child = resolve_node(xt_arg(argc, argv, 0), nullptr);
  xtgui::Node *reference = argc >= 2 ? resolve_node(xt_arg(argc, argv, 1), nullptr) : nullptr;
  if (child == nullptr) return XT_UNDEFINED;
  return xt_gui_node_handle(win, win->document->insertBefore(node, child, reference));
}

METHOD(node_remove_child) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || argc < 1) return XT_UNDEFINED;
  xtgui::Node *child = resolve_node(xt_arg(argc, argv, 0), nullptr);
  if (child == nullptr) return XT_UNDEFINED;
  win->document->removeChild(node, child);
  return xt_gui_node_handle(win, child);
}

METHOD(node_replace_child) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || argc < 2) return XT_UNDEFINED;
  xtgui::Node *newChild = resolve_node(xt_arg(argc, argv, 0), nullptr);
  xtgui::Node *oldChild = resolve_node(xt_arg(argc, argv, 1), nullptr);
  if (newChild == nullptr || oldChild == nullptr) return XT_UNDEFINED;
  return xt_gui_node_handle(win, win->document->replaceChild(node, newChild, oldChild));
}

METHOD(node_remove) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr) return XT_UNDEFINED;
  win->document->detach(node);
  win->struct_dirty = 1;
  return XT_UNDEFINED;
}

static xtgui::Node *clone_node(XtGuiWindow *win, const xtgui::Node *source, bool deep) {
  if (source->isText()) return win->document->createTextNode(source->text);
  if (!source->isElement()) return nullptr;
  xtgui::Node *copy = win->document->createElement(source->tag);
  for (const xtgui::Attr &attr : source->attrs) copy->attrs.push_back(attr);
  if (deep) {
    for (const std::unique_ptr<xtgui::Node> &child : source->children) {
      xtgui::Node *childCopy = clone_node(win, child.get(), true);
      if (childCopy != nullptr) win->document->appendChild(copy, childCopy);
    }
  }
  return copy;
}

METHOD(node_clone_node) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr) return XT_UNDEFINED;
  bool deep = argc >= 1 ? value_truthy(xt_arg(argc, argv, 0)) : false;
  return xt_gui_node_handle(win, clone_node(win, node, deep));
}

METHOD(node_get_bounding_rect) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  const xtgui::LayoutBox *box = win->document->boxOf(node);
  if (box == nullptr) return XT_UNDEFINED;
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("x"), xt_number(box->x));
  xt_object_set(object, xt_string_from_cstr("y"), xt_number(box->y));
  xt_object_set(object, xt_string_from_cstr("width"), xt_number(box->width));
  xt_object_set(object, xt_string_from_cstr("height"), xt_number(box->height));
  return object;
}

/** Laid-out border-box size, rounded like the browser `offsetWidth`/
 * `offsetHeight` (0 when the element has no box yet). Applies pending
 * mutations first so a read after a write is up to date. */
static xt_value node_offset_dimension(xt_value self, bool width) {
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr) return xt_number(0);
  xt_gui_flush_dom(win);
  const xtgui::LayoutBox *box = win->document->boxOf(node);
  if (box == nullptr) return xt_number(0);
  return xt_number((double)std::lround(width ? box->width : box->height));
}

METHOD(node_offset_width_get) {
  (void)env;
  (void)argc;
  (void)argv;
  return node_offset_dimension(self, true);
}

METHOD(node_offset_height_get) {
  (void)env;
  (void)argc;
  (void)argv;
  return node_offset_dimension(self, false);
}

METHOD(node_contains) {
  (void)env;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || argc < 1) return XT_FALSE;
  xtgui::Node *other = resolve_node(xt_arg(argc, argv, 0), nullptr);
  if (other == nullptr) return XT_FALSE;
  return node->contains(other) ? XT_TRUE : XT_FALSE;
}

METHOD(node_focus) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  if (win->document->setFocus(node)) win->geometry.dirty = 1;
  return XT_UNDEFINED;
}

METHOD(node_blur) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  if (win->document->focus() == node && win->document->setFocus(nullptr)) win->geometry.dirty = 1;
  return XT_UNDEFINED;
}

METHOD(node_click) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr) return XT_UNDEFINED;
  xt_gui_dispatch_to_node(win, node, "click");
  return XT_UNDEFINED;
}

METHOD(node_add_event_listener) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || argc < 2) return self;
  xt_value fn = xt_arg(argc, argv, 1);
  if (!XT_IS_FUNCTION(fn)) return self;
  bool capture = false;
  bool once = false;
  xt_value options = xt_arg(argc, argv, 2);
  if (XT_IS_OBJECT(options)) {
    capture = value_truthy(xt_object_get_cstr(options, "capture"));
    once = value_truthy(xt_object_get_cstr(options, "once"));
  } else if (XT_IS_BOOL(options)) {
    capture = value_truthy(options);
  }
  XtGuiNodeListener listener;
  listener.type = to_string_value(xt_arg(argc, argv, 0));
  listener.fn = fn;
  listener.once = once;
  listener.capture = capture;
  win->node_listeners[node].push_back(listener);
  return self;
}

METHOD(node_remove_event_listener) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || argc < 2) return self;
  bool capture = false;
  xt_value options = xt_arg(argc, argv, 2);
  if (XT_IS_OBJECT(options)) capture = value_truthy(xt_object_get_cstr(options, "capture"));
  else if (XT_IS_BOOL(options)) capture = value_truthy(options);
  remove_listener(win, node, to_string_value(xt_arg(argc, argv, 0)), xt_arg(argc, argv, 1), capture);
  return self;
}

METHOD(node_dispatch_event) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || argc < 1) return XT_FALSE;
  xt_value event = xt_arg(argc, argv, 0);
  std::string type = XT_IS_OBJECT(event) ? to_string_value(xt_object_get_cstr(event, "type"))
                                         : to_string_value(event);
  xt_gui_emit_dom_event(win, type.c_str(), node, event);
  return XT_TRUE;
}

/* -- event methods -------------------------------------------------------- */

METHOD(event_prevent_default) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_object_set(self, xt_string_from_cstr("defaultPrevented"), XT_TRUE);
  return XT_UNDEFINED;
}

METHOD(event_stop_propagation) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_object_set(self, xt_string_from_cstr("__stop"), XT_TRUE);
  return XT_UNDEFINED;
}

METHOD(event_stop_immediate_propagation) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_object_set(self, xt_string_from_cstr("__stop"), XT_TRUE);
  xt_object_set(self, xt_string_from_cstr("__stopImmediate"), XT_TRUE);
  return XT_UNDEFINED;
}

/* -- classList methods ---------------------------------------------------- */

static xtgui::Node *class_list_node(xt_value self, XtGuiWindow **winOut) {
  return resolve_node(xt_object_get_cstr(self, "__xt_gui_node"), winOut);
}

METHOD(class_list_add) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = class_list_node(self, &win);
  if (node == nullptr) return self;
  std::vector<std::string> names = split_classes(class_name_of(node));
  bool changed = false;
  for (int32_t i = 0; i < argc; i++) {
    std::string name = to_string_value(xt_arg(argc, argv, i));
    if (name.empty() || std::find(names.begin(), names.end(), name) != names.end()) continue;
    names.push_back(name);
    changed = true;
  }
  if (changed) set_class_name(win, node, join_classes(names));
  return self;
}

METHOD(class_list_remove) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = class_list_node(self, &win);
  if (node == nullptr) return self;
  std::vector<std::string> names = split_classes(class_name_of(node));
  bool changed = false;
  for (int32_t i = 0; i < argc; i++) {
    std::string name = to_string_value(xt_arg(argc, argv, i));
    auto it = std::find(names.begin(), names.end(), name);
    if (it != names.end()) {
      names.erase(it);
      changed = true;
    }
  }
  if (changed) set_class_name(win, node, join_classes(names));
  return self;
}

METHOD(class_list_toggle) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = class_list_node(self, &win);
  if (node == nullptr || argc < 1) return XT_FALSE;
  std::string name = to_string_value(xt_arg(argc, argv, 0));
  std::vector<std::string> names = split_classes(class_name_of(node));
  auto it = std::find(names.begin(), names.end(), name);
  if (it != names.end()) {
    names.erase(it);
    set_class_name(win, node, join_classes(names));
    return XT_FALSE;
  }
  if (!name.empty()) {
    names.push_back(name);
    set_class_name(win, node, join_classes(names));
  }
  return XT_TRUE;
}

METHOD(class_list_contains) {
  (void)env;
  xtgui::Node *node = class_list_node(self, nullptr);
  if (node == nullptr || argc < 1) return XT_FALSE;
  std::string name = to_string_value(xt_arg(argc, argv, 0));
  std::vector<std::string> names = split_classes(class_name_of(node));
  return std::find(names.begin(), names.end(), name) != names.end() ? XT_TRUE : XT_FALSE;
}

/* -- style methods -------------------------------------------------------- */

static xtgui::Node *style_node(xt_value self, XtGuiWindow **winOut) {
  return resolve_node(xt_object_get_cstr(self, "__xt_gui_node"), winOut);
}

METHOD(style_set_property) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = style_node(self, &win);
  if (node == nullptr || argc < 2) return XT_UNDEFINED;
  set_style_property(win, node, to_string_value(xt_arg(argc, argv, 0)),
                     to_string_value(xt_arg(argc, argv, 1)));
  return XT_UNDEFINED;
}

METHOD(style_get_property_value) {
  (void)env;
  xtgui::Node *node = style_node(self, nullptr);
  if (node == nullptr || argc < 1) return xt_string_from_cstr("");
  return xt_string_from_cstr(
      get_style_property(node, to_string_value(xt_arg(argc, argv, 0))).c_str());
}

METHOD(style_remove_property) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = style_node(self, &win);
  if (node == nullptr || argc < 1) return XT_UNDEFINED;
  remove_style_property(win, node, to_string_value(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

/* -- prototype construction (lazy) ---------------------------------------- */

static xt_value node_proto() {
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

static xt_value document_proto() {
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

static xt_value event_proto() {
  if (XT_IS_OBJECT(g_event_proto)) return g_event_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "preventDefault", (void *)event_prevent_default);
  define_method(proto, "stopPropagation", (void *)event_stop_propagation);
  define_method(proto, "stopImmediatePropagation", (void *)event_stop_immediate_propagation);
  g_event_proto = proto;
  return proto;
}

static xt_value class_list_proto() {
  if (XT_IS_OBJECT(g_class_list_proto)) return g_class_list_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "add", (void *)class_list_add);
  define_method(proto, "remove", (void *)class_list_remove);
  define_method(proto, "toggle", (void *)class_list_toggle);
  define_method(proto, "contains", (void *)class_list_contains);
  g_class_list_proto = proto;
  return proto;
}

static xt_value style_proto() {
  if (XT_IS_OBJECT(g_style_proto)) return g_style_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "setProperty", (void *)style_set_property);
  define_method(proto, "getPropertyValue", (void *)style_get_property_value);
  define_method(proto, "removeProperty", (void *)style_remove_property);
  g_style_proto = proto;
  return proto;
}
