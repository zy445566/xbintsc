/*
 * xbintsc GUI engine — box tree construction and the public `LayoutTree` API.
 *
 * The flow algorithm itself lives in `layout_flow.cpp`. The algorithm is
 * intentionally a pragmatic subset:
 *   - Block flow: consecutive inline-level children form an anonymous inline
 *     formatting context; block-level children are stacked vertically.
 *   - Inline flow: greedy, word-based line breaking with `text-align`; text is
 *     measured with the HarfBuzz/FreeType stack in `text.h`.
 *   - Flexbox: single-line row/column with grow/shrink, gap, justify-content
 *     and align-items.
 * Margins do not collapse yet, and only static positioning is honoured.
 */

#include "layout_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "image.h"

namespace xtgui {
namespace {

std::unique_ptr<LayoutBox> buildBoxTree(const Node *node,
                                        const std::unordered_map<const Node *, XtStyle> &styles,
                                        const XtStyle *inherited) {
  if (node->isText()) {
    auto box = std::make_unique<LayoutBox>();
    box->node = node;
    box->is_text = true;
    box->text = node->text;
    box->style = inherited;
    box->display = Display::Inline;
    return box;
  }
  auto it = styles.find(node);
  const XtStyle *style = it == styles.end() ? inherited : &it->second;
  if (style != nullptr && style->display == Display::None) return nullptr;

  auto box = std::make_unique<LayoutBox>();
  box->node = node;
  box->style = style;
  box->display = style != nullptr ? style->display : Display::Block;
  if (node->isElement() && node->isTag("img")) {
    box->is_image = true;
    const std::string *src = node->attr("src");
    if (src != nullptr) {
      box->image_src = *src;
      int width = 0;
      int height = 0;
      if (xt_image_size(*src, &width, &height)) {
        box->intrinsic_width = (float)width;
        box->intrinsic_height = (float)height;
      }
    }
  }
  for (const std::unique_ptr<Node> &child : node->children) {
    std::unique_ptr<LayoutBox> childBox = buildBoxTree(child.get(), styles, style);
    if (childBox != nullptr) {
      childBox->parent = box.get();
      box->children.push_back(std::move(childBox));
    }
  }
  return box;
}

std::string dumpBox(const LayoutBox *box, int depth) {
  std::string out(depth * 2, ' ');
  if (box->is_text) {
    out += "#text";
  } else if (box->node != nullptr && box->node->isElement()) {
    out += "<" + box->node->tag + ">";
  } else {
    out += "#root";
  }
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), " x=%.1f y=%.1f w=%.1f h=%.1f\n", box->x, box->y, box->width,
                box->height);
  out += buffer;
  for (const std::unique_ptr<LayoutBox> &child : box->children) out += dumpBox(child.get(), depth + 1);
  return out;
}

}  // namespace

void LayoutTree::compute(const Node *root, const std::unordered_map<const Node *, XtStyle> &styles,
                         float viewportWidth, float viewportHeight) {
  root_.reset();
  index_.clear();
  root_style_ = xt_style_initial();
  root_style_.display = Display::Block;

  root_ = std::make_unique<LayoutBox>();
  root_->node = root;
  root_->style = &root_style_;
  root_->display = Display::Block;
  if (root != nullptr) {
    if (root->type == NodeType::Document) {
      for (const std::unique_ptr<Node> &child : root->children) {
        std::unique_ptr<LayoutBox> box = buildBoxTree(child.get(), styles, &root_style_);
        if (box != nullptr) {
          box->parent = root_.get();
          root_->children.push_back(std::move(box));
        }
      }
    } else {
      std::unique_ptr<LayoutBox> box = buildBoxTree(root, styles, &root_style_);
      if (box != nullptr) {
        box->parent = root_.get();
        root_->children.push_back(std::move(box));
      }
    }
  }
  xt_layout_run(root_.get(), styles, viewportWidth, viewportHeight);

  std::vector<const LayoutBox *> stack;
  stack.push_back(root_.get());
  while (!stack.empty()) {
    const LayoutBox *box = stack.back();
    stack.pop_back();
    if (box->node != nullptr) index_[box->node] = box;
    for (const std::unique_ptr<LayoutBox> &child : box->children) stack.push_back(child.get());
  }
}

const LayoutBox *LayoutTree::find(const Node *node) const {
  auto it = index_.find(node);
  return it == index_.end() ? nullptr : it->second;
}

namespace {

const LayoutBox *hitTestBox(const LayoutBox *box, float x, float y) {
  if (box == nullptr || box->display == Display::None) return nullptr;
  /* Later siblings paint on top, so probe them first. */
  for (auto it = box->children.rbegin(); it != box->children.rend(); ++it) {
    if (const LayoutBox *hit = hitTestBox(it->get(), x, y)) return hit;
  }
  if (x >= box->x && x < box->x + box->width && y >= box->y && y < box->y + box->height) {
    return box;
  }
  return nullptr;
}

}  // namespace

const LayoutBox *LayoutTree::hitTest(float x, float y) const {
  return hitTestBox(root_.get(), x, y);
}

std::string LayoutTree::dump() const {
  return root_ == nullptr ? std::string() : dumpBox(root_.get(), 0);
}

}  // namespace xtgui
