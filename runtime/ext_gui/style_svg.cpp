/*
 * xbintsc GUI engine — SVG presentation properties in the document cascade.
 *
 * Inline `<svg>` is serialized and rasterised by NanoSVG, which only sees the
 * markup, not the document's computed styles. The cascade therefore records the
 * SVG presentation properties here and `inline_svg.cpp` writes them back as an
 * inline `style="…"` attribute (so NanoSVG's class/style reader applies them).
 */

#include "style_internal.h"
#include "style_values_internal.h"

#include <string>

namespace xtgui {

namespace {

bool isSvgPresentationProperty(const std::string &property) {
  static const char *const names[] = {
      "fill",          "fill-opacity",  "fill-rule",      "stroke",
      "stroke-opacity", "stroke-width", "stroke-dasharray", "stroke-dashoffset",
      "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "stop-color",
      "stop-opacity",  "paint-order"};
  for (const char *name : names) {
    if (property == name) return true;
  }
  return false;
}

}  // namespace

bool xt_style_apply_svg_property(XtStyle *style, const std::string &property,
                                 const std::string &value) {
  if (!isSvgPresentationProperty(property)) return false;
  style->svg_props[property] = xt_css_trim(value);
  return true;
}

}  // namespace xtgui
