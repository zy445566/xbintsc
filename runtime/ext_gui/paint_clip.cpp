/*
 * xbintsc GUI engine — `clip-path` geometry.
 *
 * CSS shapes become one device-space polygon, which is triangulated with ear
 * clipping (so concave `polygon()` shapes work). The renderer intersects every
 * convex shape/text/image quad with those triangles; no stencil or shader
 * change is needed.
 */

#include "paint_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace xtgui {
namespace paint_detail {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Point {
  float x = 0.0f;
  float y = 0.0f;
};

float resolveX(const Length &length, float base, float em, float vw, float vh) {
  return resolveLength(length, base, em, vw, vh);
}

float resolveY(const Length &length, float base, float em, float vw, float vh) {
  return resolveLength(length, base, em, vw, vh);
}

void appendPoint(std::vector<Point> *points, const Point &point) {
  if (!points->empty()) {
    const Point &last = points->back();
    if (std::fabs(last.x - point.x) < 0.0001f && std::fabs(last.y - point.y) < 0.0001f) return;
  }
  points->push_back(point);
}

/** Rounded-rectangle outline (used by `inset(... round r)`). */
void insetOutline(const LayoutBox &box, const XtStyle &style, const ClipPathSpec &clip, float vw,
                  float vh, std::vector<Point> *points) {
  float em = style.font_size;
  float top = resolveY(clip.inset_top, box.height, em, vw, vh);
  float right = resolveX(clip.inset_right, box.width, em, vw, vh);
  float bottom = resolveY(clip.inset_bottom, box.height, em, vw, vh);
  float left = resolveX(clip.inset_left, box.width, em, vw, vh);
  float x0 = box.x + left;
  float y0 = box.y + top;
  float x1 = box.x + box.width - right;
  float y1 = box.y + box.height - bottom;
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  if (x1 - x0 <= 0.0f || y1 - y0 <= 0.0f) return;
  float radius = resolveLength(clip.inset_radius, std::min(x1 - x0, y1 - y0), em, vw, vh);
  radius = std::max(0.0f, std::min(radius, std::min(x1 - x0, y1 - y0) * 0.5f));
  if (radius <= 0.0f) {
    appendPoint(points, {x0, y0});
    appendPoint(points, {x1, y0});
    appendPoint(points, {x1, y1});
    appendPoint(points, {x0, y1});
    return;
  }
  const int segments = 6;
  /* Clockwise from the top-left corner's start, one quarter arc per corner. */
  for (int i = 0; i <= segments; i++) {
    float angle = kPi + (float)i / (float)segments * (kPi * 0.5f);
    appendPoint(points, {x0 + radius + std::cos(angle) * radius,
                         y0 + radius + std::sin(angle) * radius});
  }
  for (int i = 0; i <= segments; i++) {
    float angle = kPi * 1.5f + (float)i / (float)segments * (kPi * 0.5f);
    appendPoint(points, {x1 - radius + std::cos(angle) * radius,
                         y0 + radius + std::sin(angle) * radius});
  }
  for (int i = 0; i <= segments; i++) {
    float angle = (float)i / (float)segments * (kPi * 0.5f);
    appendPoint(points, {x1 - radius + std::cos(angle) * radius,
                         y1 - radius + std::sin(angle) * radius});
  }
  for (int i = 0; i <= segments; i++) {
    float angle = kPi * 0.5f + (float)i / (float)segments * (kPi * 0.5f);
    appendPoint(points, {x0 + radius + std::cos(angle) * radius,
                         y1 - radius + std::sin(angle) * radius});
  }
}

void ellipseOutline(const LayoutBox &box, const XtStyle &style, const ClipPathSpec &clip, float vw,
                    float vh, std::vector<Point> *points) {
  float em = style.font_size;
  float cx = box.x + resolveX(clip.center_x, box.width, em, vw, vh);
  float cy = box.y + resolveY(clip.center_y, box.height, em, vw, vh);
  bool circle = clip.type == ClipPathSpec::Type::Circle;
  float rx = 0.0f;
  float ry = 0.0f;
  if (clip.radius_explicit) {
    if (circle && clip.radius_x.unit == Unit::Percent) {
      /* CSS resolves a circle percentage against sqrt(w² + h²) / sqrt(2). */
      float diagonal = std::sqrt(box.width * box.width + box.height * box.height);
      rx = ry = diagonal / std::sqrt(2.0f) * clip.radius_x.value / 100.0f;
    } else {
      rx = resolveX(clip.radius_x, box.width, em, vw, vh);
      ry = circle ? rx : resolveY(clip.radius_y, box.height, em, vw, vh);
    }
  } else {
    float left = std::max(0.0f, cx - box.x);
    float right = std::max(0.0f, box.x + box.width - cx);
    float top = std::max(0.0f, cy - box.y);
    float bottom = std::max(0.0f, box.y + box.height - cy);
    if (circle) {
      rx = ry = std::min(std::min(left, right), std::min(top, bottom));
    } else {
      rx = std::min(left, right);
      ry = std::min(top, bottom);
    }
  }
  if (rx <= 0.0f || ry <= 0.0f) return;
  const int segments = 48;
  for (int i = 0; i < segments; i++) {
    float angle = (float)i / (float)segments * kPi * 2.0f;
    appendPoint(points, {cx + std::cos(angle) * rx, cy + std::sin(angle) * ry});
  }
}

void polygonOutline(const LayoutBox &box, const XtStyle &style, const ClipPathSpec &clip, float vw,
                    float vh, std::vector<Point> *points) {
  float em = style.font_size;
  for (size_t i = 0; i + 1 < clip.polygon.size(); i += 2) {
    float x = box.x + resolveX(clip.polygon[i], box.width, em, vw, vh);
    float y = box.y + resolveY(clip.polygon[i + 1], box.height, em, vw, vh);
    appendPoint(points, {x, y});
  }
}

float cross(const Point &a, const Point &b, const Point &c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle(const Point &p, const Point &a, const Point &b, const Point &c) {
  float c1 = cross(a, b, p);
  float c2 = cross(b, c, p);
  float c3 = cross(c, a, p);
  const float eps = 0.0001f;
  bool hasNeg = c1 < -eps || c2 < -eps || c3 < -eps;
  bool hasPos = c1 > eps || c2 > eps || c3 > eps;
  return !(hasNeg && hasPos);
}

/** Ear clipping for a simple polygon; falls back to a fan if it stalls. */
void triangulate(std::vector<Point> points, std::vector<ClipTriangle> *triangles) {
  if (points.size() < 3) return;
  /* Remove a repeated closing point. */
  if (std::fabs(points.front().x - points.back().x) < 0.0001f &&
      std::fabs(points.front().y - points.back().y) < 0.0001f)
    points.pop_back();
  if (points.size() < 3) return;
  float area = 0.0f;
  for (size_t i = 0; i < points.size(); i++) {
    const Point &a = points[i];
    const Point &b = points[(i + 1) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  if (area < 0.0f) std::reverse(points.begin(), points.end());
  std::vector<size_t> indices(points.size());
  for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
  size_t guard = 0;
  while (indices.size() > 3 && guard++ < indices.size() * indices.size() + 8) {
    bool clipped = false;
    for (size_t i = 0; i < indices.size(); i++) {
      size_t previous = indices[(i + indices.size() - 1) % indices.size()];
      size_t current = indices[i];
      size_t next = indices[(i + 1) % indices.size()];
      const Point &a = points[previous];
      const Point &b = points[current];
      const Point &c = points[next];
      if (cross(a, b, c) <= 0.0001f) continue;
      bool contains = false;
      for (size_t other : indices) {
        if (other == previous || other == current || other == next) continue;
        if (pointInTriangle(points[other], a, b, c)) {
          contains = true;
          break;
        }
      }
      if (contains) continue;
      ClipTriangle triangle;
      triangle.x[0] = a.x;
      triangle.y[0] = a.y;
      triangle.x[1] = b.x;
      triangle.y[1] = b.y;
      triangle.x[2] = c.x;
      triangle.y[2] = c.y;
      triangles->push_back(triangle);
      indices.erase(indices.begin() + (long)i);
      clipped = true;
      break;
    }
    if (!clipped) break;
  }
  if (indices.size() >= 3) {
    for (size_t i = 1; i + 1 < indices.size(); i++) {
      ClipTriangle triangle;
      triangle.x[0] = points[indices[0]].x;
      triangle.y[0] = points[indices[0]].y;
      triangle.x[1] = points[indices[i]].x;
      triangle.y[1] = points[indices[i]].y;
      triangle.x[2] = points[indices[i + 1]].x;
      triangle.y[2] = points[indices[i + 1]].y;
      triangles->push_back(triangle);
    }
  }
}

}  // namespace

int addClipPathForBox(DisplayList &out, const LayoutBox &box, const XtStyle &style,
                      float viewportWidth, float viewportHeight) {
  if (style.clip_path.type == ClipPathSpec::Type::None) return -1;
  std::vector<Point> points;
  switch (style.clip_path.type) {
    case ClipPathSpec::Type::Inset:
      insetOutline(box, style, style.clip_path, viewportWidth, viewportHeight, &points);
      break;
    case ClipPathSpec::Type::Circle:
    case ClipPathSpec::Type::Ellipse:
      ellipseOutline(box, style, style.clip_path, viewportWidth, viewportHeight, &points);
      break;
    case ClipPathSpec::Type::Polygon:
      polygonOutline(box, style, style.clip_path, viewportWidth, viewportHeight, &points);
      break;
    default:
      return -1;
  }
  /* An empty but valid shape (`inset(50%)`, `circle(0)`) clips everything away;
   * it must not fall back to "no clip". The paint walk keeps the empty geometry
   * and the renderer emits no geometry for ops that reference it. */
  ClipPathGeometry geometry;
  geometry.points.reserve(points.size() * 2);
  for (Point &point : points) {
    float x = point.x;
    float y = point.y;
    box.world.apply(point.x, point.y, &x, &y);
    geometry.points.push_back(x);
    geometry.points.push_back(y);
  }
  if (points.size() >= 3) {
    std::vector<Point> device;
    device.reserve(points.size());
    for (size_t i = 0; i + 1 < geometry.points.size(); i += 2)
      device.push_back({geometry.points[i], geometry.points[i + 1]});
    triangulate(device, &geometry.triangles);
  }
  out.clip_paths.push_back(std::move(geometry));
  return (int)(out.clip_paths.size() - 1);
}

PaintClip addClipPath(PaintClip clip, int index) {
  if (index < 0) return clip;
  if (clip.path_count < 2) clip.paths[clip.path_count++] = index;
  return clip;
}

}  // namespace paint_detail
}  // namespace xtgui
