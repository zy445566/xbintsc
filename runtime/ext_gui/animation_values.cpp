/*
 * xbintsc GUI engine — animatable property values (see animation_values.h).
 */

#include "animation_values.h"

#include <cmath>
#include <cstring>

namespace xtgui {

namespace {

float mix(float from, float to, float progress) { return from + (to - from) * progress; }

Color mixColor(const Color &from, const Color &to, float progress) {
  Color out;
  out.r = mix(from.r, to.r, progress);
  out.g = mix(from.g, to.g, progress);
  out.b = mix(from.b, to.b, progress);
  out.a = mix(from.a, to.a, progress);
  out.valid = true;
  return out;
}

bool colorEqual(const Color &a, const Color &b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool lengthEqual(const Length &a, const Length &b) {
  if (a.unit != b.unit || a.value != b.value || a.op != b.op || a.args.size() != b.args.size())
    return false;
  for (size_t i = 0; i < a.args.size(); i++) {
    if (!lengthEqual(a.args[i], b.args[i])) return false;
  }
  return true;
}

bool interpolateLength(const Length &from, const Length &to, float progress, Length *out) {
  if (from.unit == to.unit) {
    if (!from.args.empty() || !to.args.empty()) {
      if (from.args.size() != to.args.size() || from.op != to.op) return false;
      *out = from;
      for (size_t i = 0; i < out->args.size(); i++) {
        if (!interpolateLength(from.args[i], to.args[i], progress, &out->args[i])) return false;
      }
      return true;
    }
    *out = from;
    out->value = mix(from.value, to.value, progress);
    return true;
  }
  /* A unit change needs the used value, which only layout knows. CSS falls back
   * to discrete interpolation for such pairs; so do we. */
  if ((from.unit == Unit::Px || from.unit == Unit::Number) &&
      (to.unit == Unit::Px || to.unit == Unit::Number)) {
    *out = from;
    out->unit = Unit::Px;
    out->value = mix(from.value, to.value, progress);
    return true;
  }
  return false;
}

bool shadowLengthsEqual(const Length &a, const Length &b) { return lengthEqual(a, b); }

bool interpolateTextShadows(const std::vector<TextShadow> &from, const std::vector<TextShadow> &to,
                            float progress, std::vector<TextShadow> *out) {
  if (from.size() != to.size()) return false;
  out->resize(from.size());
  for (size_t i = 0; i < from.size(); i++) {
    TextShadow shadow;
    if (!interpolateLength(from[i].dx, to[i].dx, progress, &shadow.dx)) return false;
    if (!interpolateLength(from[i].dy, to[i].dy, progress, &shadow.dy)) return false;
    if (!interpolateLength(from[i].blur, to[i].blur, progress, &shadow.blur)) return false;
    shadow.color = mixColor(from[i].color, to[i].color, progress);
    shadow.has_color = true;
    (*out)[i] = shadow;
  }
  return true;
}

bool interpolateBoxShadows(const std::vector<BoxShadow> &from, const std::vector<BoxShadow> &to,
                           float progress, std::vector<BoxShadow> *out) {
  if (from.size() != to.size()) return false;
  out->resize(from.size());
  for (size_t i = 0; i < from.size(); i++) {
    if (from[i].inset != to[i].inset) return false;
    BoxShadow shadow;
    if (!interpolateLength(from[i].dx, to[i].dx, progress, &shadow.dx)) return false;
    if (!interpolateLength(from[i].dy, to[i].dy, progress, &shadow.dy)) return false;
    if (!interpolateLength(from[i].blur, to[i].blur, progress, &shadow.blur)) return false;
    if (!interpolateLength(from[i].spread, to[i].spread, progress, &shadow.spread)) return false;
    shadow.color = mixColor(from[i].color, to[i].color, progress);
    shadow.has_color = true;
    shadow.inset = from[i].inset;
    (*out)[i] = shadow;
  }
  return true;
}

TransformOp identityTransform(TransformKind kind) {
  TransformOp op;
  op.kind = kind;
  switch (kind) {
    case TransformKind::Translate:
      op.x = Length{0.0f, Unit::Px};
      op.y = Length{0.0f, Unit::Px};
      break;
    case TransformKind::TranslateX:
      op.x = Length{0.0f, Unit::Px};
      break;
    case TransformKind::TranslateY:
      op.y = Length{0.0f, Unit::Px};
      break;
    case TransformKind::Scale:
    case TransformKind::ScaleX:
    case TransformKind::ScaleY:
    case TransformKind::Matrix:
      break;
    case TransformKind::Rotate:
      op.angle = 0.0f;
      break;
  }
  return op;
}

/** CSS interpolation pads the shorter transform list with identity functions of
 * the matching kinds (`none` animates to `translateX(0)` as if the missing
 * list were the identity). */
bool normalizeTransforms(const std::vector<TransformOp> &from,
                         const std::vector<TransformOp> &to, std::vector<TransformOp> *out_from,
                         std::vector<TransformOp> *out_to) {
  size_t common = from.size() < to.size() ? from.size() : to.size();
  for (size_t i = 0; i < common; i++) {
    if (from[i].kind != to[i].kind) return false;
  }
  *out_from = from;
  *out_to = to;
  for (size_t i = common; i < to.size(); i++) out_from->push_back(identityTransform(to[i].kind));
  for (size_t i = common; i < from.size(); i++) out_to->push_back(identityTransform(from[i].kind));
  return true;
}

bool interpolateFilters(const std::vector<FilterOp> &from, const std::vector<FilterOp> &to,
                       float progress, std::vector<FilterOp> *out) {
  if (from.size() != to.size()) return false;
  out->resize(from.size());
  for (size_t i = 0; i < from.size(); i++) {
    const FilterOp &left = from[i];
    const FilterOp &right = to[i];
    if (left.kind != right.kind) return false;
    FilterOp op = left;
    op.amount = mix(left.amount, right.amount, progress);
    if (!interpolateLength(left.dx, right.dx, progress, &op.dx)) return false;
    if (!interpolateLength(left.dy, right.dy, progress, &op.dy)) return false;
    if (!interpolateLength(left.blur, right.blur, progress, &op.blur)) return false;
    op.color = mixColor(left.color, right.color, progress);
    op.has_color = left.has_color && right.has_color;
    (*out)[i] = op;
  }
  return true;
}

bool filterEqual(const std::vector<FilterOp> &a, const std::vector<FilterOp> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    const FilterOp &x = a[i];
    const FilterOp &y = b[i];
    if (x.kind != y.kind || x.amount != y.amount || !lengthEqual(x.dx, y.dx) ||
        !lengthEqual(x.dy, y.dy) || !lengthEqual(x.blur, y.blur) ||
        x.has_color != y.has_color || (x.has_color && !colorEqual(x.color, y.color)))
      return false;
  }
  return true;
}

bool isColorProperty(const std::string &property) {
  return property == "color" || property == "background-color" || property == "border-color";
}

bool isNumberProperty(const std::string &property) {
  return property == "opacity" || property == "border-radius";
}

Length *lengthField(XtStyle *style, const std::string &property) {
  if (property == "width") return &style->width;
  if (property == "height") return &style->height;
  if (property == "min-width") return &style->min_width;
  if (property == "min-height") return &style->min_height;
  if (property == "max-width") return &style->max_width;
  if (property == "max-height") return &style->max_height;
  if (property == "top") return &style->top;
  if (property == "right") return &style->right;
  if (property == "bottom") return &style->bottom;
  if (property == "left") return &style->left;
  if (property == "margin-top") return &style->margin.top;
  if (property == "margin-right") return &style->margin.right;
  if (property == "margin-bottom") return &style->margin.bottom;
  if (property == "margin-left") return &style->margin.left;
  if (property == "padding-top") return &style->padding.top;
  if (property == "padding-right") return &style->padding.right;
  if (property == "padding-bottom") return &style->padding.bottom;
  if (property == "padding-left") return &style->padding.left;
  if (property == "border-top-width") return &style->border_width.top;
  if (property == "border-right-width") return &style->border_width.right;
  if (property == "border-bottom-width") return &style->border_width.bottom;
  if (property == "border-left-width") return &style->border_width.left;
  if (property == "gap") return &style->gap;
  if (property == "row-gap") return &style->row_gap;
  if (property == "column-gap") return &style->column_gap;
  if (property == "flex-basis") return &style->flex_basis;
  return nullptr;
}

const Length *lengthField(const XtStyle &style, const std::string &property) {
  return lengthField(const_cast<XtStyle *>(&style), property);
}

}  // namespace

const std::vector<std::string> &animatableProperties() {
  static const std::vector<std::string> properties = {
      "background-color", "color",         "border-color", "opacity",
      "border-radius",    "width",         "height",       "min-width",
      "min-height",       "max-width",     "max-height",   "top",
      "right",            "bottom",        "left",         "margin-top",
      "margin-right",     "margin-bottom", "margin-left",  "padding-top",
      "padding-right",    "padding-bottom", "padding-left", "border-top-width",
      "border-right-width", "border-bottom-width", "border-left-width", "gap",
      "row-gap",          "column-gap",    "flex-basis",   "transform",
      "text-shadow",      "box-shadow",   "filter"};
  return properties;
}

bool readAnimatable(const XtStyle &style, const std::string &property, AnimatableValue *out) {
  *out = AnimatableValue();
  if (isColorProperty(property)) {
    out->kind = AnimatableKind::Color;
    if (property == "color") out->color = style.color;
    else if (property == "background-color")
      out->color = style.has_background ? style.background : Color{0, 0, 0, 0, true};
    else out->color = style.has_border_color ? style.border_color : Color{0, 0, 0, 0, true};
    return true;
  }
  if (isNumberProperty(property)) {
    out->kind = AnimatableKind::Number;
    out->number = property == "opacity" ? style.opacity : style.border_radius;
    return true;
  }
  if (const Length *length = lengthField(style, property)) {
    out->kind = AnimatableKind::Length;
    out->length = *length;
    return true;
  }
  if (property == "font-size") {
    out->kind = AnimatableKind::Length;
    out->length = Length{style.font_size, Unit::Px};
    return true;
  }
  if (property == "transform") {
    out->kind = AnimatableKind::Transform;
    out->transform = style.transform;
    return true;
  }
  if (property == "text-shadow") {
    out->kind = AnimatableKind::TextShadow;
    out->text_shadow = style.text_shadow;
    for (TextShadow &shadow : out->text_shadow) {
      if (!shadow.has_color) {
        shadow.color = style.color;
        shadow.has_color = true;
      }
    }
    return true;
  }
  if (property == "box-shadow") {
    out->kind = AnimatableKind::BoxShadow;
    out->box_shadow = style.box_shadow;
    for (BoxShadow &shadow : out->box_shadow) {
      if (!shadow.has_color) {
        shadow.color = style.color;
        shadow.has_color = true;
      }
    }
    return true;
  }
  if (property == "filter") {
    out->kind = AnimatableKind::Filter;
    out->filter = style.filters;
    for (FilterOp &op : out->filter) {
      if (op.kind == FilterKind::DropShadow && !op.has_color) {
        op.color = style.color;
        op.has_color = true;
      }
    }
    return true;
  }
  return false;
}

void writeAnimatable(XtStyle *style, const std::string &property, const AnimatableValue &value) {
  switch (value.kind) {
    case AnimatableKind::Color:
      if (property == "color") {
        style->color = value.color;
      } else if (property == "background-color") {
        style->background = value.color;
        style->has_background = value.color.a > 0.0f;
      } else if (property == "border-color") {
        style->border_color = value.color;
        style->has_border_color = value.color.a > 0.0f;
      }
      break;
    case AnimatableKind::Number:
      if (property == "opacity") style->opacity = value.number;
      else if (property == "border-radius") style->border_radius = value.number;
      break;
    case AnimatableKind::Length:
      if (property == "font-size") {
        style->font_size = value.length.unit == Unit::Px ? value.length.value : style->font_size;
      } else if (Length *field = lengthField(style, property)) {
        *field = value.length;
      }
      break;
    case AnimatableKind::Transform:
      if (property == "transform") style->transform = value.transform;
      break;
    case AnimatableKind::TextShadow:
      if (property == "text-shadow") style->text_shadow = value.text_shadow;
      break;
    case AnimatableKind::BoxShadow:
      if (property == "box-shadow") style->box_shadow = value.box_shadow;
      break;
    case AnimatableKind::Filter:
      if (property == "filter") style->filters = value.filter;
      break;
    case AnimatableKind::None:
      break;
  }
}

bool interpolateAnimatable(const AnimatableValue &from, const AnimatableValue &to, float progress,
                           AnimatableValue *out) {
  if (from.kind != to.kind) return false;
  *out = from;
  switch (from.kind) {
    case AnimatableKind::None:
      return false;
    case AnimatableKind::Color:
      out->color = mixColor(from.color, to.color, progress);
      return true;
    case AnimatableKind::Number:
      out->number = mix(from.number, to.number, progress);
      return true;
    case AnimatableKind::Length:
      return interpolateLength(from.length, to.length, progress, &out->length);
    case AnimatableKind::Transform: {
      std::vector<TransformOp> normalized_from;
      std::vector<TransformOp> normalized_to;
      if (!normalizeTransforms(from.transform, to.transform, &normalized_from, &normalized_to))
        return false;
      return xt_transform_interpolate(normalized_from, normalized_to, progress, &out->transform);
    }
    case AnimatableKind::TextShadow:
      return interpolateTextShadows(from.text_shadow, to.text_shadow, progress,
                                    &out->text_shadow);
    case AnimatableKind::BoxShadow:
      return interpolateBoxShadows(from.box_shadow, to.box_shadow, progress, &out->box_shadow);
    case AnimatableKind::Filter:
      return interpolateFilters(from.filter, to.filter, progress, &out->filter);
  }
  return false;
}

bool animatableEqual(const AnimatableValue &a, const AnimatableValue &b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case AnimatableKind::None:
      return true;
    case AnimatableKind::Color:
      return colorEqual(a.color, b.color);
    case AnimatableKind::Number:
      return a.number == b.number;
    case AnimatableKind::Length:
      return lengthEqual(a.length, b.length);
    case AnimatableKind::Transform: {
      std::vector<TransformOp> normalized_a;
      std::vector<TransformOp> normalized_b;
      if (!normalizeTransforms(a.transform, b.transform, &normalized_a, &normalized_b))
        return false;
      return xt_transform_equal(normalized_a, normalized_b);
    }
    case AnimatableKind::TextShadow:
      if (a.text_shadow.size() != b.text_shadow.size()) return false;
      for (size_t i = 0; i < a.text_shadow.size(); i++) {
        const TextShadow &x = a.text_shadow[i];
        const TextShadow &y = b.text_shadow[i];
        if (!shadowLengthsEqual(x.dx, y.dx) || !shadowLengthsEqual(x.dy, y.dy) ||
            !shadowLengthsEqual(x.blur, y.blur) || !colorEqual(x.color, y.color) ||
            x.has_color != y.has_color)
          return false;
      }
      return true;
    case AnimatableKind::Filter:
      return filterEqual(a.filter, b.filter);
    case AnimatableKind::BoxShadow:
      if (a.box_shadow.size() != b.box_shadow.size()) return false;
      for (size_t i = 0; i < a.box_shadow.size(); i++) {
        const BoxShadow &x = a.box_shadow[i];
        const BoxShadow &y = b.box_shadow[i];
        if (x.inset != y.inset || !shadowLengthsEqual(x.dx, y.dx) ||
            !shadowLengthsEqual(x.dy, y.dy) || !shadowLengthsEqual(x.blur, y.blur) ||
            !shadowLengthsEqual(x.spread, y.spread) || !colorEqual(x.color, y.color) ||
            x.has_color != y.has_color)
          return false;
      }
      return true;
  }
  return false;
}

}  // namespace xtgui
