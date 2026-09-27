/*
 * xbintsc GUI engine — layout (box tree + geometry).
 *
 * `LayoutTree::compute` turns the styled DOM into a tree of `LayoutBox`es with
 * absolute (viewport-relative) geometry. It implements the slice a UI needs:
 * block flow, a simple inline formatting context with greedy line breaking
 * (text metrics are an approximation until the M4 text stack lands) and
 * single-line Flexbox.
 *
 * Coordinates are absolute: `x`/`y` is the border-box origin measured from the
 * viewport's top-left corner.
 */
#ifndef XT_GUI_LAYOUT_H
#define XT_GUI_LAYOUT_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dom.h"
#include "style.h"

namespace xtgui {

/** A positioned text fragment (a wrapped text box can span several lines). */
struct TextFragment {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
};

struct LayoutBox {
  const Node *node = nullptr;
  const XtStyle *style = nullptr;

  bool is_text = false;
  std::string text;               // only for text boxes
  std::vector<TextFragment> fragments;

  Display display = Display::Inline;

  /* Resolved (px) box edges. */
  float margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;
  float padding_top = 0, padding_right = 0, padding_bottom = 0, padding_left = 0;
  float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;

  /* Absolute geometry; `x`/`y`/`width`/`height` are the border box. */
  float x = 0, y = 0, width = 0, height = 0;
  float content_x = 0, content_y = 0, content_width = 0, content_height = 0;
  float baseline = 0;

  LayoutBox *parent = nullptr;
  std::vector<std::unique_ptr<LayoutBox>> children;
};

class LayoutTree {
 public:
  void compute(const Node *root, const std::unordered_map<const Node *, XtStyle> &styles,
               float viewportWidth, float viewportHeight);
  void clear() {
    root_.reset();
    index_.clear();
  }

  const LayoutBox *root() const { return root_.get(); }
  const LayoutBox *find(const Node *node) const;
  /** Human-readable geometry dump, for diagnostics/tests. */
  std::string dump() const;

 private:
  std::unique_ptr<LayoutBox> root_;
  std::unordered_map<const Node *, const LayoutBox *> index_;
  XtStyle root_style_;
};

/** Text width under `style` (approximate until M4's font stack). */
float xt_layout_text_width(const std::string &text, const XtStyle &style);

}  // namespace xtgui

#endif /* XT_GUI_LAYOUT_H */
