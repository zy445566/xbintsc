/*
 * xbintsc GUI engine — implementation of the unsupported-feature diagnostics
 * (see `warnings.h`).
 */

#include "warnings.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace xtgui {

namespace {

struct SeenNames {
  std::unordered_set<std::string> names;
};

/** Print one line per distinct name; repeated uses are dropped. */
bool firstUse(SeenNames *seen, const std::string &name) {
  return seen->names.insert(name).second;
}

bool warningsSuppressed() {
  static int suppressed = -1;
  if (suppressed < 0) {
    const char *value = SDL_getenv("XT_GUI_NO_WARNINGS");
    suppressed = value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0 ? 1 : 0;
  }
  return suppressed != 0;
}

/** The parser and `createElement` already lowercase the common case; only pay
 * for a copy when the caller really passed mixed case. */
bool hasUpperAscii(const std::string &text) {
  for (char c : text) {
    if (c >= 'A' && c <= 'Z') return true;
  }
  return false;
}

std::string lowerAscii(const std::string &text) {
  std::string out = text;
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return out;
}

std::string truncated(const std::string &text, size_t limit) {
  if (text.size() <= limit) return text;
  return text.substr(0, limit) + "...";
}

/**
 * HTML elements whose browser behaviour the engine does not implement.
 *
 * They still create a box (and their text descendants still render), but a page
 * that relies on a table layout, a native form control, an embedded document or
 * an interactive disclosure is not going to get it. Tags that render correctly
 * as a generic inline/block box (`button`, `label`, custom elements, …) are
 * intentionally absent, as are the tags the engine implements (`img`, `svg`,
 * `canvas`, `video`, `audio`, …).
 *
 * Update this list when an element gains real support, and mirror the change in
 * `doc/gui/dom.md` (and its Chinese translation).
 */
const std::unordered_set<std::string> &unsupportedElements() {
  static const std::unordered_set<std::string> names = {
      "applet",   "area",     "basefont", "bgsound",  "blink",    "caption",
      "col",      "colgroup", "datalist", "details",  "dialog",   "embed",
      "frame",    "frameset", "iframe",   "input",    "isindex",  "keygen",
      "map",      "marquee",  "math",     "meter",    "noembed",  "noframes",
      "object",   "optgroup", "option",   "output",   "param",    "plaintext",
      "progress", "select",   "summary",  "table",    "tbody",    "td",
      "template", "textarea", "tfoot",    "th",       "thead",    "tr",
      "track",    "xmp",
  };
  return names;
}

/**
 * SVG elements NanoSVG does not render.
 *
 * The list mirrors `doc/gui/known-issues.md`: text, `<use>`/symbols, filters,
 * masks, clip paths, patterns, markers, embedded images and animation are not
 * part of the rasteriser's subset.
 */
const std::unordered_set<std::string> &unsupportedSvgElements() {
  static const std::unordered_set<std::string> names = {
      "altglyph",      "altglyphdef",   "altglyphitem",  "animate",
      "animatecolor",  "animatemotion", "animatetransform", "clippath",
      "color-profile", "cursor",        "feblend",       "fecolormatrix",
      "fecomponenttransfer", "fecomposite", "feconvolvematrix", "fediffuselighting",
      "fedisplacementmap", "fedistantlight", "fedropshadow", "feflood",
      "fefunca",       "fefuncb",       "fefuncg",       "fefuncr",
      "fegaussianblur", "feimage",      "femerge",       "femergenode",
      "femorphology",  "feoffset",      "fepointlight",  "fespecularlighting",
      "fespotlight",   "fetile",        "feturbulence",  "filter",
      "font",          "font-face",     "font-face-format", "font-face-name",
      "font-face-src", "font-face-uri", "foreignobject", "glyph",
      "glyphref",      "hkern",         "image",         "marker",
      "mask",          "mpath",         "pattern",       "set",
      "switch",        "symbol",        "text",          "textpath",
      "tref",          "tspan",         "use",           "view",
      "vkern",
  };
  return names;
}

/** Attributes that request browser behaviour the engine has no subsystem for. */
const std::unordered_set<std::string> &unsupportedAttributes() {
  static const std::unordered_set<std::string> names = {
      "autofocus",    "clip-path", "contenteditable", "controls", "draggable",
      "filter",       "hidden",    "href",            "marker-end", "marker-mid",
      "marker-start", "mask",      "poster",          "sizes",    "srcset",
      "tabindex",
  };
  return names;
}

void warnElementOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr,
               "xt_gui: warning: unsupported element <%s>; the engine has no browser "
               "behaviour for it\n",
               name.c_str());
}

void warnSvgElementOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported SVG element <%s>; it is not rendered\n",
               name.c_str());
}

void warnAttributeOnce(const std::string &tag, const std::string &attribute) {
  static SeenNames seen;
  std::string key = tag;
  key += ':';
  key += attribute;
  if (!firstUse(&seen, key)) return;
  std::fprintf(stderr,
               "xt_gui: warning: unsupported attribute '%s' on <%s>; the engine has no "
               "behaviour for it\n",
               attribute.c_str(), tag.c_str());
}

void warnPropertyOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported CSS property '%s'; it is ignored\n",
               name.c_str());
}

void warnAtRuleOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported at-rule '@%s'; its rule is ignored\n",
               name.c_str());
}

void warnDescriptorOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr,
               "xt_gui: warning: unsupported @font-face descriptor '%s'; it is ignored\n",
               name.c_str());
}

void warnSelectorOnce(const std::string &selector) {
  static SeenNames seen;
  if (!firstUse(&seen, selector)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported selector '%s'; the rule is ignored\n",
               truncated(selector, 80).c_str());
}

void warnPseudoClassOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported pseudo-class ':%s'; it never matches\n",
               name.c_str());
}

void warnPseudoElementOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported pseudo-element '::%s'; it is not generated\n",
               name.c_str());
}

void warnMediaFeatureOnce(const std::string &name) {
  static SeenNames seen;
  if (!firstUse(&seen, name)) return;
  std::fprintf(stderr,
               "xt_gui: warning: unsupported media feature '%s'; the query is treated as "
               "not matching\n",
               name.c_str());
}

void warnFunctionOnce(const std::string &property, const std::string &function) {
  static SeenNames seen;
  std::string key = property;
  key += ':';
  key += function;
  if (!firstUse(&seen, key)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported function '%s()' in '%s'; it is ignored\n",
               function.c_str(), property.c_str());
}

void warnUnitOnce(const std::string &unit) {
  static SeenNames seen;
  if (!firstUse(&seen, unit)) return;
  std::fprintf(stderr, "xt_gui: warning: unsupported CSS unit '%s'; the value is ignored\n",
               unit.c_str());
}

}  // namespace

void xt_warn_unsupported_element(const std::string &tag) {
  if (warningsSuppressed()) return;
  if (!hasUpperAscii(tag)) {
    if (unsupportedElements().count(tag) != 0) warnElementOnce(tag);
    return;
  }
  const std::string name = lowerAscii(tag);
  if (unsupportedElements().count(name) != 0) warnElementOnce(name);
}

void xt_warn_unsupported_svg_element(const std::string &tag) {
  if (warningsSuppressed()) return;
  const std::string name = hasUpperAscii(tag) ? lowerAscii(tag) : tag;
  if (unsupportedSvgElements().count(name) != 0) warnSvgElementOnce(name);
}

void xt_warn_unsupported_attribute(const std::string &tag, const std::string &attribute) {
  if (warningsSuppressed()) return;
  const std::string name = hasUpperAscii(attribute) ? lowerAscii(attribute) : attribute;
  if (unsupportedAttributes().count(name) == 0) return;
  const std::string element = hasUpperAscii(tag) ? lowerAscii(tag) : tag;
  warnAttributeOnce(element, name);
}

void xt_warn_unsupported_property(const std::string &property) {
  if (warningsSuppressed()) return;
  if (!hasUpperAscii(property)) {
    warnPropertyOnce(property);
    return;
  }
  warnPropertyOnce(lowerAscii(property));
}

void xt_warn_unsupported_at_rule(const std::string &name) {
  if (warningsSuppressed() || name.empty()) return;
  warnAtRuleOnce(hasUpperAscii(name) ? lowerAscii(name) : name);
}

void xt_warn_unsupported_descriptor(const std::string &name) {
  if (warningsSuppressed() || name.empty()) return;
  warnDescriptorOnce(hasUpperAscii(name) ? lowerAscii(name) : name);
}

void xt_warn_unsupported_selector(const std::string &selector) {
  if (warningsSuppressed()) return;
  const std::string text = selector;
  if (text.empty()) return;
  warnSelectorOnce(text);
}

void xt_warn_unsupported_pseudo_class(const std::string &name) {
  if (warningsSuppressed() || name.empty()) return;
  warnPseudoClassOnce(hasUpperAscii(name) ? lowerAscii(name) : name);
}

void xt_warn_unsupported_pseudo_element(const std::string &name) {
  if (warningsSuppressed() || name.empty()) return;
  warnPseudoElementOnce(hasUpperAscii(name) ? lowerAscii(name) : name);
}

void xt_warn_unsupported_media_feature(const std::string &name) {
  if (warningsSuppressed() || name.empty()) return;
  warnMediaFeatureOnce(hasUpperAscii(name) ? lowerAscii(name) : name);
}

void xt_warn_unsupported_function(const std::string &property, const std::string &function) {
  if (warningsSuppressed() || function.empty()) return;
  warnFunctionOnce(property, hasUpperAscii(function) ? lowerAscii(function) : function);
}

void xt_warn_unsupported_unit(const std::string &unit) {
  if (warningsSuppressed() || unit.empty()) return;
  warnUnitOnce(hasUpperAscii(unit) ? lowerAscii(unit) : unit);
}

}  // namespace xtgui
