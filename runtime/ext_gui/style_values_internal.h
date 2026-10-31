#ifndef XT_GUI_STYLE_VALUES_INTERNAL_H
#define XT_GUI_STYLE_VALUES_INTERNAL_H

#include <string>

struct XtStyle;

namespace xtgui {

/** Apply `background-image` (a comma-separated layer list). */
void xt_style_apply_background_image(XtStyle *style, const std::string &value);

/** Apply the `background` shorthand (layers plus an optional final colour). */
void xt_style_apply_background(XtStyle *style, const std::string &value);

/** Apply `filter` / `backdrop-filter` / `text-shadow` / `content`; returns false
 * for unrelated properties. */
bool xt_style_apply_effect(XtStyle *style, const std::string &property,
                           const std::string &value);

/** Apply `clip-path`; returns false for an unrelated property. */
bool xt_style_apply_clip_path(XtStyle *style, const std::string &property,
                              const std::string &value);

/** Apply a grid/alignment declaration; returns false for unrelated properties. */
bool xt_style_apply_grid_property(XtStyle *style, const std::string &property,
                                  const std::string &value);

/** Apply a transition declaration; returns false for unrelated properties. */
bool xt_style_apply_transition(XtStyle *style, const std::string &property,
                               const std::string &value);

/** Apply an `animation`/`animation-*` declaration. */
bool xt_style_apply_animation(XtStyle *style, const std::string &property,
                              const std::string &value);

}  // namespace xtgui

#endif /* XT_GUI_STYLE_VALUES_INTERNAL_H */
