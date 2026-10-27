/*
 * xbintsc GUI engine — Web Audio graph rendering.
 *
 * See `audio_mixer.h` for the ownership and threading model. This file is the
 * DSP: oscillator/buffer/constant sources, gain, biquad, compressor, stereo
 * panner and delay, plus the per-sample AudioParam evaluation they all share.
 */

#include "audio_mixer.h"

#include "audio_effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xtgui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSamples = kAudioQuantum * kAudioMixChannels;

double clamp_value(double value, double low, double high) {
  if (!(value > low)) return low;
  if (value > high) return high;
  return value;
}

double notes_to_rate(double detune_cents) {
  return std::pow(2.0, detune_cents / 1200.0);
}

/* One quantized waveform sample at `phase` in [0, 1). */
double waveform(AudioOscType type, double phase) {
  switch (type) {
    case kAudioOscSquare:
      return phase < 0.5 ? 1.0 : -1.0;
    case kAudioOscSawtooth:
      return 2.0 * phase - 1.0;
    case kAudioOscTriangle:
      return phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
    case kAudioOscSine:
    default:
      return std::sin(phase * 2.0 * kPi);
  }
}

}  // namespace

/* -- AudioMixer: graph ---------------------------------------------------- */

AudioMixer::AudioMixer(bool offline) : offline_(offline) {
  pending_.reserve(kSamples);
}

AudioMixer::~AudioMixer() { close_device(); }

AudioNode *AudioMixer::node_locked(uint64_t id) {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second.get();
}

uint64_t AudioMixer::create_node(AudioNodeKind kind) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto node = std::make_shared<AudioNode>();
  node->id = next_node_id_++;
  node->kind = kind;
  node->quantum.assign(kSamples, 0.0f);
  switch (kind) {
    case kAudioGain:
      node->param_count = 1;
      node->params[0].reset(1.0);
      break;
    case kAudioOscillator:
      node->param_count = 2;
      node->params[0].reset(440.0);
      node->params[1].reset(0.0);
      break;
    case kAudioBufferSource:
      node->param_count = 2;
      node->params[0].reset(1.0);
      node->params[1].reset(0.0);
      break;
    case kAudioBiquad:
      node->param_count = 4;
      node->params[0].reset(350.0);
      node->params[1].reset(0.0);
      node->params[2].reset(1.0);
      node->params[3].reset(0.0);
      break;
    case kAudioCompressor:
      node->param_count = 5;
      node->params[0].reset(-24.0);
      node->params[1].reset(30.0);
      node->params[2].reset(12.0);
      node->params[3].reset(0.003);
      node->params[4].reset(0.25);
      break;
    case kAudioStereoPanner:
      node->param_count = 1;
      node->params[0].reset(0.0);
      break;
    case kAudioConstantSource:
      node->param_count = 1;
      node->params[0].reset(1.0);
      break;
    case kAudioDelay:
      node->param_count = 1;
      node->params[0].reset(0.0);
      break;
    case kAudioDestination:
    default:
      node->param_count = 0;
      break;
  }
  nodes_[node->id] = node;
  if (kind == kAudioDestination) destination_id_ = node->id;
  return node->id;
}

void AudioMixer::connect(uint64_t source, uint64_t target) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *from = node_locked(source);
  AudioNode *to = node_locked(target);
  if (from == nullptr || to == nullptr) return;
  for (uint64_t id : to->inputs) {
    if (id == source) return;
  }
  to->inputs.push_back(source);
}

void AudioMixer::disconnect(uint64_t source, uint64_t target) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *to = node_locked(target);
  if (to == nullptr) return;
  to->inputs.erase(std::remove(to->inputs.begin(), to->inputs.end(), source), to->inputs.end());
}

void AudioMixer::disconnect_all(uint64_t source) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &entry : nodes_) {
    std::vector<uint64_t> &inputs = entry.second->inputs;
    inputs.erase(std::remove(inputs.begin(), inputs.end(), source), inputs.end());
  }
}

void AudioMixer::set_osc_type(uint64_t node, AudioOscType type) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target != nullptr) target->osc_type = type;
}

void AudioMixer::set_biquad_type(uint64_t node, AudioBiquadType type) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target != nullptr) target->biquad_type = type;
}

void AudioMixer::set_delay_max(uint64_t node, double seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  seconds = clamp_value(seconds, 0.001, 60.0);
  target->delay_max = seconds;
  target->delay_size = static_cast<size_t>(seconds * kAudioMixRate) + 2;
  target->delay_write = 0;
  for (int channel = 0; channel < kAudioMixChannels; channel++) {
    target->delay[channel].assign(target->delay_size, 0.0f);
  }
}

void AudioMixer::set_buffer(uint64_t node, std::shared_ptr<AudioClip> clip, bool loop,
                            double loop_start, double loop_end) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  target->clip = std::move(clip);
  target->loop = loop;
  target->loop_start = loop_start;
  target->loop_end = loop_end;
}

void AudioMixer::start_source(uint64_t node, double when, double offset, double duration) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  double now = static_cast<double>(frames_rendered_) / kAudioMixRate;
  target->started = true;
  target->began = false;
  target->ended = false;
  target->start_time = when > now ? when : now;
  target->stop_time = -1.0;
  target->clip_offset = offset > 0.0 ? offset : 0.0;
  target->clip_duration = duration;
  target->clip_pos = target->clip_offset * (target->clip ? target->clip->sample_rate
                                                         : kAudioMixRate);
}

void AudioMixer::stop_source(uint64_t node, double when) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  double now = static_cast<double>(frames_rendered_) / kAudioMixRate;
  target->stop_time = when > now ? when : now;
}

/* -- AudioMixer: parameters ----------------------------------------------- */

double AudioMixer::param_value(uint64_t node, int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target == nullptr || index < 0 || index >= target->param_count) return 0.0;
  double now = static_cast<double>(frames_rendered_) / kAudioMixRate;
  return target->params[index].value_at(now);
}

void AudioMixer::param_set(uint64_t node, int index, double value) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target == nullptr || index < 0 || index >= target->param_count) return;
  double now = static_cast<double>(frames_rendered_) / kAudioMixRate;
  target->params[index].set_value(value, now);
}

void AudioMixer::param_set_at(uint64_t node, int index, double value, double time) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target != nullptr && index >= 0 && index < target->param_count) {
    target->params[index].set_value_at_time(value, time);
  }
}

void AudioMixer::param_linear_ramp(uint64_t node, int index, double value, double time) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target != nullptr && index >= 0 && index < target->param_count) {
    target->params[index].linear_ramp(value, time);
  }
}

void AudioMixer::param_exponential_ramp(uint64_t node, int index, double value, double time) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target != nullptr && index >= 0 && index < target->param_count) {
    target->params[index].exponential_ramp(value, time);
  }
}

void AudioMixer::param_set_target(uint64_t node, int index, double value, double time,
                                  double time_constant) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target != nullptr && index >= 0 && index < target->param_count) {
    target->params[index].set_target(value, time, time_constant);
  }
}

void AudioMixer::param_cancel(uint64_t node, int index, double time, bool hold) {
  std::lock_guard<std::mutex> lock(mutex_);
  AudioNode *target = node_locked(node);
  if (target != nullptr && index >= 0 && index < target->param_count) {
    if (hold) {
      target->params[index].cancel_and_hold(time);
    } else {
      target->params[index].cancel_scheduled(time);
    }
  }
}

/* -- AudioMixer: clock ---------------------------------------------------- */

double AudioMixer::current_time() {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<double>(frames_rendered_) / kAudioMixRate;
}

void AudioMixer::shutdown() {
  close_device();
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.clear();
  destination_id_ = 0;
}

/* -- AudioMixer: render --------------------------------------------------- */

void AudioMixer::render_locked(float *out, int frames) {
  int done = 0;
  while (done < frames) {
    if (pending_offset_ >= pending_.size()) {
      render_quantum_locked();
      pending_offset_ = 0;
    }
    size_t available = pending_.size() - pending_offset_;
    size_t wanted = static_cast<size_t>(frames - done) * kAudioMixChannels;
    size_t take = std::min(available, wanted);
    std::memcpy(out + static_cast<size_t>(done) * kAudioMixChannels,
                pending_.data() + pending_offset_, take * sizeof(float));
    pending_offset_ += take;
    done += static_cast<int>(take / kAudioMixChannels);
  }
}

void AudioMixer::render(float *out, int frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  render_locked(out, frames);
}

void AudioMixer::render_offline(float *out, int frames) {
  if (!offline_) return;
  std::lock_guard<std::mutex> lock(mutex_);
  render_locked(out, frames);
}

void AudioMixer::render_quantum_locked() {
  serial_++;
  pending_.assign(kSamples, 0.0f);
  pending_offset_ = 0;
  AudioNode *destination = node_locked(destination_id_);
  if (destination != nullptr) {
    const float *mixed = pull_locked(destination);
    if (mixed != nullptr) std::memcpy(pending_.data(), mixed, sizeof(float) * kSamples);
  }
  frames_rendered_ += kAudioQuantum;
}

const float *AudioMixer::pull_locked(AudioNode *node) {
  if (node == nullptr) return nullptr;
  if (node->serial == serial_) return node->quantum.data();
  if (node->rendering) {
    std::fill(node->quantum.begin(), node->quantum.end(), 0.0f);
    return node->quantum.data();
  }
  node->rendering = true;
  std::fill(node->quantum.begin(), node->quantum.end(), 0.0f);

  double quantum_start = static_cast<double>(frames_rendered_) / kAudioMixRate;
  double step = 1.0 / kAudioMixRate;

  if (node->kind == kAudioDestination || node->kind == kAudioGain ||
      node->kind == kAudioBiquad || node->kind == kAudioCompressor ||
      node->kind == kAudioStereoPanner || node->kind == kAudioDelay) {
    float input[kSamples];
    std::memset(input, 0, sizeof(input));
    for (uint64_t id : node->inputs) {
      const float *source = pull_locked(node_locked(id));
      if (source == nullptr) continue;
      for (int i = 0; i < kSamples; i++) input[i] += source[i];
    }
    if (node->kind == kAudioDestination) {
      std::memcpy(node->quantum.data(), input, sizeof(input));
    } else if (node->kind == kAudioGain) {
      for (int frame = 0; frame < kAudioQuantum; frame++) {
        double gain = node->params[0].value_at(quantum_start + frame * step);
        node->quantum[frame * 2] = static_cast<float>(input[frame * 2] * gain);
        node->quantum[frame * 2 + 1] = static_cast<float>(input[frame * 2 + 1] * gain);
      }
    } else if (node->kind == kAudioStereoPanner) {
      for (int frame = 0; frame < kAudioQuantum; frame++) {
        double pan = clamp_value(node->params[0].value_at(quantum_start + frame * step), -1.0, 1.0);
        double angle = (pan + 1.0) * kPi / 4.0;
        node->quantum[frame * 2] = static_cast<float>(input[frame * 2] * std::cos(angle));
        node->quantum[frame * 2 + 1] =
            static_cast<float>(input[frame * 2 + 1] * std::sin(angle));
      }
    } else if (node->kind == kAudioDelay) {
      if (node->delay_size > 1) {
        double maximum = static_cast<double>(node->delay_size - 2);
        for (int frame = 0; frame < kAudioQuantum; frame++) {
          double seconds = node->params[0].value_at(quantum_start + frame * step);
          double samples = clamp_value(seconds, 0.0, node->delay_max) * kAudioMixRate;
          if (samples > maximum) samples = maximum;
          node->delay[0][node->delay_write] = input[frame * 2];
          node->delay[1][node->delay_write] = input[frame * 2 + 1];
          double read = static_cast<double>(node->delay_write) - samples;
          while (read < 0.0) read += static_cast<double>(node->delay_size);
          size_t lower = static_cast<size_t>(read);
          double fraction = read - static_cast<double>(lower);
          size_t upper = lower + 1 < node->delay_size ? lower + 1 : 0;
          for (int channel = 0; channel < kAudioMixChannels; channel++) {
            double delayed = node->delay[channel][lower] * (1.0 - fraction) +
                             node->delay[channel][upper] * fraction;
            node->quantum[frame * 2 + channel] = static_cast<float>(delayed);
          }
          node->delay_write = (node->delay_write + 1) % node->delay_size;
        }
      } else {
        std::memcpy(node->quantum.data(), input, sizeof(input));
      }
    } else if (node->kind == kAudioBiquad) {
      render_biquad(node, input, quantum_start);
    } else {
      render_compressor(node, input, quantum_start);
    }
  } else if (node->kind == kAudioConstantSource) {
    if (node->started) {
      for (int frame = 0; frame < kAudioQuantum; frame++) {
        double time = quantum_start + frame * step;
        if (time < node->start_time) continue;
        if (node->stop_time >= 0.0 && time >= node->stop_time) {
          node->ended = true;
          break;
        }
        double value = node->params[0].value_at(time);
        node->quantum[frame * 2] = static_cast<float>(value);
        node->quantum[frame * 2 + 1] = static_cast<float>(value);
      }
    }
  } else if (node->kind == kAudioOscillator) {
    if (node->started) {
      for (int frame = 0; frame < kAudioQuantum; frame++) {
        double time = quantum_start + frame * step;
        if (time < node->start_time) continue;
        if (node->stop_time >= 0.0 && time >= node->stop_time) {
          node->ended = true;
          break;
        }
        if (!node->began) {
          node->began = true;
          node->phase = 0.0;
        }
        double frequency = node->params[0].value_at(time) *
                           notes_to_rate(node->params[1].value_at(time));
        frequency = std::max(frequency, 0.0);
        double sample = waveform(node->osc_type, node->phase);
        node->quantum[frame * 2] = static_cast<float>(sample);
        node->quantum[frame * 2 + 1] = static_cast<float>(sample);
        node->phase += frequency / kAudioMixRate;
        if (node->phase >= 1.0) node->phase -= std::floor(node->phase);
      }
    }
  } else if (node->kind == kAudioBufferSource) {
    std::shared_ptr<AudioClip> clip = node->clip;
    if (node->started && clip && clip->length > 0) {
      double ratio = static_cast<double>(clip->sample_rate) / kAudioMixRate;
      size_t length = clip->length;
      for (int frame = 0; frame < kAudioQuantum; frame++) {
        double time = quantum_start + frame * step;
        if (time < node->start_time) continue;
        if (node->stop_time >= 0.0 && time >= node->stop_time) {
          node->ended = true;
          break;
        }
        if (node->clip_duration > 0.0 && time - node->start_time >= node->clip_duration) {
          node->ended = true;
          break;
        }
        if (!node->began) node->began = true;
        double playback = node->params[0].value_at(time) *
                          notes_to_rate(node->params[1].value_at(time));
        if (playback < 0.0) playback = 0.0;
        double position = node->clip_pos;
        if (node->loop) {
          double loop_end = node->loop_end > node->loop_start
                                ? node->loop_end * clip->sample_rate
                                : static_cast<double>(length);
          double loop_start = node->loop_start * clip->sample_rate;
          loop_end = std::min(loop_end, static_cast<double>(length));
          if (loop_end > loop_start && position >= loop_end) {
            double span = loop_end - loop_start;
            position = loop_start + std::fmod(position - loop_start, span);
            node->clip_pos = position;
          }
        }
        if (!node->loop && position >= static_cast<double>(length)) {
          node->ended = true;
          break;
        }
        if (position < 0.0) position = 0.0;
        size_t index = static_cast<size_t>(position);
        if (index >= length) index = length - 1;
        size_t next = index + 1 < length ? index + 1 : index;
        double fraction = position - static_cast<double>(index);
        double left = clip->channels[0][index] +
                      (clip->channels[0][next] - clip->channels[0][index]) * fraction;
        double right = left;
        if (clip->channels.size() > 1) {
          right = clip->channels[1][index] +
                  (clip->channels[1][next] - clip->channels[1][index]) * fraction;
        }
        node->quantum[frame * 2] = static_cast<float>(left);
        node->quantum[frame * 2 + 1] = static_cast<float>(right);
        node->clip_pos += playback * ratio;
      }
      if (node->ended && !node->loop) node->clip.reset();
    }
  }

  node->rendering = false;
  node->serial = serial_;
  return node->quantum.data();
}

}  // namespace xtgui
