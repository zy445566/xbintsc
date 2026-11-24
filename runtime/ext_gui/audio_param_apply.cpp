/*
 * xbintsc GUI engine — queued AudioParam scheduling.
 *
 * The mixer renders a 128-frame quantum while holding its graph mutex, and a
 * reverb convolver makes that quantum take up to a few milliseconds. A page
 * schedules its sounds from `requestAnimationFrame` (`gain.value = …`,
 * `setTargetAtTime`, the ramps, …); taking the same mutex made every one of
 * those calls wait for the current quantum, which showed up as main-thread
 * frame time — on the Saltline scene it was the single largest audio cost.
 *
 * The setters below therefore *queue* a command instead: the audio thread
 * applies queued commands at the next quantum boundary, in FIFO order, with the
 * mutex already held. `param_value` flushes first because a getter must observe
 * a just-written value; ordering between queued commands is preserved.
 */

#include "audio_mixer.h"

#include <utility>

namespace xtgui {

void AudioMixer::post_command(std::function<void()> command) {
  std::lock_guard<std::mutex> lock(command_mutex_);
  commands_.push_back(std::move(command));
}

void AudioMixer::drain_commands_locked() {
  std::vector<std::function<void()>> pending;
  {
    std::lock_guard<std::mutex> lock(command_mutex_);
    pending.swap(commands_);
  }
  for (std::function<void()> &command : pending) command();
}

namespace {

double audio_now(const AudioMixer &mixer) {
  return mixer.current_time();
}

}  // namespace

double AudioMixer::param_value(uint64_t node, int index) {
  /* A node created on this thread is not in `nodes_` until the audio thread
   * inserts it; read its defaults from the pending table instead of flushing
   * the queue (which would wait for the render lock). */
  {
    std::lock_guard<std::mutex> pending_lock(pending_mutex_);
    auto found = pending_nodes_.find(node);
    if (found != pending_nodes_.end()) {
      const AudioNode &target = *found->second;
      if (index < 0 || index >= target.param_count) return 0.0;
      return target.params[index].value_at(audio_now(*this));
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr || index < 0 || index >= target->param_count) return 0.0;
  return target->params[index].value_at(audio_now(*this));
}

void AudioMixer::param_set(uint64_t node, int index, double value) {
  post_command([this, node, index, value]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr || index < 0 || index >= target->param_count) return;
    target->params[index].set_value(value, audio_now(*this));
  });
}

void AudioMixer::param_set_at(uint64_t node, int index, double value, double time) {
  post_command([this, node, index, value, time]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr || index < 0 || index >= target->param_count) return;
    target->params[index].set_value_at_time(value, time);
  });
}

void AudioMixer::param_linear_ramp(uint64_t node, int index, double value, double time) {
  post_command([this, node, index, value, time]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr || index < 0 || index >= target->param_count) return;
    target->params[index].linear_ramp(value, time);
  });
}

void AudioMixer::param_exponential_ramp(uint64_t node, int index, double value, double time) {
  post_command([this, node, index, value, time]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr || index < 0 || index >= target->param_count) return;
    target->params[index].exponential_ramp(value, time);
  });
}

void AudioMixer::param_set_target(uint64_t node, int index, double value, double time,
                                  double time_constant) {
  post_command([this, node, index, value, time, time_constant]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr || index < 0 || index >= target->param_count) return;
    target->params[index].set_target(value, time, time_constant);
  });
}

void AudioMixer::param_cancel(uint64_t node, int index, double time, bool hold) {
  post_command([this, node, index, time, hold]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr || index < 0 || index >= target->param_count) return;
    if (hold) {
      target->params[index].cancel_and_hold(time);
    } else {
      target->params[index].cancel_scheduled(time);
    }
  });
}

}  // namespace xtgui
