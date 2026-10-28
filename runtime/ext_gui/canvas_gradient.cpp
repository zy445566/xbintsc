/*
 * xbintsc GUI engine — CanvasGradient objects.
 *
 * A gradient is owned by the canvas that created it (`Canvas2D::gradients_`),
 * so a handle that outlives the document resolves to no gradient instead of a
 * dangling pointer. Colour stops stay live: a later `addColorStop` is visible
 * to every fill that already uses the gradient, which is the canvas semantics
 * the DOM layer exposes.
 */

#include <algorithm>
#include <atomic>
#include <cmath>

#include "canvas.h"

namespace xtgui {

namespace {

std::atomic<uint64_t> g_next_gradient_id{1};

float clamp_unit(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

}  // namespace

CanvasGradient::CanvasGradient(uint64_t id, Type type, float x0, float y0, float x1, float y1,
                               float r0, float r1)
    : id_(id), type_(type), x0_(x0), y0_(y0), x1_(x1), y1_(y1), r0_(r0), r1_(r1) {}

void CanvasGradient::add_color_stop(float offset, const float color[4]) {
  Stop stop;
  stop.offset = clamp_unit(offset);
  for (int i = 0; i < 4; i++) stop.color[i] = color[i];
  auto position = std::upper_bound(stops_.begin(), stops_.end(), stop,
                                   [](const Stop &a, const Stop &b) { return a.offset < b.offset; });
  stops_.insert(position, stop);
}

bool CanvasGradient::color_at(float x, float y, float out[4]) const {
  if (stops_.empty()) {
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    return true;
  }
  float t = 0.0f;
  if (type_ == Type::Linear) {
    const float dx = x1_ - x0_;
    const float dy = y1_ - y0_;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared > 1e-9f) {
      t = ((x - x0_) * dx + (y - y0_) * dy) / lengthSquared;
    }
  } else {
    /* Two-circle radial gradient (CSS/Canvas): find the smallest t >= 0 for
     * which the point lies on the circle
     *     centre = c0 + t * (c1 - c0),  radius = r0 + t * (r1 - r0).
     * This reduces to a quadratic, and it makes a non-concentric inner circle
     * produce the cone a browser draws. */
    const float dx = x1_ - x0_;
    const float dy = y1_ - y0_;
    const float qx = x - x0_;
    const float qy = y - y0_;
    const float radiusDelta = r1_ - r0_;
    const float innerDistance = std::sqrt(qx * qx + qy * qy);
    const float a = dx * dx + dy * dy - radiusDelta * radiusDelta;
    const float b = -2.0f * (qx * dx + qy * dy + r0_ * radiusDelta);
    const float c = qx * qx + qy * qy - r0_ * r0_;
    if (innerDistance <= std::fabs(r0_) + 1e-4f) {
      /* Inside the inner circle every ray starts at t = 0. */
      t = 0.0f;
    } else if (std::fabs(a) < 1e-6f) {
      if (std::fabs(b) > 1e-6f) {
        t = -c / b;
      } else {
        t = innerDistance < std::fabs(r0_) ? 0.0f : 1.0f;
      }
    } else {
      const float discriminant = b * b - 4.0f * a * c;
      if (discriminant < 0.0f) {
        t = innerDistance < std::fabs(r0_) ? 0.0f : 1.0f;
      } else {
        const float root = std::sqrt(discriminant);
        const float t0 = (-b - root) / (2.0f * a);
        const float t1 = (-b + root) / (2.0f * a);
        const float low = std::min(t0, t1);
        const float high = std::max(t0, t1);
        if (low >= 0.0f) {
          t = low;
        } else if (high >= 0.0f) {
          t = high;
        } else {
          t = innerDistance < std::fabs(r0_) ? 0.0f : 1.0f;
        }
      }
    }
  }
  t = clamp_unit(t);
  if (t <= stops_.front().offset) {
    for (int i = 0; i < 4; i++) out[i] = stops_.front().color[i];
    return true;
  }
  if (t >= stops_.back().offset) {
    for (int i = 0; i < 4; i++) out[i] = stops_.back().color[i];
    return true;
  }
  for (size_t i = 1; i < stops_.size(); i++) {
    const Stop &a = stops_[i - 1];
    const Stop &b = stops_[i];
    if (t > b.offset) continue;
    const float span = b.offset - a.offset;
    const float u = span > 1e-9f ? (t - a.offset) / span : 1.0f;
    for (int channel = 0; channel < 4; channel++) {
      out[channel] = a.color[channel] + (b.color[channel] - a.color[channel]) * u;
    }
    return true;
  }
  for (int i = 0; i < 4; i++) out[i] = stops_.back().color[i];
  return true;
}

CanvasGradient *Canvas2D::create_linear_gradient(float x0, float y0, float x1, float y1) {
  auto gradient = std::make_unique<CanvasGradient>(g_next_gradient_id.fetch_add(1),
                                                   CanvasGradient::Type::Linear, x0, y0, x1, y1,
                                                   0.0f, 1.0f);
  CanvasGradient *raw = gradient.get();
  gradients_[raw->id()] = std::move(gradient);
  return raw;
}

CanvasGradient *Canvas2D::create_radial_gradient(float x0, float y0, float r0, float x1, float y1,
                                                 float r1) {
  auto gradient = std::make_unique<CanvasGradient>(g_next_gradient_id.fetch_add(1),
                                                   CanvasGradient::Type::Radial, x0, y0, x1, y1, r0,
                                                   r1);
  CanvasGradient *raw = gradient.get();
  gradients_[raw->id()] = std::move(gradient);
  return raw;
}

CanvasGradient *Canvas2D::find_gradient(uint64_t id) const {
  auto found = gradients_.find(id);
  return found == gradients_.end() ? nullptr : found->second.get();
}

}  // namespace xtgui
