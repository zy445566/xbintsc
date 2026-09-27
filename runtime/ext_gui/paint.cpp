/*
 * xbintsc GUI engine — display list construction.
 */

#include "paint.h"

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
             const Color &color) {
  if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f) return;
  PaintRect rect;
  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;
  rect.radius = radius;
  rect.color = color;
  out.rects.push_back(rect);
}

void addTextRuns(const LayoutBox *box, const XtStyle &style, DisplayList &out) {
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
  }
}

void paintBox(const LayoutBox *box, DisplayList &out) {
  if (box == nullptr) return;
  const XtStyle *style = box->style;

  if (style != nullptr && box->display != Display::None) {
    if (box->is_text) {
      addTextRuns(box, *style, out);
    } else {
      /* Background first, then the (sharp-cornered) border ring on top, then the
       * children, so their content paints over the padding. */
      if (style->has_background) {
        addRect(out, box->x, box->y, box->width, box->height, style->border_radius,
                style->background);
      }
      if (style->border_style != BorderStyle::None && style->has_border_color) {
        const Color bc = style->border_color;
        float top = box->border_top;
        float right = box->border_right;
        float bottom = box->border_bottom;
        float left = box->border_left;
        float inner_height = box->height - top - bottom;
        /* Top/bottom span the whole width and overlap the side edges, which is
         * harmless (same colour) and avoids seams from float rounding. */
        addRect(out, box->x, box->y, box->width, top, 0.0f, bc);
        addRect(out, box->x, box->y + box->height - bottom, box->width, bottom, 0.0f, bc);
        addRect(out, box->x, box->y + top, left, inner_height, 0.0f, bc);
        addRect(out, box->x + box->width - right, box->y + top, right, inner_height, 0.0f, bc);
      }
    }
  }

  for (const std::unique_ptr<LayoutBox> &child : box->children) paintBox(child.get(), out);
}

}  // namespace

void xt_paint_build(const LayoutBox *root, DisplayList &out) { paintBox(root, out); }

std::string DisplayList::dump() const {
  std::string text;
  char buffer[192];
  for (const PaintRect &rect : rects) {
    std::snprintf(buffer, sizeof(buffer), "rect x=%.1f y=%.1f w=%.1f h=%.1f r=%.1f color=%s\n",
                  rect.x, rect.y, rect.width, rect.height, rect.radius,
                  formatColor(rect.color).c_str());
    text += buffer;
  }
  for (const PaintText &run : texts) {
    std::snprintf(buffer, sizeof(buffer),
                  "text x=%.1f baseline=%.1f size=%.1f color=%s \"%s\"\n", run.x, run.baseline,
                  run.font.pixel_size, formatColor(run.color).c_str(), run.text.c_str());
    text += buffer;
  }
  return text;
}

}  // namespace xtgui
