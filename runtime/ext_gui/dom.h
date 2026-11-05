/*
 * xbintsc GUI engine — HTML parsing and the DOM tree.
 *
 * This is a deliberately small, self-written HTML subset: enough for a UI
 * description (elements, attributes, text, comments, raw-text elements such as
 * <style>) without the full HTML5 tree-construction algorithm. It never runs
 * scripts; `<script>` content is discarded, `<style>` content is kept so the
 * CSS pass can read it.
 */
#ifndef XT_GUI_DOM_H
#define XT_GUI_DOM_H

#include <memory>
#include <string>
#include <vector>

namespace xtgui {

enum class NodeType { Document, Element, Text };

/** Which generated-content box a synthetic pseudo-element node represents. */
enum class PseudoElement { None, Before, After };

struct Attr {
  std::string name;
  std::string value;
};

/** Editable/checkable state of a form control (`<input>`, `<textarea>`,
 * `<select>`). The browser keeps this in the element's internal value, separate
 * from the `value`/`checked` *attributes*; the engine does the same so
 * `<input value="a">` (the default) and `el.value = "b"` (the live value) do not
 * overwrite each other. */
struct FormState {
  std::string value;
  bool initialized = false;
  /** `checkbox`/`radio` state (the `checked` attribute is only the default). */
  bool checked = false;
  bool checked_initialized = false;
  /** Selection in UTF-16 code units, like the DOM's `selectionStart`/`End`. */
  int selection_start = 0;
  int selection_end = 0;
  /** 0 = none, 1 = forward, -1 = backward (`selectionDirection`). */
  int selection_direction = 0;
  bool focused = false;
  /** Text value when focus arrived, so `change` fires only if it changed. */
  std::string focus_value;
  bool focus_value_valid = false;
  /** IME composition: `composition` is displayed at the selection until the
   * `compositionend` commit inserts it into `value`. */
  bool composing = false;
  std::string composition;
  int composition_start = 0;
  int composition_end = 0;
  /** `<select>`: index of the selected `<option>`, or -1 when it has none. */
  int selected_index = -1;
  /** Range drag in progress (paint and pointer dispatch share it). */
  bool dragging = false;
  /** `<textarea>` first visible line (reserved; the engine paints from line 0). */
  int scroll_top = 0;
};

/** A DOM node. Element/text/document are one type to keep the tree simple. */
struct Node {
  NodeType type = NodeType::Element;
  std::string tag;   // lowercased element name (empty for document/text)
  std::string text;  // text content (text nodes only)
  /* `createElementNS` keeps the namespace and the qualified name's prefix.
   * Ordinary HTML elements leave both empty and report the XHTML namespace. */
  std::string ns;
  std::string ns_prefix;
  std::vector<Attr> attrs;
  Node *parent = nullptr;
  std::vector<std::unique_ptr<Node>> children;
  /** Form-control state, allocated only for `<input>`/`<textarea>`/`<select>`. */
  std::unique_ptr<FormState> form;

  /* -- generated content (`::before` / `::after`) -------------------------
   * A pseudo-element is a synthetic node owned by the document's per-restyle
   * arena, not by the DOM tree: it is never in `children`, so traversal,
   * queries, serialization and `elementIndex()` do not see it. `pseudo_origin`
   * is the element the selector matched and whose computed style it inherits.
   * `pseudo_before`/`pseudo_after` are transient pointers set by the cascade
   * for the layout pass (the arena is rebuilt on every restyle). */
  bool is_pseudo = false;
  PseudoElement pseudo = PseudoElement::None;
  const Node *pseudo_origin = nullptr;
  Node *pseudo_before = nullptr;
  Node *pseudo_after = nullptr;

  Node() = default;
  explicit Node(NodeType nodeType) : type(nodeType) {}

  bool isDocument() const { return type == NodeType::Document; }
  bool isElement() const { return type == NodeType::Element; }
  bool isText() const { return type == NodeType::Text; }

  /** Attribute value, or NULL when the attribute is absent. */
  const std::string *attr(const std::string &name) const;
  /** Element name comparison (`<div>` → `isTag("div")`). */
  bool isTag(const char *name) const;

  /** Create a child element/text node, append it and return it. */
  Node *addElement(const std::string &tag);
  Node *addText(const std::string &data);
  Node *append(std::unique_ptr<Node> child);

  /** Detach a direct child, transferring ownership to the caller. */
  std::unique_ptr<Node> detachChild(Node *child);
  /** Insert `child` before `reference` (append when `reference` is nullptr). */
  Node *insertChild(std::unique_ptr<Node> child, Node *reference);
  /** True when `node` is this node or a descendant of it. */
  bool contains(const Node *node) const;

  /** First element child (skips text), or NULL. */
  Node *firstElementChild() const;
  /** Total number of element children with the same tag, and this node's index. */
  int elementIndex() const;
  int elementSiblingCount() const;
};

/** Parse an HTML document/fragment. Returns a Document node (never NULL).
 * `synthesize_structure` wraps a fragment in the implicit `<html>`/`<head>`/
 * `<body>`; `innerHTML`/`insertAdjacentHTML` pass false because their result is
 * inserted under an existing element. */
std::unique_ptr<Node> xt_html_parse(const std::string &html, bool synthesize_structure = true);

/** Serialize a subtree; used by diagnostics and tests. */
std::string xt_dom_to_string(const Node *node);

/** A short CSS-like descriptor for a node, e.g. `div#main.card` (events and
 * diagnostics). Text/document nodes use `#text` / `#document`. */
std::string xt_dom_describe(const Node *node);

} // namespace xtgui

#endif /* XT_GUI_DOM_H */
