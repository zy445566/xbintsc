/*
 * xbintsc GUI engine — form-control painting.
 *
 * The control's value, selection, caret and type-specific decorations
 * (checkbox tick, radio dot, range thumb, select arrow, colour swatch) are
 * painted from `Node::form`. Text is clipped to the content box so a long value
 * cannot bleed over the border.
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "form_state.h"
#include "layout.h"

namespace xtgui {

namespace {

FontSpec specOf(const XtStyle &style) {
  FontSpec spec;
  spec.family = style.font_family;
  spec.pixel_size = style.font_size;
  spec.weight = style.font_weight;
  spec.italic = style.font_style == FontStyle::Italic;
  spec.letter_spacing = style.letter_spacing.unit == Unit::Px ? style.letter_spacing.value : 0.0f;
  return spec;
}

float lineHeightOf(const XtStyle &style) { return style.font_size * 1.2f; }

void addRun(DisplayList &out, float x, float baseline, const std::string &text,
            const XtStyle &style, const Color &color, const LayoutBox &box, const PaintClip &clip,
            const paint_detail::FilterChain &chain, float viewportWidth, float viewportHeight) {
  if (text.empty() || color.a <= 0.0f) return;
  FontSpec spec = specOf(style);
  float width = xt_text_measure_width(text, spec);
  float height = lineHeightOf(style);
  paint_detail::addTextRun(out, x, baseline, width, height, text, spec, color, box.world, clip,
                           style, chain, viewportWidth, viewportHeight);
}

float runWidth(const std::string &text, const XtStyle &style) {
  return xt_text_measure_width(text, specOf(style));
}

float prefixWidth(const std::string &text, size_t units, const XtStyle &style) {
  return runWidth(xt_form_utf16_slice(text, 0, units), style);
}

PaintClip intersectContent(const LayoutBox &box, const PaintClip &clip) {
  PaintClip result = clip;
  float x = box.content_x;
  float y = box.content_y;
  float width = box.content_width;
  float height = box.content_height;
  if (width < 0.0f) width = 0.0f;
  if (height < 0.0f) height = 0.0f;
  if (!clip.active) {
    result.active = true;
    result.x = x;
    result.y = y;
    result.width = width;
    result.height = height;
    return result;
  }
  float x0 = std::max(x, clip.x);
  float y0 = std::max(y, clip.y);
  float x1 = std::min(x + width, clip.x + clip.width);
  float y1 = std::min(y + height, clip.y + clip.height);
  result.x = x0;
  result.y = y0;
  result.width = std::max(0.0f, x1 - x0);
  result.height = std::max(0.0f, y1 - y0);
  return result;
}

void addRect(DisplayList &out, float x, float y, float width, float height, float radius,
             const Color &color, const PaintClip &clip, const LayoutBox &box,
             const paint_detail::FilterChain &chain) {
  if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f) return;
  paint_detail::addRectFiltered(out, x, y, width, height, radius, color, clip, box.world, chain);
}

Color withAlpha(Color color, float alpha) {
  color.a *= alpha;
  return color;
}

Color shade(const XtStyle &style, bool disabled, float alpha) {
  Color color = style.color;
  if (disabled) color = Color{0.55f, 0.55f, 0.55f, color.a};
  color.a *= alpha;
  return color;
}

/** Split `text` into visual lines (`\n`) and report each line's UTF-16 range. */
struct VisualLine {
  size_t start;
  size_t end;
  std::string text;
};

std::vector<VisualLine> splitLines(const std::string &text) {
  std::vector<VisualLine> lines;
  size_t byte_start = 0;
  size_t units = 0;
  size_t line_start_units = 0;
  size_t offset = 0;
  while (offset <= text.size()) {
    if (offset == text.size() || text[offset] == '\n') {
      VisualLine line;
      line.start = line_start_units;
      line.end = units;
      line.text = text.substr(byte_start, offset - byte_start);
      lines.push_back(line);
      byte_start = offset + 1;
      line_start_units = units;
      if (offset == text.size()) break;
      offset++;
      continue;
    }
    size_t width = 1;
    uint32_t code = (unsigned char)text[offset];
    if (code < 0x80) width = 1;
    else if ((code & 0xE0) == 0xC0) width = 2;
    else if ((code & 0xF0) == 0xE0) width = 3;
    else width = 4;
    units += (code >= 0xF0) ? 2 : 1;
    offset += width;
  }
  return lines;
}

void paintTextControl(const LayoutBox &box, const XtStyle &style, const FormState &state,
                      DisplayList &out, const PaintClip &clip,
                      const paint_detail::FilterChain &chain, float viewportWidth,
                      float viewportHeight) {
  bool disabled = xt_form_is_disabled(box.node);
  bool password = xt_form_type(box.node) == "password";
  std::string display = state.value;
  size_t caret = (size_t)state.selection_end;
  if (state.composing) {
    size_t length = xt_form_utf16_length(state.value);
    size_t start = std::min((size_t)state.selection_start, length);
    size_t end = std::min((size_t)state.selection_end, length);
    display = xt_form_utf16_slice(state.value, 0, start) + state.composition +
              xt_form_utf16_slice(state.value, end, length);
    caret = start + xt_form_utf16_length(state.composition);
  }
  if (password) display = xt_form_mask(display);
  size_t display_units = xt_form_utf16_length(display);
  if (caret > display_units) caret = display_units;

  float line_height = lineHeightOf(style);
  FontSpec spec = specOf(style);
  float ascent = 0.0f;
  xt_text_metrics(spec, &ascent, nullptr, nullptr);
  if (ascent <= 0.0f) ascent = style.font_size * 0.8f;

  bool placeholder = display.empty() && !state.composing && !state.focused;
  if (state.value.empty() && !state.composing) {
    const std::string *hint = box.node->attr("placeholder");
    if (hint != nullptr && !hint->empty() && state.value.empty()) {
      std::string text = *hint;
      float baseline = box.content_y + (box.content_height - line_height) / 2.0f + ascent;
      addRun(out, box.content_x, baseline, text, style, shade(style, disabled, 0.45f), box, clip,
             chain, viewportWidth, viewportHeight);
      placeholder = true;
    }
  }

  bool multiline = xt_form_is_textarea(box.node);
  std::vector<VisualLine> lines = multiline ? splitLines(display) : std::vector<VisualLine>();
  if (!multiline) {
    VisualLine line;
    line.start = 0;
    line.end = display_units;
    line.text = display;
    lines.push_back(line);
  }

  size_t selection_start = (size_t)state.selection_start;
  size_t selection_end = (size_t)state.selection_end;
  if (selection_end < selection_start) std::swap(selection_start, selection_end);
  bool has_selection = state.focused && selection_end > selection_start && !state.composing;
  Color highlight{0.26f, 0.55f, 1.0f, 0.35f};

  if (!placeholder) {
    for (size_t index = 0; index < lines.size(); index++) {
      const VisualLine &line = lines[index];
      float y = multiline ? box.content_y + (float)index * line_height
                          : box.content_y + (box.content_height - line_height) / 2.0f;
      if (multiline && y >= box.content_y + box.content_height) break;
      float baseline = y + ascent;
      if (has_selection) {
        size_t start = std::max(selection_start, line.start);
        size_t end = std::min(selection_end, line.end);
        if (end > start) {
          float x0 = box.content_x + prefixWidth(line.text, start - line.start, style);
          float x1 = box.content_x + prefixWidth(line.text, end - line.start, style);
          addRect(out, x0, y, x1 - x0, line_height, 0.0f, highlight, clip, box, chain);
        }
      }
      addRun(out, box.content_x, baseline, line.text, style, shade(style, disabled, 1.0f), box,
             clip, chain, viewportWidth, viewportHeight);
      if (state.composing) {
        float x0 = box.content_x;
        float x1 = box.content_x + runWidth(line.text, style);
        addRect(out, x0, y + line_height - 1.0f, x1 - x0, 1.0f, 0.0f,
                shade(style, disabled, 0.9f), clip, box, chain);
      }
    }
  }

  if (state.focused && !state.composing && !has_selection) {
    size_t line_index = 0;
    size_t column = caret;
    for (size_t index = 0; index < lines.size(); index++) {
      if (caret >= lines[index].start && caret <= lines[index].end) {
        line_index = index;
        column = caret - lines[index].start;
        break;
      }
    }
    float y = multiline ? box.content_y + (float)line_index * line_height
                        : box.content_y + (box.content_height - line_height) / 2.0f;
    float x = box.content_x + prefixWidth(lines[line_index].text, column, style);
    addRect(out, x, y + 1.0f, 1.5f, line_height - 2.0f, 0.0f, shade(style, disabled, 1.0f), clip,
            box, chain);
  }
}

void paintCheckbox(const LayoutBox &box, const XtStyle &style, const FormState &state,
                   DisplayList &out, const PaintClip &clip, const paint_detail::FilterChain &chain,
                   bool radio) {
  bool disabled = xt_form_is_disabled(box.node);
  float size = std::max(13.0f, style.font_size * 0.9f);
  float x = box.content_x + (box.content_width - size) / 2.0f;
  float y = box.content_y + (box.content_height - size) / 2.0f;
  Color border = disabled ? Color{0.6f, 0.6f, 0.6f, 1.0f} : Color{0.34f, 0.34f, 0.34f, 1.0f};
  Color fill = disabled ? Color{0.94f, 0.94f, 0.94f, 1.0f} : Color{1.0f, 1.0f, 1.0f, 1.0f};
  float radius = radio ? size / 2.0f : 2.0f;
  addRect(out, x, y, size, size, radius, border, clip, box, chain);
  addRect(out, x + 1.0f, y + 1.0f, size - 2.0f, size - 2.0f, std::max(0.0f, radius - 1.0f), fill,
          clip, box, chain);
  if (state.checked) {
    Color accent = disabled ? Color{0.6f, 0.6f, 0.6f, 1.0f} : Color{0.17f, 0.42f, 0.69f, 1.0f};
    if (radio) {
      float inner = size - 8.0f;
      addRect(out, x + 4.0f, y + 4.0f, inner, inner, inner / 2.0f, accent, clip, box, chain);
    } else {
      addRect(out, x + 3.0f, y + 3.0f, size - 6.0f, size - 6.0f, 1.0f, accent, clip, box, chain);
    }
  }
}

void paintRange(const LayoutBox &box, const XtStyle &style, const FormState &state,
                DisplayList &out, const PaintClip &clip, const paint_detail::FilterChain &chain) {
  bool disabled = xt_form_is_disabled(box.node);
  double minimum = 0.0;
  double maximum = 100.0;
  const std::string *min_attr = box.node->attr("min");
  const std::string *max_attr = box.node->attr("max");
  if (min_attr != nullptr) minimum = std::strtod(min_attr->c_str(), nullptr);
  if (max_attr != nullptr) maximum = std::strtod(max_attr->c_str(), nullptr);
  if (maximum <= minimum) maximum = minimum + 1.0;
  double value = std::strtod(state.value.c_str(), nullptr);
  float fraction = (float)((value - minimum) / (maximum - minimum));
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  float track_height = 4.0f;
  float track_y = box.content_y + (box.content_height - track_height) / 2.0f;
  float thumb = 12.0f;
  float travel = std::max(0.0f, box.content_width - thumb);
  float thumb_x = box.content_x + fraction * travel;
  float thumb_y = box.content_y + (box.content_height - thumb) / 2.0f;
  Color track = disabled ? Color{0.8f, 0.8f, 0.8f, 1.0f} : Color{0.78f, 0.78f, 0.78f, 1.0f};
  Color accent = disabled ? Color{0.6f, 0.6f, 0.6f, 1.0f} : Color{0.17f, 0.42f, 0.69f, 1.0f};
  addRect(out, box.content_x, track_y, box.content_width, track_height, 2.0f, track, clip, box,
          chain);
  addRect(out, box.content_x, track_y, thumb_x - box.content_x + thumb / 2.0f, track_height, 2.0f,
          accent, clip, box, chain);
  addRect(out, thumb_x, thumb_y, thumb, thumb, thumb / 2.0f, accent, clip, box, chain);
}

void paintColorSwatch(const LayoutBox &box, const XtStyle &style, const FormState &state,
                      DisplayList &out, const PaintClip &clip,
                      const paint_detail::FilterChain &chain) {
  float size = std::min(box.content_width, box.content_height);
  if (size <= 0.0f) return;
  float x = box.content_x + (box.content_width - size) / 2.0f;
  float y = box.content_y + (box.content_height - size) / 2.0f;
  Color color = xt_css_parse_color(state.value);
  if (!color.valid) color = Color{0.0f, 0.0f, 0.0f, 1.0f};
  addRect(out, x, y, size, size, 2.0f, Color{0.34f, 0.34f, 0.34f, 1.0f}, clip, box, chain);
  addRect(out, x + 1.0f, y + 1.0f, size - 2.0f, size - 2.0f, 1.0f, color, clip, box, chain);
}

void paintSelect(const LayoutBox &box, const XtStyle &style, const FormState &state,
                 DisplayList &out, const PaintClip &clip, const paint_detail::FilterChain &chain,
                 float viewportWidth, float viewportHeight) {
  const Node *option = xt_form_selected_option(box.node);
  std::string label = option != nullptr ? xt_form_option_label(option) : "";
  float line_height = lineHeightOf(style);
  FontSpec spec = specOf(style);
  float ascent = 0.0f;
  xt_text_metrics(spec, &ascent, nullptr, nullptr);
  if (ascent <= 0.0f) ascent = style.font_size * 0.8f;
  float baseline = box.content_y + (box.content_height - line_height) / 2.0f + ascent;
  addRun(out, box.content_x, baseline, label, style, shade(style, xt_form_is_disabled(box.node), 1.0f),
         box, clip, chain, viewportWidth, viewportHeight);
  /* A downward chevron on the right, like a native select. */
  float arrow_size = 5.0f;
  float ax = box.content_x + box.content_width - 10.0f;
  float ay = box.content_y + (box.content_height - arrow_size) / 2.0f + 1.0f;
  addRect(out, ax, ay, arrow_size, 1.5f, 0.0f, Color{0.25f, 0.25f, 0.25f, 1.0f}, clip, box, chain);
  addRect(out, ax + 0.8f, ay + 1.2f, arrow_size - 1.6f, 1.5f, 0.0f,
          Color{0.25f, 0.25f, 0.25f, 1.0f}, clip, box, chain);
}

void paintButtonLabel(const LayoutBox &box, const LayoutBox &shape, const XtStyle &style,
                      const std::string &label, DisplayList &out, const PaintClip &clip,
                      const paint_detail::FilterChain &chain, float viewportWidth,
                      float viewportHeight) {
  float width = runWidth(label, style);
  float line_height = lineHeightOf(style);
  FontSpec spec = specOf(style);
  float ascent = 0.0f;
  xt_text_metrics(spec, &ascent, nullptr, nullptr);
  if (ascent <= 0.0f) ascent = style.font_size * 0.8f;
  float x = box.content_x + (box.content_width - width) / 2.0f;
  float baseline = box.content_y + (box.content_height - line_height) / 2.0f + ascent;
  addRun(out, x, baseline, label, style, shade(style, xt_form_is_disabled(box.node), 1.0f), shape,
         clip, chain, viewportWidth, viewportHeight);
}

}  // namespace

namespace paint_detail {

void xt_paint_form_control(const LayoutBox &box, const XtStyle &style, DisplayList &out,
                           const PaintClip &clip, const FilterChain &chain, float viewportWidth,
                           float viewportHeight) {
  if (box.node == nullptr || !xt_form_is_control(box.node)) return;
  const FormState *state = xt_form_state(box.node);
  if (state == nullptr) return;
  PaintClip content = intersectContent(box, clip);
  std::string type = xt_form_type(box.node);
  if (xt_form_is_textarea(box.node) || xt_form_is_text_like(box.node)) {
    paintTextControl(box, style, *state, out, content, chain, viewportWidth, viewportHeight);
    return;
  }
  if (type == "checkbox" || type == "radio") {
    paintCheckbox(box, style, *state, out, content, chain, type == "radio");
    return;
  }
  if (type == "range") {
    paintRange(box, style, *state, out, content, chain);
    return;
  }
  if (type == "color") {
    paintColorSwatch(box, style, *state, out, content, chain);
    return;
  }
  if (xt_form_is_select(box.node)) {
    paintSelect(box, style, *state, out, content, chain, viewportWidth, viewportHeight);
    return;
  }
  if (xt_form_is_button_like(box.node)) {
    paintButtonLabel(box, box, style, xt_form_button_label(box.node), out, content, chain,
                     viewportWidth, viewportHeight);
    return;
  }
  if (type == "file") {
    paintButtonLabel(box, box, style, "Choose File", out, content, chain, viewportWidth,
                     viewportHeight);
  }
}

}  // namespace paint_detail

}  // namespace xtgui
