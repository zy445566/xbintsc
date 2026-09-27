/*
 * xbintsc GUI engine — document model implementation.
 */

#include "document.h"

namespace xtgui {

void XtDocument::collectStyleText(const Node *node, std::string *out) const {
  if (node->isElement() && node->isTag("style")) {
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child->isText()) {
        *out += child->text;
        *out += "\n";
      }
    }
    return;
  }
  /* A <link rel="stylesheet"> would need I/O; skip it (and everything else). */
  for (const std::unique_ptr<Node> &child : node->children) {
    collectStyleText(child.get(), out);
  }
}

void XtDocument::load(const std::string &html, float viewportWidth, float viewportHeight) {
  root_ = xt_html_parse(html);
  std::string css;
  collectStyleText(root_.get(), &css);
  sheet_ = xt_css_parse_stylesheet(css);
  restyle(viewportWidth, viewportHeight);
}

void XtDocument::restyle(float viewportWidth, float viewportHeight) {
  viewportWidth_ = viewportWidth;
  viewportHeight_ = viewportHeight;
  if (root_ != nullptr) {
    xt_style_compute(root_.get(), sheet_, styles_, viewportWidth, viewportHeight);
    layout_.compute(root_.get(), styles_, viewportWidth, viewportHeight);
  }
}

const XtStyle *XtDocument::styleOf(const Node *node) const {
  auto it = styles_.find(node);
  return (it == styles_.end()) ? nullptr : &it->second;
}

const LayoutBox *XtDocument::boxOf(const Node *node) const { return layout_.find(node); }

std::vector<const Node *> XtDocument::querySelectorAll(const std::string &selectorText) const {
  std::vector<const Node *> matches;
  if (root_ == nullptr) return matches;
  std::vector<ComplexSelector> selectors = xt_css_parse_selector_list(selectorText);
  if (selectors.empty()) return matches;
  std::vector<const Node *> stack;
  stack.push_back(root_.get());
  while (!stack.empty()) {
    const Node *node = stack.back();
    stack.pop_back();
    if (node->isElement()) {
      for (const ComplexSelector &selector : selectors) {
        if (xt_css_match(node, selector)) {
          matches.push_back(node);
          break;
        }
      }
    }
    /* Push children in reverse so the traversal stays in document order. */
    for (size_t i = node->children.size(); i-- > 0;) {
      stack.push_back(node->children[i].get());
    }
  }
  return matches;
}

const Node *XtDocument::querySelector(const std::string &selectorText) const {
  std::vector<const Node *> matches = querySelectorAll(selectorText);
  return matches.empty() ? nullptr : matches.front();
}

std::string XtDocument::toDebugString() const {
  return root_ == nullptr ? std::string() : xt_dom_to_string(root_.get());
}

}  // namespace xtgui
