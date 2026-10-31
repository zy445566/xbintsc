/*
 * xbintsc GUI engine — document model implementation.
 */

#include "document.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "perf.h"
#include "style_internal.h"
#include "text_fonts.h"

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
  detached_.clear();
  dirty_ = false;
  hover_ = nullptr;
  focus_ = nullptr;
  active_ = nullptr;
  styles_.clear();
  display_.clear();
  transitions_.clear();
  animations_.clear();
  mediaSizes_.clear();
  canvasInfo_.clear();
  clock_ms_ = 0.0;
  std::string css;
  collectStyleText(root_.get(), &css);
  sheet_ = xt_css_parse_stylesheet(css);

  /* Author `@font-face` families are global to the process's text stack, so a
   * new document replaces the previous set before its first layout. */
  std::vector<UserFontFace> user_faces;
  user_faces.reserve(sheet_.font_faces.size());
  for (const FontFaceRule &rule : sheet_.font_faces) {
    UserFontFace face;
    face.family = rule.family;
    face.sources.reserve(rule.sources.size());
    for (const FontFaceSource &source : rule.sources) {
      face.sources.push_back(UserFontSource{source.url, source.format});
    }
    face.has_weight = rule.has_weight;
    face.weight_min = rule.weight_min;
    face.weight_max = rule.weight_max;
    face.has_italic = rule.has_italic;
    face.italic = rule.italic;
    user_faces.push_back(std::move(face));
  }
  xt_font_set_user_faces(user_faces);
  restyle(viewportWidth, viewportHeight);
}

void XtDocument::restyle(float viewportWidth, float viewportHeight) {
  viewportWidth_ = viewportWidth;
  viewportHeight_ = viewportHeight;
  g_perf_frame.restyles += 1;
  if (root_ != nullptr) {
    MatchState state;
    state.hover = hover_;
    state.focus = focus_;
    state.active = active_;
    bool hasTransitions = sheet_.has_transitions;
    bool hasAnimations = sheet_.has_animations;
    xt_style_compute(root_.get(), sheet_, styles_, viewportWidth, viewportHeight, state, &pseudos_,
                     &hasTransitions, &hasAnimations);
    has_transitions_ = hasTransitions;
    has_animations_ = hasAnimations;
    startTransitions();
    startAnimations();
    updateDisplay();
  }
}

bool XtDocument::setHover(const Node *node) {
  if (hover_ == node) return false;
  hover_ = node;
  /* No `:hover` rule anywhere: the state changed but the paint cannot. */
  if (!sheet_.has_hover) return false;
  restyle(viewportWidth_, viewportHeight_);
  return true;
}

bool XtDocument::setFocus(const Node *node) {
  if (focus_ == node) return false;
  focus_ = node;
  if (!sheet_.has_focus) return false;
  restyle(viewportWidth_, viewportHeight_);
  return true;
}

bool XtDocument::setActive(const Node *node) {
  if (active_ == node) return false;
  active_ = node;
  if (!sheet_.has_active) return false;
  restyle(viewportWidth_, viewportHeight_);
  return true;
}

const XtStyle *XtDocument::styleOf(const Node *node) const {
  auto it = display_.find(node);
  if (it != display_.end()) return &it->second;
  it = styles_.find(node);
  return (it == styles_.end()) ? nullptr : &it->second;
}

const LayoutBox *XtDocument::boxOf(const Node *node) const { return layout_.find(node); }

bool XtDocument::setMediaSize(const Node *node, float width, float height, uint64_t id) {
  MediaSize &entry = mediaSizes_[node];
  if (entry.width == width && entry.height == height && entry.id == id) return false;
  entry.width = width;
  entry.height = height;
  entry.id = id;
  dirty_ = true;
  return true;
}

bool XtDocument::setCanvasInfo(const Node *node, float width, float height, uint64_t id) {
  CanvasInfo &entry = canvasInfo_[node];
  if (entry.width == width && entry.height == height && entry.id == id) return false;
  entry.width = width;
  entry.height = height;
  entry.id = id;
  dirty_ = true;
  return true;
}

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

/* -- DOM mutation --------------------------------------------------------- */

Node *XtDocument::createElement(const std::string &tag) {
  auto node = std::make_unique<Node>(NodeType::Element);
  node->tag = tag;
  Node *raw = node.get();
  detached_[raw] = std::move(node);
  return raw;
}

Node *XtDocument::createTextNode(const std::string &text) {
  auto node = std::make_unique<Node>(NodeType::Text);
  node->text = text;
  Node *raw = node.get();
  detached_[raw] = std::move(node);
  return raw;
}

namespace {

/** Take ownership of a node that is either attached or sitting in the pool. */
std::unique_ptr<Node> takeNode(
    Node *node, std::unordered_map<const Node *, std::unique_ptr<Node>> &pool) {
  if (node->parent != nullptr) return node->parent->detachChild(node);
  auto it = pool.find(node);
  if (it == pool.end()) return nullptr;
  std::unique_ptr<Node> owned = std::move(it->second);
  pool.erase(it);
  return owned;
}

}  // namespace

Node *XtDocument::appendChild(Node *parent, Node *child) {
  if (parent == nullptr || child == nullptr) return nullptr;
  if (!parent->isElement() || child->isDocument() || child == root_.get()) return nullptr;
  if (child->contains(parent)) return nullptr; /* reject cycles */
  std::unique_ptr<Node> owned = takeNode(child, detached_);
  if (owned == nullptr) return nullptr;
  invalidate();
  return parent->append(std::move(owned));
}

Node *XtDocument::insertBefore(Node *parent, Node *child, Node *reference) {
  if (reference == nullptr) return appendChild(parent, child);
  if (parent == nullptr || child == nullptr || child == reference) return child;
  if (!parent->isElement() || child->isDocument() || child == root_.get()) return nullptr;
  if (child->contains(parent)) return nullptr; /* reject cycles */
  bool reference_is_child = false;
  for (const std::unique_ptr<Node> &entry : parent->children) {
    if (entry.get() == reference) {
      reference_is_child = true;
      break;
    }
  }
  if (!reference_is_child) return nullptr;
  std::unique_ptr<Node> owned = takeNode(child, detached_);
  if (owned == nullptr) return nullptr;
  invalidate();
  return parent->insertChild(std::move(owned), reference);
}

bool XtDocument::removeChild(Node *parent, Node *child) {
  if (parent == nullptr || child == nullptr || child->parent != parent) return false;
  std::unique_ptr<Node> owned = parent->detachChild(child);
  if (owned == nullptr) return false;
  detached_[child] = std::move(owned);
  invalidate();
  return true;
}

Node *XtDocument::replaceChild(Node *parent, Node *newChild, Node *oldChild) {
  if (parent == nullptr || oldChild == nullptr || oldChild->parent != parent) return nullptr;
  if (newChild == oldChild) return newChild;
  if (newChild != nullptr && !insertBefore(parent, newChild, oldChild)) return nullptr;
  removeChild(parent, oldChild);
  return newChild;
}

void XtDocument::detach(Node *node) {
  if (node == nullptr || node == root_.get()) return;
  if (node->parent != nullptr) {
    std::unique_ptr<Node> owned = node->parent->detachChild(node);
    if (owned != nullptr) detached_[node] = std::move(owned);
  }
  invalidate();
}

void XtDocument::discard(Node *node) {
  if (node == nullptr || node == root_.get()) return;
  auto it = detached_.find(node);
  if (it == detached_.end()) return;
  /* Children are owned by their parent, never by the pool (takeNode erases the
   * pool entry when a node is attached), so dropping the root drops it all. */
  detached_.erase(it);
}

namespace {

size_t countSubtree(const Node *node) {
  size_t total = 1;
  for (const std::unique_ptr<Node> &child : node->children) total += countSubtree(child.get());
  return total;
}

}  // namespace

size_t XtDocument::treeNodeCount() const { return root_ == nullptr ? 0 : countSubtree(root_.get()); }

size_t XtDocument::detachedNodeCount() const {
  size_t total = 0;
  for (const auto &entry : detached_) total += countSubtree(entry.second.get());
  return total;
}

}  // namespace xtgui
