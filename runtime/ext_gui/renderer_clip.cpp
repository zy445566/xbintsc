/*
 * xbintsc GUI engine — CPU geometry clipping for `clip-path`.
 *
 * Every painted shape is a convex quad. The paint pass leaves each clip path as
 * a triangulated device-space polygon; clipping a convex quad by one triangle
 * keeps it convex, so the result is emitted as a triangle fan. Clipping against
 * a concave path is correct because its triangles partition the path.
 */

#include "renderer_internal.h"

#include <algorithm>
#include <vector>

namespace xtgui {
namespace renderer_detail {

namespace {

float side(const ClipTriangle &triangle, int edge, float x, float y) {
  int next = (edge + 1) % 3;
  float ax = triangle.x[edge];
  float ay = triangle.y[edge];
  float bx = triangle.x[next];
  float by = triangle.y[next];
  return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}

bool triangleOrientation(const ClipTriangle &triangle, float *orientation) {
  float cross = (triangle.x[1] - triangle.x[0]) * (triangle.y[2] - triangle.y[0]) -
                (triangle.y[1] - triangle.y[0]) * (triangle.x[2] - triangle.x[0]);
  if (cross > -0.00001f && cross < 0.00001f) return false;
  *orientation = cross > 0.0f ? 1.0f : -1.0f;
  return true;
}

template <typename V, typename Mix>
std::vector<V> clipToTriangle(const std::vector<V> &subject, const ClipTriangle &triangle,
                              Mix mix) {
  float orientation = 0.0f;
  if (!triangleOrientation(triangle, &orientation)) return std::vector<V>();
  std::vector<V> out = subject;
  for (int edge = 0; edge < 3 && !out.empty(); edge++) {
    std::vector<V> input = out;
    out.clear();
    for (size_t i = 0; i < input.size(); i++) {
      const V &current = input[i];
      const V &previous = input[(i + input.size() - 1) % input.size()];
      float currentSide = side(triangle, edge, current.x, current.y) * orientation;
      float previousSide = side(triangle, edge, previous.x, previous.y) * orientation;
      bool currentInside = currentSide >= -0.0001f;
      bool previousInside = previousSide >= -0.0001f;
      if (currentInside) {
        if (!previousInside) {
          float t = previousSide / (previousSide - currentSide);
          out.push_back(mix(previous, current, t));
        }
        out.push_back(current);
      } else if (previousInside) {
        float t = previousSide / (previousSide - currentSide);
        out.push_back(mix(previous, current, t));
      }
    }
  }
  return out;
}

template <typename V>
void appendFan(const std::vector<V> &polygon, std::vector<V> *out) {
  for (size_t i = 1; i + 1 < polygon.size(); i++) {
    out->push_back(polygon[0]);
    out->push_back(polygon[i]);
    out->push_back(polygon[i + 1]);
  }
}

const ClipPathGeometry *pathAt(const DisplayList &list, int index) {
  if (index < 0 || (size_t)index >= list.clip_paths.size()) return nullptr;
  return &list.clip_paths[(size_t)index];
}

template <typename V, typename Mix>
uint32_t appendClipped(std::vector<V> &out, const std::vector<V> &quad, const PaintClip &clip,
                       const DisplayList &list, Mix mix) {
  if (clip.path_count <= 0) {
    appendFan(quad, &out);
    return 6;
  }
  const ClipPathGeometry *first = pathAt(list, clip.paths[0]);
  if (first == nullptr || first->triangles.empty()) return 0;
  uint32_t before = (uint32_t)out.size();
  if (clip.path_count == 1) {
    for (const ClipTriangle &triangle : first->triangles) {
      std::vector<V> clipped = clipToTriangle(quad, triangle, mix);
      if (clipped.size() >= 3) appendFan(clipped, &out);
    }
  } else {
    const ClipPathGeometry *second = pathAt(list, clip.paths[1]);
    if (second == nullptr || second->triangles.empty()) return 0;
    for (const ClipTriangle &outer : first->triangles) {
      std::vector<V> firstClip = clipToTriangle(quad, outer, mix);
      if (firstClip.size() < 3) continue;
      for (const ClipTriangle &inner : second->triangles) {
        std::vector<V> secondClip = clipToTriangle(firstClip, inner, mix);
        if (secondClip.size() >= 3) appendFan(secondClip, &out);
      }
    }
  }
  return (uint32_t)out.size() - before;
}

Vertex mixVertex(const Vertex &a, const Vertex &b, float t) {
  Vertex out;
  out.x = a.x + (b.x - a.x) * t;
  out.y = a.y + (b.y - a.y) * t;
  out.lx = a.lx + (b.lx - a.lx) * t;
  out.ly = a.ly + (b.ly - a.ly) * t;
  out.hx = a.hx + (b.hx - a.hx) * t;
  out.hy = a.hy + (b.hy - a.hy) * t;
  out.radius = a.radius + (b.radius - a.radius) * t;
  out.r = a.r + (b.r - a.r) * t;
  out.g = a.g + (b.g - a.g) * t;
  out.b = a.b + (b.b - a.b) * t;
  out.a = a.a + (b.a - a.a) * t;
  return out;
}

TextVertex mixTextVertex(const TextVertex &a, const TextVertex &b, float t) {
  TextVertex out;
  out.x = a.x + (b.x - a.x) * t;
  out.y = a.y + (b.y - a.y) * t;
  out.u = a.u + (b.u - a.u) * t;
  out.v = a.v + (b.v - a.v) * t;
  out.r = a.r + (b.r - a.r) * t;
  out.g = a.g + (b.g - a.g) * t;
  out.b = a.b + (b.b - a.b) * t;
  out.a = a.a + (b.a - a.a) * t;
  return out;
}

}  // namespace

uint32_t pushShape(std::vector<Vertex> &verts, const PaintRect &rect, const PaintClip &clip,
                   const DisplayList &list) {
  float hw = rect.width * 0.5f;
  float hh = rect.height * 0.5f;
  float cx = rect.x + hw;
  float cy = rect.y + hh;
  float radius = std::min(rect.radius, std::min(hw, hh));
  const float local_x[4] = {-hw, hw, hw, -hw};
  const float local_y[4] = {-hh, -hh, hh, hh};
  std::vector<Vertex> quad(4);
  for (int i = 0; i < 4; i++) {
    rect.transform.apply(cx + local_x[i], cy + local_y[i], &quad[i].x, &quad[i].y);
    quad[i].lx = local_x[i];
    quad[i].ly = local_y[i];
    quad[i].hx = hw;
    quad[i].hy = hh;
    quad[i].radius = radius;
    quad[i].r = rect.color.r;
    quad[i].g = rect.color.g;
    quad[i].b = rect.color.b;
    quad[i].a = rect.color.a;
  }
  return appendClipped(verts, quad, clip, list, mixVertex);
}

uint32_t pushQuad(std::vector<TextVertex> &verts, const float corners_x[4],
                  const float corners_y[4], const float u[4], const float v[4],
                  const Color &color, const Matrix2D &transform, const PaintClip &clip,
                  const DisplayList &list) {
  std::vector<TextVertex> quad(4);
  for (int i = 0; i < 4; i++) {
    transform.apply(corners_x[i], corners_y[i], &quad[i].x, &quad[i].y);
    quad[i].u = u[i];
    quad[i].v = v[i];
    quad[i].r = color.r;
    quad[i].g = color.g;
    quad[i].b = color.b;
    quad[i].a = color.a;
  }
  return appendClipped(verts, quad, clip, list, mixTextVertex);
}

uint32_t pushGlyph(std::vector<TextVertex> &verts, float x, float y, float width, float height,
                   const GlyphEntry &entry, const Color &color, const Matrix2D &transform,
                   const PaintClip &clip, const DisplayList &list) {
  const float corners_x[4] = {x, x + width, x + width, x};
  const float corners_y[4] = {y, y, y + height, y + height};
  const float u[4] = {entry.u0, entry.u1, entry.u1, entry.u0};
  const float v[4] = {entry.v0, entry.v0, entry.v1, entry.v1};
  return pushQuad(verts, corners_x, corners_y, u, v, color, transform, clip, list);
}

}  // namespace renderer_detail
}  // namespace xtgui
