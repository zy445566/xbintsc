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

enum class Unit {
  Auto,
  None,
  Px,
  Percent,
  Em,
  Rem,
  Vw,
  Vh,
  Number,
  Clamp,
  Min,
  Max,
  /** A `calc()` sub-expression: `args[0] <op> args[1]`. Nested arithmetic keeps
   * its operands unresolved, so only the units actually present are resolved
   * against the element (a `calc(100% - 2em)` depends on both the containing
   * block and the font size). */
  Calc,
  Invalid
};

struct Length {
  float value = 0.0f;
  Unit unit = Unit::Auto;
  /** `+`, `-`, `*` or `/` for `Unit::Calc`; zero otherwise. */
  char op = 0;
  /* Arguments of `clamp(...)`, `min(...)`, `max(...)` and `calc(...)`; empty for
   * a scalar length. Kept unresolved so vw/vh/em are resolved against the
   * element that uses the length, not the viewport at parse time. */
  std::vector<Length> args = {};
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
  /** `before`/`after` for a `::before`/`::after` (or legacy `:before`)
   * pseudo-element; empty for an ordinary selector. */
  std::string pseudo_element;
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
  /** Raw `@media` prelude this rule came from (empty = unconditional). The
   * query is evaluated against the current viewport during the cascade, so a
   * resize restyles with the right rules. */
  std::string media;
};

/** One step of an `@keyframes` rule: a normalised offset (0..1) and the
 * declarations that apply there. */
struct Keyframe {
  float offset = 0.0f;
  std::vector<Declaration> declarations;
};

/** One `@keyframes <name>` block. Offsets are kept sorted so the animation
 * engine can bracket a progress with two adjacent steps. */
struct KeyframesRule {
  std::string name;
  std::vector<Keyframe> keyframes;
};

/** One `@font-face` source: a local file URL plus the optional `format(...)`
 * hint. `local(...)` sources are dropped at parse time (the runtime has no
 * name-to-file font lookup for them). */
struct FontFaceSource {
  std::string url;
  std::string format;
};

/** One author `@font-face` rule. The runtime resolves the sources to a real
 * font file when the document loads; the declared family name is what CSS
 * matches, even when it differs from the font's internal name. */
struct FontFaceRule {
  std::string family;
  std::vector<FontFaceSource> sources;
  /** `font-weight` descriptor. A range (`100 900`) is kept as min/max; when the
   * descriptor is absent the font's OS/2 weight is used. */
  bool has_weight = false;
  int weight_min = 400;
  int weight_max = 400;
  /** `font-style` descriptor; absent means the font's own italic flag. */
  bool has_italic = false;
  bool italic = false;
};

struct StyleSheet {
  std::vector<Rule> rules;
  /** `@keyframes` blocks, in declaration order. */
  std::vector<KeyframesRule> keyframes;
  /** `@font-face` blocks, in declaration order. */
  std::vector<FontFaceRule> font_faces;
  /* Facts the cascade can act on once per sheet instead of once per element (a
   * `@media` block, a `::before` rule or a `transition` declaration used to be
   * discovered by scanning every rule for every node). */
  bool has_pseudo_elements = false;
  bool has_hover = false;
  bool has_focus = false;
  bool has_active = false;
  bool has_transitions = false;
  bool has_animations = false;
};

/** Look up the last `@keyframes` rule with `name` (later blocks replace
 * earlier ones), or null when the sheet has no such animation. */
const KeyframesRule *xt_css_find_keyframes(const StyleSheet &sheet, const std::string &name);

/** Dynamic element state consulted by `:hover` / `:focus` while matching. */
struct MatchState {
  const Node *hover = nullptr;
  const Node *focus = nullptr;
  /** The element currently held down by the primary pointer (`:active`). */
  const Node *active = nullptr;
};

StyleSheet xt_css_parse_stylesheet(const std::string &text);
std::vector<ComplexSelector> xt_css_parse_selector_list(const std::string &text);
std::vector<Declaration> xt_css_parse_declarations(const std::string &text);

bool xt_css_match(const Node *node, const ComplexSelector &selector);
bool xt_css_match(const Node *node, const ComplexSelector &selector, const MatchState &state);

/** True when a raw `@media` prelude matches a viewport. Supports media types
 * (`all`/`screen`), comma-separated query lists, `not`/`only`, `and`, and the
 * width/height/orientation/hover/pointer/colour-scheme features (with
 * `min-`/`max-` ranges). Unknown features never match, as in a browser. */
bool xt_css_media_matches(const std::string &query, float viewport_width, float viewport_height);

/** Split `text` at top-level commas (outside `(...)`, `[...]` and quotes),
 * trimming each part and dropping empty ones. */
std::vector<std::string> xt_css_split_commas(const std::string &text);

Length xt_css_parse_length(const std::string &value);
Color xt_css_parse_color(const std::string &value);
bool xt_css_parse_number(const std::string &value, float *out);

std::string xt_css_trim(const std::string &text);
std::vector<std::string> xt_css_split_whitespace(const std::string &text);
std::string xt_css_lower(const std::string &text);

}  // namespace xtgui

#endif /* XT_GUI_CSS_H */
