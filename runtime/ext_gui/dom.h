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

struct Attr {
  std::string name;
  std::string value;
};

/** A DOM node. Element/text/document are one type to keep the tree simple. */
struct Node {
  NodeType type = NodeType::Element;
  std::string tag;   // lowercased element name (empty for document/text)
  std::string text;  // text content (text nodes only)
  std::vector<Attr> attrs;
  Node *parent = nullptr;
  std::vector<std::unique_ptr<Node>> children;

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

  /** First element child (skips text), or NULL. */
  Node *firstElementChild() const;
  /** Total number of element children with the same tag, and this node's index. */
  int elementIndex() const;
  int elementSiblingCount() const;
};

/** Parse an HTML document/fragment. Returns a Document node (never NULL). */
std::unique_ptr<Node> xt_html_parse(const std::string &html);

/** Serialize a subtree; used by diagnostics and tests. */
std::string xt_dom_to_string(const Node *node);

} // namespace xtgui

#endif /* XT_GUI_DOM_H */
