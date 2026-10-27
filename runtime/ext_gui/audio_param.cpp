/*
 * xbintsc GUI engine — AudioParam automation curves.
 *
 * A param is a base value plus a time-ordered event list. Every accessor on the
 * page's AudioParam (value, setValueAtTime, the ramps, setTargetAtTime,
 * cancel*) appends an event; the mixer evaluates the list per sample so the
 * curve keeps moving even when no JS calls happen between frames.
 *
 * The timeline belongs to the mixer and is only reached with the mixer's lock
 * held, so it needs no lock of its own.
 */

#include "audio_mixer.h"

#include <cmath>

namespace xtgui {

void AudioParamState::reset(double value) {
  base_ = value;
  events_.clear();
}

void AudioParamState::insert(Event event) {
  for (size_t i = 0; i < events_.size(); i++) {
    if (std::fabs(events_[i].time - event.time) < 1e-12) {
      events_[i] = event;
      return;
    }
    if (events_[i].time > event.time) {
      events_.insert(events_.begin() + static_cast<std::ptrdiff_t>(i), event);
      return;
    }
  }
  events_.push_back(event);
}

void AudioParamState::set_value(double value, double time) {
  if (events_.empty()) base_ = value;
  insert({time, kEventSet, value, 0.0});
}

void AudioParamState::set_value_at_time(double value, double time) {
  insert({time, kEventSet, value, 0.0});
}

void AudioParamState::linear_ramp(double value, double time) {
  insert({time, kEventLinear, value, 0.0});
}

void AudioParamState::exponential_ramp(double value, double time) {
  insert({time, kEventExponential, value, 0.0});
}

void AudioParamState::set_target(double value, double time, double time_constant) {
  insert({time, kEventTarget, value, time_constant});
}

void AudioParamState::cancel_scheduled(double time) {
  while (!events_.empty() && events_.back().time >= time) events_.pop_back();
}

void AudioParamState::cancel_and_hold(double time) {
  double value = value_at(time);
  cancel_scheduled(time);
  insert({time, kEventSet, value, 0.0});
}

double AudioParamState::evaluate(size_t index, double time) const {
  const Event &event = events_[index];
  if (event.kind == kEventSet) return event.value;
  if (event.kind == kEventTarget) {
    double start = index > 0 ? evaluate(index - 1, events_[index - 1].time) : base_;
    if (event.time_constant <= 0.0) return event.value;
    double decay = std::exp(-(time - event.time) / event.time_constant);
    return event.value + (start - event.value) * decay;
  }
  /* A ramp that has already ended holds its target value. */
  return event.value;
}

double AudioParamState::value_at(double time) const {
  if (events_.empty() || time < events_.front().time) return base_;
  size_t index = 0;
  while (index + 1 < events_.size() && events_[index + 1].time <= time) index++;
  if (index + 1 < events_.size()) {
    const Event &next = events_[index + 1];
    if ((next.kind == kEventLinear || next.kind == kEventExponential) && time < next.time) {
      double start_time = events_[index].time;
      double start_value = evaluate(index, start_time);
      double span = next.time - start_time;
      double fraction = span > 0.0 ? (time - start_time) / span : 1.0;
      if (next.kind == kEventLinear) {
        return start_value + (next.value - start_value) * fraction;
      }
      double from = std::fabs(start_value) < 1e-9 ? 1e-6 : start_value;
      double to = std::fabs(next.value) < 1e-9 ? 1e-6 : next.value;
      return from * std::pow(to / from, fraction);
    }
  }
  return evaluate(index, time);
}

}  // namespace xtgui
