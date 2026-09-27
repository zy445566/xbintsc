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
  /** Serialized DOM, for diagnostics. */
  std::string toDebugString() const;

 private:
  void collectStyleText(const Node *node, std::string *out) const;
  void startTransitions();
  void updateDisplay();

  std::unique_ptr<Node> root_;
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
