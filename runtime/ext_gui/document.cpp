/*
 * xbintsc GUI engine — document model implementation.
 */

#include "document.h"

namespace xtgui {

void XtDocument::collectStyleText(const Node *node, std::string *out) const {
  if (node->isElement() && node->isTag("style")) {
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child->isText()) {
        *out += child->text;
        *out += "\n";
      }
    }
    return;
  }
  /* A <link rel="stylesheet"> would need I/O; skip it (and everything else). */
  for (const std::unique_ptr<Node> &child : node->children) {
    collectStyleText(child.get(), out);
  }
}

void XtDocument::load(const std::string &html, float viewportWidth, float viewportHeight) {
  root_ = xt_html_parse(html);
  hover_ = nullptr;
  focus_ = nullptr;
  styles_.clear();
  display_.clear();
  transitions_.clear();
  clock_ms_ = 0.0;
  std::string css;
  collectStyleText(root_.get(), &css);
  sheet_ = xt_css_parse_stylesheet(css);
  restyle(viewportWidth, viewportHeight);
}

void XtDocument::restyle(float viewportWidth, float viewportHeight) {
  viewportWidth_ = viewportWidth;
  viewportHeight_ = viewportHeight;
  if (root_ != nullptr) {
    MatchState state;
    state.hover = hover_;
    state.focus = focus_;
    xt_style_compute(root_.get(), sheet_, styles_, viewportWidth, viewportHeight, state);
    startTransitions();
    updateDisplay();
  }
}

namespace {

float easeProgress(float t, TimingFunction timing) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  switch (timing) {
    case TimingFunction::Linear:
      return t;
    case TimingFunction::EaseIn:
      return t * t * t;
    case TimingFunction::EaseOut: {
      float u = 1.0f - t;
      return 1.0f - u * u * u;
    }
    case TimingFunction::EaseInOut:
      return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);
    case TimingFunction::Ease:
    default:
      return t * t * (3.0f - 2.0f * t);  // smoothstep approximation of `ease`
  }
}

float mix(float a, float b, float t) { return a + (b - a) * t; }

const float *colorOf(const XtStyle &style, const std::string &property) {
  static const float transparent[4] = {0, 0, 0, 0};
  if (property == "background-color") {
    return style.has_background ? &style.background.r : transparent;
  }
  if (property == "border-color") {
    return style.has_border_color ? &style.border_color.r : transparent;
  }
  return &style.color.r;
}

float numberOf(const XtStyle &style, const std::string &property) {
  if (property == "border-radius") return style.border_radius;
  return 0.0f;
}

bool isNumberProperty(const std::string &property) { return property == "border-radius"; }

bool specMatches(const std::string &spec, const std::string &property) {
  return spec == "all" || spec == property;
}

void applyColor(XtStyle *style, const std::string &property, const float *rgba) {
  if (property == "background-color") {
    style->background = Color{rgba[0], rgba[1], rgba[2], rgba[3], true};
    style->has_background = rgba[3] > 0.0f;
  } else if (property == "border-color") {
    style->border_color = Color{rgba[0], rgba[1], rgba[2], rgba[3], true};
    style->has_border_color = rgba[3] > 0.0f;
  } else {
    style->color = Color{rgba[0], rgba[1], rgba[2], rgba[3], true};
  }
}

void applyNumber(XtStyle *style, const std::string &property, float value) {
  if (property == "border-radius") style->border_radius = value;
}

}  // namespace

void XtDocument::startTransitions() {
  if (display_.empty()) return;  // first style computation: nothing to animate from
  static const char *const kProperties[] = {"background-color", "color", "border-color", "border-radius"};
  for (auto &entry : styles_) {
    const Node *node = entry.first;
    const XtStyle &target = entry.second;
    if (target.transitions.empty()) continue;
    auto prev_it = display_.find(node);
    if (prev_it == display_.end()) continue;
    const XtStyle &current = prev_it->second;
    for (const TransitionSpec &spec : target.transitions) {
      if (spec.duration <= 0.0f) continue;
      for (const char *property : kProperties) {
        if (!specMatches(spec.property, property)) continue;
        ActiveTransition running;
        running.node = node;
        running.property = property;
        running.timing = spec.timing;
        running.duration_ms = spec.duration * 1000.0f;
        running.delay_ms = spec.delay * 1000.0f;
        running.start_ms = (float)clock_ms_ + running.delay_ms;
        if (isNumberProperty(property)) {
          float from = numberOf(current, property);
          float to = numberOf(target, property);
          if (from == to) continue;
          running.is_color = false;
          running.from_number = from;
          running.to_number = to;
        } else {
          const float *from = colorOf(current, property);
          const float *to = colorOf(target, property);
          if (from[0] == to[0] && from[1] == to[1] && from[2] == to[2] && from[3] == to[3]) continue;
          running.is_color = true;
          for (int i = 0; i < 4; i++) {
            running.from_color[i] = from[i];
            running.to_color[i] = to[i];
          }
        }
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

void XtDocument::updateDisplay() {
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
    XtStyle &style = node_it->second;
    if (transition.is_color) {
      float rgba[4];
      for (int c = 0; c < 4; c++) rgba[c] = mix(transition.from_color[c], transition.to_color[c], eased);
      applyColor(&style, transition.property, rgba);
    } else {
      applyNumber(&style, transition.property, mix(transition.from_number, transition.to_number, eased));
    }
    if (progress >= 1.0f) {
      transitions_.erase(transitions_.begin() + (long)i);
    } else {
      i++;
    }
  }
  layout_.compute(root_.get(), display_, viewportWidth_, viewportHeight_);
}

bool XtDocument::advance(double delta_ms) {
  clock_ms_ += delta_ms;
  if (transitions_.empty()) return false;
  updateDisplay();
  return true;
}

bool XtDocument::setHover(const Node *node) {
  if (hover_ == node) return false;
  hover_ = node;
  restyle(viewportWidth_, viewportHeight_);
  return true;
}

bool XtDocument::setFocus(const Node *node) {
  if (focus_ == node) return false;
  focus_ = node;
  restyle(viewportWidth_, viewportHeight_);
  return true;
}

const XtStyle *XtDocument::styleOf(const Node *node) const {
  auto it = display_.find(node);
  if (it != display_.end()) return &it->second;
  it = styles_.find(node);
  return (it == styles_.end()) ? nullptr : &it->second;
}

const LayoutBox *XtDocument::boxOf(const Node *node) const { return layout_.find(node); }

std::vector<const Node *> XtDocument::querySelectorAll(const std::string &selectorText) const {
  std::vector<const Node *> matches;
  if (root_ == nullptr) return matches;
  std::vector<ComplexSelector> selectors = xt_css_parse_selector_list(selectorText);
  if (selectors.empty()) return matches;
  std::vector<const Node *> stack;
  stack.push_back(root_.get());
  while (!stack.empty()) {
    const Node *node = stack.back();
    stack.pop_back();
    if (node->isElement()) {
      for (const ComplexSelector &selector : selectors) {
        if (xt_css_match(node, selector)) {
          matches.push_back(node);
          break;
        }
      }
    }
    /* Push children in reverse so the traversal stays in document order. */
    for (size_t i = node->children.size(); i-- > 0;) {
      stack.push_back(node->children[i].get());
    }
  }
  return matches;
}

const Node *XtDocument::querySelector(const std::string &selectorText) const {
  std::vector<const Node *> matches = querySelectorAll(selectorText);
  return matches.empty() ? nullptr : matches.front();
}

std::string XtDocument::toDebugString() const {
  return root_ == nullptr ? std::string() : xt_dom_to_string(root_.get());
}

}  // namespace xtgui
