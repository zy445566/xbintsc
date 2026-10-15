/*
 * xbintsc GUI engine — layout (box tree + geometry).
 *
 * `LayoutTree::compute` turns the styled DOM into a tree of `LayoutBox`es with
 * absolute (viewport-relative) geometry. It implements the slice a UI needs:
 * block flow, a simple inline formatting context with greedy line breaking
 * (text metrics come from the HarfBuzz/FreeType stack) and single-line
 * Flexbox. `<img>` is a replaced element sized from its intrinsic dimensions.
 *
 * Coordinates are absolute: `x`/`y` is the border-box origin measured from the
 * viewport's top-left corner.
 */
#ifndef XT_GUI_LAYOUT_H
#define XT_GUI_LAYOUT_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dom.h"
#include "style.h"

namespace xtgui {

/** Intrinsic size and decoder id of one `<video>` element, resolved by the
 * engine and keyed by node. `width`/`height` are the decoded pixel size, or
 * the 300x150 default before metadata is available. */
struct MediaSize {
  float width = 0;
  float height = 0;
  uint64_t id = 0;
};
using MediaSizeMap = std::unordered_map<const Node *, MediaSize>;

/** Intrinsic size and render id of one `<canvas>` element, resolved by the
 * engine and keyed by node. `width`/`height` are the bitmap's pixel size. */
struct CanvasInfo {
  float width = 0;
  float height = 0;
  uint64_t id = 0;
};
using CanvasInfoMap = std::unordered_map<const Node *, CanvasInfo>;

/** A positioned text fragment (a wrapped text box can span several lines).
 * `text` holds the run this fragment covers (a word or a run of spaces). */
struct TextFragment {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  std::string text;
};

struct LayoutBox {
  const Node *node = nullptr;
  const XtStyle *style = nullptr;

  bool is_text = false;
  std::string text;               // only for text boxes
  std::vector<TextFragment> fragments;

  /* Replaced element (currently `<img>`): source + intrinsic pixel size. */
  bool is_image = false;
  std::string image_src;
  float intrinsic_width = 0;
  float intrinsic_height = 0;

  /* `<video>`: a replaced element whose pixels come from a live decoder
   * (`media_id`) instead of a file. `is_image` is set too so the
   * replaced-element sizing in `layout_flow.cpp` applies unchanged. */
  bool is_media = false;
  uint64_t media_id = 0;

  /* `<canvas>`: a replaced element whose pixels are a script-owned `Canvas2D`
   * (`canvas_id`). `is_image`-style intrinsic sizing applies. */
  bool is_canvas = false;
  uint64_t canvas_id = 0;

  Display display = Display::Inline;

  /* Resolved (px) box edges. */
  float margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;
  float padding_top = 0, padding_right = 0, padding_bottom = 0, padding_left = 0;
  float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;

  /* Absolute geometry; `x`/`y`/`width`/`height` are the border box. */
  float x = 0, y = 0, width = 0, height = 0;
  float content_x = 0, content_y = 0, content_width = 0, content_height = 0;
  float baseline = 0;

  /* The padding box of the nearest positioned ancestor (the viewport when there
   * is none): the containing block `position: absolute` resolves against. */
  float cb_x = 0, cb_y = 0, cb_width = 0, cb_height = 0;

  LayoutBox *parent = nullptr;
  std::vector<std::unique_ptr<LayoutBox>> children;
};

class LayoutTree {
 public:
  void compute(const Node *root, const std::unordered_map<const Node *, XtStyle> &styles,
               float viewportWidth, float viewportHeight, const MediaSizeMap *media = nullptr,
               const CanvasInfoMap *canvases = nullptr);
  void clear() {
    root_.reset();
    index_.clear();
  }

  const LayoutBox *root() const { return root_.get(); }
  const LayoutBox *find(const Node *node) const;
  /** Deepest box whose border box contains the viewport-relative point. */
  const LayoutBox *hitTest(float x, float y) const;
  /** Human-readable geometry dump, for diagnostics/tests. */
  std::string dump() const;

 private:
  std::unique_ptr<LayoutBox> root_;
  std::unordered_map<const Node *, const LayoutBox *> index_;
  XtStyle root_style_;
};

/** Text width under `style` (shaped with the HarfBuzz/FreeType stack). */
float xt_layout_text_width(const std::string &text, const XtStyle &style);

}  // namespace xtgui

#endif /* XT_GUI_LAYOUT_H */
