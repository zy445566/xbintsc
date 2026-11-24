/*
 * xbintsc GUI engine — queued graph mutations.
 *
 * `connect`, `disconnect`, the source start/stop pair and the per-node setup
 * calls mutate the graph from the page's thread. Taking the mixer's render
 * mutex here made a sound effect's setup wait for the current quantum (up to a
 * few milliseconds with a convolver send), which dominated the main thread's
 * audio time on a scene that spawns effects while it renders. They queue a
 * command instead; the audio thread applies it at the next quantum boundary,
 * in the same FIFO as the AudioParam updates, with the mutex already held.
 */

#include "audio_mixer.h"

#include <algorithm>
#include <utility>

namespace xtgui {

namespace {

double clamp_delay_seconds(double value, double low, double high) {
  if (!(value > low)) return low;
  if (value > high) return high;
  return value;
}

}  // namespace

void AudioMixer::connect(uint64_t source, uint64_t target) {
  post_command([this, source, target]() {
    AudioNode *from = node_locked(source);
    AudioNode *to = node_locked(target);
    if (from == nullptr || to == nullptr) return;
    for (uint64_t id : to->inputs) {
      if (id == source) return;
    }
    to->inputs.push_back(source);
  });
}

void AudioMixer::disconnect(uint64_t source, uint64_t target) {
  post_command([this, source, target]() {
    AudioNode *to = node_locked(target);
    if (to == nullptr) return;
    to->inputs.erase(std::remove(to->inputs.begin(), to->inputs.end(), source), to->inputs.end());
  });
}

void AudioMixer::disconnect_all(uint64_t source) {
  post_command([this, source]() {
    for (auto &entry : nodes_) {
      std::vector<uint64_t> &inputs = entry.second->inputs;
      inputs.erase(std::remove(inputs.begin(), inputs.end(), source), inputs.end());
    }
  });
}

void AudioMixer::set_osc_type(uint64_t node, AudioOscType type) {
  post_command([this, node, type]() {
    AudioNode *target = node_locked(node);
    if (target != nullptr) target->osc_type = type;
  });
}

void AudioMixer::set_biquad_type(uint64_t node, AudioBiquadType type) {
  post_command([this, node, type]() {
    AudioNode *target = node_locked(node);
    if (target != nullptr) target->biquad_type = type;
  });
}

void AudioMixer::set_delay_max(uint64_t node, double seconds) {
  post_command([this, node, seconds]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr) return;
    const double clamped = clamp_delay_seconds(seconds, 0.001, 60.0);
    target->delay_max = clamped;
    target->delay_size = static_cast<size_t>(clamped * kAudioMixRate) + 2;
    target->delay_write = 0;
    for (int channel = 0; channel < kAudioMixChannels; channel++) {
      target->delay[channel].assign(target->delay_size, 0.0f);
    }
  });
}

void AudioMixer::set_buffer(uint64_t node, std::shared_ptr<AudioClip> clip, bool loop,
                            double loop_start, double loop_end) {
  post_command([this, node, clip = std::move(clip), loop, loop_start, loop_end]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr) return;
    target->clip = clip;
    target->loop = loop;
    target->loop_start = loop_start;
    target->loop_end = loop_end;
  });
}

void AudioMixer::start_source(uint64_t node, double when, double offset, double duration) {
  post_command([this, node, when, offset, duration]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr) return;
    const double now = static_cast<double>(frames_rendered_.load(std::memory_order_relaxed)) /
                       kAudioMixRate;
    target->started = true;
    target->began = false;
    target->ended = false;
    target->start_time = when > now ? when : now;
    target->stop_time = -1.0;
    target->clip_offset = offset > 0.0 ? offset : 0.0;
    target->clip_duration = duration;
    target->clip_pos = target->clip_offset * (target->clip ? target->clip->sample_rate
                                                           : kAudioMixRate);
  });
}

void AudioMixer::stop_source(uint64_t node, double when) {
  post_command([this, node, when]() {
    AudioNode *target = node_locked(node);
    if (target == nullptr) return;
    const double now = static_cast<double>(frames_rendered_.load(std::memory_order_relaxed)) /
                       kAudioMixRate;
    target->stop_time = when > now ? when : now;
  });
}

}  // namespace xtgui
