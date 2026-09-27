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

class XtDocument {
 public:
  /** Parse `html`, gather `<style>` rules and compute styles for the viewport. */
  void load(const std::string &html, float viewportWidth, float viewportHeight);
  /** Recompute styles (e.g. after the viewport size changed). */
  void restyle(float viewportWidth, float viewportHeight);

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

  std::unique_ptr<Node> root_;
  StyleSheet sheet_;
  std::unordered_map<const Node *, XtStyle> styles_;
  LayoutTree layout_;
  float viewportWidth_ = 0.0f;
  float viewportHeight_ = 0.0f;
};

}  // namespace xtgui

#endif /* XT_GUI_DOCUMENT_H */
