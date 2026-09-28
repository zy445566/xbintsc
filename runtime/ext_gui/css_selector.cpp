/*
 * xbintsc GUI engine — CSS selector list parser.
 */

#include "css_internal.h"

namespace xtgui {

namespace {

class SelectorParser {
 public:
  explicit SelectorParser(const std::string &text) : text_(text) {}

  std::vector<ComplexSelector> parseList() {
    std::vector<ComplexSelector> selectors;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i <= text_.size(); i++) {
      char c = (i < text_.size()) ? text_[i] : ',';
      if (c == '(' || c == '[') depth++;
      else if (c == ')' || c == ']') depth--;
      else if (c == ',' && depth == 0) {
        std::string part = text_.substr(start, i - start);
        ComplexSelector selector;
        if (parseComplex(part, &selector) && !selector.compounds.empty()) {
          selectors.push_back(std::move(selector));
        }
        start = i + 1;
      }
    }
    return selectors;
  }

 private:
  const std::string &text_;

  std::string readIdent(const std::string &text, size_t *index) {
    size_t start = *index;
    while (*index < text.size() && xt_css_is_ident_char(text[*index])) (*index)++;
    return text.substr(start, *index - start);
  }

  /** Parse `div.foo#bar[a="b"]:pseudo` into one compound selector. */
  bool parseCompound(const std::string &text, size_t *index, CompoundSelector *compound) {
    bool any = false;
    while (*index < text.size()) {
      char c = text[*index];
      if (xt_is_css_space(c) || c == '>' || c == '+' || c == '~' || c == ',') break;
      if (c == '*') {
        (*index)++;
        any = true;
      } else if (c == '.') {
        (*index)++;
        std::string name = readIdent(text, index);
        if (name.empty()) return false;
        compound->classes.push_back(name);
        any = true;
      } else if (c == '#') {
        (*index)++;
        compound->id = readIdent(text, index);
        any = true;
      } else if (c == '[') {
        if (!parseAttribute(text, index, compound)) return false;
        any = true;
      } else if (c == ':') {
        (*index)++;
        bool pseudoElement = false;
        if (*index < text.size() && text[*index] == ':') {
          pseudoElement = true;
          (*index)++;
        }
        std::string name = xt_css_lower(readIdent(text, index));
        if (name.empty()) return false;
        if (!pseudoElement && *index < text.size() && text[*index] == '(') {
          int depth = 0;
          size_t start = *index;
          while (*index < text.size()) {
            if (text[*index] == '(') depth++;
            else if (text[*index] == ')') {
              depth--;
              if (depth == 0) {
                (*index)++;
                break;
              }
            }
            (*index)++;
          }
          name += text.substr(start, *index - start);
        }
        compound->pseudoClasses.push_back(name);
        any = true;
      } else if (xt_css_is_ident_start(c)) {
        compound->tag = xt_css_lower(readIdent(text, index));
        any = true;
      } else {
        return false;
      }
    }
    return any;
  }

  bool parseAttribute(const std::string &text, size_t *index, CompoundSelector *compound) {
    (*index)++;  // '['
    while (*index < text.size() && xt_is_css_space(text[*index])) (*index)++;
    AttrSelector attr;
    attr.name = xt_css_lower(readIdent(text, index));
    if (attr.name.empty()) return false;
    while (*index < text.size() && xt_is_css_space(text[*index])) (*index)++;
    if (*index < text.size() && text[*index] == ']') {
      (*index)++;
      compound->attrs.push_back(attr);
      return true;
    }
    if (*index < text.size() && (text[*index] == '=' || text[*index] == '~' || text[*index] == '|' ||
                                 text[*index] == '^' || text[*index] == '$' || text[*index] == '*')) {
      std::string op(1, text[*index]);
      (*index)++;
      if (*index < text.size() && text[*index] == '=') {
        op += '=';
        (*index)++;
      } else if (op != "=") {
        /* Only `~= |= ^= $= *=` need a trailing `=`; a lone `=` is complete. */
        return false;
      }
      while (*index < text.size() && xt_is_css_space(text[*index])) (*index)++;
      std::string value;
      if (*index < text.size() && (text[*index] == '"' || text[*index] == '\'')) {
        char quote = text[(*index)++];
        size_t start = *index;
        while (*index < text.size() && text[*index] != quote) (*index)++;
        value = text.substr(start, *index - start);
        if (*index < text.size()) (*index)++;
      } else {
        value = readIdent(text, index);
      }
      attr.op = op;
      attr.value = value;
    }
    while (*index < text.size() && xt_is_css_space(text[*index])) (*index)++;
    if (*index >= text.size() || text[*index] != ']') return false;
    (*index)++;
    compound->attrs.push_back(attr);
    return true;
  }

  bool parseComplex(const std::string &text, ComplexSelector *selector) {
    size_t index = 0;
    while (index < text.size() && xt_is_css_space(text[index])) index++;
    while (index < text.size()) {
      CompoundSelector compound;
      if (!parseCompound(text, &index, &compound)) return false;
      selector->compounds.push_back(std::move(compound));
      bool hadSpace = false;
      while (index < text.size() && xt_is_css_space(text[index])) {
        hadSpace = true;
        index++;
      }
      if (index >= text.size() || text[index] == ',') break;
      char c = text[index];
      if (c == '>' || c == '+' || c == '~') {
        index++;
        while (index < text.size() && xt_is_css_space(text[index])) index++;
        selector->combinators.push_back(c == '>' ? Combinator::Child
                                          : c == '+' ? Combinator::Adjacent
                                                     : Combinator::Sibling);
      } else if (hadSpace) {
        selector->combinators.push_back(Combinator::Descendant);
      } else {
        return false;
      }
    }
    if (selector->combinators.size() + 1 != selector->compounds.size()) {
      /* Ensure the invariant holds even if parsing stopped early. */
      selector->combinators.resize(selector->compounds.size() > 0 ? selector->compounds.size() - 1 : 0);
    }
    selector->specificity = computeSpecificity(*selector);
    return true;
  }

  static int computeCompoundSpecificity(const CompoundSelector &compound) {
    /* `:not(x)` is approximated as a class; pseudo-elements would count as a
     * type but are not modelled yet. Good enough for a UI stylesheet. */
    return (int)(compound.classes.size() + compound.attrs.size() + compound.pseudoClasses.size());
  }

  static int computeSpecificity(const ComplexSelector &selector) {
    int ids = 0;
    int classes = 0;
    int types = 0;
    for (const CompoundSelector &compound : selector.compounds) {
      if (!compound.id.empty()) ids++;
      classes += computeCompoundSpecificity(compound);
      if (!compound.tag.empty()) types++;
    }
    return (ids << 20) | (classes << 10) | types;
  }
};

}  // namespace

std::vector<ComplexSelector> xt_css_parse_selector_list(const std::string &text) {
  SelectorParser parser(text);
  return parser.parseList();
}

}  // namespace xtgui
