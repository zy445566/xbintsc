/*
 * xbintsc GUI engine — DOM serialization and diagnostics.
 */

#include "dom_internal.h"

#include <cctype>
#include <string>

namespace xtgui {

static void serialize(const Node *node, std::string &out) {
  if (node->type == NodeType::Document) {
    for (const std::unique_ptr<Node> &child : node->children) serialize(child.get(), out);
    return;
  }
  if (node->type == NodeType::Text) {
    out += xt_dom_trim(node->text);
    return;
  }
  out += "<" + node->tag;
  for (const Attr &attr : node->attrs) out += " " + attr.name + "=\"" + attr.value + "\"";
  out += ">";
  for (const std::unique_ptr<Node> &child : node->children) serialize(child.get(), out);
  out += "</" + node->tag + ">";
}

std::string xt_dom_to_string(const Node *node) {
  std::string out;
  if (node != nullptr) serialize(node, out);
  return out;
}

std::string xt_dom_describe(const Node *node) {
  if (node == nullptr) return "";
  if (node->isText()) return "#text";
  if (!node->isElement()) return "#document";
  std::string out = node->tag;
  const std::string *id = node->attr("id");
  if (id != nullptr && !id->empty()) out += "#" + *id;
  const std::string *classes = node->attr("class");
  if (classes != nullptr) {
    size_t index = 0;
    while (index < classes->size()) {
      while (index < classes->size() && std::isspace((unsigned char)(*classes)[index])) index++;
      size_t start = index;
      while (index < classes->size() && !std::isspace((unsigned char)(*classes)[index])) index++;
      if (index > start) out += "." + classes->substr(start, index - start);
    }
  }
  return out;
}

}  // namespace xtgui
