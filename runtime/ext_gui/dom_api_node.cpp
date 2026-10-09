/*
 * xbintsc GUI engine — element handle methods (attributes, queries, tree
 * mutation, geometry reads, focus, events).
 */

#include "dom_api_internal.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace domapi {

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

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

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
  /* `replaceChild` frees the node it replaces, so its slot may not survive. */
  xtgui::Node *replaced = win->document->replaceChild(node, newChild, oldChild);
  if (replaced != oldChild) xt_gui_node_handle_forget(win, oldChild);
  return xt_gui_node_handle(win, replaced);
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

#undef METHOD

}  // namespace domapi
