/*
 * xbintsc GUI engine — interpolation of animatable CSS properties.
 *
 * CSS transitions and `@keyframes` both need a property value detached from
 * `XtStyle`, interpolated at a progress, and written back. Keeping that switch
 * in one place means a newly supported transition property automatically works
 * in keyframes too (and vice versa).
 */
#ifndef XT_GUI_ANIMATION_VALUES_H
#define XT_GUI_ANIMATION_VALUES_H

#include <string>
#include <vector>

#include "style.h"

namespace xtgui {

enum class AnimatableKind {
  None,
  Color,
  Number,
  Length,
  Transform,
  TextShadow,
  BoxShadow,
  Filter
};

struct AnimatableValue {
  AnimatableKind kind = AnimatableKind::None;
  Color color;
  float number = 0.0f;
  Length length;
  std::vector<TransformOp> transform;
  std::vector<TextShadow> text_shadow;
  std::vector<BoxShadow> box_shadow;
  std::vector<FilterOp> filter;
};

/** Every property the animation machinery can read, interpolate and write. The
 * list drives `transition: all` as well. */
const std::vector<std::string> &animatableProperties();

/** Read `property` from `style`; false when it is not animatable. Colours that
 * default to `currentColor` are resolved against `style.color`. */
bool readAnimatable(const XtStyle &style, const std::string &property, AnimatableValue *out);

/** Write an interpolated value back onto `style`. */
void writeAnimatable(XtStyle *style, const std::string &property, const AnimatableValue &value);

/** Interpolate two values at `progress` (0..1). Returns false when the values
 * cannot be mixed (different kinds, list shapes or units); callers then fall
 * back to a discrete switch at the halfway point, as CSS does. */
bool interpolateAnimatable(const AnimatableValue &from, const AnimatableValue &to, float progress,
                           AnimatableValue *out);

/** True when two values would paint identically (so no transition should
 * start). */
bool animatableEqual(const AnimatableValue &a, const AnimatableValue &b);

}  // namespace xtgui

#endif /* XT_GUI_ANIMATION_VALUES_H */
