/*
 * xbintsc GUI engine — the parent/child convenience mutators.
 *
 * `append`, `prepend`, `replaceChildren` and `insertAdjacentHTML` are the APIs
 * modern pages actually use to build DOM (they accept nodes and plain strings
 * in one call); the engine previously only had `appendChild`/`innerHTML`.
 * Strings become text nodes, as in the DOM.
 */

#include "dom_api_internal.h"

#include <memory>
#include <string>
#include <vector>

namespace domapi {

namespace {

/** A node handle, or a new text node for a string argument. */
xtgui::Node *argument_node(XtGuiWindow *win, xt_value value) {
  xtgui::Node *node = resolve_node(value, nullptr);
  if (node != nullptr) return node;
  if (XT_IS_UNDEFINED(value) || value == XT_NULL) return nullptr;
  return win->document->createTextNode(to_string_value(value));
}

void insert_argument(XtGuiWindow *win, xtgui::Node *parent, xt_value value, xtgui::Node *before) {
  xtgui::Node *node = argument_node(win, value);
  if (node == nullptr) return;
  if (before != nullptr) {
    win->document->insertBefore(parent, node, before);
  } else {
    win->document->appendChild(parent, node);
  }
}

/** Copy a parsed fragment's children into `dest`, inserting before `before`
 * (nullptr appends). The parser's nodes belong to a standalone tree, so they are
 * recreated through the document rather than adopted. */
void import_children(XtGuiWindow *win, xtgui::Node *dest, const xtgui::Node *parsed,
                     xtgui::Node *before) {
  for (const std::unique_ptr<xtgui::Node> &child : parsed->children) {
    xtgui::Node *copy = nullptr;
    if (child->isText()) {
      copy = win->document->createTextNode(child->text);
    } else {
      copy = win->document->createElement(child->tag);
      if (copy == nullptr) continue;
      for (const xtgui::Attr &attr : child->attrs) copy->attrs.push_back(attr);
      import_children(win, copy, child.get(), nullptr);
    }
    if (copy == nullptr) continue;
    if (before != nullptr) {
      win->document->insertBefore(dest, copy, before);
    } else {
      win->document->appendChild(dest, copy);
    }
  }
}

xtgui::Node *next_sibling(xtgui::Node *parent, xtgui::Node *node) {
  if (parent == nullptr) return nullptr;
  for (size_t i = 0; i + 1 < parent->children.size(); i++) {
    if (parent->children[i].get() == node) return parent->children[i + 1].get();
  }
  return nullptr;
}

xtgui::Node *first_child(xtgui::Node *node) {
  return node->children.empty() ? nullptr : node->children.front().get();
}

}  // namespace

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(node_append) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr) return XT_UNDEFINED;
  for (int32_t i = 0; i < argc; i++) insert_argument(win, node, xt_arg(argc, argv, i), nullptr);
  mark_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_prepend) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr) return XT_UNDEFINED;
  /* The reference stays the original first child; inserting each argument before
   * it keeps the argument order. */
  xtgui::Node *reference = first_child(node);
  for (int32_t i = 0; i < argc; i++) {
    insert_argument(win, node, xt_arg(argc, argv, i), reference);
  }
  mark_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_replace_children) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  /* Detach node arguments first: one of them may be a current child that the
   * replacement keeps. */
  for (int32_t i = 0; i < argc; i++) {
    xtgui::Node *child = resolve_node(xt_arg(argc, argv, i), nullptr);
    if (child == nullptr) continue;
    if (child->parent == node) win->document->removeChild(node, child);
  }
  std::vector<xtgui::Node *> existing;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    existing.push_back(child.get());
  }
  for (xtgui::Node *child : existing) {
    win->document->removeChild(node, child);
    discard_subtree(win, child);
  }
  for (int32_t i = 0; i < argc; i++) insert_argument(win, node, xt_arg(argc, argv, i), nullptr);
  mark_dirty(win);
  return XT_UNDEFINED;
}

/** `insertAdjacentHTML(position, html)`; `position` is beforebegin,
 * afterbegin, beforeend or afterend. Scripts in the fragment do not run. */
METHOD(node_insert_adjacent_html) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr || argc < 2) {
    return XT_UNDEFINED;
  }
  std::string position = to_string_value(xt_arg(argc, argv, 0));
  std::string html = to_string_value(xt_arg(argc, argv, 1));
  std::unique_ptr<xtgui::Node> parsed = xtgui::xt_html_parse(html);
  if (!parsed) return XT_UNDEFINED;
  if (position == "beforebegin" || position == "afterend") {
    xtgui::Node *parent = node->parent;
    if (parent == nullptr || parent->isDocument()) return XT_UNDEFINED;
    xtgui::Node *reference = position == "beforebegin" ? node : next_sibling(parent, node);
    import_children(win, parent, parsed.get(), reference);
  } else if (position == "afterbegin") {
    import_children(win, node, parsed.get(), first_child(node));
  } else if (position == "beforeend") {
    import_children(win, node, parsed.get(), nullptr);
  } else {
    return XT_UNDEFINED;
  }
  mark_dirty(win);
  return XT_UNDEFINED;
}

#undef METHOD

}  // namespace domapi
