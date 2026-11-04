/*
 * xbintsc GUI engine — diagnostics for the features outside the supported
 * HTML/CSS subset.
 *
 * The engine deliberately keeps rendering when a page uses an element or a
 * declaration it does not implement; these helpers make that fallback visible
 * instead of silent. Each name is reported once per process, so a property used
 * by every rule (or a table cell repeated on every row) cannot flood stderr on
 * every restyle.
 *
 * Set `XT_GUI_NO_WARNINGS=1` to silence the reports (they are development
 * aids; a shipped application usually starts without a console on Windows).
 */
#ifndef XT_GUI_WARNINGS_H
#define XT_GUI_WARNINGS_H

#include <string>

namespace xtgui {

/** Report an unsupported HTML element once per tag name. */
void xt_warn_unsupported_element(const std::string &tag);

/** Report an unsupported SVG child element once per tag name. */
void xt_warn_unsupported_svg_element(const std::string &tag);

/** Report an unsupported HTML attribute once per element/tag pair. */
void xt_warn_unsupported_attribute(const std::string &tag, const std::string &attribute);

/** Report an unsupported CSS property once per property name. */
void xt_warn_unsupported_property(const std::string &property);

/** Report an ignored at-rule once per at-rule name (`supports`, `layer`, …). */
void xt_warn_unsupported_at_rule(const std::string &name);

/** Report an ignored `@font-face` descriptor once per descriptor name. */
void xt_warn_unsupported_descriptor(const std::string &name);

/** Report a selector the parser had to drop. */
void xt_warn_unsupported_selector(const std::string &selector);

/** Report an unsupported pseudo-class (`:has`, `:nth-of-type`, …). */
void xt_warn_unsupported_pseudo_class(const std::string &name);

/** Report an unsupported pseudo-element (`::selection`, `::marker`, …). */
void xt_warn_unsupported_pseudo_element(const std::string &name);

/** Report an unsupported `@media` feature and why its query is treated as false. */
void xt_warn_unsupported_media_feature(const std::string &name);

/** Report an unsupported CSS function inside `property` (`transform`, `filter`, …). */
void xt_warn_unsupported_function(const std::string &property, const std::string &function);

/** Report an unsupported length unit (`vmin`, `ch`, `dvh`, …). */
void xt_warn_unsupported_unit(const std::string &unit);

}  // namespace xtgui

#endif /* XT_GUI_WARNINGS_H */
