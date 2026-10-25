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


/* -- custom properties (var()) ------------------------------------------- */

static bool isCustomProperty(const std::string &property) {
  return property.size() > 2 && property[0] == '-' && property[1] == '-';
}

/** Find the `)` matching the `(` at `open`, respecting nested functions. */
static size_t matchingParen(const std::string &text, size_t open) {
  int depth = 0;
  for (size_t i = open; i < text.size(); i++) {
    if (text[i] == '(') depth++;
    else if (text[i] == ')') {
      depth--;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

/** Split a `var(...)` body at the first top-level comma. */
static void splitVarBody(const std::string &body, std::string *name, std::string *fallback,
                         bool *hasFallback) {
  int depth = 0;
  for (size_t i = 0; i < body.size(); i++) {
    if (body[i] == '(') depth++;
    else if (body[i] == ')') depth--;
    else if (body[i] == ',' && depth == 0) {
      *name = xt_css_trim(body.substr(0, i));
      *fallback = xt_css_trim(body.substr(i + 1));
      *hasFallback = true;
      return;
    }
  }
  *name = xt_css_trim(body);
  *fallback = "";
  *hasFallback = false;
}

/** Substitute every `var(--name[, fallback])` in `value`. Returns false when a
 * referenced property is undefined and no fallback exists, which makes the
 * declaration invalid (the browser rule). */
static bool substituteVariables(const std::string &value,
                                const std::unordered_map<std::string, std::string> &variables,
                                int depth, std::string *out) {
  if (depth > 16) return false;
  out->clear();
  size_t i = 0;
  while (i < value.size()) {
    if ((value[i] == 'v' || value[i] == 'V') && i + 4 <= value.size() &&
        value.compare(i, 4, "var(") == 0) {
      size_t close = matchingParen(value, i + 3);
      if (close == std::string::npos) return false;
      std::string body = value.substr(i + 4, close - i - 4);
      std::string name, fallback;
      bool hasFallback = false;
      splitVarBody(body, &name, &fallback, &hasFallback);
      auto found = variables.find(name);
      std::string replacement;
      if (found != variables.end()) {
        if (!substituteVariables(found->second, variables, depth + 1, &replacement)) return false;
      } else if (hasFallback) {
        if (!substituteVariables(fallback, variables, depth + 1, &replacement)) return false;
      } else {
        return false;
      }
      *out += replacement;
      i = close + 1;
      continue;
    }
    *out += value[i];
    i += 1;
  }
  return true;
}

void finalizeStyle(XtStyle *style, const ApplyContext &context) {
  switch (style->line_height.unit) {
    case Unit::Em:
    case Unit::Percent:
    case Unit::Rem:
    case Unit::Vw:
    case Unit::Vh:
    case Unit::Clamp:
    case Unit::Min:
    case Unit::Max:
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
      style.visible = parent->visible;
      style.pointer_events = parent->pointer_events;
      style.variables = parent->variables;
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
    /* Custom properties participate in the cascade first; their final values
     * are then available to every normal declaration on this element. */
    for (const Candidate &candidate : candidates) {
      if (isCustomProperty(candidate.declaration->property)) {
        style.variables[candidate.declaration->property] = candidate.declaration->value;
      }
    }
    for (const Candidate &candidate : candidates) {
      if (isCustomProperty(candidate.declaration->property)) continue;
      std::string value = candidate.declaration->value;
      if (value.find("var(") != std::string::npos) {
        std::string resolved;
        if (!substituteVariables(value, style.variables, 0, &resolved)) continue;
        value = resolved;
      }
      xt_style_apply_declaration(&style, candidate.declaration->property, value, nodeContext);
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
