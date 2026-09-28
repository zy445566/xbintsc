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
};

/** Resolve a length to pixels given the current font/viewport context. */
float xt_style_resolve_absolute(const Length &length, float base, float em, float root, float vw,
                                float vh);

/** Apply the built-in user-agent rules for one element. */
void xt_style_apply_user_agent(const Node *node, XtStyle *style);

/** Apply one parsed declaration (`property: value`) onto a computed style. */
void xt_style_apply_declaration(XtStyle *style, const std::string &property,
                                const std::string &rawValue, const ApplyContext &context);

}  // namespace xtgui

#endif /* XT_GUI_STYLE_INTERNAL_H */
