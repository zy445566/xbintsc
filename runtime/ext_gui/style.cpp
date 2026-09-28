/*
 * xbintsc GUI engine — cascade, inheritance and computed style.
 *
 * Property parsing/application lives in `style_values.cpp`, the built-in
 * user-agent rules in `style_ua.cpp` and the diagnostic stringification in
 * `style_print.cpp`.
 */

#include "style_internal.h"

#include <algorithm>
#include <climits>
#include <unordered_map>
#include <vector>

namespace xtgui {

XtStyle xt_style_initial() { return XtStyle(); }

/* -- cascade -------------------------------------------------------------- */

namespace {

struct Candidate {
  int important = 0;
  int specificity = 0;
  int order = 0;
  const Declaration *declaration = nullptr;
};

bool candidateBefore(const Candidate &a, const Candidate &b) {
  if (a.important != b.important) return a.important < b.important;
  if (a.specificity != b.specificity) return a.specificity < b.specificity;
  return a.order < b.order;
}

void finalizeStyle(XtStyle *style, const ApplyContext &context) {
  switch (style->line_height.unit) {
    case Unit::Em:
    case Unit::Percent:
    case Unit::Rem:
    case Unit::Vw:
    case Unit::Vh:
      style->line_height.value = xt_style_resolve_absolute(
          style->line_height, style->font_size, style->font_size, context.rootFontSize,
          context.viewportWidth, context.viewportHeight);
      style->line_height.unit = Unit::Px;
      break;
    default:
      break;
  }
}

void computeNode(const Node *node, const XtStyle *parent, const StyleSheet &sheet,
                 std::unordered_map<const Node *, XtStyle> &out, const ApplyContext &context,
                 const MatchState &state) {
  const XtStyle *childParent = parent;
  if (node->isElement()) {
    XtStyle style = xt_style_initial();
    if (parent != nullptr) {
      /* Inherited properties start from the parent; everything else keeps its
       * initial value until a declaration overrides it. */
      style.color = parent->color;
      style.font_size = parent->font_size;
      style.font_weight = parent->font_weight;
      style.font_family = parent->font_family;
      style.font_style = parent->font_style;
      style.line_height = parent->line_height;
      style.text_align = parent->text_align;
      style.white_space = parent->white_space;
    } else {
      style.color = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    xt_style_apply_user_agent(node, &style);

    ApplyContext nodeContext = context;
    nodeContext.parentFontSize = (parent != nullptr) ? parent->font_size : context.rootFontSize;

    std::vector<Candidate> candidates;
    for (const Rule &rule : sheet.rules) {
      for (const ComplexSelector &selector : rule.selectors) {
        if (!xt_css_match(node, selector, state)) continue;
        for (const Declaration &declaration : rule.declarations) {
          candidates.push_back({declaration.important ? 1 : 0, selector.specificity, rule.order, &declaration});
        }
      }
    }
    const std::string *inlineStyle = node->attr("style");
    std::vector<Declaration> inlineDeclarations;
    if (inlineStyle != nullptr) {
      inlineDeclarations = xt_css_parse_declarations(*inlineStyle);
      for (const Declaration &declaration : inlineDeclarations) {
        candidates.push_back({declaration.important ? 1 : 0, 1 << 28, INT_MAX, &declaration});
      }
    }
    std::stable_sort(candidates.begin(), candidates.end(), candidateBefore);
    for (const Candidate &candidate : candidates) {
      xt_style_apply_declaration(&style, candidate.declaration->property, candidate.declaration->value,
                                 nodeContext);
    }
    finalizeStyle(&style, nodeContext);

    out[node] = style;
    childParent = &out[node];
  }
  for (const std::unique_ptr<Node> &child : node->children) {
    computeNode(child.get(), childParent, sheet, out, context, state);
  }
}

}  // namespace

void xt_style_compute(const Node *root, const StyleSheet &sheet,
                      std::unordered_map<const Node *, XtStyle> &out, float viewportWidth,
                      float viewportHeight, const MatchState &state) {
  out.clear();
  ApplyContext context;
  context.viewportWidth = viewportWidth;
  context.viewportHeight = viewportHeight;
  computeNode(root, nullptr, sheet, out, context, state);
}

}  // namespace xtgui
