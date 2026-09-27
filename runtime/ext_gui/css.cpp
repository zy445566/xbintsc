/*
 * xbintsc GUI engine — CSS parser, selector matcher and value parser.
 */

#include "css.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace xtgui {

/* -- string helpers ------------------------------------------------------- */

bool xt_is_css_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string xt_css_trim(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && xt_is_css_space(text[begin])) begin++;
  while (end > begin && xt_is_css_space(text[end - 1])) end--;
  return text.substr(begin, end - begin);
}

std::string xt_css_lower(const std::string &text) {
  std::string out = text;
  for (char &c : out) c = (char)std::tolower((unsigned char)c);
  return out;
}

std::vector<std::string> xt_css_split_whitespace(const std::string &text) {
  std::vector<std::string> parts;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && xt_is_css_space(text[i])) i++;
    size_t start = i;
    while (i < text.size() && !xt_is_css_space(text[i])) i++;
    if (i > start) parts.push_back(text.substr(start, i - start));
  }
  return parts;
}

static bool isIdentStart(char c) {
  return std::isalpha((unsigned char)c) || c == '_' || c == '-' || (unsigned char)c >= 0x80;
}

static bool isIdentChar(char c) {
  return std::isalnum((unsigned char)c) || c == '_' || c == '-' || (unsigned char)c >= 0x80;
}

static std::string stripComments(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      size_t end = text.find("*/", i + 2);
      i = (end == std::string::npos) ? text.size() : end + 1;
      continue;
    }
    out.push_back(text[i]);
  }
  return out;
}

/* -- numbers / lengths / colors ------------------------------------------- */

bool xt_css_parse_number(const std::string &value, float *out) {
  std::string text = xt_css_trim(value);
  if (text.empty()) return false;
  char *end = nullptr;
  float number = strtof(text.c_str(), &end);
  if (end == text.c_str()) return false;
  while (end != nullptr && *end != '\0' && xt_is_css_space(*end)) end++;
  if (end != nullptr && *end != '\0') return false;
  *out = number;
  return true;
}

Length xt_css_parse_length(const std::string &value) {
  Length length;
  std::string text = xt_css_lower(xt_css_trim(value));
  if (text.empty()) {
    length.unit = Unit::Invalid;
    return length;
  }
  if (text == "auto") {
    length.unit = Unit::Auto;
    return length;
  }
  if (text == "none") {
    length.unit = Unit::None;
    return length;
  }
  char *end = nullptr;
  float number = strtof(text.c_str(), &end);
  if (end == text.c_str()) {
    length.unit = Unit::Invalid;
    return length;
  }
  std::string unit = end;
  while (!unit.empty() && xt_is_css_space(unit[0])) unit.erase(unit.begin());
  length.value = number;
  if (unit.empty() || unit == "px") length.unit = Unit::Px;
  else if (unit == "%") length.unit = Unit::Percent;
  else if (unit == "em") length.unit = Unit::Em;
  else if (unit == "rem") length.unit = Unit::Rem;
  else if (unit == "vw") length.unit = Unit::Vw;
  else if (unit == "vh") length.unit = Unit::Vh;
  else if (unit == "pt") { length.value = number * (96.0f / 72.0f); length.unit = Unit::Px; }
  else if (unit == "pc") { length.value = number * 16.0f; length.unit = Unit::Px; }
  else if (unit == "in") { length.value = number * 96.0f; length.unit = Unit::Px; }
  else if (unit == "cm") { length.value = number * (96.0f / 2.54f); length.unit = Unit::Px; }
  else if (unit == "mm") { length.value = number * (96.0f / 25.4f); length.unit = Unit::Px; }
  else if (unit == "q") { length.value = number * (96.0f / 101.6f); length.unit = Unit::Px; }
  else length.unit = Unit::Invalid;
  return length;
}

namespace {

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHexColor(const std::string &text, Color *color) {
  std::string hex = text.substr(1);
  if (hex.size() != 3 && hex.size() != 4 && hex.size() != 6 && hex.size() != 8) return false;
  for (char c : hex) {
    if (hexDigit(c) < 0) return false;
  }
  int values[8] = {0};
  if (hex.size() == 3 || hex.size() == 4) {
    for (size_t i = 0; i < hex.size(); i++) {
      int v = hexDigit(hex[i]);
      values[i] = v * 17;
    }
    color->r = values[0] / 255.0f;
    color->g = values[1] / 255.0f;
    color->b = values[2] / 255.0f;
    color->a = (hex.size() == 4) ? values[3] / 255.0f : 1.0f;
  } else {
    for (size_t i = 0; i < hex.size() / 2; i++) {
      values[i] = hexDigit(hex[i * 2]) * 16 + hexDigit(hex[i * 2 + 1]);
    }
    color->r = values[0] / 255.0f;
    color->g = values[1] / 255.0f;
    color->b = values[2] / 255.0f;
    color->a = (hex.size() == 8) ? values[3] / 255.0f : 1.0f;
  }
  color->valid = true;
  return true;
}

bool parseFunctionColor(const std::string &text, Color *color) {
  size_t open = text.find('(');
  size_t close = text.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open) return false;
  std::string name = xt_css_lower(xt_css_trim(text.substr(0, open)));
  std::string args = text.substr(open + 1, close - open - 1);
  std::vector<std::string> parts;
  std::string current;
  for (char c : args) {
    if (c == ',' || c == '/' || xt_is_css_space(c)) {
      if (!current.empty()) parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(current);
  if (parts.size() < 3) return false;
  float values[4] = {0, 0, 0, 1};
  for (size_t i = 0; i < parts.size() && i < 4; i++) {
    const std::string &part = parts[i];
    if (i < 3) {
      if (name == "hsl" || name == "hsla") return false;  // hsl not supported yet
      if (!part.empty() && part.back() == '%') {
        values[i] = strtof(part.c_str(), nullptr) / 100.0f;
      } else {
        values[i] = strtof(part.c_str(), nullptr) / 255.0f;
      }
    } else {
      if (!part.empty() && part.back() == '%') values[i] = strtof(part.c_str(), nullptr) / 100.0f;
      else values[i] = strtof(part.c_str(), nullptr);
    }
  }
  (void)name;
  color->r = values[0] < 0 ? 0 : (values[0] > 1 ? 1 : values[0]);
  color->g = values[1] < 0 ? 0 : (values[1] > 1 ? 1 : values[1]);
  color->b = values[2] < 0 ? 0 : (values[2] > 1 ? 1 : values[2]);
  color->a = values[3] < 0 ? 0 : (values[3] > 1 ? 1 : values[3]);
  color->valid = true;
  return true;
}

const std::unordered_map<std::string, unsigned int> &namedColors() {
  static const std::unordered_map<std::string, unsigned int> colors = {
      {"transparent", 0x00000000u}, {"black", 0x000000ffu}, {"white", 0xffffffffu},
      {"red", 0xff0000ffu},         {"green", 0x008000ffu}, {"lime", 0x00ff00ffu},
      {"blue", 0x0000ffffu},        {"yellow", 0xffff00ffu}, {"cyan", 0x00ffffffu},
      {"aqua", 0x00ffffffu},        {"magenta", 0xff00ffffu}, {"fuchsia", 0xff00ffffu},
      {"gray", 0x808080ffu},        {"grey", 0x808080ffu},  {"silver", 0xc0c0c0ffu},
      {"maroon", 0x800000ffu},      {"olive", 0x808000ffu}, {"navy", 0x000080ffu},
      {"teal", 0x008080ffu},        {"purple", 0x800080ffu}, {"orange", 0xffa500ffu},
      {"pink", 0xffc0cbffu},        {"brown", 0xa52a2affu}, {"gold", 0xffd700ffu},
      {"indigo", 0x4b0082ffu},      {"violet", 0xee82eeffu}, {"crimson", 0xdc143cffu},
      {"salmon", 0xfa8072ffu},      {"tomato", 0xff6347ffu}, {"khaki", 0xf0e68cffu},
      {"beige", 0xf5f5dcffu},       {"ivory", 0xfffff0ffu}, {"linen", 0xfaf0e6ffu},
      {"coral", 0xff7f50ffu},       {"turquoise", 0x40e0d0ffu}, {"plum", 0xdda0ddffu},
      {"orchid", 0xda70d6ffu},      {"skyblue", 0x87ceebffu}, {"steelblue", 0x4682b4ffu},
      {"darkgray", 0xa9a9a9ffu},    {"darkgrey", 0xa9a9a9ffu}, {"lightgray", 0xd3d3d3ffu},
      {"lightgrey", 0xd3d3d3ffu},   {"slategray", 0x708090ffu}, {"slategrey", 0x708090ffu},
  };
  return colors;
}

}  // namespace

Color xt_css_parse_color(const std::string &value) {
  Color color;
  std::string text = xt_css_lower(xt_css_trim(value));
  if (text.empty()) return color;
  if (text[0] == '#') {
    parseHexColor(text, &color);
    return color;
  }
  if (text.rfind("rgb", 0) == 0 || text.rfind("rgba", 0) == 0 || text.rfind("hsl", 0) == 0 ||
      text.rfind("hsla", 0) == 0) {
    parseFunctionColor(text, &color);
    return color;
  }
  auto it = namedColors().find(text);
  if (it != namedColors().end()) {
    unsigned int rgba = it->second;
    color.r = ((rgba >> 24) & 0xff) / 255.0f;
    color.g = ((rgba >> 16) & 0xff) / 255.0f;
    color.b = ((rgba >> 8) & 0xff) / 255.0f;
    color.a = (rgba & 0xff) / 255.0f;
    color.valid = true;
  }
  return color;
}

/* -- selector parsing ----------------------------------------------------- */

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
    while (*index < text.size() && isIdentChar(text[*index])) (*index)++;
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
      } else if (isIdentStart(c)) {
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

/* -- declaration / stylesheet parsing ------------------------------------- */

namespace {

/** Find the first `needle` at nesting depth zero (parens/brackets/quotes). */
size_t findTopLevel(const std::string &text, char needle, size_t from) {
  int depth = 0;
  char quote = 0;
  for (size_t i = from; i < text.size(); i++) {
    char c = text[i];
    if (quote != 0) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') quote = c;
    else if (c == '(' || c == '[') depth++;
    else if (c == ')' || c == ']') depth--;
    else if (c == needle && depth == 0) return i;
  }
  return std::string::npos;
}

std::string stripImportant(std::string value, bool *important) {
  size_t pos = value.rfind('!');
  if (pos == std::string::npos) return value;
  std::string flag = xt_css_lower(xt_css_trim(value.substr(pos + 1)));
  if (flag == "important") {
    *important = true;
    return xt_css_trim(value.substr(0, pos));
  }
  return value;
}

}  // namespace

std::vector<Declaration> xt_css_parse_declarations(const std::string &text) {
  std::vector<Declaration> declarations;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = findTopLevel(text, ';', start);
    if (end == std::string::npos) end = text.size();
    std::string part = text.substr(start, end - start);
    start = end + 1;
    size_t colon = findTopLevel(part, ':', 0);
    if (colon == std::string::npos) continue;
    Declaration declaration;
    declaration.property = xt_css_lower(xt_css_trim(part.substr(0, colon)));
    declaration.value = stripImportant(xt_css_trim(part.substr(colon + 1)), &declaration.important);
    if (!declaration.property.empty() && !declaration.value.empty()) {
      declarations.push_back(std::move(declaration));
    }
  }
  return declarations;
}

StyleSheet xt_css_parse_stylesheet(const std::string &text) {
  StyleSheet sheet;
  std::string clean = stripComments(text);
  size_t index = 0;
  int order = 0;
  while (index < clean.size()) {
    while (index < clean.size() && xt_is_css_space(clean[index])) index++;
    if (index >= clean.size()) break;
    if (clean[index] == '@') {
      size_t brace = clean.find('{', index);
      size_t semi = clean.find(';', index);
      if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
        int depth = 0;
        size_t i = brace;
        for (; i < clean.size(); i++) {
          if (clean[i] == '{') depth++;
          else if (clean[i] == '}') {
            depth--;
            if (depth == 0) break;
          }
        }
        index = (i < clean.size()) ? i + 1 : clean.size();
      } else {
        index = (semi == std::string::npos) ? clean.size() : semi + 1;
      }
      continue;
    }
    size_t brace = clean.find('{', index);
    if (brace == std::string::npos) break;
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = brace; i < clean.size(); i++) {
      if (clean[i] == '{') depth++;
      else if (clean[i] == '}') {
        depth--;
        if (depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) break;
    Rule rule;
    rule.selectors = xt_css_parse_selector_list(clean.substr(index, brace - index));
    rule.declarations = xt_css_parse_declarations(clean.substr(brace + 1, close - brace - 1));
    rule.order = order++;
    if (!rule.selectors.empty() && !rule.declarations.empty()) sheet.rules.push_back(std::move(rule));
    index = close + 1;
  }
  return sheet;
}

/* -- matching ------------------------------------------------------------- */

namespace {

bool hasClass(const Node *node, const std::string &name) {
  const std::string *value = node->attr("class");
  if (value == nullptr) return false;
  for (const std::string &token : xt_css_split_whitespace(*value)) {
    if (token == name) return true;
  }
  return false;
}

bool matchAttribute(const Node *node, const AttrSelector &attr) {
  const std::string *value = node->attr(attr.name);
  if (value == nullptr) return false;
  if (attr.op.empty()) return true;
  if (attr.op == "=") return *value == attr.value;
  if (attr.op == "~=") {
    for (const std::string &token : xt_css_split_whitespace(*value)) {
      if (token == attr.value) return true;
    }
    return false;
  }
  if (attr.op == "|=") {
    return *value == attr.value || value->rfind(attr.value + "-", 0) == 0;
  }
  if (attr.op == "^=") return !attr.value.empty() && value->rfind(attr.value, 0) == 0;
  if (attr.op == "$=") {
    return !attr.value.empty() && value->size() >= attr.value.size() &&
           value->compare(value->size() - attr.value.size(), attr.value.size(), attr.value) == 0;
  }
  if (attr.op == "*=") {
    return !attr.value.empty() && value->find(attr.value) != std::string::npos;
  }
  return false;
}

/** Parse the `an+b` micro-syntax of `:nth-child`. */
bool parseNth(const std::string &expression, int *a, int *b) {
  std::string text;
  for (char c : xt_css_lower(xt_css_trim(expression))) {
    if (!xt_is_css_space(c)) text.push_back(c);
  }
  if (text == "odd") { *a = 2; *b = 1; return true; }
  if (text == "even") { *a = 2; *b = 0; return true; }
  size_t nPos = text.find('n');
  if (nPos == std::string::npos) {
    *a = 0;
    *b = atoi(text.c_str());
    return !text.empty();
  }
  std::string aPart = text.substr(0, nPos);
  std::string bPart = text.substr(nPos + 1);
  if (aPart.empty() || aPart == "+") *a = 1;
  else if (aPart == "-") *a = -1;
  else *a = atoi(aPart.c_str());
  if (bPart.empty()) *b = 0;
  else *b = atoi(bPart.c_str());
  return true;
}

bool matchesNth(int index, const std::string &expression) {
  int a = 0;
  int b = 0;
  if (!parseNth(expression, &a, &b)) return false;
  if (a == 0) return index == b;
  int diff = index - b;
  if (a > 0) return diff >= 0 && diff % a == 0;
  return diff <= 0 && (-diff) % (-a) == 0;
}

bool matchPseudo(const Node *node, const std::string &pseudo) {
  if (pseudo == "first-child") return node->elementIndex() == 0 && node->elementSiblingCount() > 0;
  if (pseudo == "last-child") return node->elementIndex() == node->elementSiblingCount() - 1;
  if (pseudo == "only-child") return node->elementSiblingCount() == 1;
  if (pseudo == "root") return node->parent != nullptr && node->parent->isDocument();
  if (pseudo == "empty") {
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child->isElement()) return false;
      if (child->isText() && !xt_css_trim(child->text).empty()) return false;
    }
    return true;
  }
  if (pseudo == "disabled") return node->attr("disabled") != nullptr;
  if (pseudo == "checked") return node->attr("checked") != nullptr;
  if (pseudo.rfind("nth-child(", 0) == 0) {
    return matchesNth(node->elementIndex() + 1, pseudo.substr(10, pseudo.size() - 11));
  }
  if (pseudo.rfind("not(", 0) == 0) {
    std::string inner = xt_css_trim(pseudo.substr(4, pseudo.size() - 5));
    std::vector<ComplexSelector> parsed = xt_css_parse_selector_list(inner);
    if (parsed.empty() || parsed[0].compounds.size() != 1) return false;
    const CompoundSelector &compound = parsed[0].compounds[0];
    for (const AttrSelector &attr : compound.attrs) {
      if (!matchAttribute(node, attr)) return true;
    }
    if (!compound.id.empty()) {
      const std::string *id = node->attr("id");
      if (id == nullptr || *id != compound.id) return true;
    }
    if (!compound.tag.empty() && node->tag != compound.tag) return true;
    for (const std::string &name : compound.classes) {
      if (!hasClass(node, name)) return true;
    }
    for (const std::string &nested : compound.pseudoClasses) {
      if (!matchPseudo(node, nested)) return true;
    }
    return false;
  }
  /* Dynamic pseudo-classes (`:hover`, `:focus`, ...) never match a static
   * cascade; stateful matching arrives with input handling (M5). */
  return false;
}

bool matchCompound(const Node *node, const CompoundSelector &compound) {
  if (!node->isElement()) return false;
  if (!compound.tag.empty() && node->tag != compound.tag) return false;
  if (!compound.id.empty()) {
    const std::string *id = node->attr("id");
    if (id == nullptr || *id != compound.id) return false;
  }
  for (const std::string &name : compound.classes) {
    if (!hasClass(node, name)) return false;
  }
  for (const AttrSelector &attr : compound.attrs) {
    if (!matchAttribute(node, attr)) return false;
  }
  for (const std::string &pseudo : compound.pseudoClasses) {
    if (!matchPseudo(node, pseudo)) return false;
  }
  return true;
}

bool matchAt(const Node *node, const ComplexSelector &selector, size_t index) {
  if (!matchCompound(node, selector.compounds[index])) return false;
  if (index == 0) return true;
  Combinator combinator = selector.combinators[index - 1];
  if (combinator == Combinator::Child) {
    return node->parent != nullptr && node->parent->isElement() && matchAt(node->parent, selector, index - 1);
  }
  if (combinator == Combinator::Descendant) {
    for (const Node *parent = node->parent; parent != nullptr; parent = parent->parent) {
      if (parent->isElement() && matchAt(parent, selector, index - 1)) return true;
    }
    return false;
  }
  if (node->parent == nullptr) return false;
  if (combinator == Combinator::Adjacent) {
    const Node *previous = nullptr;
    for (const std::unique_ptr<Node> &sibling : node->parent->children) {
      if (sibling.get() == node) break;
      if (sibling->isElement()) previous = sibling.get();
    }
    return previous != nullptr && matchAt(previous, selector, index - 1);
  }
  /* General sibling combinator (`~`): any previous element sibling. */
  for (const std::unique_ptr<Node> &sibling : node->parent->children) {
    if (sibling.get() == node) break;
    if (sibling->isElement() && matchAt(sibling.get(), selector, index - 1)) return true;
  }
  return false;
}

}  // namespace

bool xt_css_match(const Node *node, const ComplexSelector &selector) {
  if (selector.compounds.empty()) return false;
  return matchAt(node, selector, selector.compounds.size() - 1);
}

}  // namespace xtgui
