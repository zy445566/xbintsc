/*
 * xbintsc GUI engine — the document model.
 *
 * An `XtDocument` owns the DOM tree, the author stylesheet gathered from the
 * document's `<style>` elements, and the computed style of every element. It is
 * the hand-off point from parsing to layout.
 */
#ifndef XT_GUI_DOCUMENT_H
#define XT_GUI_DOCUMENT_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "css.h"
#include "dom.h"
#include "layout.h"
#include "style.h"

namespace xtgui {

/** A running CSS transition (only paint-affecting properties are supported). */
struct ActiveTransition {
  const Node *node = nullptr;
  std::string property;
  bool is_color = false;
  float from_color[4] = {0, 0, 0, 0};
  float to_color[4] = {0, 0, 0, 0};
  float from_number = 0;
  float to_number = 0;
  float start_ms = 0;     // when interpolation begins (after `delay`)
  float duration_ms = 0;
  float delay_ms = 0;
  TimingFunction timing = TimingFunction::Ease;
};

class XtDocument {
 public:
  /** Parse `html`, gather `<style>` rules and compute styles for the viewport. */
  void load(const std::string &html, float viewportWidth, float viewportHeight);
  /** Recompute styles (e.g. after the viewport size changed). */
  void restyle(float viewportWidth, float viewportHeight);
  /** Advance the animation clock by `delta_ms`; recomputes the displayed styles
   * and layout when a transition is running. Returns true when anything
   * transitioned (the caller should repaint). */
  bool advance(double delta_ms);
  /** True while at least one transition is running. */
  bool animating() const { return !transitions_.empty(); }
  /** Set the hovered/focused element; restyles when it changed. Returns true
   * when the dynamic state (and therefore the styles) changed. */
  bool setHover(const Node *node);
  bool setFocus(const Node *node);
  const Node *hover() const { return hover_; }
  const Node *focus() const { return focus_; }

  const Node *root() const { return root_.get(); }
  const StyleSheet &sheet() const { return sheet_; }
  const XtStyle *styleOf(const Node *node) const;
  const LayoutBox *boxOf(const Node *node) const;
  const LayoutTree &layout() const { return layout_; }
  /** First element matching a CSS selector (a small querySelector). */
  const Node *querySelector(const std::string &selectorText) const;
  /** Elements matching a CSS selector, in document order. */
  std::vector<const Node *> querySelectorAll(const std::string &selectorText) const;

  /* -- DOM mutation (element handles) ------------------------------------ */
  /** Create a detached element/text node owned by the document. */
  Node *createElement(const std::string &tag);
  Node *createTextNode(const std::string &text);
  /** Move `child` under `parent` (from its current parent or the detached pool). */
  Node *appendChild(Node *parent, Node *child);
  Node *insertBefore(Node *parent, Node *child, Node *reference);
  bool removeChild(Node *parent, Node *child);
  Node *replaceChild(Node *parent, Node *newChild, Node *oldChild);
  /** Release a node from the document entirely (still owned, detached). */
  void detach(Node *node);
  /** Free a detached node and its subtree. The caller must already have
   * forgotten every handle in that subtree (see `domapi::discard_subtree`), so
   * no slot is left pointing at freed memory. Nodes still attached to the tree
   * are ignored: use `removeChild`/`detach` for those. */
  void discard(Node *node);

  /** Nodes in the document tree (diagnostics). */
  size_t treeNodeCount() const;
  /** Nodes held by the detached pool, subtrees included (diagnostics). */
  size_t detachedNodeCount() const;
  /** Top-level entries in the detached pool (diagnostics). */
  size_t detachedRootCount() const { return detached_.size(); }

  /** Mark the document as needing a restyle/relayout before the next paint. */
  void invalidate() { dirty_ = true; }
  bool takeDirty() {
    bool was = dirty_;
    dirty_ = false;
    return was;
  }
  /** Serialized DOM, for diagnostics. */
  std::string toDebugString() const;

 private:
  void collectStyleText(const Node *node, std::string *out) const;
  void startTransitions();
  void updateDisplay();

  std::unique_ptr<Node> root_;
  /** Detached nodes (created or removed) stay alive here so handles remain valid. */
  std::unordered_map<const Node *, std::unique_ptr<Node>> detached_;
  bool dirty_ = false;
  StyleSheet sheet_;
  std::unordered_map<const Node *, XtStyle> styles_;   // target (cascade)
  std::unordered_map<const Node *, XtStyle> display_;  // after transitions
  std::vector<ActiveTransition> transitions_;
  double clock_ms_ = 0.0;
  LayoutTree layout_;
  const Node *hover_ = nullptr;
  const Node *focus_ = nullptr;
  float viewportWidth_ = 0.0f;
  float viewportHeight_ = 0.0f;
};

}  // namespace xtgui

#endif /* XT_GUI_DOCUMENT_H */
