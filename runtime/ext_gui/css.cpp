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

}  // namespace xtgui
