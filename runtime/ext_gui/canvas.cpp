/*
 * xbintsc GUI engine — the Canvas2D object model.
 *
 * This unit owns the drawing state, the current path and the draw entry
 * points; the actual pixel work is delegated to `canvas_raster.cpp`. Canvases
 * are registered in a process-wide id table so the GPU renderer can find the
 * bitmap behind a display-list entry without knowing about the DOM.
 */

#include "canvas.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "canvas_internal.h"
#include "css.h"

namespace xtgui {

namespace {

std::atomic<uint64_t> g_next_canvas_id{1};

/** Process-wide id -> canvas table. The engine is single-threaded; the table
 * lives for the process while the objects are owned by the window. */
std::unordered_map<uint64_t, Canvas2D *> &canvas_registry() {
  static std::unordered_map<uint64_t, Canvas2D *> registry;
  return registry;
}

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
    if (lower == "bold") {
      spec.weight = 700;
      continue;
    }
    if (lower == "bolder") {
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

CanvasPoint lerp(const CanvasPoint &a, const CanvasPoint &b, float t) {
  return CanvasPoint{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

}  // namespace

Canvas2D* xt_canvas_by_id(uint64_t id) {
  auto found = canvas_registry().find(id);
  return found == canvas_registry().end() ? nullptr : found->second;
}

float CanvasTransform::scale() const {
  float determinant = std::fabs(a * d - b * c);
  return std::sqrt(determinant);
}

Canvas2D::Canvas2D(int width, int height) : id_(g_next_canvas_id.fetch_add(1)) {
  resize(width, height);
  canvas_registry()[id_] = this;
}

Canvas2D::~Canvas2D() {
  std::unordered_map<uint64_t, Canvas2D *> &registry = canvas_registry();
  auto found = registry.find(id_);
  if (found != registry.end() && found->second == this) registry.erase(found);
}

void Canvas2D::resize(int width, int height) {
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  image_.width = width;
  image_.height = height;
  image_.pixels.assign((size_t)width * (size_t)height * 4, 0);
  generation_++;
}

void Canvas2D::save() { stack_.push_back(state_); }

void Canvas2D::restore() {
  if (stack_.empty()) return;
  state_ = stack_.back();
  stack_.pop_back();
}

void Canvas2D::translate(float x, float y) {
  transform(1.0f, 0.0f, 0.0f, 1.0f, x, y);
}

void Canvas2D::rotate(float radians) {
  float c = std::cos(radians);
  float s = std::sin(radians);
  transform(c, s, -s, c, 0.0f, 0.0f);
}

void Canvas2D::scale(float sx, float sy) { transform(sx, 0.0f, 0.0f, sy, 0.0f, 0.0f); }

void Canvas2D::transform(float a, float b, float c, float d, float e, float f) {
  const CanvasTransform &m = state_.transform;
  CanvasTransform next;
  next.a = a * m.a + c * m.b;
  next.b = b * m.a + d * m.b;
  next.c = a * m.c + c * m.d;
  next.d = b * m.c + d * m.d;
  next.e = a * m.e + c * m.f + e;
  next.f = b * m.e + d * m.f + f;
  state_.transform = next;
}

void Canvas2D::set_transform(float a, float b, float c, float d, float e, float f) {
  state_.transform = CanvasTransform{a, b, c, d, e, f};
}

void Canvas2D::set_fill_style(const std::string &css) {
  parse_color_into(css, state_.fill_color, &state_.fill_style);
}

void Canvas2D::set_stroke_style(const std::string &css) {
  parse_color_into(css, state_.stroke_color, &state_.stroke_style);
}

void Canvas2D::set_global_alpha(float alpha) {
  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha > 1.0f) alpha = 1.0f;
  state_.global_alpha = alpha;
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

void Canvas2D::begin_path() { path_.clear(); }

void Canvas2D::move_to(float x, float y) { path_.push_back(CanvasSubpath{CanvasPoint{x, y}}); }

void Canvas2D::line_to(float x, float y) {
  if (path_.empty()) path_.push_back(CanvasSubpath{});
  path_.back().push_back(CanvasPoint{x, y});
}

void Canvas2D::close_path() {
  if (path_.empty() || path_.back().size() < 2) return;
  CanvasPoint first = path_.back().front();
  CanvasPoint last = path_.back().back();
  if (first.x != last.x || first.y != last.y) path_.back().push_back(first);
}

void Canvas2D::rect(float x, float y, float width, float height) {
  path_.push_back(CanvasSubpath{CanvasPoint{x, y}, CanvasPoint{x + width, y},
                                CanvasPoint{x + width, y + height}, CanvasPoint{x, y + height},
                                CanvasPoint{x, y}});
}

void Canvas2D::flatten_arc(float cx, float cy, float radius, float start, float end, bool ccw,
                           CanvasSubpath *out) const {
  float sweep = end - start;
  if (ccw) {
    while (sweep > 0.0f) sweep -= 2.0f * 3.14159265358979323846f;
    while (sweep < -2.0f * 3.14159265358979323846f) sweep += 2.0f * 3.14159265358979323846f;
  } else {
    while (sweep < 0.0f) sweep += 2.0f * 3.14159265358979323846f;
    while (sweep > 2.0f * 3.14159265358979323846f) sweep -= 2.0f * 3.14159265358979323846f;
  }
  int segments = (int)std::ceil(std::fabs(sweep) / (3.14159265358979323846f / 32.0f));
  if (segments < 1) segments = 1;
  if (segments > 2048) segments = 2048;
  for (int i = 0; i <= segments; i++) {
    float angle = start + sweep * ((float)i / (float)segments);
    out->push_back(CanvasPoint{cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
  }
}

void Canvas2D::arc(float cx, float cy, float radius, float start, float end, bool ccw) {
  CanvasSubpath arcPoints;
  flatten_arc(cx, cy, radius, start, end, ccw, &arcPoints);
  if (arcPoints.empty()) return;
  if (path_.empty() || path_.back().empty()) {
    path_.push_back(CanvasSubpath{arcPoints.front()});
  } else {
    CanvasPoint last = path_.back().back();
    if (last.x != arcPoints.front().x || last.y != arcPoints.front().y) {
      path_.back().push_back(arcPoints.front());
    }
  }
  for (size_t i = 1; i < arcPoints.size(); i++) path_.back().push_back(arcPoints[i]);
}

void Canvas2D::quadratic_curve_to(float cpx, float cpy, float x, float y) {
  if (path_.empty() || path_.back().empty()) move_to(cpx, cpy);
  CanvasPoint start = path_.back().back();
  CanvasPoint control{cpx, cpy};
  CanvasPoint target{x, y};
  const int segments = 16;
  for (int i = 1; i <= segments; i++) {
    float t = (float)i / (float)segments;
    CanvasPoint a = lerp(start, control, t);
    CanvasPoint b = lerp(control, target, t);
    CanvasPoint point = lerp(a, b, t);
    path_.back().push_back(point);
  }
}

void Canvas2D::bezier_curve_to(float c1x, float c1y, float c2x, float c2y, float x, float y) {
  if (path_.empty() || path_.back().empty()) move_to(c1x, c1y);
  CanvasPoint p0 = path_.back().back();
  CanvasPoint p1{c1x, c1y};
  CanvasPoint p2{c2x, c2y};
  CanvasPoint p3{x, y};
  const int segments = 24;
  for (int i = 1; i <= segments; i++) {
    float t = (float)i / (float)segments;
    float u = 1.0f - t;
    CanvasPoint point{u * u * u * p0.x + 3.0f * u * u * t * p1.x + 3.0f * u * t * t * p2.x + t * t * t * p3.x,
                      u * u * u * p0.y + 3.0f * u * u * t * p1.y + 3.0f * u * t * t * p2.y + t * t * t * p3.y};
    path_.back().push_back(point);
  }
}

void Canvas2D::to_device(float x, float y, float *outX, float *outY) const {
  state_.transform.apply(x, y, outX, outY);
}

std::vector<CanvasSubpath> Canvas2D::device_subpaths() const {
  std::vector<CanvasSubpath> result;
  result.reserve(path_.size());
  for (const CanvasSubpath &subpath : path_) {
    CanvasSubpath device;
    device.reserve(subpath.size());
    for (const CanvasPoint &point : subpath) {
      CanvasPoint mapped;
      to_device(point.x, point.y, &mapped.x, &mapped.y);
      device.push_back(mapped);
    }
    result.push_back(std::move(device));
  }
  return result;
}

void Canvas2D::fill() {
  std::vector<CanvasSubpath> subpaths = device_subpaths();
  std::vector<CanvasSubpath> loops;
  for (CanvasSubpath &subpath : subpaths) {
    if (subpath.size() < 3) continue;
    loops.push_back(std::move(subpath));
  }
  if (loops.empty()) return;
  canvas_detail::fill_loops(image_.pixels, image_.width, image_.height, loops, state_.fill_color,
                            state_.global_alpha);
  generation_++;
}

void Canvas2D::stroke_subpath(const CanvasSubpath &points, bool closed, const float color[4]) {
  if (points.size() < 2) return;
  float half = state_.line_width * state_.transform.scale() * 0.5f;
  if (half <= 0.0f) half = 0.5f;
  size_t segmentCount = points.size() - 1;
  for (size_t i = 0; i < segmentCount; i++) {
    const CanvasPoint &p = points[i];
    const CanvasPoint &q = points[i + 1];
    float dx = q.x - p.x;
    float dy = q.y - p.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0001f) continue;
    float nx = -dy / length * half;
    float ny = dx / length * half;
    CanvasSubpath quad{CanvasPoint{p.x + nx, p.y + ny}, CanvasPoint{q.x + nx, q.y + ny},
                       CanvasPoint{q.x - nx, q.y - ny}, CanvasPoint{p.x - nx, p.y - ny}};
    canvas_detail::fill_loop(image_.pixels, image_.width, image_.height, quad, color,
                             state_.global_alpha);
  }
  (void)closed;
  for (const CanvasPoint &p : points) {
    CanvasSubpath disc;
    const int segments = 16;
    for (int i = 0; i < segments; i++) {
      float angle = 2.0f * 3.14159265358979323846f * ((float)i / (float)segments);
      disc.push_back(CanvasPoint{p.x + half * std::cos(angle), p.y + half * std::sin(angle)});
    }
    canvas_detail::fill_loop(image_.pixels, image_.width, image_.height, disc, color,
                             state_.global_alpha);
  }
}

void Canvas2D::stroke() {
  std::vector<CanvasSubpath> subpaths = device_subpaths();
  for (const CanvasSubpath &subpath : subpaths) {
    if (subpath.size() < 2) continue;
    bool closed = subpath.size() >= 3 && subpath.front().x == subpath.back().x &&
                  subpath.front().y == subpath.back().y;
    stroke_subpath(subpath, closed, state_.stroke_color);
  }
  generation_++;
}

void Canvas2D::clear_rect(float x, float y, float width, float height) {
  float x0, y0, x1, y1;
  to_device(x, y, &x0, &y0);
  to_device(x + width, y + height, &x1, &y1);
  canvas_detail::clear_rect(image_.pixels, image_.width, image_.height, x0, y0, x1, y1);
  generation_++;
}

void Canvas2D::fill_rect(float x, float y, float width, float height) {
  if (state_.transform.axis_aligned()) {
    float x0, y0, x1, y1;
    to_device(x, y, &x0, &y0);
    to_device(x + width, y + height, &x1, &y1);
    canvas_detail::fill_rect(image_.pixels, image_.width, image_.height, x0, y0, x1, y1,
                             state_.fill_color, state_.global_alpha);
  } else {
    CanvasSubpath loop;
    const float corners[4][2] = {
        {x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
    for (const auto &corner : corners) {
      CanvasPoint point;
      to_device(corner[0], corner[1], &point.x, &point.y);
      loop.push_back(point);
    }
    canvas_detail::fill_loop(image_.pixels, image_.width, image_.height, loop, state_.fill_color,
                             state_.global_alpha);
  }
  generation_++;
}

void Canvas2D::stroke_rect(float x, float y, float width, float height) {
  CanvasSubpath loop;
  const float corners[5][2] = {
      {x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}, {x, y}};
  for (const auto &corner : corners) {
    CanvasPoint point;
    to_device(corner[0], corner[1], &point.x, &point.y);
    loop.push_back(point);
  }
  stroke_subpath(loop, true, state_.stroke_color);
  generation_++;
}

float Canvas2D::measure_text(const std::string &text) const {
  return xt_text_measure_width(text, state_.font);
}

void Canvas2D::paint_text(const std::string &text, float x, float y, const float color[4],
                          bool stroke) {
  Font *font = xt_text_resolve(state_.font);
  if (font == nullptr) return;
  std::vector<ShapedGlyph> glyphs;
  float width = xt_text_shape_run(text, font, glyphs);
  float ascent = 0.0f, descent = 0.0f, lineHeight = 0.0f;
  xt_text_metrics(state_.font, &ascent, &descent, &lineHeight);

  float originX = x;
  if (state_.text_align == 1) {
    originX -= width * 0.5f;
  } else if (state_.text_align == 2) {
    originX -= width;
  }
  float originY = y;
  switch (state_.text_baseline) {
    case 1:
      originY += ascent;
      break;
    case 2:
      originY += (ascent - descent) * 0.5f;
      break;
    case 3:
      originY -= descent;
      break;
    case 4:
      originY += ascent * 0.8f;
      break;
    case 5:
      originY -= descent;
      break;
    default:
      break;
  }

  float scale = state_.transform.scale();
  if (scale <= 0.0001f) scale = 1.0f;
  bool scaled = std::fabs(scale - 1.0f) > 0.01f;

  float penX = 0.0f;
  for (const ShapedGlyph &glyph : glyphs) {
    float userX = originX + penX + glyph.x_offset;
    float userY = originY + glyph.y_offset;
    float deviceX = 0.0f, deviceY = 0.0f;
    to_device(userX, userY, &deviceX, &deviceY);
    Font *face = glyph.font != nullptr ? glyph.font : font;
    if (scaled) {
      Font *sized = xt_text_scale(face, state_.font.pixel_size * scale);
      if (sized != nullptr) face = sized;
    }
    GlyphImage image;
    if (xt_text_rasterize(face, glyph.glyph, image) && image.width > 0 && image.height > 0) {
      canvas_detail::blit_glyph(image_.pixels, image_.width, image_.height, image.pixels, image.width,
                                image.height, image.left, image.top, deviceX, deviceY, color,
                                state_.global_alpha);
    }
    penX += glyph.x_advance;
  }
  (void)stroke;
  generation_++;
}

void Canvas2D::fill_text(const std::string &text, float x, float y) {
  paint_text(text, x, y, state_.fill_color, false);
}

void Canvas2D::stroke_text(const std::string &text, float x, float y) {
  paint_text(text, x, y, state_.stroke_color, true);
}

void Canvas2D::draw_image(const Image &source, float dx, float dy, float dw, float dh, float sx,
                          float sy, float sw, float sh) {
  if (state_.transform.axis_aligned()) {
    float x0, y0, x1, y1;
    to_device(dx, dy, &x0, &y0);
    to_device(dx + dw, dy + dh, &x1, &y1);
    canvas_detail::draw_image(image_.pixels, image_.width, image_.height, source, x0, y0, x1 - x0,
                              y1 - y0, sx, sy, sw, sh, state_.global_alpha);
  } else {
    float cornersX[4], cornersY[4];
    const float corners[4][2] = {
        {dx, dy}, {dx + dw, dy}, {dx + dw, dy + dh}, {dx, dy + dh}};
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    for (int i = 0; i < 4; i++) {
      to_device(corners[i][0], corners[i][1], &cornersX[i], &cornersY[i]);
      if (i == 0) {
        minX = maxX = cornersX[i];
        minY = maxY = cornersY[i];
      } else {
        minX = std::min(minX, cornersX[i]);
        maxX = std::max(maxX, cornersX[i]);
        minY = std::min(minY, cornersY[i]);
        maxY = std::max(maxY, cornersY[i]);
      }
    }
    canvas_detail::draw_image(image_.pixels, image_.width, image_.height, source, minX, minY,
                              maxX - minX, maxY - minY, sx, sy, sw, sh, state_.global_alpha);
  }
  generation_++;
}

bool Canvas2D::get_image_data(int x, int y, int width, int height,
                              std::vector<unsigned char> *out) const {
  if (width <= 0 || height <= 0 || out == nullptr) return false;
  out->assign((size_t)width * (size_t)height * 4, 0);
  for (int py = 0; py < height; py++) {
    int sourceY = y + py;
    if (sourceY < 0 || sourceY >= image_.height) continue;
    for (int px = 0; px < width; px++) {
      int sourceX = x + px;
      if (sourceX < 0 || sourceX >= image_.width) continue;
      const unsigned char *src =
          image_.pixels.data() + ((size_t)sourceY * (size_t)image_.width + (size_t)sourceX) * 4;
      unsigned char *dst = out->data() + ((size_t)py * (size_t)width + (size_t)px) * 4;
      std::memcpy(dst, src, 4);
    }
  }
  return true;
}

}  // namespace xtgui
