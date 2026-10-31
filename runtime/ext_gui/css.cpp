/*
 * xbintsc GUI engine — CSS string helpers and value parsing (numbers, lengths,
 * colors). Selector parsing lives in `css_selector.cpp`, at-rule/declaration
 * parsing in `css_sheet.cpp` and matching in `css_match.cpp`.
 */

#include "css_internal.h"

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

std::vector<std::string> xt_css_split_commas(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  char quote = 0;
  for (char c : text) {
    if (quote != 0) {
      current.push_back(c);
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      current.push_back(c);
    } else if (c == '(' || c == '[') {
      depth++;
      current.push_back(c);
    } else if (c == ')' || c == ']') {
      if (depth > 0) depth--;
      current.push_back(c);
    } else if (c == ',' && depth == 0) {
      std::string part = xt_css_trim(current);
      if (!part.empty()) parts.push_back(part);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  std::string part = xt_css_trim(current);
  if (!part.empty()) parts.push_back(part);
  return parts;
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


/** Split a CSS function's argument list at top-level commas. */
static std::vector<std::string> splitFunctionArgs(const std::string &text) {
  std::vector<std::string> args;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if (c == ',' && depth == 0) {
      args.push_back(xt_css_trim(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty() || !args.empty()) args.push_back(xt_css_trim(current));
  return args;
}

/** A scalar token (`12px`, `50%`, `1.5`) as a `Length`. Absolute lengths are
 * converted to pixels here; relative units stay symbolic so layout can resolve
 * them against the element that uses them. */
static Length makeScalarLength(double number, const std::string &rawUnit) {
  Length length;
  std::string unit = xt_css_lower(xt_css_trim(rawUnit));
  length.value = (float)number;
  if (unit.empty() || unit == "px") length.unit = Unit::Px;
  else if (unit == "%") length.unit = Unit::Percent;
  else if (unit == "em") length.unit = Unit::Em;
  else if (unit == "rem") length.unit = Unit::Rem;
  else if (unit == "vw") length.unit = Unit::Vw;
  else if (unit == "vh") length.unit = Unit::Vh;
  else if (unit == "pt") { length.value = (float)number * (96.0f / 72.0f); length.unit = Unit::Px; }
  else if (unit == "pc") { length.value = (float)number * 16.0f; length.unit = Unit::Px; }
  else if (unit == "in") { length.value = (float)number * 96.0f; length.unit = Unit::Px; }
  else if (unit == "cm") { length.value = (float)number * (96.0f / 2.54f); length.unit = Unit::Px; }
  else if (unit == "mm") { length.value = (float)number * (96.0f / 25.4f); length.unit = Unit::Px; }
  else if (unit == "q") { length.value = (float)number * (96.0f / 101.6f); length.unit = Unit::Px; }
  else length.unit = Unit::Invalid;
  return length;
}

static Length parseFunctionLength(const std::string &name, const std::string &inner);
static bool parseMathExpression(const std::string &text, Length *out);

/** Recursive-descent parser for `calc()` arithmetic (`+ - * /`, parentheses,
 * nested `calc`/`min`/`max`/`clamp` and unit-bearing numbers). The expression
 * tree stays unresolved: `xt_style_resolve_absolute` walks it once the
 * containing block and font size are known. */
struct MathParser {
  explicit MathParser(const std::string &text) : text_(text) {}

  bool parse(Length *out) {
    if (!parseSum(out)) return false;
    skip();
    return index_ == text_.size();
  }

 private:
  void skip() {
    while (index_ < text_.size() && xt_is_css_space(text_[index_])) index_++;
  }

  bool parseSum(Length *out) {
    Length left;
    if (!parseProduct(&left)) return false;
    for (;;) {
      skip();
      if (index_ >= text_.size() || (text_[index_] != '+' && text_[index_] != '-')) break;
      char op = text_[index_++];
      Length right;
      if (!parseProduct(&right)) return false;
      Length node;
      node.unit = Unit::Calc;
      node.op = op;
      node.args.push_back(left);
      node.args.push_back(right);
      left = node;
    }
    *out = left;
    return true;
  }

  bool parseProduct(Length *out) {
    Length left;
    if (!parseOperand(&left)) return false;
    for (;;) {
      skip();
      if (index_ >= text_.size() || (text_[index_] != '*' && text_[index_] != '/')) break;
      char op = text_[index_++];
      Length right;
      if (!parseOperand(&right)) return false;
      Length node;
      node.unit = Unit::Calc;
      node.op = op;
      node.args.push_back(left);
      node.args.push_back(right);
      left = node;
    }
    *out = left;
    return true;
  }

  bool parseOperand(Length *out) {
    skip();
    if (index_ >= text_.size()) return false;
    if (text_[index_] == '(') {
      index_++;
      if (!parseSum(out)) return false;
      skip();
      if (index_ >= text_.size() || text_[index_] != ')') return false;
      index_++;
      return true;
    }
    if (std::isalpha((unsigned char)text_[index_])) return parseFunction(out);
    return parseNumber(out);
  }

  bool parseFunction(Length *out) {
    size_t nameStart = index_;
    while (index_ < text_.size() &&
           (std::isalpha((unsigned char)text_[index_]) || text_[index_] == '-'))
      index_++;
    std::string name = xt_css_lower(text_.substr(nameStart, index_ - nameStart));
    skip();
    if (index_ >= text_.size() || text_[index_] != '(') return false;
    size_t open = index_;
    int depth = 0;
    size_t close = std::string::npos;
    for (; index_ < text_.size(); index_++) {
      if (text_[index_] == '(') depth++;
      else if (text_[index_] == ')') {
        depth--;
        if (depth == 0) {
          close = index_;
          break;
        }
      }
    }
    if (close == std::string::npos) return false;
    std::string inner = text_.substr(open + 1, close - open - 1);
    index_ = close + 1;
    if (name == "calc") return parseMathExpression(inner, out);
    if (name == "min" || name == "max" || name == "clamp" || name == "env") {
      *out = parseFunctionLength(name, inner);
      return out->unit != Unit::Invalid;
    }
    return false;
  }

  bool parseNumber(Length *out) {
    skip();
    const char *begin = text_.c_str() + index_;
    char *end = nullptr;
    double number = std::strtod(begin, &end);
    if (end == begin || end == nullptr) return false;
    index_ = (size_t)(end - text_.c_str());
    size_t unitStart = index_;
    while (index_ < text_.size() &&
           (std::isalpha((unsigned char)text_[index_]) || text_[index_] == '%'))
      index_++;
    std::string unit = text_.substr(unitStart, index_ - unitStart);
    *out = makeScalarLength(number, unit);
    if (unit.empty()) out->unit = Unit::Number;
    return out->unit != Unit::Invalid;
  }

  const std::string &text_;
  size_t index_ = 0;
};

static bool parseMathExpression(const std::string &text, Length *out) {
  MathParser parser(text);
  return parser.parse(out);
}

/** `clamp(min, value, max)`, `min(...)` and `max(...)` over lengths. The
 * arguments stay unresolved; layout resolves them against the element's em and
 * the viewport. `env(safe-area-inset-*)` has no notch information in a desktop
 * window, so it resolves to zero (its fallback is ignored, matching the
 * browser's value without insets). */
static Length parseFunctionLength(const std::string &name, const std::string &inner) {
  Length result;
  if (name == "env") {
    result.unit = Unit::Px;
    return result;
  }
  if (name != "clamp" && name != "min" && name != "max") {
    result.unit = Unit::Invalid;
    return result;
  }
  std::vector<std::string> args = splitFunctionArgs(inner);
  size_t expected = name == "clamp" ? 3 : 1;
  if (args.size() < expected) {
    result.unit = Unit::Invalid;
    return result;
  }
  result.unit = name == "clamp" ? Unit::Clamp : (name == "min" ? Unit::Min : Unit::Max);
  for (const std::string &arg : args) {
    Length value;
    if (!parseMathExpression(arg, &value)) {
      result.unit = Unit::Invalid;
      result.args.clear();
      return result;
    }
    result.args.push_back(value);
  }
  return result;
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
  size_t open = text.find('(');
  if (open != std::string::npos && !text.empty() && text.back() == ')') {
    std::string name = xt_css_trim(text.substr(0, open));
    std::string inner = text.substr(open + 1, text.size() - open - 2);
    if (name == "calc") {
      Length parsed;
      if (!parseMathExpression(inner, &parsed)) parsed.unit = Unit::Invalid;
      return parsed;
    }
    if (name == "clamp" || name == "min" || name == "max" || name == "env") {
      return parseFunctionLength(name, inner);
    }
  }
  /* A scalar keeps the historical `px` default for a unitless token; only
   * arithmetic treats `2` as a number. */
  char *end = nullptr;
  double number = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || end == nullptr) {
    length.unit = Unit::Invalid;
    return length;
  }
  std::string unit = end;
  while (!unit.empty() && xt_is_css_space(unit[0])) unit.erase(unit.begin());
  return makeScalarLength(number, unit);
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

}  // namespace xtgui
