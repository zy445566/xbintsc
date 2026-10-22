/*
 * xbintsc GUI engine — inline `<svg>` serialization. See `inline_svg.h`.
 */

#include "inline_svg.h"

#include <cctype>
#include <string>
#include <unordered_map>

namespace xtgui {
namespace {

/** SVG element names that are case-sensitive and contain an upper-case letter.
 * The HTML parser lowercases tag names, so the serializer maps them back;
 * everything not listed is already lower case in SVG (`path`, `rect`, `g`,
 * `defs`, `stop`, …). */
const std::unordered_map<std::string, const char *> &canonicalTags() {
  static const std::unordered_map<std::string, const char *> tags = {
      {"altglyph", "altGlyph"},
      {"altglyphdef", "altGlyphDef"},
      {"altglyphitem", "altGlyphItem"},
      {"animatecolor", "animateColor"},
      {"animatemotion", "animateMotion"},
      {"animatetransform", "animateTransform"},
      {"clippath", "clipPath"},
      {"feblend", "feBlend"},
      {"fecolormatrix", "feColorMatrix"},
      {"fecomponenttransfer", "feComponentTransfer"},
      {"fecomposite", "feComposite"},
      {"feconvolvematrix", "feConvolveMatrix"},
      {"fediffuselighting", "feDiffuseLighting"},
      {"fedisplacementmap", "feDisplacementMap"},
      {"fedistantlight", "feDistantLight"},
      {"fedropshadow", "feDropShadow"},
      {"feflood", "feFlood"},
      {"fefunca", "feFuncA"},
      {"fefuncb", "feFuncB"},
      {"fefuncg", "feFuncG"},
      {"fefuncr", "feFuncR"},
      {"fegaussianblur", "feGaussianBlur"},
      {"feimage", "feImage"},
      {"femerge", "feMerge"},
      {"femergenode", "feMergeNode"},
      {"femorphology", "feMorphology"},
      {"feoffset", "feOffset"},
      {"fepointlight", "fePointLight"},
      {"fespecularlighting", "feSpecularLighting"},
      {"fespotlight", "feSpotLight"},
      {"fetile", "feTile"},
      {"feturbulence", "feTurbulence"},
      {"foreignobject", "foreignObject"},
      {"glyphref", "glyphRef"},
      {"lineargradient", "linearGradient"},
      {"radialgradient", "radialGradient"},
      {"textpath", "textPath"},
  };
  return tags;
}

/** The same for attributes: NanoSVG compares these names exactly, and the HTML
 * parser delivers them in lower case. The table also covers the common SVG
 * attributes NanoSVG does not read, so the markup it is handed stays faithful
 * to what a browser would parse. */
const std::unordered_map<std::string, const char *> &canonicalAttrs() {
  static const std::unordered_map<std::string, const char *> attrs = {
      {"attributename", "attributeName"},
      {"attributetype", "attributeType"},
      {"basefrequency", "baseFrequency"},
      {"baseprofile", "baseProfile"},
      {"calcmode", "calcMode"},
      {"clippathunits", "clipPathUnits"},
      {"contentscripttype", "contentScriptType"},
      {"contentstyletype", "contentStyleType"},
      {"diffuseconstant", "diffuseConstant"},
      {"edgemode", "edgeMode"},
      {"filterunits", "filterUnits"},
      {"gradienttransform", "gradientTransform"},
      {"gradientunits", "gradientUnits"},
      {"kernelmatrix", "kernelMatrix"},
      {"kernelunitlength", "kernelUnitLength"},
      {"keypoints", "keyPoints"},
      {"keysplines", "keySplines"},
      {"keytimes", "keyTimes"},
      {"lengthadjust", "lengthAdjust"},
      {"limitingconeangle", "limitingConeAngle"},
      {"markerheight", "markerHeight"},
      {"markerunits", "markerUnits"},
      {"markerwidth", "markerWidth"},
      {"maskcontentunits", "maskContentUnits"},
      {"maskunits", "maskUnits"},
      {"numoctaves", "numOctaves"},
      {"pathlength", "pathLength"},
      {"patterncontentunits", "patternContentUnits"},
      {"patterntransform", "patternTransform"},
      {"patternunits", "patternUnits"},
      {"pointsatx", "pointsAtX"},
      {"pointsaty", "pointsAtY"},
      {"pointsatz", "pointsAtZ"},
      {"preserveaspectratio", "preserveAspectRatio"},
      {"primitiveunits", "primitiveUnits"},
      {"refx", "refX"},
      {"refy", "refY"},
      {"repeatcount", "repeatCount"},
      {"repeatdur", "repeatDur"},
      {"requiredextensions", "requiredExtensions"},
      {"requiredfeatures", "requiredFeatures"},
      {"specularconstant", "specularConstant"},
      {"specularexponent", "specularExponent"},
      {"spreadmethod", "spreadMethod"},
      {"startoffset", "startOffset"},
      {"stddeviation", "stdDeviation"},
      {"stitchtiles", "stitchTiles"},
      {"surfacescale", "surfaceScale"},
      {"systemlanguage", "systemLanguage"},
      {"tablevalues", "tableValues"},
      {"targetx", "targetX"},
      {"targety", "targetY"},
      {"textlength", "textLength"},
      {"transformorigin", "transformOrigin"},
      {"viewbox", "viewBox"},
      {"viewtarget", "viewTarget"},
      {"xchannelselector", "xChannelSelector"},
      {"ychannelselector", "yChannelSelector"},
      {"zoomandpan", "zoomAndPan"},
  };
  return attrs;
}

void escapeAttribute(const std::string &value, std::string *out) {
  for (char c : value) {
    switch (c) {
      case '&': out->append("&amp;"); break;
      case '<': out->append("&lt;"); break;
      case '>': out->append("&gt;"); break;
      case '"': out->append("&quot;"); break;
      default: out->push_back(c); break;
    }
  }
}

std::string canonicalTag(const std::string &tag) {
  const auto &tags = canonicalTags();
  auto found = tags.find(tag);
  return found == tags.end() ? tag : std::string(found->second);
}

const char *canonicalAttr(const std::string &name) {
  const auto &attrs = canonicalAttrs();
  auto found = attrs.find(name);
  return found == attrs.end() ? nullptr : found->second;
}

bool isNamespaceAttr(const std::string &name) {
  return name == "xmlns" || name.compare(0, 6, "xmlns:") == 0;
}

/** HTML-only attributes that must not reach the SVG reader (inline `style` and
 * `class` are legitimate on SVG elements and stay). */
bool isHtmlAttr(const std::string &name) {
  return name == "tabindex" || name == "role" || name == "contenteditable";
}

void serializeNode(const Node *node, std::string *out);

void serializeStyleText(const Node *node, std::string *out) {
  for (const std::unique_ptr<Node> &child : node->children) {
    if (child->isText()) {
      out->append(child->text);
    } else if (child->isElement()) {
      serializeNode(child.get(), out);
    }
  }
}

void serializeNode(const Node *node, std::string *out) {
  if (node == nullptr || !node->isElement()) return;
  std::string tag = canonicalTag(node->tag);
  out->push_back('<');
  out->append(tag);
  if (node->isTag("svg")) out->append(" xmlns=\"http://www.w3.org/2000/svg\"");
  for (const Attr &attr : node->attrs) {
    if (isNamespaceAttr(attr.name) || isHtmlAttr(attr.name)) continue;
    /* The root's xmlns is emitted above, so a second copy would be an XML
     * duplicate-attribute error. */
    const char *name = canonicalAttr(attr.name);
    out->push_back(' ');
    out->append(name != nullptr ? name : attr.name);
    out->append("=\"");
    escapeAttribute(attr.value, out);
    out->push_back('"');
  }
  out->push_back('>');
  if (node->isTag("style")) {
    serializeStyleText(node, out);
  } else {
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child->isElement()) serializeNode(child.get(), out);
    }
  }
  out->append("</");
  out->append(tag);
  out->push_back('>');
}

}  // namespace

bool xt_inline_svg_is_root(const Node *node) {
  return node != nullptr && node->isElement() && node->isTag("svg");
}

std::string xt_inline_svg_serialize(const Node *node) {
  if (!xt_inline_svg_is_root(node)) return std::string();
  std::string out;
  serializeNode(node, &out);
  return out;
}

}  // namespace xtgui
