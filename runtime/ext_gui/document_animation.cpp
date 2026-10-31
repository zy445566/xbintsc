/*
 * xbintsc GUI engine — CSS transitions and `@keyframes` animations.
 *
 * The animatable value switch lives in `animation_values.cpp`; this unit owns
 * the document clock: which transitions/animations are active, how local time
 * maps to an iteration progress, and how the displayed style map is rebuilt
 * before layout.
 */

#include "document.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "style_internal.h"

namespace xtgui {

namespace {

/** A cubic Bézier with endpoints (0,0) and (1,1), evaluated at `t` on the *x*
 * axis. CSS uses this for `cubic-bezier()` and for its `ease*` keywords. The
 * solver is a few bisection steps: timing functions are evaluated at most once
 * per animated property per frame, so a closed form is not worth the edge cases
 * of a degenerate curve. */
float cubicBezier(float t, float x1, float y1, float x2, float y2) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  auto sample = [](float u, float p1, float p2) {
    float v = 1.0f - u;
    return 3.0f * v * v * u * p1 + 3.0f * v * u * u * p2 + u * u * u;
  };
  float low = 0.0f;
  float high = 1.0f;
  float u = t;
  for (int i = 0; i < 24; i++) {
    float x = sample(u, x1, x2);
    if (x < t) low = u;
    else high = u;
    u = (low + high) * 0.5f;
  }
  return sample(u, y1, y2);
}

float easeProgress(float t, const TimingFunction &timing) {
  if (t <= 0.0f) {
    if (timing.kind == TimingFunction::Kind::StepsStart) {
      float steps = timing.steps > 0 ? (float)timing.steps : 1.0f;
      return 1.0f / steps;
    }
    return 0.0f;
  }
  if (t >= 1.0f) return 1.0f;
  switch (timing.kind) {
    case TimingFunction::Kind::Linear:
      return t;
    case TimingFunction::Kind::EaseIn:
      return cubicBezier(t, 0.42f, 0.0f, 1.0f, 1.0f);
    case TimingFunction::Kind::EaseOut:
      return cubicBezier(t, 0.0f, 0.0f, 0.58f, 1.0f);
    case TimingFunction::Kind::EaseInOut:
      return cubicBezier(t, 0.42f, 0.0f, 0.58f, 1.0f);
    case TimingFunction::Kind::CubicBezier:
      return cubicBezier(t, timing.x1, timing.y1, timing.x2, timing.y2);
    case TimingFunction::Kind::StepsStart: {
      float steps = timing.steps > 0 ? (float)timing.steps : 1.0f;
      float value = std::ceil(t * steps) / steps;
      return value > 1.0f ? 1.0f : value;
    }
    case TimingFunction::Kind::StepsEnd: {
      float steps = timing.steps > 0 ? (float)timing.steps : 1.0f;
      if (t >= 1.0f) return 1.0f;
      return std::floor(t * steps) / steps;
    }
    case TimingFunction::Kind::Ease:
    default:
      return cubicBezier(t, 0.25f, 0.1f, 0.25f, 1.0f);
  }
}

bool timingEqual(const TimingFunction &a, const TimingFunction &b) {
  return a.kind == b.kind && a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2 &&
         a.steps == b.steps;
}

bool animationSpecEqual(const AnimationSpec &a, const AnimationSpec &b) {
  return a.name == b.name && a.duration == b.duration && a.delay == b.delay &&
         a.iterations == b.iterations && a.direction == b.direction && a.fill == b.fill &&
         a.paused == b.paused && timingEqual(a.timing, b.timing);
}

bool specMatches(const std::string &spec, const std::string &property) {
  return spec == "all" || spec == property;
}

bool directionReversed(const AnimationSpec &spec, float iteration) {
  int parity = (int)std::fmod(std::floor(iteration), 2.0f);
  switch (spec.direction) {
    case AnimationDirection::Normal:
      return false;
    case AnimationDirection::Reverse:
      return true;
    case AnimationDirection::Alternate:
      return parity != 0;
    case AnimationDirection::AlternateReverse:
      return parity == 0;
  }
  return false;
}

float directionProgress(const AnimationSpec &spec, float iteration, float fraction) {
  return directionReversed(spec, iteration) ? 1.0f - fraction : fraction;
}

struct AnimationProgress {
  bool apply = false;
  float progress = 0.0f;
};

/** Resolve the animation's local time to a progress in [0,1], honouring the
 * iteration count, direction and fill mode. The timing function is applied per
 * keyframe interval by the caller, as CSS specifies. */
AnimationProgress animationProgress(const AnimationSpec &spec, float local_ms) {
  AnimationProgress result;
  if (spec.name == "none" || spec.iterations == 0.0f) return result;
  const float duration = spec.duration * 1000.0f;
  const bool backwards =
      spec.fill == AnimationFillMode::Backwards || spec.fill == AnimationFillMode::Both;
  const bool forwards =
      spec.fill == AnimationFillMode::Forwards || spec.fill == AnimationFillMode::Both;
  if (local_ms < 0.0f) {
    if (!backwards) return result;
    result.apply = true;
    result.progress = directionProgress(spec, 0.0f, 0.0f);
    return result;
  }
  if (duration <= 0.0f) {
    if (!forwards) return result;
    result.apply = true;
    result.progress = directionProgress(spec, 0.0f, 1.0f);
    return result;
  }
  const bool infinite = spec.iterations < 0.0f;
  const float iterations = infinite ? 0.0f : spec.iterations;
  float t = local_ms / duration;
  if (!infinite && t >= iterations) {
    if (!forwards) return result;
    float last = std::ceil(iterations) - 1.0f;
    if (last < 0.0f) last = 0.0f;
    float fraction = iterations - last;
    result.apply = true;
    result.progress = directionProgress(spec, last, fraction);
    return result;
  }
  if (spec.paused) t = 0.0f;
  float index = std::floor(t);
  float fraction = t - index;
  result.apply = true;
  result.progress = directionProgress(spec, index, fraction);
  return result;
}

void applyKeyframeDeclarations(XtStyle *style, const std::vector<Declaration> &declarations,
                               const ApplyContext &context) {
  for (const Declaration &declaration : declarations) {
    /* Custom properties and animation bookkeeping cannot change a keyframe
     * result; the engine's own value parser handles everything else. */
    if (declaration.property.size() > 2 && declaration.property[0] == '-' &&
        declaration.property[1] == '-')
      continue;
    if (declaration.property.compare(0, 9, "animation") == 0) continue;
    xt_style_apply_declaration(style, declaration.property, declaration.value, context);
  }
}

}  // namespace

void XtDocument::startTransitions() {
  if (display_.empty()) return;  // first style computation: nothing to animate from
  /* Without a `transition` declaration anywhere in the sheet (or in an inline
   * style) no transition can start, so skip the O(nodes) scan `restyle` would
   * otherwise do every time. */
  if (!has_transitions_) return;
  const std::vector<std::string> &properties = animatableProperties();
  for (auto &entry : styles_) {
    const Node *node = entry.first;
    const XtStyle &target = entry.second;
    /* Generated boxes are rebuilt on every restyle; a transition keyed by one
     * would address a node that no longer exists. */
    if (node->is_pseudo) continue;
    if (target.transitions.empty()) continue;
    auto prev_it = display_.find(node);
    if (prev_it == display_.end()) continue;
    const XtStyle &current = prev_it->second;
    for (const TransitionSpec &spec : target.transitions) {
      if (spec.duration <= 0.0f) continue;
      for (const std::string &property : properties) {
        if (!specMatches(spec.property, property)) continue;
        AnimatableValue from;
        AnimatableValue to;
        if (!readAnimatable(current, property, &from)) continue;
        if (!readAnimatable(target, property, &to)) continue;
        if (animatableEqual(from, to)) continue;
        AnimatableValue probe;
        /* Values that cannot be mixed (a unit change, a different shadow list
         * shape) snap instead of animating, as in CSS's discrete fallback. */
        if (!interpolateAnimatable(from, to, 0.5f, &probe)) continue;
        ActiveTransition running;
        running.node = node;
        running.property = property;
        running.from = from;
        running.to = to;
        running.timing = spec.timing;
        running.duration_ms = spec.duration * 1000.0f;
        running.delay_ms = spec.delay * 1000.0f;
        running.start_ms = (float)clock_ms_ + running.delay_ms;
        /* Retarget an existing transition (mid-flight) instead of stacking. */
        bool replaced = false;
        for (ActiveTransition &existing : transitions_) {
          if (existing.node == node && existing.property == property) {
            existing = running;
            replaced = true;
            break;
          }
        }
        if (!replaced) transitions_.push_back(running);
      }
    }
  }
}

void XtDocument::startAnimations() {
  if (!has_animations_) {
    animations_.clear();
    return;
  }
  std::vector<ActiveAnimation> active;
  for (auto &entry : styles_) {
    const Node *node = entry.first;
    for (const AnimationSpec &spec : entry.second.animations) {
      if (spec.name.empty() || spec.name == "none") continue;
      const ActiveAnimation *existing = nullptr;
      for (const ActiveAnimation &candidate : animations_) {
        if (candidate.node == node && candidate.name == spec.name) {
          existing = &candidate;
          break;
        }
      }
      ActiveAnimation running;
      running.node = node;
      running.name = spec.name;
      running.spec = spec;
      running.start_ms =
          (existing != nullptr && animationSpecEqual(existing->spec, spec)) ? existing->start_ms
                                                                            : (float)clock_ms_;
      active.push_back(std::move(running));
    }
  }
  animations_ = std::move(active);
}

bool declarationsAffect(const std::vector<Declaration> &declarations,
                        const std::string &property) {
  for (const Declaration &declaration : declarations) {
    const std::string &name = declaration.property;
    if (name == property) return true;
    if (name == "background" && property.compare(0, 11, "background-") == 0) return true;
    if (name == "border" && property.compare(0, 7, "border-") == 0) return true;
    if (name == "margin" && property.compare(0, 7, "margin-") == 0) return true;
    if (name == "padding" && property.compare(0, 8, "padding-") == 0) return true;
    if (name == "inset" &&
        (property == "top" || property == "right" || property == "bottom" ||
         property == "left"))
      return true;
    if (name == "gap" && (property == "row-gap" || property == "column-gap")) return true;
    if (name == "flex" && property == "flex-basis") return true;
    if (name == "font" && property == "font-size") return true;
  }
  return false;
}

void XtDocument::applyAnimations(std::unordered_map<const Node *, XtStyle> *out) {
  if (animations_.empty() || out == nullptr) return;
  const std::vector<std::string> &properties = animatableProperties();
  for (const ActiveAnimation &animation : animations_) {
    auto node_it = out->find(animation.node);
    if (node_it == out->end()) continue;
    const KeyframesRule *rule = xt_css_find_keyframes(sheet_, animation.name);
    if (rule == nullptr || rule->keyframes.empty()) continue;
    AnimationProgress progress = animationProgress(
        animation.spec, (float)clock_ms_ - animation.start_ms - animation.spec.delay * 1000.0f);
    if (!progress.apply) continue;

    ApplyContext context;
    const XtStyle *base_style = &node_it->second;
    auto target_it = styles_.find(animation.node);
    if (target_it != styles_.end()) base_style = &target_it->second;
    context.rootFontSize = 16.0f;
    context.parentFontSize = base_style->font_size;
    context.viewportWidth = viewportWidth_;
    context.viewportHeight = viewportHeight_;
    /* One style per keyframe step: applying the declarations once lets the
     * per-property search below see through shorthands such as `background`.
     * Missing keyframe values come from the cascade target, not from a
     * transition that may be mid-flight on another property. */
    std::vector<XtStyle> step_styles(rule->keyframes.size(), *base_style);
    for (size_t i = 0; i < rule->keyframes.size(); i++) {
      applyKeyframeDeclarations(&step_styles[i], rule->keyframes[i].declarations, context);
    }

    for (const std::string &property : properties) {
      /* Missing keyframes are the underlying style, not the adjacent step that
       * happens to declare other properties. A transform declared at 0% and
       * 100% therefore animates across the whole run even if 50% only changes
       * `opacity`. */
      long lower = -1;
      long upper = -1;
      for (size_t i = 0; i < rule->keyframes.size(); i++) {
        if (!declarationsAffect(rule->keyframes[i].declarations, property)) continue;
        if (rule->keyframes[i].offset <= progress.progress) lower = (long)i;
        if (upper < 0 && rule->keyframes[i].offset >= progress.progress) upper = (long)i;
      }
      if (lower < 0 && upper < 0) continue;
      const XtStyle &from_style = lower >= 0 ? step_styles[(size_t)lower] : *base_style;
      const XtStyle &to_style = upper >= 0 ? step_styles[(size_t)upper] : *base_style;
      float segment_start = lower >= 0 ? rule->keyframes[(size_t)lower].offset : 0.0f;
      float segment_end = upper >= 0 ? rule->keyframes[(size_t)upper].offset : 1.0f;
      float segment = 1.0f;
      float span = segment_end - segment_start;
      if (span > 0.00001f) {
        segment = (progress.progress - segment_start) / span;
        if (segment < 0.0f) segment = 0.0f;
        if (segment > 1.0f) segment = 1.0f;
      }
      float eased = easeProgress(segment, animation.spec.timing);

      AnimatableValue from;
      AnimatableValue to;
      if (!readAnimatable(from_style, property, &from)) continue;
      if (!readAnimatable(to_style, property, &to)) continue;
      AnimatableValue current;
      bool has_current = readAnimatable(node_it->second, property, &current);
      if (animatableEqual(from, to) && has_current && animatableEqual(current, from)) continue;
      AnimatableValue mixed;
      if (!interpolateAnimatable(from, to, eased, &mixed)) {
        mixed = eased < 0.5f ? from : to;
      }
      writeAnimatable(&node_it->second, property, mixed);
    }
  }
}

/** True while an animation can still change the painted style. A finished
 * `forwards`/`both` animation stays in `animations_` (its value must survive a
 * later restyle) but no longer makes every frame dirty. */
bool animationRunning(const ActiveAnimation &animation, double clock_ms) {
  if (animation.spec.paused) return false;
  if (animation.spec.name == "none" || animation.spec.iterations == 0.0f) return false;
  if (animation.spec.iterations < 0.0f) return true;
  double end = (double)animation.start_ms + (double)animation.spec.delay * 1000.0 +
               (double)animation.spec.duration * 1000.0 * (double)animation.spec.iterations;
  return clock_ms < end;
}

void XtDocument::updateDisplay() {
  /* Without a transition or animation the displayed style *is* the cascade
   * result: skip the per-node deep copy of the whole style map (and keep
   * `display_` empty, which `styleOf` already falls back from). */
  if (!has_transitions_ && !has_animations_) {
    if (!transitions_.empty()) transitions_.clear();
    if (!animations_.empty()) animations_.clear();
    if (!display_.empty()) display_.clear();
    layout_.compute(root_.get(), styles_, viewportWidth_, viewportHeight_, &mediaSizes_,
                    &canvasInfo_);
    return;
  }
  display_ = styles_;
  for (size_t i = 0; i < transitions_.size();) {
    ActiveTransition &transition = transitions_[i];
    auto node_it = display_.find(transition.node);
    if (node_it == display_.end()) {
      transitions_.erase(transitions_.begin() + (long)i);
      continue;
    }
    float elapsed = (float)clock_ms_ - transition.start_ms;
    float progress = transition.duration_ms > 0.0f ? elapsed / transition.duration_ms : 1.0f;
    if (progress > 1.0f) progress = 1.0f;
    float eased = easeProgress(progress, transition.timing);
    AnimatableValue mixed;
    if (interpolateAnimatable(transition.from, transition.to, eased, &mixed)) {
      writeAnimatable(&node_it->second, transition.property, mixed);
    }
    if (progress >= 1.0f) {
      transitions_.erase(transitions_.begin() + (long)i);
    } else {
      i++;
    }
  }
  applyAnimations(&display_);
  layout_.compute(root_.get(), display_, viewportWidth_, viewportHeight_, &mediaSizes_,
                  &canvasInfo_);
}

bool XtDocument::advance(double delta_ms) {
  const double previous = clock_ms_;
  clock_ms_ += delta_ms;
  if (transitions_.empty() && animations_.empty()) return false;
  bool running = !transitions_.empty();
  bool just_finished = false;
  for (const ActiveAnimation &animation : animations_) {
    if (animationRunning(animation, clock_ms_)) {
      running = true;
      continue;
    }
    if (animation.spec.paused || animation.spec.iterations < 0.0f) continue;
    double end = (double)animation.start_ms + (double)animation.spec.delay * 1000.0 +
                 (double)animation.spec.duration * 1000.0 * (double)animation.spec.iterations;
    if (previous < end && clock_ms_ >= end) just_finished = true;
  }
  if (!running && !just_finished) return false;
  updateDisplay();
  return true;
}


}  // namespace xtgui
