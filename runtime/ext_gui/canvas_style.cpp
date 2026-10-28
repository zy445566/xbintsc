/*
 * xbintsc GUI engine — Canvas2D drawing-state parsing.
 *
 * The state object itself lives in `canvas.h`; this unit parses the strings the
 * DOM setters receive (CSS colours, the `font` shorthand, `lineCap`/`lineJoin`
 * keywords) and validates the dash list. Keeping the parsing out of
 * `canvas.cpp` leaves that unit free to own the path and draw entry points.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "canvas.h"
#include "css.h"

namespace xtgui {

namespace {

std::string lower_ascii(const std::string &value) {
  std::string out = value;
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return out;
}

bool parse_font_size_token(const std::string &token, float fallback, float *out) {
  std::string lower = lower_ascii(token);
  struct Suffix {
    const char *text;
    float factor;
  };
  static const Suffix suffixes[] = {{"px", 1.0f},  {"pt", 96.0f / 72.0f}, {"pc", 16.0f},
                                    {"in", 96.0f}, {"cm", 96.0f / 2.54f}, {"mm", 96.0f / 25.4f},
                                    {"q", 96.0f / 101.6f}, {"em", 0.0f}, {"rem", 0.0f}};
  for (const Suffix &suffix : suffixes) {
    size_t length = std::strlen(suffix.text);
    if (lower.size() <= length || lower.compare(lower.size() - length, length, suffix.text) != 0)
      continue;
    char *end = nullptr;
    float value = std::strtof(lower.c_str(), &end);
    if (end == lower.c_str()) return false;
    if (suffix.factor == 0.0f) {
      *out = value * fallback;
    } else {
      *out = value * suffix.factor;
    }
    return true;
  }
  return false;
}

/** Parse the CSS `font` shorthand into a `FontSpec` (the pieces Canvas2D
 * understands; unknown tokens are ignored). */
FontSpec parse_font_string(const std::string &text) {
  FontSpec spec;
  spec.family = "sans-serif";
  spec.pixel_size = 10.0f;
  spec.weight = 400;
  spec.italic = false;
  std::vector<std::string> tokens = xt_css_split_whitespace(text);
  std::vector<std::string> family;
  float size = 10.0f;
  bool have_size = false;
  for (const std::string &token : tokens) {
    std::string lower = lower_ascii(token);
    float parsed = 0.0f;
    if (!have_size && parse_font_size_token(lower, 16.0f, &parsed)) {
      size = parsed;
      have_size = true;
      continue;
    }
    if (lower == "italic" || lower == "oblique") {
      spec.italic = true;
      continue;
    }
    if (lower == "bold" || lower == "bolder") {
      spec.weight = 700;
      continue;
    }
    if (lower == "lighter") {
      spec.weight = 300;
      continue;
    }
    if (lower == "normal" || lower == "small-caps") continue;
    if (!lower.empty() && lower.back() == ',') lower.pop_back();
    if (!lower.empty()) family.push_back(lower);
  }
  spec.pixel_size = size;
  if (!family.empty()) spec.family = family[0];
  return spec;
}

void parse_color_into(const std::string &css, float color[4], std::string *stored) {
  Color parsed = xt_css_parse_color(css);
  if (!parsed.valid) return;
  color[0] = parsed.r;
  color[1] = parsed.g;
  color[2] = parsed.b;
  color[3] = parsed.a;
  *stored = css;
}

bool positive_finite(float value) { return std::isfinite(value) && value >= 0.0f; }

}  // namespace

void Canvas2D::set_fill_style(const std::string &css) {
  parse_color_into(css, state_.fill_color, &state_.fill_style);
  state_.fill_gradient = 0;
  state_.fill_pattern = 0;
}

void Canvas2D::set_stroke_style(const std::string &css) {
  parse_color_into(css, state_.stroke_color, &state_.stroke_style);
  state_.stroke_gradient = 0;
  state_.stroke_pattern = 0;
}

void Canvas2D::set_fill_gradient(uint64_t id) {
  if (id == 0) {
    state_.fill_gradient = 0;
    return;
  }
  if (find_gradient(id) != nullptr) { state_.fill_gradient = id; state_.fill_pattern = 0; }
}

void Canvas2D::set_stroke_gradient(uint64_t id) {
  if (id == 0) {
    state_.stroke_gradient = 0;
    return;
  }
  if (find_gradient(id) != nullptr) { state_.stroke_gradient = id; state_.stroke_pattern = 0; }
}

void Canvas2D::set_global_alpha(float alpha) {
  if (!std::isfinite(alpha)) return;
  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha > 1.0f) alpha = 1.0f;
  state_.global_alpha = alpha;
}

void Canvas2D::set_global_composite_operation(const std::string &value) {
  std::string lower = lower_ascii(value);
  struct Entry {
    const char *name;
    int value;
  };
  static const Entry entries[] = {
      {"source-over", kCanvasCompositeSourceOver},
      {"source-in", kCanvasCompositeSourceIn},
      {"source-out", kCanvasCompositeSourceOut},
      {"source-atop", kCanvasCompositeSourceAtop},
      {"destination-over", kCanvasCompositeDestinationOver},
      {"destination-in", kCanvasCompositeDestinationIn},
      {"destination-out", kCanvasCompositeDestinationOut},
      {"destination-atop", kCanvasCompositeDestinationAtop},
      {"lighter", kCanvasCompositeLighter},
      {"copy", kCanvasCompositeCopy},
      {"xor", kCanvasCompositeXor},
      {"multiply", kCanvasCompositeMultiply},
      {"screen", kCanvasCompositeScreen},
      {"overlay", kCanvasCompositeOverlay},
      {"darken", kCanvasCompositeDarken},
      {"lighten", kCanvasCompositeLighten},
      {"color-dodge", kCanvasCompositeColorDodge},
      {"color-burn", kCanvasCompositeColorBurn},
      {"hard-light", kCanvasCompositeHardLight},
      {"soft-light", kCanvasCompositeSoftLight},
      {"difference", kCanvasCompositeDifference},
      {"exclusion", kCanvasCompositeExclusion},
      {"hue", kCanvasCompositeHue},
      {"saturation", kCanvasCompositeSaturation},
      {"color", kCanvasCompositeColor},
      {"luminosity", kCanvasCompositeLuminosity},
  };
  for (const Entry &entry : entries) {
    if (lower == entry.name) {
      state_.global_composite = entry.value;
      state_.global_composite_string = entry.name;
      return;
    }
  }
}

void Canvas2D::set_shadow_color(const std::string &css) {
  parse_color_into(css, state_.shadow_color, &state_.shadow_color_string);
}

void Canvas2D::set_shadow_blur(float blur) {
  if (!positive_finite(blur)) return;
  state_.shadow_blur = blur;
}

void Canvas2D::set_image_smoothing_quality(const std::string &value) {
  std::string lower = lower_ascii(value);
  if (lower == "low" || lower == "medium" || lower == "high") {
    state_.image_smoothing_quality = lower;
  }
}

void Canvas2D::set_font(const std::string &font) {
  state_.font = parse_font_string(font);
  state_.font_string = font;
}

void Canvas2D::set_text_align(const std::string &value) {
  std::string lower = lower_ascii(value);
  if (lower == "start" || lower == "left") {
    state_.text_align = 0;
  } else if (lower == "center") {
    state_.text_align = 1;
  } else if (lower == "right" || lower == "end") {
    state_.text_align = 2;
  } else {
    return;
  }
  state_.text_align_string = lower;
}

void Canvas2D::set_text_baseline(const std::string &value) {
  std::string lower = lower_ascii(value);
  if (lower == "alphabetic") {
    state_.text_baseline = 0;
  } else if (lower == "top") {
    state_.text_baseline = 1;
  } else if (lower == "middle") {
    state_.text_baseline = 2;
  } else if (lower == "bottom") {
    state_.text_baseline = 3;
  } else if (lower == "hanging") {
    state_.text_baseline = 4;
  } else if (lower == "ideographic") {
    state_.text_baseline = 5;
  } else {
    return;
  }
  state_.text_baseline_string = lower;
}

void Canvas2D::set_line_cap(const std::string &value) {
  std::string lower = lower_ascii(value);
  if (lower == "butt") {
    state_.line_cap = 0;
  } else if (lower == "round") {
    state_.line_cap = 1;
  } else if (lower == "square") {
    state_.line_cap = 2;
  } else {
    return;
  }
  state_.line_cap_string = lower;
}

void Canvas2D::set_line_join(const std::string &value) {
  std::string lower = lower_ascii(value);
  if (lower == "miter") {
    state_.line_join = 0;
  } else if (lower == "round") {
    state_.line_join = 1;
  } else if (lower == "bevel") {
    state_.line_join = 2;
  } else {
    return;
  }
  state_.line_join_string = lower;
}

void Canvas2D::set_miter_limit(float limit) {
  if (!std::isfinite(limit) || limit <= 0.0f) return;
  state_.miter_limit = limit;
}

void Canvas2D::set_line_dash(const std::vector<float> &segments) {
  if (segments.empty()) {
    state_.line_dash.clear();
    return;
  }
  for (float segment : segments) {
    /* The spec ignores a list containing a negative or non-finite value. */
    if (!positive_finite(segment)) return;
  }
  std::vector<float> pattern = segments;
  if (pattern.size() % 2 == 1) {
    const size_t count = pattern.size();
    for (size_t i = 0; i < count; i++) pattern.push_back(pattern[i]);
  }
  float total = 0.0f;
  for (float segment : pattern) total += segment;
  if (total <= 0.0f) {
    state_.line_dash.clear();
    return;
  }
  state_.line_dash = std::move(pattern);
}

}  // namespace xtgui
