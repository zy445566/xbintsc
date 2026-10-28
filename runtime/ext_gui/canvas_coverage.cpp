/*
 * xbintsc GUI engine — path coverage rasteriser.
 *
 * Fills a device-space polygon with 4 vertical sub-scanlines per pixel row and
 * analytic horizontal coverage, using the non-zero winding rule or the even-odd
 * rule. Two properties keep this cheap for a multi-element scene:
 *
 *   - edges are bucketed into an **active-edge list**, so a row only pays for
 *     the edges that actually cross it (the naive version scanned every edge of
 *     every sub-scanline);
 *   - only the **covered span of each row** is materialised, so a thin diagonal
 *     stroke costs its own pixels rather than its bounding box.
 *
 * The coverage is returned as concatenated row spans (`CoverageGrid`); blending
 * and clip-mask construction live in `canvas_raster.cpp` / `canvas_clip.cpp`.
 */

#include "canvas_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace xtgui {
namespace canvas_detail {

namespace {

constexpr int kSubSamples = 4;

/** One edge crossing on the current sub-scanline. */
struct Crossing {
  float x;
  int winding;
};

/** Add the horizontal span `[xa, xb)` into a coverage row whose first device
 * pixel is `rowOriginX`. */
void add_span(float *row, int rowOriginX, float xa, float xb, float weight, int clipX0,
              int clipX1) {
  if (xb <= xa) return;
  if (xa < (float)clipX0) xa = (float)clipX0;
  if (xb > (float)clipX1) xb = (float)clipX1;
  if (xb <= xa) return;
  int ia = (int)std::floor(xa);
  int ib = (int)std::ceil(xb) - 1;
  if (ib < ia) return;
  if (ia == ib) {
    row[ia - rowOriginX] += (xb - xa) * weight;
    return;
  }
  row[ia - rowOriginX] += ((float)(ia + 1) - xa) * weight;
  for (int x = ia + 1; x < ib; x++) row[x - rowOriginX] += weight;
  float last = xb - (float)ib;
  if (last > 0.0f) row[ib - rowOriginX] += last * weight;
}

/** Order crossings left to right. A scanline usually has a handful of them, so
 * an insertion sort beats `std::sort`'s setup cost; a pathological path still
 * falls back to the library sort. */
void sort_crossings(std::vector<Crossing> &crossings) {
  const size_t count = crossings.size();
  if (count > 16) {
    std::sort(crossings.begin(), crossings.end(),
              [](const Crossing &a, const Crossing &b) { return a.x < b.x; });
    return;
  }
  for (size_t i = 1; i < count; i++) {
    const Crossing value = crossings[i];
    size_t j = i;
    while (j > 0 && crossings[j - 1].x > value.x) {
      crossings[j] = crossings[j - 1];
      j -= 1;
    }
    crossings[j] = value;
  }
}

}  // namespace

void compute_coverage(int width, int height,
                      const std::vector<std::vector<CanvasPoint>> &loops, bool even_odd,
                      CoverageGrid *out) {
  out->x0 = out->y0 = out->x1 = out->y1 = 0;
  out->values.clear();
  out->row_offset.clear();
  out->row_x.clear();
  if (width <= 0 || height <= 0 || loops.empty()) return;
  bool havePoint = false;
  float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
  for (const std::vector<CanvasPoint> &loop : loops) {
    for (const CanvasPoint &point : loop) {
      if (!havePoint) {
        minX = maxX = point.x;
        minY = maxY = point.y;
        havePoint = true;
      } else {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
      }
    }
  }
  if (!havePoint) return;
  int x0 = std::max(0, (int)std::floor(minX));
  int x1 = std::min(width, (int)std::ceil(maxX));
  int y0 = std::max(0, (int)std::floor(minY));
  int y1 = std::min(height, (int)std::ceil(maxY));
  if (x1 <= x0 || y1 <= y0) return;

  out->x0 = x0;
  out->y0 = y0;
  out->x1 = x1;
  out->y1 = y1;
  out->row_offset.reserve((size_t)(y1 - y0) + 1);
  out->row_x.reserve((size_t)(y1 - y0));
  out->row_offset.push_back(0);
  const float weight = 1.0f / (float)kSubSamples;

  /* Build one edge record per non-horizontal segment and the row range it can
   * cross. */
  struct Edge {
    float x0 = 0.0f, y0 = 0.0f;  // upper endpoint
    float x1 = 0.0f, y1 = 0.0f;  // lower endpoint
    int winding = 0;
    int first_row = 0;
    int last_row = 0;  // exclusive
  };
  std::vector<Edge> edges;
  for (const std::vector<CanvasPoint> &loop : loops) {
    if (loop.size() < 3) continue;
    for (size_t i = 0; i < loop.size(); i++) {
      const CanvasPoint &p0 = loop[i];
      const CanvasPoint &p1 = loop[(i + 1) % loop.size()];
      if (p0.y == p1.y) continue;
      Edge edge;
      if (p0.y < p1.y) {
        edge.x0 = p0.x;
        edge.y0 = p0.y;
        edge.x1 = p1.x;
        edge.y1 = p1.y;
        edge.winding = 1;
      } else {
        edge.x0 = p1.x;
        edge.y0 = p1.y;
        edge.x1 = p0.x;
        edge.y1 = p0.y;
        edge.winding = -1;
      }
      edge.first_row = (int)std::floor(edge.y0);
      edge.last_row = (int)std::ceil(edge.y1);
      edges.push_back(edge);
    }
  }
  if (edges.empty()) return;
  std::sort(edges.begin(), edges.end(),
            [](const Edge &a, const Edge &b) { return a.first_row < b.first_row; });

  struct Span {
    float xa = 0.0f;
    float xb = 0.0f;
  };
  std::vector<Crossing> crossings;
  std::vector<Span> spans;
  std::vector<float> row_values;
  std::vector<int> active;
  size_t nextEdge = 0;
  for (int py = y0; py < y1; py++) {
    while (nextEdge < edges.size() && edges[nextEdge].first_row <= py) {
      active.push_back((int)nextEdge);
      nextEdge += 1;
    }
    /* Drop edges whose bottom is above this row. */
    size_t kept = 0;
    for (size_t i = 0; i < active.size(); i++) {
      const Edge &edge = edges[(size_t)active[i]];
      if (edge.last_row > py) active[kept++] = active[i];
    }
    active.resize(kept);
    spans.clear();
    for (int sample = 0; sample < kSubSamples; sample++) {
      float sy = (float)py + ((float)sample + 0.5f) / (float)kSubSamples;
      crossings.clear();
      for (int index : active) {
        const Edge &edge = edges[(size_t)index];
        if (sy < edge.y0 || sy >= edge.y1) continue;
        const float t = (sy - edge.y0) / (edge.y1 - edge.y0);
        Crossing crossing;
        crossing.x = edge.x0 + t * (edge.x1 - edge.x0);
        crossing.winding = edge.winding;
        crossings.push_back(crossing);
      }
      if (crossings.empty()) continue;
      sort_crossings(crossings);
      if (even_odd) {
        /* Every second crossing toggles the covered span, regardless of the
         * direction each contour is wound. */
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
          spans.push_back(Span{crossings[i].x, crossings[i + 1].x});
        }
        continue;
      }
      int winding = 0;
      float spanStart = 0.0f;
      for (const Crossing &crossing : crossings) {
        if (winding == 0) spanStart = crossing.x;
        winding += crossing.winding;
        if (winding == 0) spans.push_back(Span{spanStart, crossing.x});
      }
    }
    if (spans.empty()) {
      out->row_offset.push_back((uint32_t)out->values.size());
      out->row_x.push_back(0);
      continue;
    }
    float minSpan = spans[0].xa;
    float maxSpan = spans[0].xb;
    for (const Span &span : spans) {
      minSpan = std::min(minSpan, span.xa);
      maxSpan = std::max(maxSpan, span.xb);
    }
    const int rx0 = std::max(x0, (int)std::floor(minSpan));
    const int rx1 = std::min(x1, (int)std::ceil(maxSpan));
    if (rx1 <= rx0) {
      out->row_offset.push_back((uint32_t)out->values.size());
      out->row_x.push_back(0);
      continue;
    }
    row_values.assign((size_t)(rx1 - rx0), 0.0f);
    for (const Span &span : spans) {
      add_span(row_values.data(), rx0, span.xa, span.xb, weight, x0, x1);
    }
    out->row_x.push_back(rx0);
    out->values.insert(out->values.end(), row_values.begin(), row_values.end());
    out->row_offset.push_back((uint32_t)out->values.size());
  }
}

}  // namespace canvas_detail
}  // namespace xtgui
