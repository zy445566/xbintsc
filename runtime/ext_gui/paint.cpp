/*
 * xbintsc GUI engine — display list construction.
 */

#include "paint.h"

#include <algorithm>
#include <cstdio>
#include <utility>

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

FontSpec specOf(const XtStyle &style) {
  FontSpec spec;
  spec.family = style.font_family;
  spec.pixel_size = style.font_size;
  spec.weight = style.font_weight;
  spec.italic = style.font_style == FontStyle::Italic;
  return spec;
}

void addRect(DisplayList &out, float x, float y, float width, float height, float radius,
             const Color &color, const PaintClip &clip) {
  if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f) return;
  PaintRect rect;
  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;
  rect.radius = radius;
  rect.color = color;
  out.rects.push_back(rect);
  out.ops.push_back(PaintOp{PaintKind::Rect, (uint32_t)(out.rects.size() - 1), clip});
}

void addTextRuns(const LayoutBox *box, const XtStyle &style, DisplayList &out,
                 const PaintClip &clip) {
  if (box->fragments.empty() || style.color.a <= 0.0f) return;
  FontSpec spec = specOf(style);
  float ascent = 0;
  xt_text_metrics(spec, &ascent, nullptr, nullptr);
  if (ascent <= 0.0f) ascent = style.font_size * 0.8f;
  for (const TextFragment &fragment : box->fragments) {
    if (fragment.text.empty()) continue;
    PaintText run;
    run.x = fragment.x;
    run.baseline = fragment.y + ascent;
    run.width = fragment.width;
    run.height = fragment.height;
    run.color = style.color;
    run.text = fragment.text;
    run.font = spec;
    out.texts.push_back(std::move(run));
    out.ops.push_back(PaintOp{PaintKind::Text, (uint32_t)(out.texts.size() - 1), clip});
  }
}

void addImage(const PaintImage &image, DisplayList &out, const PaintClip &clip) {
  out.images.push_back(image);
  out.ops.push_back(PaintOp{PaintKind::Image, (uint32_t)(out.images.size() - 1), clip});
}

void paintBox(const LayoutBox *box, DisplayList &out, const PaintClip &clip);

/** The clip a box imposes on its content: its padding box, intersected with the
 * clip already in effect. Only `overflow: hidden/scroll/auto` clips. */
PaintClip clipForChildren(const LayoutBox *box, const PaintClip &parent) {
  if (box->style == nullptr || box->style->overflow == Overflow::Visible) return parent;
  float x = box->x + box->border_left;
  float y = box->y + box->border_top;
  float width = box->width - box->border_left - box->border_right;
  float height = box->height - box->border_top - box->border_bottom;
  if (width < 0) width = 0;
  if (height < 0) height = 0;
  if (!parent.active) return PaintClip{true, x, y, width, height};
  float x0 = std::max(x, parent.x);
  float y0 = std::max(y, parent.y);
  float x1 = std::min(x + width, parent.x + parent.width);
  float y1 = std::min(y + height, parent.y + parent.height);
  return PaintClip{true, x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

int zIndexOf(const LayoutBox *box) {
  if (box->style == nullptr || !box->style->has_z_index) return 0;
  return box->style->z_index;
}

/** Paint `box`'s children following z-index: negative z first (ascending), then
 * auto/zero in document order, then positive z (ascending), so higher stacks
 * paint last. */
void paintChildren(const LayoutBox *box, DisplayList &out, const PaintClip &clip) {
  std::vector<const LayoutBox *> children;
  for (const std::unique_ptr<LayoutBox> &child : box->children) {
    if (child->display == Display::None || child->style == nullptr) continue;
    children.push_back(child.get());
  }
  std::stable_sort(children.begin(), children.end(), [](const LayoutBox *a, const LayoutBox *b) {
    return zIndexOf(a) < zIndexOf(b);
  });
  for (const LayoutBox *child : children) paintBox(child, out, clip);
}

void paintBox(const LayoutBox *box, DisplayList &out, const PaintClip &clip) {
  if (box == nullptr) return;
  const XtStyle *style = box->style;

  /* A fully transparent box paints nothing — not itself, and not its subtree. */
  if (style != nullptr && style->opacity <= 0.0f) return;

  if (style != nullptr && box->display != Display::None) {
    if (box->is_text) {
      addTextRuns(box, *style, out, clip);
    } else {
      /* Background first, then the border ring, then replaced content. Children
       * are clipped by this box's padding box when it establishes one. */
      if (style->has_background) {
        addRect(out, box->x, box->y, box->width, box->height, style->border_radius,
                style->background, clip);
      }
      if (style->border_style != BorderStyle::None && style->has_border_color) {
        const Color bc = style->border_color;
        float top = box->border_top;
        float right = box->border_right;
        float bottom = box->border_bottom;
        float left = box->border_left;
        float inner_height = box->height - top - bottom;
        addRect(out, box->x, box->y, box->width, top, 0.0f, bc, clip);
        addRect(out, box->x, box->y + box->height - bottom, box->width, bottom, 0.0f, bc, clip);
        addRect(out, box->x, box->y + top, left, inner_height, 0.0f, bc, clip);
        addRect(out, box->x + box->width - right, box->y + top, right, inner_height, 0.0f, bc,
                clip);
      }
      if (box->is_canvas && box->canvas_id != 0 && box->content_width > 0.0f &&
          box->content_height > 0.0f) {
        PaintImage image;
        image.x = box->content_x;
        image.y = box->content_y;
        image.width = box->content_width;
        image.height = box->content_height;
        image.canvas_id = box->canvas_id;
        addImage(image, out, clip);
      } else if (box->is_media && box->media_id != 0 && box->content_width > 0.0f &&
                 box->content_height > 0.0f) {
        PaintImage image;
        image.x = box->content_x;
        image.y = box->content_y;
        image.width = box->content_width;
        image.height = box->content_height;
        image.media_id = box->media_id;
        addImage(image, out, clip);
      } else if (box->is_image && !box->image_src.empty() && box->content_width > 0.0f &&
                 box->content_height > 0.0f) {
        PaintImage image;
        image.x = box->content_x;
        image.y = box->content_y;
        image.width = box->content_width;
        image.height = box->content_height;
        image.src = box->image_src;
        addImage(image, out, clip);
      }
    }
  }

  paintChildren(box, out, clipForChildren(box, clip));
}

}  // namespace

void xt_paint_build(const LayoutBox *root, DisplayList &out) {
  PaintClip none;
  paintBox(root, out, none);
}

std::string DisplayList::dump() const {
  std::string text;
  char buffer[224];
  for (const PaintOp &op : ops) {
    std::string prefix;
    if (op.clip.active) {
      std::snprintf(buffer, sizeof(buffer), "clip=%.1f,%.1f,%.1f,%.1f ", op.clip.x, op.clip.y,
                    op.clip.width, op.clip.height);
      prefix = buffer;
    }
    if (op.kind == PaintKind::Rect) {
      const PaintRect &rect = rects[op.index];
      std::snprintf(buffer, sizeof(buffer),
                    "rect x=%.1f y=%.1f w=%.1f h=%.1f r=%.1f color=%s\n", rect.x, rect.y,
                    rect.width, rect.height, rect.radius, formatColor(rect.color).c_str());
      text += prefix + buffer;
    } else if (op.kind == PaintKind::Text) {
      const PaintText &run = texts[op.index];
      std::snprintf(buffer, sizeof(buffer),
                    "text x=%.1f baseline=%.1f size=%.1f color=%s \"%s\"\n", run.x, run.baseline,
                    run.font.pixel_size, formatColor(run.color).c_str(), run.text.c_str());
      text += prefix + buffer;
    } else {
      const PaintImage &image = images[op.index];
      if (image.media_id != 0) {
        std::snprintf(buffer, sizeof(buffer), "image x=%.1f y=%.1f w=%.1f h=%.1f media=%llu\n",
                      image.x, image.y, image.width, image.height,
                      (unsigned long long)image.media_id);
      } else if (image.canvas_id != 0) {
        std::snprintf(buffer, sizeof(buffer), "image x=%.1f y=%.1f w=%.1f h=%.1f canvas=%llu\n",
                      image.x, image.y, image.width, image.height,
                      (unsigned long long)image.canvas_id);
      } else {
        std::snprintf(buffer, sizeof(buffer), "image x=%.1f y=%.1f w=%.1f h=%.1f src=%s\n",
                      image.x, image.y, image.width, image.height, image.src.c_str());
      }
      text += prefix + buffer;
    }
  }
  return text;
}

}  // namespace xtgui
