/*
 * xbintsc GUI engine — CSS `transform`/`transform-origin` parsing, composition
 * and interpolation.
 *
 * `style.cpp`/`style_values.cpp` call `xt_transform_parse` to turn a declaration
 * value into a list of `TransformOp`s; `layout.cpp` asks `xt_transform_matrix`
 * for the composition once it knows the box size; `document.cpp` asks
 * `xt_transform_interpolate` while a transition runs; `style_print.cpp` asks
 * `xt_transform_to_string` for `getComputedStyle`.
 */

#include "transform.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "css_internal.h"
#include "style_internal.h"

namespace xtgui {

namespace {

constexpr float kPi = 3.14159265358979323846f;
/** `rem` units inside a transform resolve against the initial font size; the
 * computed style carries `font-size` in pixels, so the root size is not
 * available here (layout only has the element's own style). */
constexpr float kRootFontSize = 16.0f;

bool isSpace(char c) { return xt_is_css_space(c); }

std::string trim(const std::string &text) { return xt_css_trim(text); }

/** Split `a, b` / `a b` / `a,b` into its arguments, keeping a nested
 * `calc(10px + 2px)` argument together. */
std::vector<std::string> splitArgs(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if ((isSpace(c) || c == ',') && depth == 0) {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

bool parseLengthArg(const std::string &token, Length *out) {
  Length length = xt_css_parse_length(token);
  if (length.unit == Unit::Invalid || length.unit == Unit::Auto || length.unit == Unit::None) {
    return false;
  }
  *out = length;
  return true;
}

/** A unitless number, optionally a percentage (which is divided by 100). */
bool parseFactorArg(const std::string &raw, float *out) {
  std::string token = trim(raw);
  if (token.empty()) return false;
  bool percent = token.back() == '%';
  if (percent) token.pop_back();
  float number = 0;
  if (!xt_css_parse_number(token, &number)) return false;
  *out = percent ? number / 100.0f : number;
  return true;
}

bool parseAngleArg(const std::string &raw, float *out) {
  std::string token = xt_css_lower(trim(raw));
  if (token.empty()) return false;
  char *end = nullptr;
  double number = std::strtod(token.c_str(), &end);
  if (end == token.c_str()) return false;
  std::string unit = trim(std::string(end));
  float radians = 0;
  if (unit.empty() || unit == "deg") radians = (float)number * kPi / 180.0f;
  else if (unit == "rad") radians = (float)number;
  else if (unit == "grad") radians = (float)number * kPi / 200.0f;
  else if (unit == "turn") radians = (float)number * 2.0f * kPi;
  else return false;
  *out = radians;
  return true;
}

/** Translate one parsed function call into a `TransformOp`. */
bool applyFunction(const std::string &name, const std::vector<std::string> &args,
                   TransformOp *op) {
  if (name == "translate" || name == "translate3d") {
    /* The 3d spelling takes a `z` we parse and drop: the engine is 2D. */
    size_t most = name == "translate3d" ? 3 : 2;
    if (args.empty() || args.size() > most) return false;
    op->kind = TransformKind::Translate;
    if (!parseLengthArg(args[0], &op->x)) return false;
    op->y = Length{0.0f, Unit::Px};
    if (args.size() >= 2 && !parseLengthArg(args[1], &op->y)) return false;
    return true;
  }
  if (name == "translatex") {
    if (args.size() != 1) return false;
    op->kind = TransformKind::TranslateX;
    op->y = Length{0.0f, Unit::Px};
    return parseLengthArg(args[0], &op->x);
  }
  if (name == "translatey") {
    if (args.size() != 1) return false;
    op->kind = TransformKind::TranslateY;
    op->x = Length{0.0f, Unit::Px};
    return parseLengthArg(args[0], &op->y);
  }
  if (name == "scale" || name == "scale3d") {
    size_t most = name == "scale3d" ? 3 : 2;
    if (args.empty() || args.size() > most) return false;
    op->kind = TransformKind::Scale;
    if (!parseFactorArg(args[0], &op->scale_x)) return false;
    op->scale_y = op->scale_x;
    if (args.size() >= 2 && !parseFactorArg(args[1], &op->scale_y)) return false;
    return true;
  }
  if (name == "scalex") {
    if (args.size() != 1) return false;
    op->kind = TransformKind::ScaleX;
    op->scale_y = 1.0f;
    return parseFactorArg(args[0], &op->scale_x);
  }
  if (name == "scaley") {
    if (args.size() != 1) return false;
    op->kind = TransformKind::ScaleY;
    op->scale_x = 1.0f;
    return parseFactorArg(args[0], &op->scale_y);
  }
  if (name == "rotate" || name == "rotatez") {
    if (args.size() != 1) return false;
    op->kind = TransformKind::Rotate;
    return parseAngleArg(args[0], &op->angle);
  }
  if (name == "matrix") {
    if (args.size() != 6) return false;
    op->kind = TransformKind::Matrix;
    for (size_t i = 0; i < 6; i++) {
      if (!xt_css_parse_number(args[i], &op->matrix[i])) return false;
    }
    return true;
  }
  return false;  // skew/perspective/… are not supported yet
}

float resolveLength(const Length &length, float base, float em, float vw, float vh) {
  return xt_style_resolve_absolute(length, base, em, kRootFontSize, vw, vh);
}

bool sameLength(const Length &left, const Length &right) {
  if (left.unit != right.unit || left.value != right.value || left.op != right.op ||
      left.args.size() != right.args.size())
    return false;
  for (size_t i = 0; i < left.args.size(); i++) {
    if (!sameLength(left.args[i], right.args[i])) return false;
  }
  return true;
}

bool sameOp(const TransformOp &left, const TransformOp &right) {
  if (left.kind != right.kind) return false;
  switch (left.kind) {
    case TransformKind::Translate:
      return sameLength(left.x, right.x) && sameLength(left.y, right.y);
    case TransformKind::TranslateX:
      return sameLength(left.x, right.x);
    case TransformKind::TranslateY:
      return sameLength(left.y, right.y);
    case TransformKind::Scale:
      return left.scale_x == right.scale_x && left.scale_y == right.scale_y;
    case TransformKind::ScaleX:
      return left.scale_x == right.scale_x;
    case TransformKind::ScaleY:
      return left.scale_y == right.scale_y;
    case TransformKind::Rotate:
      return left.angle == right.angle;
    case TransformKind::Matrix:
      for (int i = 0; i < 6; i++) {
        if (left.matrix[i] != right.matrix[i]) return false;
      }
      return true;
  }
  return false;
}

bool mixLength(const Length &from, const Length &to, float t, Length *out) {
  if (from.unit != to.unit) return false;
  if (from.unit == Unit::Calc) {
    if (from.op != to.op || from.args.size() != to.args.size()) return false;
    out->unit = Unit::Calc;
    out->op = from.op;
    out->args.resize(from.args.size());
    for (size_t i = 0; i < from.args.size(); i++) {
      if (!mixLength(from.args[i], to.args[i], t, &out->args[i])) return false;
    }
    return true;
  }
  if (from.unit == Unit::Clamp || from.unit == Unit::Min || from.unit == Unit::Max) {
    if (from.args.size() != to.args.size()) return false;
    out->unit = from.unit;
    out->args.resize(from.args.size());
    for (size_t i = 0; i < from.args.size(); i++) {
      if (!mixLength(from.args[i], to.args[i], t, &out->args[i])) return false;
    }
    return true;
  }
  out->unit = from.unit;
  out->value = from.value + (to.value - from.value) * t;
  return true;
}

}  // namespace

Matrix2D Matrix2D::rotate(float radians) {
  Matrix2D out;
  float cosine = std::cos(radians);
  float sine = std::sin(radians);
  out.a = cosine;
  out.b = sine;
  out.c = -sine;
  out.d = cosine;
  return out;
}

Matrix2D Matrix2D::multiply(const Matrix2D &other) const {
  Matrix2D out;
  out.a = a * other.a + c * other.b;
  out.b = b * other.a + d * other.b;
  out.c = a * other.c + c * other.d;
  out.d = b * other.c + d * other.d;
  out.e = a * other.e + c * other.f + e;
  out.f = b * other.e + d * other.f + f;
  return out;
}

bool Matrix2D::invert(Matrix2D *out) const {
  float determinant = a * d - b * c;
  if (std::fabs(determinant) < 1e-9f) return false;
  float inverse = 1.0f / determinant;
  Matrix2D result;
  result.a = d * inverse;
  result.b = -b * inverse;
  result.c = -c * inverse;
  result.d = a * inverse;
  result.e = -(result.a * e + result.c * f);
  result.f = -(result.b * e + result.d * f);
  *out = result;
  return true;
}

float Matrix2D::magnification() const {
  float determinant = std::fabs(a * d - b * c);
  return std::sqrt(determinant);
}

bool xt_transform_parse(const std::string &value, std::vector<TransformOp> *out) {
  out->clear();
  std::string text = trim(value);
  std::string lowered = xt_css_lower(text);
  if (lowered.empty() || lowered == "none") return true;

  size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && isSpace(text[pos])) pos++;
    if (pos >= text.size()) break;
    size_t name_start = pos;
    /* A function name may carry digits: `translate3d`, `scale3d`, `matrix3d`. */
    while (pos < text.size() && (std::isalnum((unsigned char)text[pos]) || text[pos] == '-')) pos++;
    if (pos == name_start) return false;
    std::string name = xt_css_lower(text.substr(name_start, pos - name_start));
    while (pos < text.size() && isSpace(text[pos])) pos++;
    if (pos >= text.size() || text[pos] != '(') return false;
    pos++;
    size_t args_start = pos;
    int depth = 1;
    while (pos < text.size() && depth > 0) {
      if (text[pos] == '(') depth++;
      else if (text[pos] == ')') depth--;
      pos++;
    }
    if (depth != 0) return false;
    std::string inner = text.substr(args_start, pos - 1 - args_start);
    TransformOp op;
    if (!applyFunction(name, splitArgs(inner), &op)) return false;
    out->push_back(op);
  }
  return true;
}

bool xt_transform_equal(const std::vector<TransformOp> &left,
                        const std::vector<TransformOp> &right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); i++) {
    if (!sameOp(left[i], right[i])) return false;
  }
  return true;
}

bool xt_transform_interpolate(const std::vector<TransformOp> &from,
                              const std::vector<TransformOp> &to, float t,
                              std::vector<TransformOp> *out) {
  if (from.size() != to.size()) return false;
  std::vector<TransformOp> mixed(from.size());
  for (size_t i = 0; i < from.size(); i++) {
    const TransformOp &left = from[i];
    const TransformOp &right = to[i];
    if (left.kind != right.kind) return false;
    TransformOp op;
    op.kind = left.kind;
    switch (left.kind) {
      case TransformKind::Translate:
        if (!mixLength(left.x, right.x, t, &op.x)) return false;
        if (!mixLength(left.y, right.y, t, &op.y)) return false;
        break;
      case TransformKind::TranslateX:
        if (!mixLength(left.x, right.x, t, &op.x)) return false;
        break;
      case TransformKind::TranslateY:
        if (!mixLength(left.y, right.y, t, &op.y)) return false;
        break;
      case TransformKind::Scale:
        op.scale_x = left.scale_x + (right.scale_x - left.scale_x) * t;
        op.scale_y = left.scale_y + (right.scale_y - left.scale_y) * t;
        break;
      case TransformKind::ScaleX:
        op.scale_x = left.scale_x + (right.scale_x - left.scale_x) * t;
        break;
      case TransformKind::ScaleY:
        op.scale_y = left.scale_y + (right.scale_y - left.scale_y) * t;
        break;
      case TransformKind::Rotate:
        op.angle = left.angle + (right.angle - left.angle) * t;
        break;
      case TransformKind::Matrix:
        for (int entry = 0; entry < 6; entry++) {
          op.matrix[entry] = left.matrix[entry] + (right.matrix[entry] - left.matrix[entry]) * t;
        }
        break;
    }
    mixed[i] = op;
  }
  *out = mixed;
  return true;
}

Matrix2D xt_transform_matrix(const std::vector<TransformOp> &ops, float percent_width,
                             float percent_height, float em, float viewport_width,
                             float viewport_height) {
  Matrix2D result;
  for (const TransformOp &op : ops) {
    Matrix2D step;
    switch (op.kind) {
      case TransformKind::Translate:
        step.e = resolveLength(op.x, percent_width, em, viewport_width, viewport_height);
        step.f = resolveLength(op.y, percent_height, em, viewport_width, viewport_height);
        break;
      case TransformKind::TranslateX:
        step.e = resolveLength(op.x, percent_width, em, viewport_width, viewport_height);
        break;
      case TransformKind::TranslateY:
        step.f = resolveLength(op.y, percent_height, em, viewport_width, viewport_height);
        break;
      case TransformKind::Scale:
        step = Matrix2D::scale(op.scale_x, op.scale_y);
        break;
      case TransformKind::ScaleX:
        step = Matrix2D::scale(op.scale_x, 1.0f);
        break;
      case TransformKind::ScaleY:
        step = Matrix2D::scale(1.0f, op.scale_y);
        break;
      case TransformKind::Rotate:
        step = Matrix2D::rotate(op.angle);
        break;
      case TransformKind::Matrix:
        step.a = op.matrix[0];
        step.b = op.matrix[1];
        step.c = op.matrix[2];
        step.d = op.matrix[3];
        step.e = op.matrix[4];
        step.f = op.matrix[5];
        break;
    }
    result = result.multiply(step);
  }
  return result;
}

std::string xt_transform_to_string(const std::vector<TransformOp> &ops) {
  if (ops.empty()) return "none";
  std::string text;
  for (size_t i = 0; i < ops.size(); i++) {
    if (i > 0) text += " ";
    const TransformOp &op = ops[i];
    switch (op.kind) {
      case TransformKind::Translate:
        text += "translate(" + xt_style_length_to_string(op.x) + ", " +
                xt_style_length_to_string(op.y) + ")";
        break;
      case TransformKind::TranslateX:
        text += "translateX(" + xt_style_length_to_string(op.x) + ")";
        break;
      case TransformKind::TranslateY:
        text += "translateY(" + xt_style_length_to_string(op.y) + ")";
        break;
      case TransformKind::Scale:
        text += "scale(" + xt_style_length_to_string(Length{op.scale_x, Unit::Number}) + ", " +
                xt_style_length_to_string(Length{op.scale_y, Unit::Number}) + ")";
        break;
      case TransformKind::ScaleX:
        text += "scaleX(" + xt_style_length_to_string(Length{op.scale_x, Unit::Number}) + ")";
        break;
      case TransformKind::ScaleY:
        text += "scaleY(" + xt_style_length_to_string(Length{op.scale_y, Unit::Number}) + ")";
        break;
      case TransformKind::Rotate: {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "rotate(%.4gdeg)", (double)(op.angle * 180.0f / kPi));
        text += buffer;
        break;
      }
      case TransformKind::Matrix: {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "matrix(%.6g, %.6g, %.6g, %.6g, %.6g, %.6g)",
                      (double)op.matrix[0], (double)op.matrix[1], (double)op.matrix[2],
                      (double)op.matrix[3], (double)op.matrix[4], (double)op.matrix[5]);
        text += buffer;
        break;
      }
    }
  }
  return text;
}

}  // namespace xtgui
