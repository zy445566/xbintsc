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

#include "animation_values.h"
#include "css.h"
#include "dom.h"
#include "layout.h"
#include "style.h"

namespace xtgui {

/** A running CSS transition. */
struct ActiveTransition {
  const Node *node = nullptr;
  std::string property;
  AnimatableValue from;
  AnimatableValue to;
  float start_ms = 0;     // when interpolation begins (after `delay`)
  float duration_ms = 0;
  float delay_ms = 0;
  TimingFunction timing;
};

/** A running `@keyframes` animation on one element. */
struct ActiveAnimation {
  const Node *node = nullptr;
  std::string name;
  AnimationSpec spec;
  float start_ms = 0;
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
  /** True while at least one transition or animation is running. */
  bool animating() const { return !transitions_.empty() || !animations_.empty(); }
  /** Set the hovered/focused element; restyles when it changed. Returns true
   * when the dynamic state (and therefore the styles) changed. */
  bool setHover(const Node *node);
  bool setFocus(const Node *node);
  /** Element held down by the primary pointer (`:active`). */
  bool setActive(const Node *node);
  const Node *hover() const { return hover_; }
  const Node *focus() const { return focus_; }
  const Node *active() const { return active_; }

  const Node *root() const { return root_.get(); }
  const StyleSheet &sheet() const { return sheet_; }
  const std::unordered_map<const Node *, XtStyle> &styles() const { return styles_; }
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
  /** Whether a restyle is pending (peek; `takeDirty` consumes it). The frame
   * loop uses this to skip reconciling media/canvas tables when nothing in the
   * document changed. */
  bool dirty() const { return dirty_; }
  bool takeDirty() {
    bool was = dirty_;
    dirty_ = false;
    return was;
  }

  /* -- media (`<video>`) metadata --------------------------------------- */
  /** Record (or update) the decoded size and decoder id of one `<video>` node.
   * Returns true when the stored value changed, so the caller can invalidate a
   * layout that was computed with the old value. */
  bool setMediaSize(const Node *node, float width, float height, uint64_t id);
  /** Record (or update) the pixel size and render id of one `<canvas>` node.
   * Returns true when the stored value changed. */
  bool setCanvasInfo(const Node *node, float width, float height, uint64_t id);
  /** Serialized DOM, for diagnostics. */
  std::string toDebugString() const;

 private:
  void collectStyleText(const Node *node, std::string *out) const;
  void startTransitions();
  void startAnimations();
  void updateDisplay();
  void applyAnimations(std::unordered_map<const Node *, XtStyle> *out);

  std::unique_ptr<Node> root_;
  /** Detached nodes (created or removed) stay alive here so handles remain valid. */
  std::unordered_map<const Node *, std::unique_ptr<Node>> detached_;
  /** Synthetic `::before`/`::after` nodes for the current style generation. */
  PseudoNodeSet pseudos_;
  bool dirty_ = false;
  StyleSheet sheet_;
  std::unordered_map<const Node *, XtStyle> styles_;   // target (cascade)
  std::unordered_map<const Node *, XtStyle> display_;  // after transitions
  MediaSizeMap mediaSizes_;                            // `<video>` intrinsic sizes
  CanvasInfoMap canvasInfo_;                           // `<canvas>` sizes + ids
  std::vector<ActiveTransition> transitions_;
  std::vector<ActiveAnimation> animations_;
  /** Whether any computed style declares a `transition` (from the sheet or an
   * inline `style="…"`): when false the document skips the transition snapshot
   * and `display_` stays empty. */
  bool has_transitions_ = false;
  /** Whether any computed style declares an `animation`. */
  bool has_animations_ = false;
  double clock_ms_ = 0.0;
  LayoutTree layout_;
  const Node *hover_ = nullptr;
  const Node *focus_ = nullptr;
  const Node *active_ = nullptr;
  float viewportWidth_ = 0.0f;
  float viewportHeight_ = 0.0f;
};

}  // namespace xtgui

#endif /* XT_GUI_DOCUMENT_H */
