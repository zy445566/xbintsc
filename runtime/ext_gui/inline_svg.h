/*
 * xbintsc GUI engine — inline `<svg>` elements.
 *
 * An SVG that arrives as a file is decoded by `svg.h` (`<img src="logo.svg">`).
 * An inline `<svg>` tree is already part of the document, so this module turns
 * the subtree back into a standalone SVG document and hands it to the same
 * NanoSVG parser/rasteriser. Layout then treats `<svg>` as a replaced element
 * (its intrinsic size comes from the parsed document) and paint emits an image
 * op carrying the markup, so the renderer rasterises it at the drawn device
 * pixel size — vector art stays crisp exactly like an SVG file.
 *
 * The HTML parser lowercases element and attribute names (so `viewBox` arrives
 * as `viewbox`), while NanoSVG matches SVG's case-sensitive spellings. The
 * serializer therefore restores the canonical names of the elements and
 * attributes NanoSVG understands, and drops the HTML-only ones that would
 * confuse the XML reader.
 */
#ifndef XT_GUI_INLINE_SVG_H
#define XT_GUI_INLINE_SVG_H

#include <string>
#include <unordered_map>

#include "dom.h"
#include "style.h"
#include "svg.h"

namespace xtgui {

/** True when `node` is an `<svg>` element (an inline SVG root). */
bool xt_inline_svg_is_root(const Node *node);

/** Serialize an `<svg>` DOM subtree into a standalone SVG document: the root
 * element, its descendants (including `<defs>`, gradients and `<style>` text)
 * and the canonical `width`/`height`/`viewBox`/`gradient*` spellings NanoSVG
 * expects. Text outside `<style>` is dropped (NanoSVG renders shapes, not
 * `<text>`), and HTML-only attributes are skipped. Returns an empty string when
 * `node` is not an `<svg>` element. */
std::string xt_inline_svg_serialize(
    const Node *node,
    const std::unordered_map<const Node *, XtStyle> *styles = nullptr);

}  // namespace xtgui

#endif /* XT_GUI_INLINE_SVG_H */
