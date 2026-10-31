#ifndef XT_GUI_STYLE_VALUES_INTERNAL_H
#define XT_GUI_STYLE_VALUES_INTERNAL_H

#include <string>

struct XtStyle;
struct ApplyContext;

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

/** Apply text declarations (`letter-spacing`, `text-transform`,
 * `writing-mode`, `vertical-align`, the `font` shorthand, …). */
bool xt_style_apply_text_property(XtStyle *style, const std::string &property,
                                  const std::string &value, const ApplyContext &context);

/** Apply UI/interaction declarations (`outline`, `cursor`, `isolation`, and
 * the platform hints the engine accepts but does not act on). */
bool xt_style_apply_ui_property(XtStyle *style, const std::string &property,
                                const std::string &value);

/** Record an SVG presentation property (`fill`, `stroke`, …) for the inline
 * `<svg>` serializer. Returns false for unrelated properties. */
bool xt_style_apply_svg_property(XtStyle *style, const std::string &property,
                                 const std::string &value);

/** Apply `background-size` (a comma-separated list, one per layer). */
bool xt_style_apply_background_size(XtStyle *style, const std::string &value);

}  // namespace xtgui

#endif /* XT_GUI_STYLE_VALUES_INTERNAL_H */
