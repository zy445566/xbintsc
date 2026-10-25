#ifndef XT_GUI_STYLE_VALUES_INTERNAL_H
#define XT_GUI_STYLE_VALUES_INTERNAL_H

#include <string>

struct XtStyle;

namespace xtgui {

/** Apply the gradient half of `background` / `background-image`; returns false
 * when the value has no linear gradient so the caller can parse a solid colour. */
bool xt_style_apply_gradient(XtStyle *style, const std::string &value);

/** Apply a grid/alignment declaration; returns false for unrelated properties. */
bool xt_style_apply_grid_property(XtStyle *style, const std::string &property,
                                  const std::string &value);

/** Apply a transition declaration; returns false for unrelated properties. */
bool xt_style_apply_transition(XtStyle *style, const std::string &property,
                               const std::string &value);

}  // namespace xtgui

#endif /* XT_GUI_STYLE_VALUES_INTERNAL_H */
