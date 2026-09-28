/*
 * xbintsc GUI engine — CSS selector matching against the DOM.
 */

#include "css_internal.h"

#include <cstdlib>
#include <memory>

namespace xtgui {

namespace {

bool hasClass(const Node *node, const std::string &name) {
  const std::string *value = node->attr("class");
  if (value == nullptr) return false;
  for (const std::string &token : xt_css_split_whitespace(*value)) {
    if (token == name) return true;
  }
  return false;
}

bool matchAttribute(const Node *node, const AttrSelector &attr) {
  const std::string *value = node->attr(attr.name);
  if (value == nullptr) return false;
  if (attr.op.empty()) return true;
  if (attr.op == "=") return *value == attr.value;
  if (attr.op == "~=") {
    for (const std::string &token : xt_css_split_whitespace(*value)) {
      if (token == attr.value) return true;
    }
    return false;
  }
  if (attr.op == "|=") {
    return *value == attr.value || value->rfind(attr.value + "-", 0) == 0;
  }
  if (attr.op == "^=") return !attr.value.empty() && value->rfind(attr.value, 0) == 0;
  if (attr.op == "$=") {
    return !attr.value.empty() && value->size() >= attr.value.size() &&
           value->compare(value->size() - attr.value.size(), attr.value.size(), attr.value) == 0;
  }
  if (attr.op == "*=") {
    return !attr.value.empty() && value->find(attr.value) != std::string::npos;
  }
  return false;
}

/** Parse the `an+b` micro-syntax of `:nth-child`. */
bool parseNth(const std::string &expression, int *a, int *b) {
  std::string text;
  for (char c : xt_css_lower(xt_css_trim(expression))) {
    if (!xt_is_css_space(c)) text.push_back(c);
  }
  if (text == "odd") { *a = 2; *b = 1; return true; }
  if (text == "even") { *a = 2; *b = 0; return true; }
  size_t nPos = text.find('n');
  if (nPos == std::string::npos) {
    *a = 0;
    *b = atoi(text.c_str());
    return !text.empty();
  }
  std::string aPart = text.substr(0, nPos);
  std::string bPart = text.substr(nPos + 1);
  if (aPart.empty() || aPart == "+") *a = 1;
  else if (aPart == "-") *a = -1;
  else *a = atoi(aPart.c_str());
  if (bPart.empty()) *b = 0;
  else *b = atoi(bPart.c_str());
  return true;
}

bool matchesNth(int index, const std::string &expression) {
  int a = 0;
  int b = 0;
  if (!parseNth(expression, &a, &b)) return false;
  if (a == 0) return index == b;
  int diff = index - b;
  if (a > 0) return diff >= 0 && diff % a == 0;
  return diff <= 0 && (-diff) % (-a) == 0;
}

bool isHoverTarget(const Node *node, const Node *hover) {
  for (const Node *current = hover; current != nullptr; current = current->parent) {
    if (current == node) return true;
  }
  return false;
}

bool matchPseudo(const Node *node, const std::string &pseudo, const MatchState &state) {
  if (pseudo == "first-child") return node->elementIndex() == 0 && node->elementSiblingCount() > 0;
  if (pseudo == "last-child") return node->elementIndex() == node->elementSiblingCount() - 1;
  if (pseudo == "only-child") return node->elementSiblingCount() == 1;
  if (pseudo == "root") return node->parent != nullptr && node->parent->isDocument();
  if (pseudo == "hover") return isHoverTarget(node, state.hover);
  if (pseudo == "focus") return node == state.focus;
  if (pseudo == "empty") {
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child->isElement()) return false;
      if (child->isText() && !xt_css_trim(child->text).empty()) return false;
    }
    return true;
  }
  if (pseudo == "disabled") return node->attr("disabled") != nullptr;
  if (pseudo == "checked") return node->attr("checked") != nullptr;
  if (pseudo.rfind("nth-child(", 0) == 0) {
    return matchesNth(node->elementIndex() + 1, pseudo.substr(10, pseudo.size() - 11));
  }
  if (pseudo.rfind("not(", 0) == 0) {
    std::string inner = xt_css_trim(pseudo.substr(4, pseudo.size() - 5));
    std::vector<ComplexSelector> parsed = xt_css_parse_selector_list(inner);
    if (parsed.empty() || parsed[0].compounds.size() != 1) return false;
    const CompoundSelector &compound = parsed[0].compounds[0];
    for (const AttrSelector &attr : compound.attrs) {
      if (!matchAttribute(node, attr)) return true;
    }
    if (!compound.id.empty()) {
      const std::string *id = node->attr("id");
      if (id == nullptr || *id != compound.id) return true;
    }
    if (!compound.tag.empty() && node->tag != compound.tag) return true;
    for (const std::string &name : compound.classes) {
      if (!hasClass(node, name)) return true;
    }
    for (const std::string &nested : compound.pseudoClasses) {
      if (!matchPseudo(node, nested, state)) return true;
    }
    return false;
  }
  /* Truly unknown / unsupported pseudo-classes never match. */
  return false;
}

bool matchCompound(const Node *node, const CompoundSelector &compound, const MatchState &state) {
  if (!node->isElement()) return false;
  if (!compound.tag.empty() && node->tag != compound.tag) return false;
  if (!compound.id.empty()) {
    const std::string *id = node->attr("id");
    if (id == nullptr || *id != compound.id) return false;
  }
  for (const std::string &name : compound.classes) {
    if (!hasClass(node, name)) return false;
  }
  for (const AttrSelector &attr : compound.attrs) {
    if (!matchAttribute(node, attr)) return false;
  }
  for (const std::string &pseudo : compound.pseudoClasses) {
    if (!matchPseudo(node, pseudo, state)) return false;
  }
  return true;
}

bool matchAt(const Node *node, const ComplexSelector &selector, size_t index,
             const MatchState &state) {
  if (!matchCompound(node, selector.compounds[index], state)) return false;
  if (index == 0) return true;
  Combinator combinator = selector.combinators[index - 1];
  if (combinator == Combinator::Child) {
    return node->parent != nullptr && node->parent->isElement() &&
           matchAt(node->parent, selector, index - 1, state);
  }
  if (combinator == Combinator::Descendant) {
    for (const Node *parent = node->parent; parent != nullptr; parent = parent->parent) {
      if (parent->isElement() && matchAt(parent, selector, index - 1, state)) return true;
    }
    return false;
  }
  if (node->parent == nullptr) return false;
  if (combinator == Combinator::Adjacent) {
    const Node *previous = nullptr;
    for (const std::unique_ptr<Node> &sibling : node->parent->children) {
      if (sibling.get() == node) break;
      if (sibling->isElement()) previous = sibling.get();
    }
    return previous != nullptr && matchAt(previous, selector, index - 1, state);
  }
  /* General sibling combinator (`~`): any previous element sibling. */
  for (const std::unique_ptr<Node> &sibling : node->parent->children) {
    if (sibling.get() == node) break;
    if (sibling->isElement() && matchAt(sibling.get(), selector, index - 1, state)) return true;
  }
  return false;
}

}  // namespace

bool xt_css_match(const Node *node, const ComplexSelector &selector) {
  return xt_css_match(node, selector, MatchState());
}

bool xt_css_match(const Node *node, const ComplexSelector &selector, const MatchState &state) {
  if (selector.compounds.empty()) return false;
  return matchAt(node, selector, selector.compounds.size() - 1, state);
}

}  // namespace xtgui
