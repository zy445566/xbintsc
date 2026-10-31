/*
 * xbintsc GUI engine — box tree construction and the public `LayoutTree` API.
 *
 * The flow algorithm itself lives in `layout_flow.cpp`. The algorithm is
 * intentionally a pragmatic subset:
 *   - Block flow: consecutive inline-level children form an anonymous inline
 *     formatting context; block-level children are stacked vertically.
 *   - Inline flow: greedy, word-based line breaking with `text-align`; text is
 *     measured with the stb_truetype stack in `text.h`.
 *   - Flexbox: single-line row/column with grow/shrink, gap, justify-content
 *     and align-items.
 * Margins do not collapse yet, and only static positioning is honoured.
 */

#include "layout_internal.h"

#include "image_api.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "image.h"
#include "inline_svg.h"
#include "perf.h"
#include "style_internal.h"

namespace xtgui {
namespace {

/** Compose `transform`/`transform-origin` down the tree (defined below). */
void applyTransforms(LayoutBox *box, const Matrix2D &parent_world, bool parent_transformed,
                     float viewportWidth, float viewportHeight);

std::unique_ptr<LayoutBox> buildBoxTree(const Node *node,
                                        const std::unordered_map<const Node *, XtStyle> &styles,
                                        const XtStyle *inherited, const MediaSizeMap *media,
                                        const CanvasInfoMap *canvases) {
  if (node->isText()) {
    auto box = std::make_unique<LayoutBox>();
    box->node = node;
    box->is_text = true;
    box->text = inherited != nullptr ? xt_apply_text_transform(node->text, inherited->text_transform)
                                     : node->text;
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
  const XtImageApi *images = xt_image_api();
  if (node->isElement() && node->isTag("img")) {
    box->is_image = true;
    const std::string *src = node->attr("src");
    if (src != nullptr) {
      box->image_src = *src;
      int width = 0;
      int height = 0;
      if (images != nullptr && images->size(*src, &width, &height)) {
        box->intrinsic_width = (float)width;
        box->intrinsic_height = (float)height;
      }
    }
  } else if (node->isElement() && node->isTag("video")) {
    /* Default the video box to the HTML 300x150 until the decoder reports the
     * real pixel size (fetched as soon as the element is associated with a
     * player, before the next layout pass). */
    box->is_image = true;
    box->is_media = true;
    box->intrinsic_width = 300;
    box->intrinsic_height = 150;
    if (media != nullptr) {
      auto found = media->find(node);
      if (found != media->end()) {
        box->media_id = found->second.id;
        if (found->second.width > 0 && found->second.height > 0) {
          box->intrinsic_width = found->second.width;
          box->intrinsic_height = found->second.height;
        }
      }
    }
  } else if (node->isElement() && node->isTag("canvas")) {
    /* A replaced inline-block whose pixels are a script-owned bitmap. The
     * intrinsic size is the HTML 300x150 default, overridden by the
     * `width`/`height` attributes (or a script assignment to `canvas.width`),
     * published by the window as `CanvasInfo`. */
    box->is_canvas = true;
    box->intrinsic_width = 300;
    box->intrinsic_height = 150;
    float parsed = 0.0f;
    const std::string *attr_width = node->attr("width");
    const std::string *attr_height = node->attr("height");
    if (attr_width != nullptr && xt_css_parse_number(*attr_width, &parsed) && parsed > 0.0f)
      box->intrinsic_width = parsed;
    if (attr_height != nullptr && xt_css_parse_number(*attr_height, &parsed) && parsed > 0.0f)
      box->intrinsic_height = parsed;
    if (canvases != nullptr) {
      auto found = canvases->find(node);
      if (found != canvases->end()) {
        box->canvas_id = found->second.id;
        if (found->second.width > 0 && found->second.height > 0) {
          box->intrinsic_width = found->second.width;
          box->intrinsic_height = found->second.height;
        }
      }
    }
  } else if (node->isElement() && node->isTag("svg")) {
    /* An inline `<svg>` is a replaced element: the subtree is vector art, not
     * HTML boxes. Serialize it once, take the intrinsic size from the parsed
     * document (width/height attributes, else the viewBox, else 300x150), and
     * let paint/rasterisation handle the children — the normal box walk must
     * not descend into them. */
    box->is_inline_svg = true;
    if (images != nullptr) {
      box->svg_markup = images->inline_serialize(node, &styles);
      int width = 0;
      int height = 0;
      if (images->svg_markup_size(box->svg_markup, &width, &height)) {
        box->intrinsic_width = (float)width;
        box->intrinsic_height = (float)height;
      }
    }
    if (box->intrinsic_width <= 0.0f) box->intrinsic_width = 300.0f;
    if (box->intrinsic_height <= 0.0f) box->intrinsic_height = 150.0f;
  }
  if (box->is_inline_svg) return box;

  /* `::before` / `::after` boxes are emitted around the DOM children. Only a
   * pseudo-element with `content` generates a box (CSS: a `::before` rule with
   * no content has no effect). Replaced elements never generate them. */
  auto appendPseudo = [&](Node *pseudo) {
    if (pseudo == nullptr || box->is_image || box->is_canvas || box->is_media) return;
    auto pseudoStyle = styles.find(pseudo);
    if (pseudoStyle == styles.end() || !pseudoStyle->second.has_content) return;
    std::unique_ptr<LayoutBox> pseudoBox = buildBoxTree(pseudo, styles, style, media, canvases);
    if (pseudoBox != nullptr) {
      pseudoBox->parent = box.get();
      box->children.push_back(std::move(pseudoBox));
    }
  };
  if (node->isElement() && !node->is_pseudo) appendPseudo(node->pseudo_before);
  for (const std::unique_ptr<Node> &child : node->children) {
    std::unique_ptr<LayoutBox> childBox =
        buildBoxTree(child.get(), styles, style, media, canvases);
    if (childBox != nullptr) {
      childBox->parent = box.get();
      box->children.push_back(std::move(childBox));
    }
  }
  if (node->isElement() && !node->is_pseudo) appendPseudo(node->pseudo_after);
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
                         float viewportWidth, float viewportHeight, const MediaSizeMap *media,
                         const CanvasInfoMap *canvases) {
  PerfScope perf_scope(&g_perf_frame_ms.layout);
  g_perf_frame.layouts += 1;
  root_.reset();
  index_.clear();
  root_style_ = xt_style_initial();
  root_style_.display = Display::Block;

  root_ = std::make_unique<LayoutBox>();
  root_->node = root;
  root_->style = &root_style_;
  root_->display = Display::Block;
  /* The initial containing block: absolute boxes with no positioned ancestor
   * resolve their insets against the viewport. */
  root_->cb_x = 0;
  root_->cb_y = 0;
  root_->cb_width = viewportWidth;
  root_->cb_height = viewportHeight;
  if (root != nullptr) {
    if (root->type == NodeType::Document) {
      for (const std::unique_ptr<Node> &child : root->children) {
        std::unique_ptr<LayoutBox> box =
            buildBoxTree(child.get(), styles, &root_style_, media, canvases);
        if (box != nullptr) {
          box->parent = root_.get();
          root_->children.push_back(std::move(box));
        }
      }
    } else {
      std::unique_ptr<LayoutBox> box = buildBoxTree(root, styles, &root_style_, media, canvases);
      if (box != nullptr) {
        box->parent = root_.get();
        root_->children.push_back(std::move(box));
      }
    }
  }
  xt_layout_run(root_.get(), styles, viewportWidth, viewportHeight);
  applyTransforms(root_.get(), Matrix2D(), false, viewportWidth, viewportHeight);

  std::vector<const LayoutBox *> stack;
  stack.push_back(root_.get());
  while (!stack.empty()) {
    const LayoutBox *box = stack.back();
    stack.pop_back();
    if (box->node != nullptr) index_[box->node] = box;
    g_perf_frame.layoutBoxes += 1;
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
  if (box->style != nullptr && !box->style->visible) return nullptr;
  /* Later siblings paint on top, so probe them first. */
  for (auto it = box->children.rbegin(); it != box->children.rend(); ++it) {
    if (const LayoutBox *hit = hitTestBox(it->get(), x, y)) return hit;
  }
  if (box->style != nullptr && !box->style->pointer_events) return nullptr;
  float local_x = x;
  float local_y = y;
  if (box->has_transform) box->inverse.apply(x, y, &local_x, &local_y);
  if (local_x >= box->x && local_x < box->x + box->width && local_y >= box->y &&
      local_y < box->y + box->height) {
    /* A generated box is part of its originating element: events must target
     * the real element handle, not the synthetic node. */
    if (box->node != nullptr && box->node->is_pseudo && box->parent != nullptr) return box->parent;
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

/* -- transforms ----------------------------------------------------------- */

namespace {

/** The transform a single box applies to its own geometry, relative to its
 * `transform-origin`. Text boxes inherit their parent's style pointer, so only
 * element boxes may contribute one (otherwise the style would be applied
 * twice). */
Matrix2D boxLocalTransform(const LayoutBox *box, float viewportWidth, float viewportHeight) {
  Matrix2D local;
  if (box->style == nullptr || box->style->transform.empty()) return local;
  if (box->node == nullptr || !box->node->isElement()) return local;
  const XtStyle &style = *box->style;
  Matrix2D body = xt_transform_matrix(style.transform, box->width, box->height, style.font_size,
                                      viewportWidth, viewportHeight);
  float origin_x = box->x + xt_style_resolve_absolute(style.transform_origin_x, box->width,
                                                      style.font_size, 16.0f, viewportWidth,
                                                      viewportHeight);
  float origin_y = box->y + xt_style_resolve_absolute(style.transform_origin_y, box->height,
                                                      style.font_size, 16.0f, viewportWidth,
                                                      viewportHeight);
  return Matrix2D::translate(origin_x, origin_y)
      .multiply(body)
      .multiply(Matrix2D::translate(-origin_x, -origin_y));
}

void applyTransforms(LayoutBox *box, const Matrix2D &parent_world, bool parent_transformed,
                     float viewportWidth, float viewportHeight) {
  Matrix2D local = boxLocalTransform(box, viewportWidth, viewportHeight);
  box->world = parent_world.multiply(local);
  box->has_transform = parent_transformed || !local.identity();
  if (!box->world.invert(&box->inverse)) box->inverse = Matrix2D();
  for (std::unique_ptr<LayoutBox> &child : box->children) {
    applyTransforms(child.get(), box->world, box->has_transform, viewportWidth, viewportHeight);
  }
}

}  // namespace

LayoutRect xt_layout_transform_rect(const Matrix2D &matrix, float x, float y, float width,
                                    float height) {
  LayoutRect out{x, y, width, height};
  if (matrix.identity()) return out;
  const float corners_x[4] = {x, x + width, x + width, x};
  const float corners_y[4] = {y, y, y + height, y + height};
  float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
  for (int i = 0; i < 4; i++) {
    float corner_x = 0;
    float corner_y = 0;
    matrix.apply(corners_x[i], corners_y[i], &corner_x, &corner_y);
    if (i == 0 || corner_x < min_x) min_x = corner_x;
    if (i == 0 || corner_x > max_x) max_x = corner_x;
    if (i == 0 || corner_y < min_y) min_y = corner_y;
    if (i == 0 || corner_y > max_y) max_y = corner_y;
  }
  out.x = min_x;
  out.y = min_y;
  out.width = max_x - min_x;
  out.height = max_y - min_y;
  return out;
}

LayoutRect xt_layout_transformed_bounds(const LayoutBox *box) {
  if (box == nullptr) return LayoutRect();
  return xt_layout_transform_rect(box->world, box->x, box->y, box->width, box->height);
}

}  // namespace xtgui
