/*
 * xbintsc GUI engine — CSS parsing, selector matching and value parsing.
 *
 * The CSS subset is the practical slice a UI needs: type/class/id/attribute and
 * a few structural pseudo-class selectors, descendant and child combinators,
 * and the value types used by the cascade (lengths, percentages, colors,
 * keywords). At-rules are skipped.
 */
#ifndef XT_GUI_CSS_H
#define XT_GUI_CSS_H

#include <string>
#include <utility>
#include <vector>

#include "dom.h"

namespace xtgui {

enum class Unit { Auto, None, Px, Percent, Em, Rem, Vw, Vh, Number, Invalid };

struct Length {
  float value = 0.0f;
  Unit unit = Unit::Auto;
};

struct Color {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
  bool valid = false;
};

struct Declaration {
  std::string property;
  std::string value;
  bool important = false;
};

/** One `[name op value]` attribute selector. `op` empty = existence test. */
struct AttrSelector {
  std::string name;
  std::string value;
  std::string op;
};

/** One compound selector: `div.card#main[data-x="1"]:first-child`. */
struct CompoundSelector {
  std::string tag;       // empty = universal (`*`)
  std::string id;        // empty = none
  std::vector<std::string> classes;
  std::vector<AttrSelector> attrs;
  std::vector<std::string> pseudoClasses;
};

enum class Combinator { Descendant, Child, Adjacent, Sibling };

struct ComplexSelector {
  std::vector<CompoundSelector> compounds;  // left -> right
  std::vector<Combinator> combinators;      // compounds.size() - 1 entries
  int specificity = 0;                      // packed (a<<20)|(b<<10)|c
};

struct Rule {
  std::vector<ComplexSelector> selectors;
  std::vector<Declaration> declarations;
  int order = 0;
};

struct StyleSheet {
  std::vector<Rule> rules;
};

/** Dynamic element state consulted by `:hover` / `:focus` while matching. */
struct MatchState {
  const Node *hover = nullptr;
  const Node *focus = nullptr;
};

StyleSheet xt_css_parse_stylesheet(const std::string &text);
std::vector<ComplexSelector> xt_css_parse_selector_list(const std::string &text);
std::vector<Declaration> xt_css_parse_declarations(const std::string &text);

bool xt_css_match(const Node *node, const ComplexSelector &selector);
bool xt_css_match(const Node *node, const ComplexSelector &selector, const MatchState &state);

Length xt_css_parse_length(const std::string &value);
Color xt_css_parse_color(const std::string &value);
bool xt_css_parse_number(const std::string &value, float *out);

std::string xt_css_trim(const std::string &text);
std::vector<std::string> xt_css_split_whitespace(const std::string &text);
std::string xt_css_lower(const std::string &text);

}  // namespace xtgui

#endif /* XT_GUI_CSS_H */
