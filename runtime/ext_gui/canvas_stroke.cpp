/*
 * xbintsc GUI engine — Canvas2D stroking.
 *
 * A stroked path is flattened into segments; each segment becomes a quad and
 * every joint/cap is filled with the shape the current `lineJoin`/`lineCap`
 * asks for. Dash patterns split a polyline into the "on" pieces first, so a
 * dashed stroke is just a set of open polylines with the same cap rules.
 *
 * Everything here works in device space: `canvas.cpp` flattens the path and
 * passes the paint (solid colour or gradient) through.
 */

#include <algorithm>
#include <cmath>
#include <vector>

#include "canvas_internal.h"

namespace xtgui {

namespace {

constexpr float kPi = 3.14159265358979323846f;

CanvasPoint lerp_point(const CanvasPoint &a, const CanvasPoint &b, float t) {
  return CanvasPoint{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

bool same_point(const CanvasPoint &a, const CanvasPoint &b) {
  return std::fabs(a.x - b.x) < 1e-6f && std::fabs(a.y - b.y) < 1e-6f;
}

/** Twice the signed area of a closed polygon (shoelace). */
float signed_area(const CanvasSubpath &loop) {
  float sum = 0.0f;
  const size_t count = loop.size();
  for (size_t i = 0; i < count; i++) {
    const CanvasPoint &a = loop[i];
    const CanvasPoint &b = loop[(i + 1) % count];
    sum += a.x * b.y - b.x * a.y;
  }
  return sum;
}

/** Append one stroke piece to the batch, normalising its winding: a stroke is
 * filled as one nonzero path, so a join that happened to be wound the other
 * way would cancel the quads it overlaps instead of covering them. */
void append_piece(std::vector<CanvasSubpath> *loops, CanvasSubpath piece) {
  if (piece.size() < 3) return;
  if (signed_area(piece) < 0.0f) std::reverse(piece.begin(), piece.end());
  loops->push_back(std::move(piece));
}

}  // namespace

void Canvas2D::stroke_subpath(const CanvasSubpath &points, bool closed, const CanvasPaint &paint) {
  if (points.size() < 2) return;
  float scale = state_.transform.scale();
  if (!(scale > 0.0f)) scale = 1.0f;
  float half = state_.line_width * scale * 0.5f;
  if (half <= 0.0f) half = 0.5f;
  if (!state_.line_dash.empty()) {
    std::vector<CanvasSubpath> pieces;
    dash_polyline(points, closed, &pieces);
    for (const CanvasSubpath &piece : pieces) stroke_polyline(piece, false, half, paint);
    return;
  }
  stroke_polyline(points, closed, half, paint);
}

void Canvas2D::stroke_polyline(const CanvasSubpath &points, bool closed, float half,
                               const CanvasPaint &paint) {
  CanvasSubpath pts = points;
  if (closed && !same_point(pts.front(), pts.back())) pts.push_back(pts.front());
  const size_t n = pts.size();
  if (n < 2) return;

  /* Every segment, join and cap becomes one polygon; the whole polyline is
   * filled in a single coverage pass instead of one pass per piece (which was
   * the dominant cost of a many-segment stroke). */
  std::vector<CanvasSubpath> loops;
  loops.reserve(n * 2 + 2);

  auto fill_disc = [&](const CanvasPoint &p) {
    CanvasSubpath disc;
    const int segments = 24;
    disc.reserve((size_t)segments);
    for (int i = 0; i < segments; i++) {
      float angle = 2.0f * kPi * ((float)i / (float)segments);
      disc.push_back(CanvasPoint{p.x + half * std::cos(angle), p.y + half * std::sin(angle)});
    }
    append_piece(&loops, std::move(disc));
  };

  /* Segments. A square cap extends the first/last quad along the line by half
   * the width; round caps add a disc at the endpoints below. */
  for (size_t i = 0; i + 1 < n; i++) {
    CanvasPoint p = pts[i];
    CanvasPoint q = pts[i + 1];
    float dx = q.x - p.x;
    float dy = q.y - p.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1e-6f) continue;
    float ux = dx / length;
    float uy = dy / length;
    float nx = -uy * half;
    float ny = ux * half;
    if (!closed && i == 0 && state_.line_cap == 2) {
      p.x -= ux * half;
      p.y -= uy * half;
    }
    if (!closed && i + 2 == n && state_.line_cap == 2) {
      q.x += ux * half;
      q.y += uy * half;
    }
    CanvasSubpath quad{CanvasPoint{p.x + nx, p.y + ny}, CanvasPoint{q.x + nx, q.y + ny},
                       CanvasPoint{q.x - nx, q.y - ny}, CanvasPoint{p.x - nx, p.y - ny}};
    append_piece(&loops, std::move(quad));
  }

  /* Joins: round adds a disc, bevel a triangle, miter the intersect of the two
   * outer offset lines (clamped by `miterLimit`). */
  auto add_join = [&](const CanvasPoint &previous, const CanvasPoint &p, const CanvasPoint &next) {
    float v1x = p.x - previous.x;
    float v1y = p.y - previous.y;
    float v2x = next.x - p.x;
    float v2y = next.y - p.y;
    float len1 = std::sqrt(v1x * v1x + v1y * v1y);
    float len2 = std::sqrt(v2x * v2x + v2y * v2y);
    if (len1 <= 1e-6f || len2 <= 1e-6f) return;
    v1x /= len1;
    v1y /= len1;
    v2x /= len2;
    v2y /= len2;
    float cross = v1x * v2y - v1y * v2x;
    if (std::fabs(cross) < 1e-6f) return;
    if (state_.line_join == 1) {
      fill_disc(p);
      return;
    }
    /* The outer side is the opposite side of the turn. */
    float side = cross > 0.0f ? -1.0f : 1.0f;
    CanvasPoint a{p.x - v1y * half * side, p.y + v1x * half * side};
    CanvasPoint b{p.x - v2y * half * side, p.y + v2x * half * side};
    if (state_.line_join == 2) {
      append_piece(&loops, CanvasSubpath{a, b, p});
      return;
    }
    float t = ((b.x - a.x) * v2y - (b.y - a.y) * v2x) / cross;
    CanvasPoint miter{a.x + v1x * t, a.y + v1y * t};
    float miterLength = std::sqrt((miter.x - p.x) * (miter.x - p.x) +
                                  (miter.y - p.y) * (miter.y - p.y));
    if (std::isfinite(miterLength) && miterLength <= half * state_.miter_limit) {
      append_piece(&loops, CanvasSubpath{a, miter, b, p});
    } else {
      append_piece(&loops, CanvasSubpath{a, b, p});
    }
  };

  if (closed) {
    const size_t unique = n - 1;
    for (size_t i = 0; i < unique; i++) {
      const CanvasPoint &previous = pts[(i + unique - 1) % unique];
      const CanvasPoint &next = pts[(i + 1) % unique];
      add_join(previous, pts[i], next);
    }
  } else {
    for (size_t i = 1; i + 1 < n; i++) add_join(pts[i - 1], pts[i], pts[i + 1]);
    if (state_.line_cap == 1) {
      fill_disc(pts.front());
      fill_disc(pts.back());
    }
  }

  if (!loops.empty()) {
    canvas_detail::fill_loops(image_.pixels, image_.width, image_.height, loops, paint,
                              state_.global_alpha, false, clip_mask());
  }
}

void Canvas2D::dash_polyline(const CanvasSubpath &points, bool closed,
                             std::vector<CanvasSubpath> *out) const {
  if (points.size() < 2) return;
  float scale = state_.transform.scale();
  if (!(scale > 0.0f)) scale = 1.0f;
  std::vector<float> pattern;
  pattern.reserve(state_.line_dash.size());
  float total = 0.0f;
  for (float entry : state_.line_dash) {
    pattern.push_back(entry * scale);
    total += entry * scale;
  }
  if (pattern.empty() || total <= 1e-6f) {
    out->push_back(points);
    return;
  }

  CanvasSubpath pts = points;
  if (closed && !same_point(pts.front(), pts.back())) pts.push_back(pts.front());
  const size_t n = pts.size();

  /* Consume `lineDashOffset` from the repeating pattern before drawing. */
  float dashOffset = state_.line_dash_offset;
  if (!std::isfinite(dashOffset)) dashOffset = 0.0f;
  float offset = std::fmod(dashOffset * scale, total);
  if (offset < 0.0f) offset += total;
  size_t index = 0;
  float remain = pattern[0];
  int guard = 0;
  while (offset > 1e-6f && guard++ < 100000) {
    if (offset >= remain) {
      offset -= remain;
      index = (index + 1) % pattern.size();
      remain = pattern[index];
    } else {
      remain -= offset;
      offset = 0.0f;
    }
  }
  auto skip_empty = [&]() {
    int skipped = 0;
    while (remain <= 1e-6f && skipped++ < 100000) {
      index = (index + 1) % pattern.size();
      remain = pattern[index];
    }
  };
  skip_empty();
  bool on = (index % 2 == 0);

  CanvasSubpath current;
  bool have_current = false;
  auto finish_dash = [&]() {
    if (have_current) {
      out->push_back(current);
      current.clear();
      have_current = false;
    }
  };

  for (size_t i = 0; i + 1 < n; i++) {
    const CanvasPoint &p = pts[i];
    const CanvasPoint &q = pts[i + 1];
    float dx = q.x - p.x;
    float dy = q.y - p.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1e-6f) continue;
    float position = 0.0f;
    while (position < length - 1e-6f) {
      if (remain <= 1e-6f) {
        skip_empty();
        on = (index % 2 == 0);
        continue;
      }
      float step = std::min(remain, length - position);
      if (on) {
        if (!have_current) {
          current.clear();
          current.push_back(lerp_point(p, q, position / length));
          have_current = true;
        }
        position += step;
        current.push_back(lerp_point(p, q, position / length));
      } else {
        position += step;
      }
      remain -= step;
      if (remain <= 1e-6f) {
        if (on) finish_dash();
        index = (index + 1) % pattern.size();
        remain = pattern[index];
        on = (index % 2 == 0);
        skip_empty();
        on = (index % 2 == 0);
      }
    }
  }
  finish_dash();
}

}  // namespace xtgui
