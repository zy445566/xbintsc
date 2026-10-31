/*
 * xbintsc GUI engine — cascade value helpers.
 *
 * CSS-wide `inherit` for the shorthands that appear in real stylesheets, and
 * `currentColor` substitution. They live apart from `style.cpp` so the cascade
 * core stays under the repository's per-file line budget.
 */

#include "style_internal.h"

#include <cctype>
#include <cstdio>
#include <string>

namespace xtgui {

void xt_style_apply_inherit(XtStyle *style, const XtStyle *parent, const std::string &property) {
  if (parent == nullptr) return;
  if (property == "color") {
    style->color = parent->color;
  } else if (property == "clip-path") {
    style->clip_path = parent->clip_path;
  } else if (property == "font") {
    style->font_size = parent->font_size;
    style->font_weight = parent->font_weight;
    style->font_family = parent->font_family;
    style->font_style = parent->font_style;
    style->line_height = parent->line_height;
    style->letter_spacing = parent->letter_spacing;
    style->font_variant_numeric = parent->font_variant_numeric;
  }
}

std::string xt_style_replace_current_color(const std::string &value, const Color &color) {
  std::string out;
  char quote = 0;
  auto matches = [&value](size_t at) {
    if (at + 12 > value.size()) return false;
    for (size_t i = 0; i < 12; i++) {
      char c = value[at + i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != "currentcolor"[i]) return false;
    }
    if (at > 0) {
      char before = value[at - 1];
      if (std::isalnum((unsigned char)before) || before == '-' || before == '_') return false;
    }
    if (at + 12 < value.size()) {
      char after = value[at + 12];
      if (std::isalnum((unsigned char)after) || after == '-' || after == '_') return false;
    }
    return true;
  };
  for (size_t i = 0; i < value.size();) {
    char c = value[i];
    if (quote != 0) {
      out.push_back(c);
      if (c == quote) quote = 0;
      i++;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      out.push_back(c);
      i++;
      continue;
    }
    if (matches(i)) {
      char buffer[96];
      std::snprintf(buffer, sizeof(buffer), "rgba(%ld, %ld, %ld, %.3f)",
                    (long)(color.r * 255.0f + 0.5f), (long)(color.g * 255.0f + 0.5f),
                    (long)(color.b * 255.0f + 0.5f), (double)color.a);
      out += buffer;
      i += 12;
      continue;
    }
    out.push_back(c);
    i++;
  }
  return out;
}

}  // namespace xtgui
