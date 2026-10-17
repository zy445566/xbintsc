/*
 * xbintsc GUI engine — CSS `transform` and `transform-origin`.
 *
 * A `transform` declaration is parsed into a list of `TransformOp`s (the
 * familiar `translate(...) rotate(...) scale(...)` chain, plus `matrix(...)`).
 * Lengths stay unresolved in the list, because percentages and `em`/`vw`
 * units need the box that carries them — which only layout knows. Layout
 * therefore resolves the list into a `Matrix2D`, and paint, hit testing and
 * `getBoundingClientRect` all consume that one matrix.
 *
 * Transforms are a *paint-time* effect: they never change layout geometry, so
 * a transform cannot move a sibling, and it must be composed down the subtree
 * (a child of a transformed box is transformed too).
 */
#ifndef XT_GUI_TRANSFORM_H
#define XT_GUI_TRANSFORM_H

#include <string>
#include <vector>

#include "css.h"

namespace xtgui {

/** A 2D affine transform laid out like the DOM `DOMMatrix` (a b c d e f):
 * `x' = a*x + c*y + e`, `y' = b*x + d*y + f`. */
struct Matrix2D {
  float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;

  bool identity() const {
    return a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f && e == 0.0f && f == 0.0f;
  }
  /** True when the transform keeps axis-aligned rectangles axis aligned (no
   * rotation or skew). */
  bool axis_aligned() const { return b == 0.0f && c == 0.0f; }
  void apply(float x, float y, float *out_x, float *out_y) const {
    *out_x = a * x + c * y + e;
    *out_y = b * x + d * y + f;
  }
  /** `this * other`: the point is put through `other` first, then `this`. */
  Matrix2D multiply(const Matrix2D &other) const;
  /** The inverse transform, or false for a singular matrix (scale 0). */
  bool invert(Matrix2D *out) const;
  /** Approximate uniform magnification (geometric mean of the axis scales). */
  float magnification() const;

  static Matrix2D translate(float x, float y) {
    Matrix2D out;
    out.e = x;
    out.f = y;
    return out;
  }
  static Matrix2D scale(float x, float y) {
    Matrix2D out;
    out.a = x;
    out.d = y;
    return out;
  }
  static Matrix2D rotate(float radians);
};

enum class TransformKind {
  Translate,
  TranslateX,
  TranslateY,
  Scale,
  ScaleX,
  ScaleY,
  Rotate,
  Matrix,
};

/** One function in a parsed `transform` list. Only the fields its `kind` uses
 * carry a value; the rest stay at their neutral defaults. */
struct TransformOp {
  TransformKind kind = TransformKind::Translate;
  Length x;                    // translate x; `%` resolves against the box width
  Length y;                    // translate y; `%` resolves against the box height
  float scale_x = 1.0f;        // scale x (a unitless factor)
  float scale_y = 1.0f;        // scale y
  float angle = 0.0f;          // rotate, in radians
  float matrix[6] = {1, 0, 0, 1, 0, 0};  // a b c d e f
};

/** Parse a `transform` value. `none` (or an empty list) yields an empty vector.
 * An unsupported function or a malformed argument returns false, which makes
 * the whole declaration invalid — exactly what CSS does. */
bool xt_transform_parse(const std::string &value, std::vector<TransformOp> *out);

/** Structural equality (used to decide whether a transition is needed). */
bool xt_transform_equal(const std::vector<TransformOp> &left,
                        const std::vector<TransformOp> &right);

/** Interpolate two lists of the same shape (same kinds, compatible units) at
 * progress `t` in [0, 1]. Returns false when the lists cannot be mixed, in
 * which case the caller should snap to the target. */
bool xt_transform_interpolate(const std::vector<TransformOp> &from,
                              const std::vector<TransformOp> &to, float t,
                              std::vector<TransformOp> *out);

/** Compose a `transform` list into one matrix. `percent_width`/`percent_height`
 * are the element's border-box size (the base for `%`), `em` its font size. */
Matrix2D xt_transform_matrix(const std::vector<TransformOp> &ops, float percent_width,
                             float percent_height, float em, float viewport_width,
                             float viewport_height);

/** Canonical function-list spelling, for `getComputedStyle(el, "transform")`. */
std::string xt_transform_to_string(const std::vector<TransformOp> &ops);

}  // namespace xtgui

#endif /* XT_GUI_TRANSFORM_H */
