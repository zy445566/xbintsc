/*
 * xbintsc GUI engine — DOM node model.
 *
 * Defines the `Node` helpers shared by the HTML tree builder (`dom_parse.cpp`)
 * and the serializer (`dom_serialize.cpp`). The tree is deliberately simple:
 * document/element/text nodes are one type so the rest of the engine can walk
 * it without casts.
 */

#include "dom.h"

#include <memory>
#include <utility>

namespace xtgui {

/* -- Node helpers --------------------------------------------------------- */

const std::string *Node::attr(const std::string &name) const {
  for (const Attr &entry : attrs) {
    if (entry.name == name) return &entry.value;
  }
  return nullptr;
}

bool Node::isTag(const char *name) const {
  return type == NodeType::Element && tag == name;
}

Node *Node::append(std::unique_ptr<Node> child) {
  child->parent = this;
  Node *raw = child.get();
  children.push_back(std::move(child));
  return raw;
}

std::unique_ptr<Node> Node::detachChild(Node *child) {
  for (auto it = children.begin(); it != children.end(); ++it) {
    if (it->get() == child) {
      std::unique_ptr<Node> owned = std::move(*it);
      children.erase(it);
      owned->parent = nullptr;
      return owned;
    }
  }
  return nullptr;
}

Node *Node::insertChild(std::unique_ptr<Node> child, Node *reference) {
  child->parent = this;
  Node *raw = child.get();
  if (reference == nullptr) {
    children.push_back(std::move(child));
    return raw;
  }
  for (auto it = children.begin(); it != children.end(); ++it) {
    if (it->get() == reference) {
      children.insert(it, std::move(child));
      return raw;
    }
  }
  children.push_back(std::move(child));
  return raw;
}

bool Node::contains(const Node *node) const {
  for (const Node *current = node; current != nullptr; current = current->parent) {
    if (current == this) return true;
  }
  return false;
}

Node *Node::addElement(const std::string &tagName) {
  auto node = std::make_unique<Node>(NodeType::Element);
  node->tag = tagName;
  return append(std::move(node));
}

Node *Node::addText(const std::string &data) {
  if (data.empty()) return nullptr;
  auto node = std::make_unique<Node>(NodeType::Text);
  node->text = data;
  return append(std::move(node));
}

Node *Node::firstElementChild() const {
  for (const std::unique_ptr<Node> &child : children) {
    if (child->type == NodeType::Element) return child.get();
  }
  return nullptr;
}

int Node::elementIndex() const {
  if (parent == nullptr) return 0;
  int index = 0;
  for (const std::unique_ptr<Node> &sibling : parent->children) {
    if (sibling.get() == this) break;
    if (sibling->type == NodeType::Element) index++;
  }
  return index;
}

int Node::elementSiblingCount() const {
  if (parent == nullptr) return 1;
  int count = 0;
  for (const std::unique_ptr<Node> &sibling : parent->children) {
    if (sibling->type == NodeType::Element) count++;
  }
  return count;
}

}  // namespace xtgui
