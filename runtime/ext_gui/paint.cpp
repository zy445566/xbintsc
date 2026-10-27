/*
 * xbintsc GUI engine — display list construction.
 *
 * The tree walk lives here. Background layers are in `paint_background.cpp`,
 * filter/shadow approximations in `paint_effects.cpp` and `clip-path`
 * triangulation in `paint_clip.cpp`.
 */

#include "paint.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "paint_internal.h"
#include "style.h"

namespace xtgui {

namespace {

std::string formatColor(const Color &color) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", (int)(color.r * 255.0f + 0.5f),
                (int)(color.g * 255.0f + 0.5f), (int)(color.b * 255.0f + 0.5f),
                (int)(color.a * 255.0f + 0.5f));
  return buffer;
}

/** ` transform=matrix(...)` for a non-identity matrix, so the dump (and the
 * tests that read it) can see where a box was transformed to. */
std::string transformSuffix(const Matrix2D &matrix) {
  if (matrix.identity()) return "";
  char buffer[192];
  std::snprintf(buffer, sizeof(buffer),
                " transform=matrix(%.4g,%.4g,%.4g,%.4g,%.4g,%.4g)", (double)matrix.a,
                (double)matrix.b, (double)matrix.c, (double)matrix.d, (double)matrix.e,
                (double)matrix.f);
  return buffer;
}

FontSpec specOf(const XtStyle &style) {
  FontSpec spec;
  spec.family = style.font_family;
  spec.pixel_size = style.font_size;
  spec.weight = style.font_weight;
  spec.italic = style.font_style == FontStyle::Italic;
  return spec;
}

int zIndexOf(const LayoutBox *box) {
  if (box->style == nullptr || !box->style->has_z_index) return 0;
  return box->style->z_index;
}

void paintBox(const LayoutBox *box, DisplayList &out, const PaintClip &clip,
              const paint_detail::FilterChain &parentChain, float viewportWidth,
              float viewportHeight);

/** The clip a box imposes on its content: its padding box, intersected with the
 * clip already in effect. Only `overflow: hidden/scroll/auto` clips. A
 * transformed box clips to the bounding box of its transformed padding box. */
PaintClip clipForChildren(const LayoutBox *box, const PaintClip &parent) {
  if (box->style == nullptr || box->style->overflow == Overflow::Visible) return parent;
  float x = box->x + box->border_left;
  float y = box->y + box->border_top;
  float width = box->width - box->border_left - box->border_right;
  float height = box->height - box->border_top - box->border_bottom;
  if (width < 0) width = 0;
  if (height < 0) height = 0;
  if (box->has_transform) {
    LayoutRect bounds = xt_layout_transform_rect(box->world, x, y, width, height);
    x = bounds.x;
    y = bounds.y;
    width = bounds.width;
    height = bounds.height;
  }
  PaintClip result = parent;
  result.active = true;
  if (!parent.active) {
    result.x = x;
    result.y = y;
    result.width = width;
    result.height = height;
    return result;
  }
  float x0 = std::max(x, parent.x);
  float y0 = std::max(y, parent.y);
  float x1 = std::min(x + width, parent.x + parent.width);
  float y1 = std::min(y + height, parent.y + parent.height);
  result.x = x0;
  result.y = y0;
  result.width = std::max(0.0f, x1 - x0);
  result.height = std::max(0.0f, y1 - y0);
  return result;
}

/** Paint `box`'s children following z-index: negative z first (ascending), then
 * auto/zero in document order, then positive z (ascending), so higher stacks
 * paint last. */
void paintChildren(const LayoutBox *box, DisplayList &out, const PaintClip &clip,
                   const paint_detail::FilterChain &chain, float viewportWidth,
                   float viewportHeight) {
  std::vector<const LayoutBox *> children;
  for (const std::unique_ptr<LayoutBox> &child : box->children) {
    if (child->display == Display::None || child->style == nullptr) continue;
    children.push_back(child.get());
  }
  std::stable_sort(children.begin(), children.end(), [](const LayoutBox *a, const LayoutBox *b) {
    return zIndexOf(a) < zIndexOf(b);
  });
  for (const LayoutBox *child : children)
    paintBox(child, out, clip, chain, viewportWidth, viewportHeight);
}

void addTextRuns(const LayoutBox *box, const XtStyle &style, DisplayList &out,
                 const PaintClip &clip, const paint_detail::FilterChain &chain,
                 float viewportWidth, float viewportHeight) {
  if (box->fragments.empty() || style.color.a <= 0.0f) return;
  FontSpec spec = specOf(style);
  float ascent = 0;
  xt_text_metrics(spec, &ascent, nullptr, nullptr);
  if (ascent <= 0.0f) ascent = style.font_size * 0.8f;
  Color base = paint_detail::filterColor(chain, style.color);
  for (const TextFragment &fragment : box->fragments) {
    if (fragment.text.empty()) continue;
    paint_detail::addTextRun(out, fragment.x, fragment.y + ascent, fragment.width,
                             fragment.height, fragment.text, spec, base, box->world, clip, style,
                             chain, viewportWidth, viewportHeight);
  }
}

void paintBox(const LayoutBox *box, DisplayList &out, const PaintClip &clip,
              const paint_detail::FilterChain &parentChain, float viewportWidth,
              float viewportHeight) {
  if (box == nullptr) return;
  const XtStyle *style = box->style;

  /* A fully transparent or hidden box paints nothing — not itself, and not its
   * subtree. */
  if (style != nullptr && (style->opacity <= 0.0f || !style->visible)) return;

  /* A text box points at its parent element's style; its filters were already
   * folded into `parentChain`, so applying them again would double them. */
  paint_detail::FilterChain chain = parentChain;
  if (style != nullptr && !box->is_text && !style->filters.empty()) {
    chain = paint_detail::extendFilter(parentChain, style->filters, *style, viewportWidth,
                                       viewportHeight);
  }

  PaintClip own = clip;
  if (style != nullptr && box->display != Display::None && !box->is_text) {
    if (style->clip_path.type != ClipPathSpec::Type::None) {
      int index = paint_detail::addClipPathForBox(out, *box, *style, viewportWidth, viewportHeight);
      own = paint_detail::addClipPath(own, index);
    }
  }

  if (style != nullptr && box->display != Display::None) {
    if (box->is_text) {
      addTextRuns(box, *style, out, own, chain, viewportWidth, viewportHeight);
    } else {
      /* Drop shadows sit behind the box's own background but above earlier
       * siblings. */
      paint_detail::addBoxDropShadows(out, *box, *style, chain, own);
      paint_detail::paintBackgrounds(out, *box, *style, chain, own, viewportWidth,
                                     viewportHeight);
      const Matrix2D &matrix = box->world;
      if (style->border_style != BorderStyle::None && style->has_border_color) {
        const Color bc = paint_detail::filterColor(chain, style->border_color);
        float top = box->border_top;
        float right = box->border_right;
        float bottom = box->border_bottom;
        float left = box->border_left;
        float inner_height = box->height - top - bottom;
        paint_detail::addRectFiltered(out, box->x, box->y, box->width, top, 0.0f, bc, own,
                                      matrix, chain);
        paint_detail::addRectFiltered(out, box->x, box->y + box->height - bottom, box->width,
                                      bottom, 0.0f, bc, own, matrix, chain);
        paint_detail::addRectFiltered(out, box->x, box->y + top, left, inner_height, 0.0f, bc,
                                      own, matrix, chain);
        paint_detail::addRectFiltered(out, box->x + box->width - right, box->y + top, right,
                                      inner_height, 0.0f, bc, own, matrix, chain);
      }
      if (box->is_canvas && box->canvas_id != 0 && box->content_width > 0.0f &&
          box->content_height > 0.0f) {
        PaintImage image;
        image.x = box->content_x;
        image.y = box->content_y;
        image.width = box->content_width;
        image.height = box->content_height;
        image.canvas_id = box->canvas_id;
        image.transform = matrix;
        paint_detail::addImage(out, image, own, chain);
      } else if (box->is_media && box->media_id != 0 && box->content_width > 0.0f &&
                 box->content_height > 0.0f) {
        PaintImage image;
        image.x = box->content_x;
        image.y = box->content_y;
        image.width = box->content_width;
        image.height = box->content_height;
        image.media_id = box->media_id;
        image.transform = matrix;
        paint_detail::addImage(out, image, own, chain);
      } else if (box->is_inline_svg && !box->svg_markup.empty() && box->content_width > 0.0f &&
                 box->content_height > 0.0f) {
        PaintImage image;
        image.x = box->content_x;
        image.y = box->content_y;
        image.width = box->content_width;
        image.height = box->content_height;
        image.svg = box->svg_markup;
        image.transform = matrix;
        paint_detail::addImage(out, image, own, chain);
      } else if (box->is_image && !box->image_src.empty() && box->content_width > 0.0f &&
                 box->content_height > 0.0f) {
        PaintImage image;
        image.x = box->content_x;
        image.y = box->content_y;
        image.width = box->content_width;
        image.height = box->content_height;
        image.src = box->image_src;
        image.transform = matrix;
        paint_detail::addImage(out, image, own, chain);
      }
    }
  }

  /* Drop shadows belong to the box that declared them: its silhouette has
   * already been emitted, so descendants must not repeat it. */
  paint_detail::FilterChain childChain = chain;
  childChain.shadows.clear();
  paintChildren(box, out, clipForChildren(box, own), childChain, viewportWidth, viewportHeight);
}

}  // namespace

void xt_paint_build(const LayoutBox *root, DisplayList &out, float viewportWidth,
                    float viewportHeight) {
  PaintClip none;
  paint_detail::FilterChain chain;
  if (root != nullptr && viewportWidth <= 0.0f) {
    viewportWidth = root->cb_width;
    viewportHeight = root->cb_height;
  }
  paintBox(root, out, none, chain, viewportWidth, viewportHeight);
}

void xt_paint_translate(DisplayList &list, float dx, float dy) {
  if (dx == 0.0f && dy == 0.0f) return;
  for (PaintRect &rect : list.rects) {
    rect.x += dx;
    rect.y += dy;
  }
  for (PaintText &text : list.texts) {
    text.x += dx;
    text.baseline += dy;
  }
  for (PaintImage &image : list.images) {
    image.x += dx;
    image.y += dy;
  }
  for (PaintOp &op : list.ops) {
    op.clip.x += dx;
    op.clip.y += dy;
  }
  for (ClipPathGeometry &path : list.clip_paths) {
    for (ClipTriangle &triangle : path.triangles) {
      for (int i = 0; i < 3; i++) {
        triangle.x[i] += dx;
        triangle.y[i] += dy;
      }
    }
  }
}

std::string DisplayList::dump() const {
  std::string text;
  char buffer[320];
  for (const PaintOp &op : ops) {
    std::string prefix;
    if (op.clip.active) {
      std::snprintf(buffer, sizeof(buffer), "clip=%.1f,%.1f,%.1f,%.1f ", op.clip.x, op.clip.y,
                    op.clip.width, op.clip.height);
      prefix = buffer;
    }
    if (op.clip.path_count > 0) {
      prefix += "clip-path=";
      for (int i = 0; i < op.clip.path_count; i++) {
        if (i > 0) prefix += ",";
        prefix += std::to_string(op.clip.paths[i]);
      }
      prefix += " ";
    }
    std::string suffix;
    if (op.kind == PaintKind::Rect) {
      suffix = transformSuffix(rects[op.index].transform);
      const PaintRect &rect = rects[op.index];
      std::snprintf(buffer, sizeof(buffer),
                    "rect x=%.1f y=%.1f w=%.1f h=%.1f r=%.1f color=%s", rect.x, rect.y,
                    rect.width, rect.height, rect.radius, formatColor(rect.color).c_str());
      text += prefix + buffer;
    } else if (op.kind == PaintKind::Text) {
      suffix = transformSuffix(texts[op.index].transform);
      const PaintText &run = texts[op.index];
      std::snprintf(buffer, sizeof(buffer),
                    "text x=%.1f baseline=%.1f size=%.1f color=%s \"%s\"", run.x, run.baseline,
                    run.font.pixel_size, formatColor(run.color).c_str(), run.text.c_str());
      text += prefix + buffer;
      if (run.shadow) text += " shadow=1";
    } else {
      const PaintImage &image = images[op.index];
      suffix = transformSuffix(image.transform);
      if (image.media_id != 0) {
        std::snprintf(buffer, sizeof(buffer), "image x=%.1f y=%.1f w=%.1f h=%.1f media=%llu",
                      image.x, image.y, image.width, image.height,
                      (unsigned long long)image.media_id);
      } else if (!image.svg.empty()) {
        std::snprintf(buffer, sizeof(buffer),
                      "image x=%.1f y=%.1f w=%.1f h=%.1f inline-svg=%zu", image.x, image.y,
                      image.width, image.height, image.svg.size());
      } else if (image.canvas_id != 0) {
        std::snprintf(buffer, sizeof(buffer), "image x=%.1f y=%.1f w=%.1f h=%.1f canvas=%llu",
                      image.x, image.y, image.width, image.height,
                      (unsigned long long)image.canvas_id);
      } else {
        std::snprintf(buffer, sizeof(buffer), "image x=%.1f y=%.1f w=%.1f h=%.1f src=%s", image.x,
                      image.y, image.width, image.height, image.src.c_str());
      }
      text += prefix + buffer;
    }
    text += suffix + "\n";
  }
  return text;
}

}  // namespace xtgui
