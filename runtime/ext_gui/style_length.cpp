/*
 * xbintsc GUI engine — resolution of computed CSS lengths.
 *
 * `Length` keeps a symbolic expression (`calc()`, `min()`/`max()`/`clamp()`,
 * percentages and relative units) until layout knows the containing block, the
 * element's font size and the viewport. This unit is that one entry point; it
 * lives apart from declaration parsing to keep `style_values.cpp` focused.
 */

#include "style_internal.h"

namespace xtgui {

float xt_style_resolve_absolute(const Length &length, float base, float em, float root, float vw,
                                float vh) {
  switch (length.unit) {
    case Unit::Clamp: {
      if (length.args.size() < 3) return 0.0f;
      float low = xt_style_resolve_absolute(length.args[0], base, em, root, vw, vh);
      float value = xt_style_resolve_absolute(length.args[1], base, em, root, vw, vh);
      float high = xt_style_resolve_absolute(length.args[2], base, em, root, vw, vh);
      return value < low ? low : (value > high ? high : value);
    }
    case Unit::Min: {
      float best = 0.0f;
      bool first = true;
      for (const Length &arg : length.args) {
        float value = xt_style_resolve_absolute(arg, base, em, root, vw, vh);
        if (first || value < best) best = value;
        first = false;
      }
      return best;
    }
    case Unit::Max: {
      float best = 0.0f;
      bool first = true;
      for (const Length &arg : length.args) {
        float value = xt_style_resolve_absolute(arg, base, em, root, vw, vh);
        if (first || value > best) best = value;
        first = false;
      }
      return best;
    }
    case Unit::Calc: {
      if (length.args.size() < 2) return 0.0f;
      float left = xt_style_resolve_absolute(length.args[0], base, em, root, vw, vh);
      float right = xt_style_resolve_absolute(length.args[1], base, em, root, vw, vh);
      switch (length.op) {
        case '+':
          return left + right;
        case '-':
          return left - right;
        case '*':
          return left * right;
        case '/':
          /* Division by zero makes the whole `calc()` invalid in CSS; the
           * declaration is dropped, which here means "no value contributed". */
          return right == 0.0f ? 0.0f : left / right;
        default:
          return left;
      }
    }
    case Unit::Px:
    case Unit::Number:
      return length.value;
    case Unit::Percent:
      return base * length.value / 100.0f;
    case Unit::Em:
      return em * length.value;
    case Unit::Rem:
      return root * length.value;
    case Unit::Vw:
      return vw * length.value / 100.0f;
    case Unit::Vh:
      return vh * length.value / 100.0f;
    default:
      return 0.0f;
  }
}

}  // namespace xtgui
