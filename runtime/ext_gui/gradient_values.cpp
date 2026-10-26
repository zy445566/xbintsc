/*
 * xbintsc GUI engine — CSS background layer values.
 *
 * `background-image` is a comma-separated layer list; each layer may be a
 * linear or radial gradient. They are modelled (not rasterised here) and painted
 * back-to-front by `paint_background.cpp`. `background` also accepts a final
 * colour layer.
 */
#include <cmath>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "style_internal.h"
#include "style_values_internal.h"

namespace xtgui {

namespace {

/** Split at top-level commas (function arguments stay intact). */
std::vector<std::string> splitCommas(const std::string &text) {
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
    } else if (c == '(') {
      depth++;
      current.push_back(c);
    } else if (c == ')') {
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

/** Split on whitespace outside function parentheses. */
std::vector<std::string> splitTokens(const std::string &text) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (char c : text) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && depth == 0) {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

/** Locate `<name>(...)` in `text`, returning the body and the call's bounds. */
bool findCall(const std::string &text, const std::string &name, std::string *body) {
  std::string lower = xt_css_lower(text);
  size_t start = lower.find(name + "(");
  if (start == std::string::npos) return false;
  size_t open = start + name.size();
  int depth = 0;
  for (size_t i = open; i < text.size(); i++) {
    if (text[i] == '(') depth++;
    else if (text[i] == ')') {
      depth--;
      if (depth == 0) {
        *body = text.substr(open + 1, i - open - 1);
        return true;
      }
    }
  }
  return false;
}

/** Parse a colour at the start of a gradient stop, leaving the offsets in
 * `rest`. Handles `#rgb`, `rgb()/rgba()`, `hsl()` and named colours. */
bool parseGradientColor(const std::string &token, Color *color, std::string *rest) {
  std::string text = xt_css_trim(token);
  if (text.empty()) return false;
  size_t end = 0;
  if (text[0] == '#') {
    end = 1;
    while (end < text.size() && std::isxdigit((unsigned char)text[end])) end++;
  } else if (text.compare(0, 4, "rgb(") == 0 || text.compare(0, 5, "rgba(") == 0 ||
             text.compare(0, 4, "hsl(") == 0 || text.compare(0, 5, "hsla(") == 0) {
    int depth = 0;
    for (; end < text.size(); end++) {
      if (text[end] == '(') depth++;
      else if (text[end] == ')') {
        depth--;
        if (depth == 0) {
          end++;
          break;
        }
      }
    }
  } else {
    while (end < text.size() && text[end] != ' ' && text[end] != '\t' && text[end] != '\n' &&
           text[end] != '\r')
      end++;
  }
  if (end == 0) return false;
  std::string colorText = text.substr(0, end);
  *color = xt_css_parse_color(colorText);
  if (!color->valid && xt_css_lower(colorText) == "transparent") {
    color->valid = true;
    color->r = color->g = color->b = 0.0f;
    color->a = 0.0f;
  }
  if (!color->valid) return false;
  *rest = xt_css_trim(text.substr(end));
  return true;
}

/** Parse one `[<percent>]` / `<number>` offset. */
bool parseGradientOffset(const std::string &raw, float *out) {
  std::string text = xt_css_trim(raw);
  if (text.empty()) return false;
  bool percent = text.back() == '%';
  if (percent) text.pop_back();
  float value = 0;
  if (!xt_css_parse_number(text, &value)) return false;
  if (percent) value /= 100.0f;
  else if (value > 1.0f) value /= 100.0f;
  if (value < 0) value = 0;
  if (value > 1) value = 1;
  *out = value;
  return true;
}

/** Parse a comma-separated `[color [offset [offset]]]` stop list. */
bool parseStops(const std::string &text, std::vector<BackgroundGradientStop> *stops) {
  std::vector<std::string> parts = splitCommas(text);
  if (parts.size() < 2) return false;
  float last = -1.0f;
  for (const std::string &part : parts) {
    Color color;
    std::string rest;
    if (!parseGradientColor(part, &color, &rest)) continue;
    BackgroundGradientStop stop;
    stop.color = color;
    stop.offset = -1.0f;
    if (!rest.empty()) {
      std::vector<std::string> offsets = splitTokens(rest);
      if (!offsets.empty()) {
        float first = 0;
        if (parseGradientOffset(offsets[0], &first)) {
          stop.offset = first;
          if (stop.offset < last) stop.offset = last;
          last = stop.offset;
        }
      }
    }
    stops->push_back(stop);
  }
  if (stops->size() < 2) return false;
  /* Fill implicit offsets by distributing them between their neighbours. */
  size_t i = 0;
  while (i < stops->size()) {
    if ((*stops)[i].offset >= 0) {
      i++;
      continue;
    }
    size_t j = i;
    while (j < stops->size() && (*stops)[j].offset < 0) j++;
    float before = (i == 0) ? 0.0f : (*stops)[i - 1].offset;
    float after = (j >= stops->size()) ? 1.0f : (*stops)[j].offset;
    size_t count = j - i;
    for (size_t k = 0; k < count; k++)
      (*stops)[i + k].offset = before + (after - before) * (float)(k + 1) / (float)(count + 1);
    i = j;
  }
  stops->front().offset = 0.0f;
  stops->back().offset = 1.0f;
  return true;
}

/** CSS gradient direction/angle. Returns true when `text` was a direction. */
bool parseDirection(const std::string &text, float *angle) {
  std::string value = xt_css_lower(xt_css_trim(text));
  if (value.rfind("to ", 0) == 0) {
    std::string side = xt_css_trim(value.substr(3));
    if (side == "top") *angle = 0;
    else if (side == "right") *angle = 90;
    else if (side == "bottom") *angle = 180;
    else if (side == "left") *angle = 270;
    else if (side == "top right" || side == "right top") *angle = 45;
    else if (side == "bottom right" || side == "right bottom") *angle = 135;
    else if (side == "bottom left" || side == "left bottom") *angle = 225;
    else if (side == "top left" || side == "left top") *angle = 315;
    else return false;
    return true;
  }
  const char *units[] = {"deg", "grad", "rad", "turn"};
  for (const char *unit : units) {
    size_t length = std::string(unit).size();
    if (value.size() <= length || value.compare(value.size() - length, length, unit) != 0) continue;
    float number = 0;
    if (!xt_css_parse_number(value.substr(0, value.size() - length), &number)) return false;
    if (std::string(unit) == "deg") *angle = number;
    else if (std::string(unit) == "grad") *angle = number * 0.9f;
    else if (std::string(unit) == "rad") *angle = (float)(number * 180.0 / 3.141592653589793);
    else *angle = number * 360.0f;
    return true;
  }
  return false;
}

/** `linear-gradient([<direction>,] <stops>)`. */
bool parseLinearGradient(const std::string &body, BackgroundLayer *layer) {
  std::vector<std::string> parts = splitCommas(body);
  if (parts.size() < 2) return false;
  std::string stopsText;
  float angle = 180.0f;
  if (parseDirection(parts.front(), &angle)) {
    stopsText = body.substr(body.find(',') + 1);
  } else {
    stopsText = body;
  }
  if (!parseStops(stopsText, &layer->stops)) return false;
  layer->kind = BackgroundKind::Linear;
  layer->angle = angle;
  return true;
}

/** Resolve one position token (keyword or length). */
bool parsePositionToken(const std::string &raw, Length *out, bool *vertical) {
  std::string text = xt_css_lower(xt_css_trim(raw));
  if (text == "left") { *out = {0.0f, Unit::Percent}; *vertical = false; return true; }
  if (text == "right") { *out = {100.0f, Unit::Percent}; *vertical = false; return true; }
  if (text == "top") { *out = {0.0f, Unit::Percent}; *vertical = true; return true; }
  if (text == "bottom") { *out = {100.0f, Unit::Percent}; *vertical = true; return true; }
  if (text == "center") { *out = {50.0f, Unit::Percent}; *vertical = false; return true; }
  Length length = xt_css_parse_length(text);
  if (length.unit == Unit::Invalid || length.unit == Unit::Auto || length.unit == Unit::None)
    return false;
  *out = length;
  *vertical = false;
  return true;
}

/** True for a radial-gradient prefix (`circle 80px at 20% 30%`, `10px 20px`). */
bool looksLikeRadialConfig(const std::string &part) {
  std::string lower = xt_css_lower(part);
  if (lower.rfind("circle", 0) == 0 || lower.rfind("ellipse", 0) == 0 ||
      lower.rfind("closest-", 0) == 0 || lower.rfind("farthest-", 0) == 0 ||
      lower.rfind("at ", 0) == 0 || lower.find(" at ") != std::string::npos)
    return true;
  std::vector<std::string> tokens = splitTokens(part);
  bool any = false;
  for (const std::string &raw : tokens) {
    std::string token = xt_css_lower(raw);
    if (token == "circle" || token == "ellipse" || token == "at" ||
        token == "closest-side" || token == "closest-corner" ||
        token == "farthest-side" || token == "farthest-corner" ||
        token == "left" || token == "right" || token == "top" || token == "bottom" ||
        token == "center") {
      any = true;
      continue;
    }
    Length length = xt_css_parse_length(token);
    if (length.unit == Unit::Invalid || length.unit == Unit::Auto || length.unit == Unit::None)
      return false;
    any = true;
  }
  return any;
}

/** `radial-gradient([<shape> || <size>] [at <position>]?, <stops>)`. */
bool parseRadialGradient(const std::string &body, BackgroundLayer *layer) {
  std::vector<std::string> top = splitCommas(body);
  size_t firstStop = 0;
  std::vector<std::string> config;
  /* The configuration is everything before the first component that starts with
   * a colour. That is the first segment unless it is a shape/size/at clause. */
  for (size_t i = 0; i < top.size(); i++) {
    if (looksLikeRadialConfig(top[i])) {
      config.push_back(top[i]);
      firstStop = i + 1;
      continue;
    }
    firstStop = i;
    break;
  }
  if (firstStop >= top.size()) return false;
  std::string configText;
  for (const std::string &part : config) configText += part + " ";
  std::vector<std::string> tokens = splitTokens(configText);
  Length cx{50.0f, Unit::Percent};
  Length cy{50.0f, Unit::Percent};
  Length rx{0.0f, Unit::Auto};
  Length ry{0.0f, Unit::Auto};
  bool explicitRadius = false;
  bool circle = false;
  bool hasShape = false;
  RadialSize size = RadialSize::FarthestCorner;
  for (size_t i = 0; i < tokens.size(); i++) {
    std::string token = xt_css_lower(tokens[i]);
    if (token == "circle") {
      circle = true;
      hasShape = true;
    } else if (token == "ellipse") {
      circle = false;
      hasShape = true;
    } else if (token == "closest-side") {
      size = RadialSize::ClosestSide;
    } else if (token == "farthest-side") {
      size = RadialSize::FarthestSide;
    } else if (token == "closest-corner") {
      size = RadialSize::ClosestCorner;
    } else if (token == "farthest-corner") {
      size = RadialSize::FarthestCorner;
    } else if (token == "at") {
      Length values[2];
      bool vertical[2] = {false, false};
      size_t used = 0;
      while (i + 1 < tokens.size() && used < 2) {
        i++;
        if (!parsePositionToken(tokens[i], &values[used], &vertical[used])) {
          i--;
          break;
        }
        used++;
      }
      if (used == 1) {
        if (vertical[0]) {
          cy = values[0];
          cx = {50.0f, Unit::Percent};
        } else {
          cx = values[0];
          cy = {50.0f, Unit::Percent};
        }
      } else if (used == 2) {
        if (vertical[0] && !vertical[1]) {
          cx = values[1];
          cy = values[0];
        } else {
          cx = values[0];
          cy = values[1];
        }
      }
    } else {
      Length length = xt_css_parse_length(token);
      if (length.unit == Unit::Invalid || length.unit == Unit::Auto ||
          length.unit == Unit::None)
        continue;
      if (!explicitRadius) {
        rx = length;
        explicitRadius = true;
      } else if (ry.unit == Unit::Auto) {
        ry = length;
      }
      size = RadialSize::Explicit;
    }
  }
  std::string stopsText;
  for (size_t i = firstStop; i < top.size(); i++) {
    if (!stopsText.empty()) stopsText += ", ";
    stopsText += top[i];
  }
  if (!parseStops(stopsText, &layer->stops)) return false;
  layer->kind = BackgroundKind::Radial;
  layer->circle = circle;
  layer->has_shape = hasShape;
  layer->size = size;
  layer->center_x = cx;
  layer->center_y = cy;
  layer->radius_x = rx;
  layer->radius_y = explicitRadius ? (ry.unit == Unit::Auto ? rx : ry) : Length{0.0f, Unit::Auto};
  return true;
}

/** Parse one layer, appending it when it is a supported gradient. */
bool parseLayer(const std::string &part, BackgroundLayer *layer) {
  std::string body;
  if (findCall(part, "linear-gradient", &body) ||
      findCall(part, "repeating-linear-gradient", &body)) {
    return parseLinearGradient(body, layer);
  }
  if (findCall(part, "radial-gradient", &body) ||
      findCall(part, "repeating-radial-gradient", &body)) {
    return parseRadialGradient(body, layer);
  }
  return false;
}

/** Parse the layer list. `allowColor` is true for the `background` shorthand. */
bool parseLayers(const std::string &value, std::vector<BackgroundLayer> *layers, Color *color,
                 bool *hasColor, bool allowColor) {
  layers->clear();
  std::string text = xt_css_trim(value);
  std::string lower = xt_css_lower(text);
  if (lower == "none") return true;
  for (const std::string &part : splitCommas(text)) {
    BackgroundLayer layer;
    if (parseLayer(part, &layer)) {
      layers->push_back(layer);
      /* `background: linear-gradient(...) #fff` mixes the final colour with the
       * image layer; the gradient itself is one token, so a colour scan cannot
       * pick up its stops. */
      if (allowColor) {
        for (const std::string &token : splitTokens(part)) {
          Color parsed = xt_css_parse_color(token);
          if (parsed.valid) {
            *color = parsed;
            *hasColor = true;
          }
        }
      }
      continue;
    }
    if (!allowColor) continue;
    Color parsed = xt_css_parse_color(part);
    if (!parsed.valid) {
      for (const std::string &token : splitTokens(part)) {
        parsed = xt_css_parse_color(token);
        if (parsed.valid) break;
      }
    }
    if (parsed.valid) {
      *color = parsed;
      *hasColor = true;
    }
  }
  return true;
}

}  // namespace

void xt_style_apply_background_image(XtStyle *style, const std::string &value) {
  Color color;
  bool hasColor = false;
  std::vector<BackgroundLayer> layers;
  parseLayers(value, &layers, &color, &hasColor, false);
  style->background_layers = std::move(layers);
}

void xt_style_apply_background(XtStyle *style, const std::string &value) {
  /* The shorthand resets every background sub-property, so a later
   * `background: linear-gradient(...)` drops an earlier `background-color`. */
  style->has_background = false;
  style->background = Color();
  Color color;
  bool hasColor = false;
  std::vector<BackgroundLayer> layers;
  parseLayers(value, &layers, &color, &hasColor, true);
  style->background_layers = std::move(layers);
  if (hasColor) {
    style->background = color;
    style->has_background = true;
  }
}

}  // namespace xtgui
