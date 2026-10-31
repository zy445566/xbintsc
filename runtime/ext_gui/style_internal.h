/*
 * xbintsc GUI engine — shared declarations for the style translation units
 * (`style.cpp`, `style_values.cpp`, `style_ua.cpp`, `style_print.cpp`).
 */
#ifndef XT_GUI_STYLE_INTERNAL_H
#define XT_GUI_STYLE_INTERNAL_H

#include "style.h"

namespace xtgui {

/** Per-node cascade inputs threaded through declaration application. */
struct ApplyContext {
  float rootFontSize = 16.0f;
  float parentFontSize = 16.0f;
  float viewportWidth = 0.0f;
  float viewportHeight = 0.0f;
  /** Parent computed style; needed by `inherit` and the `font` shorthand. */
  const XtStyle *parent = nullptr;
};

/** Resolve a length to pixels given the current font/viewport context. */
float xt_style_resolve_absolute(const Length &length, float base, float em, float root, float vw,
                                float vh);

/** CSS-wide `inherit` for the shorthands the engine handles. */
void xt_style_apply_inherit(XtStyle *style, const XtStyle *parent, const std::string &property);

/** Replace `currentcolor` outside quoted strings with `color`'s rgba spelling. */
std::string xt_style_replace_current_color(const std::string &value, const Color &color);

/** Apply the built-in user-agent rules for one element. */
void xt_style_apply_user_agent(const Node *node, XtStyle *style);

/** Resolve a `border-radius` against a border box (percent uses min(w, h)). */
float xt_style_border_radius_px(const Length &radius, float width, float height, float em,
                                float root, float vw, float vh);

/** Apply a parsed declaration to a computed style. */
void xt_style_apply_declaration(XtStyle *style, const std::string &property,
                                const std::string &rawValue, const ApplyContext &context);

}  // namespace xtgui

#endif /* XT_GUI_STYLE_INTERNAL_H */
