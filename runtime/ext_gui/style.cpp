/*
 * xbintsc GUI engine — cascade, inheritance and computed style.
 *
 * Property parsing/application lives in `style_values.cpp`, the built-in
 * user-agent rules in `style_ua.cpp` and the diagnostic stringification in
 * `style_print.cpp`.
 */

#include "style_internal.h"

#include "css_internal.h"
#include "perf.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace xtgui {

XtStyle xt_style_initial() { return XtStyle(); }

/* -- generated-content arena ---------------------------------------------- */

void PseudoNodeSet::clear() { nodes_.clear(); }

Node *PseudoNodeSet::create(Node *origin, PseudoElement side) {
  auto node = std::make_unique<Node>(NodeType::Element);
  node->is_pseudo = true;
  node->pseudo = side;
  node->pseudo_origin = origin;
  node->parent = origin;
  node->tag = side == PseudoElement::Before ? "::before" : "::after";
  Node *raw = node.get();
  nodes_.push_back(std::move(node));
  return raw;
}

/* -- cascade -------------------------------------------------------------- */

namespace {

struct Candidate {
  int important = 0;
  int specificity = 0;
  int order = 0;
  /** Push order, so two candidates with the same `order` (the inline
   * declarations all use `INT_MAX`) keep their declaration order. */
  int sequence = 0;
  const Declaration *declaration = nullptr;
};

bool candidateBefore(const Candidate &a, const Candidate &b) {
  if (a.important != b.important) return a.important < b.important;
  if (a.specificity != b.specificity) return a.specificity < b.specificity;
  if (a.order != b.order) return a.order < b.order;
  return a.sequence < b.sequence;
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
    case Unit::Calc:
      style->line_height.value = xt_style_resolve_absolute(
          style->line_height, style->font_size, style->font_size, context.rootFontSize,
          context.viewportWidth, context.viewportHeight);
      style->line_height.unit = Unit::Px;
      break;
    default:
      break;
  }
}

void inheritFrom(XtStyle *style, const XtStyle *parent) {
  /* Inherited properties start from the parent; everything else keeps its
   * initial value until a declaration overrides it. */
  style->color = parent->color;
  style->visible = parent->visible;
  style->pointer_events = parent->pointer_events;
  /* Most documents define no custom properties; copying the (empty) map for
   * every element is pure overhead. */
  if (!parent->variables.empty()) style->variables = parent->variables;
  style->font_size = parent->font_size;
  style->font_weight = parent->font_weight;
  style->font_family = parent->font_family;
  style->font_style = parent->font_style;
  style->line_height = parent->line_height;
  style->text_align = parent->text_align;
  style->white_space = parent->white_space;
  style->text_shadow = parent->text_shadow;
}

void applyCandidates(XtStyle *style, std::vector<Candidate> *candidates,
                     const ApplyContext &nodeContext) {
  /* `candidateBefore` is a total order now (the sequence breaks ties), so an
   * unstable `std::sort` is equivalent to the old `stable_sort` without its
   * temporary buffer. */
  std::sort(candidates->begin(), candidates->end(), candidateBefore);
  /* Custom properties participate in the cascade first; their final values are
   * then available to every normal declaration on this element. */
  for (const Candidate &candidate : *candidates) {
    if (isCustomProperty(candidate.declaration->property)) {
      style->variables[candidate.declaration->property] = candidate.declaration->value;
    }
  }
  for (const Candidate &candidate : *candidates) {
    if (isCustomProperty(candidate.declaration->property)) continue;
    std::string value = candidate.declaration->value;
    if (value.find("var(") != std::string::npos) {
      std::string resolved;
      if (!substituteVariables(value, style->variables, 0, &resolved)) continue;
      value = resolved;
    }
    xt_style_apply_declaration(style, candidate.declaration->property, value, nodeContext);
  }
}

/* -- generated content ---------------------------------------------------- */

void appendUtf8(std::string *out, unsigned int codepoint) {
  if (codepoint <= 0x7f) {
    out->push_back((char)codepoint);
  } else if (codepoint <= 0x7ff) {
    out->push_back((char)(0xc0 | (codepoint >> 6)));
    out->push_back((char)(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    out->push_back((char)(0xe0 | (codepoint >> 12)));
    out->push_back((char)(0x80 | ((codepoint >> 6) & 0x3f)));
    out->push_back((char)(0x80 | (codepoint & 0x3f)));
  } else {
    out->push_back((char)(0xf0 | (codepoint >> 18)));
    out->push_back((char)(0x80 | ((codepoint >> 12) & 0x3f)));
    out->push_back((char)(0x80 | ((codepoint >> 6) & 0x3f)));
    out->push_back((char)(0x80 | (codepoint & 0x3f)));
  }
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/** Decode a CSS string literal (without its quotes), honouring `\` escapes. */
std::string decodeCssString(const std::string &text) {
  std::string out;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] != '\\' || i + 1 >= text.size()) {
      out.push_back(text[i]);
      continue;
    }
    i++;
    int value = hexValue(text[i]);
    if (value < 0) {
      if (text[i] != '\n') out.push_back(text[i]);
      continue;
    }
    unsigned int codepoint = (unsigned int)value;
    size_t digits = 1;
    while (digits < 6 && i + 1 < text.size() && hexValue(text[i + 1]) >= 0) {
      i++;
      digits++;
      codepoint = codepoint * 16 + (unsigned int)hexValue(text[i]);
    }
    if (i + 1 < text.size() && xt_is_css_space(text[i + 1])) i++;
    appendUtf8(&out, codepoint);
  }
  return out;
}

/** Resolve a `content` value into generated text. `*generated` is false for
 * `content: none`/`normal`. `attr()` reads the originating element. */
std::string resolveContent(const Node *origin, const std::string &raw, bool *generated) {
  *generated = false;
  std::string text;
  std::string value = xt_css_trim(raw);
  std::string lower = xt_css_lower(value);
  if (lower == "none" || lower == "normal" || value.empty()) return "";
  *generated = true;
  size_t i = 0;
  while (i < value.size()) {
    char c = value[i];
    if (xt_is_css_space(c) || c == ',') {
      i++;
      continue;
    }
    if (c == '"' || c == '\'') {
      char quote = c;
      size_t start = ++i;
      while (i < value.size() && value[i] != quote) {
        if (value[i] == '\\' && i + 1 < value.size()) i++;
        i++;
      }
      text += decodeCssString(value.substr(start, i - start));
      if (i < value.size()) i++;
      continue;
    }
    if (c == '/') break;  // alternative text after the slash
    size_t start = i;
    while (i < value.size() && (std::isalnum((unsigned char)value[i]) || value[i] == '-' ||
                                value[i] == '_'))
      i++;
    std::string token = xt_css_lower(value.substr(start, i - start));
    while (i < value.size() && xt_is_css_space(value[i])) i++;
    if (token == "none" || token == "normal") {
      *generated = false;
      return "";
    }
    if (token == "open-quote") text += "\"";
    else if (token == "close-quote") text += "\"";
    if (token == "attr" && i < value.size() && value[i] == '(') {
      int depth = 0;
      size_t open = i;
      for (; i < value.size(); i++) {
        if (value[i] == '(') depth++;
        else if (value[i] == ')') {
          depth--;
          if (depth == 0) {
            i++;
            break;
          }
        }
      }
      std::string name = xt_css_trim(value.substr(open + 1, i - open - 2));
      const std::string *attribute = origin != nullptr ? origin->attr(name) : nullptr;
      if (attribute != nullptr) text += *attribute;
      continue;
    }
    if (i < value.size() && value[i] == '(') {
      /* Unsupported function (`counter`, `counters`, `url`, …): skip it. */
      int depth = 0;
      for (; i < value.size(); i++) {
        if (value[i] == '(') depth++;
        else if (value[i] == ')') {
          depth--;
          if (depth == 0) {
            i++;
            break;
          }
        }
      }
    }
  }
  return text;
}

void resetPseudoPointers(const Node *node) {
  if (node->isElement() && !node->is_pseudo) {
    const_cast<Node *>(node)->pseudo_before = nullptr;
    const_cast<Node *>(node)->pseudo_after = nullptr;
  }
  for (const std::unique_ptr<Node> &child : node->children) resetPseudoPointers(child.get());
}

/** Per-computation scratch buffers. The cascade runs depth-first, so one
 * candidate list (and one inline-declaration list) can be reused for every
 * node — the old code allocated both per element. */
struct ComputeScratch {
  std::vector<Candidate> candidates;
  std::vector<Declaration> inline_declarations;
  int sequence = 0;
  /** Set when any computed style declares a `transition` (including an inline
   * `style="transition: …"`), so the document knows whether it needs the
   * transition snapshot path at all. */
  bool has_transitions = false;
  bool has_animations = false;
};

void pushCandidates(std::vector<Candidate> *candidates, ComputeScratch *scratch,
                    const Declaration &declaration, int important, int specificity, int order) {
  Candidate candidate;
  candidate.important = important;
  candidate.specificity = specificity;
  candidate.order = order;
  candidate.sequence = scratch->sequence++;
  candidate.declaration = &declaration;
  candidates->push_back(candidate);
}

/** Compute one `::before`/`::after` style and attach its generated content. */
void computePseudo(const Node *origin, PseudoElement side, const XtStyle &originStyle,
                   const StyleSheet &sheet, const std::vector<char> &applies,
                   std::unordered_map<const Node *, XtStyle> &out, const ApplyContext &context,
                   const MatchState &state, PseudoNodeSet *pseudos, ComputeScratch *scratch) {
  if (pseudos == nullptr || originStyle.display == Display::None) return;
  Node *pseudo = pseudos->create(const_cast<Node *>(origin), side);
  if (side == PseudoElement::Before) const_cast<Node *>(origin)->pseudo_before = pseudo;
  else const_cast<Node *>(origin)->pseudo_after = pseudo;

  XtStyle style = xt_style_initial();
  inheritFrom(&style, &originStyle);
  xt_style_apply_user_agent(pseudo, &style);

  ApplyContext nodeContext = context;
  nodeContext.parentFontSize = originStyle.font_size;

  std::string pseudoName = side == PseudoElement::Before ? "before" : "after";
  std::vector<Candidate> &candidates = scratch->candidates;
  candidates.clear();
  for (size_t ruleIndex = 0; ruleIndex < sheet.rules.size(); ruleIndex++) {
    if (applies[ruleIndex] == 0) continue;
    const Rule &rule = sheet.rules[ruleIndex];
    for (const ComplexSelector &selector : rule.selectors) {
      if (selector.compounds.empty() || selector.compounds.back().pseudo_element != pseudoName)
        continue;
      /* Match the originating element against the selector with its final
       * pseudo-element suffix removed: `.card.info::before` is `.card.info`. */
      ComplexSelector copy = selector;
      copy.compounds.back().pseudo_element.clear();
      g_perf_frame.rulesEvaluated += 1;
      if (!xt_css_match(origin, copy, state)) continue;
      for (const Declaration &declaration : rule.declarations) {
        pushCandidates(&candidates, scratch, declaration, declaration.important ? 1 : 0,
                       selector.specificity, rule.order);
      }
    }
  }
  applyCandidates(&style, &candidates, nodeContext);
  finalizeStyle(&style, nodeContext);
  if (!style.transitions.empty()) scratch->has_transitions = true;
  if (!style.animations.empty()) scratch->has_animations = true;

  bool generated = false;
  std::string text;
  if (style.has_content) text = resolveContent(origin, style.content, &generated);
  style.has_content = generated;
  style.content = text;
  out[pseudo] = style;
  if (generated) pseudo->addText(text);
}

void computeNode(const Node *node, const XtStyle *parent, const StyleSheet &sheet,
                 const std::vector<char> &applies, std::unordered_map<const Node *, XtStyle> &out,
                 const ApplyContext &context, const MatchState &state, PseudoNodeSet *pseudos,
                 ComputeScratch *scratch) {
  const XtStyle *childParent = parent;
  if (node->isElement() && !node->is_pseudo) {
    XtStyle style = xt_style_initial();
    if (parent != nullptr) {
      inheritFrom(&style, parent);
    } else {
      style.color = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    xt_style_apply_user_agent(node, &style);

    ApplyContext nodeContext = context;
    nodeContext.parentFontSize = (parent != nullptr) ? parent->font_size : context.rootFontSize;

    std::vector<Candidate> &candidates = scratch->candidates;
    candidates.clear();
    for (size_t ruleIndex = 0; ruleIndex < sheet.rules.size(); ruleIndex++) {
      if (applies[ruleIndex] == 0) continue;
      const Rule &rule = sheet.rules[ruleIndex];
      for (const ComplexSelector &selector : rule.selectors) {
        g_perf_frame.rulesEvaluated += 1;
        if (!xt_css_match(node, selector, state)) continue;
        for (const Declaration &declaration : rule.declarations) {
          pushCandidates(&candidates, scratch, declaration, declaration.important ? 1 : 0,
                         selector.specificity, rule.order);
        }
      }
    }
    const std::string *inlineStyle = node->attr("style");
    if (inlineStyle != nullptr) {
      std::vector<Declaration> &inlineDeclarations = scratch->inline_declarations;
      inlineDeclarations = xt_css_parse_declarations(*inlineStyle);
      for (const Declaration &declaration : inlineDeclarations) {
        pushCandidates(&candidates, scratch, declaration, declaration.important ? 1 : 0, 1 << 28,
                       INT_MAX);
      }
    }
    applyCandidates(&style, &candidates, nodeContext);
    finalizeStyle(&style, nodeContext);
    if (!style.transitions.empty()) scratch->has_transitions = true;
    if (!style.animations.empty()) scratch->has_animations = true;

    /* `emplace` keeps the address of an existing entry stable, which is what
     * `box->style` pointers rely on. */
    out[node] = style;
    childParent = &out[node];
    const XtStyle &computed = out[node];
    if (pseudos != nullptr && sheet.has_pseudo_elements) {
      computePseudo(node, PseudoElement::Before, computed, sheet, applies, out, context, state,
                    pseudos, scratch);
      computePseudo(node, PseudoElement::After, computed, sheet, applies, out, context, state,
                    pseudos, scratch);
    }
    g_perf_frame.restyleNodes += 1;
  }
  for (const std::unique_ptr<Node> &child : node->children) {
    computeNode(child.get(), childParent, sheet, applies, out, context, state, pseudos, scratch);
  }
}

}  // namespace

void xt_style_compute(const Node *root, const StyleSheet &sheet,
                      std::unordered_map<const Node *, XtStyle> &out, float viewportWidth,
                      float viewportHeight, const MatchState &state, PseudoNodeSet *pseudos,
                      bool *hasTransitions, bool *hasAnimations) {
  out.clear();
  bool wantsPseudo = pseudos != nullptr && sheet.has_pseudo_elements;
  if (wantsPseudo && root != nullptr) {
    resetPseudoPointers(root);
    pseudos->clear();
  }
  ApplyContext context;
  context.viewportWidth = viewportWidth;
  context.viewportHeight = viewportHeight;
  /* An `@media` prelude is evaluated once per rule (not once per rule *and*
   * node): the query never depends on the element, so this was the engine's
   * largest per-element cost on a large document. */
  std::vector<char> applies(sheet.rules.size(), 0);
  for (size_t i = 0; i < sheet.rules.size(); i++) {
    const std::string &media = sheet.rules[i].media;
    if (media.empty()) {
      applies[i] = 1;
      continue;
    }
    g_perf_frame.mediaEvaluated += 1;
    applies[i] =
        xt_css_media_matches(media, viewportWidth, viewportHeight) ? (char)1 : (char)0;
  }
  ComputeScratch scratch;
  scratch.candidates.reserve(32);
  if (root != nullptr) {
    computeNode(root, nullptr, sheet, applies, out, context, state, wantsPseudo ? pseudos : nullptr,
                &scratch);
  }
  if (hasTransitions != nullptr) {
    *hasTransitions = *hasTransitions || scratch.has_transitions;
  }
  if (hasAnimations != nullptr) {
    *hasAnimations = *hasAnimations || scratch.has_animations;
  }
}

}  // namespace xtgui
