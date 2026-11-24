/*
 * xbintsc GUI engine — Web Audio graph rendering.
 *
 * See `audio_mixer.h` for the ownership and threading model. This file is the
 * DSP: oscillator/buffer/constant sources, gain, biquad, compressor, stereo
 * panner and delay, plus the per-sample AudioParam evaluation they all share.
 */

#include "audio_mixer.h"

#include "audio_effects.h"
#include "audio_extra.h"
#include "audio_fft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

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
  const char *silent = std::getenv("XT_GUI_AUDIO_SILENT");
  silent_ = silent != NULL && silent[0] != '\0' && silent[0] != '0';
  const char *profile = std::getenv("XT_AUDIO_PROFILE");
  profile_ = profile != NULL && profile[0] != '\0' && profile[0] != '0';
  start_ns_ = SDL_GetTicksNS();
}

AudioMixer::~AudioMixer() { close_device(); }

AudioNode *AudioMixer::node_locked(uint64_t id) {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second.get();
}

uint64_t AudioMixer::create_node(AudioNodeKind kind) {
  auto node = std::make_shared<AudioNode>();
  node->id = next_node_id_.fetch_add(1, std::memory_order_relaxed);
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
    case kAudioWaveShaper:
    case kAudioConvolver:
    case kAudioAnalyser:
    case kAudioMediaSource:
      node->param_count = 0;
      break;
    case kAudioPanner:
      /* positionX/Y/Z (0,0,0) and orientationX/Y/Z (1,0,0), all automatable. */
      node->param_count = 6;
      node->params[0].reset(0.0);
      node->params[1].reset(0.0);
      node->params[2].reset(0.0);
      node->params[3].reset(1.0);
      node->params[4].reset(0.0);
      node->params[5].reset(0.0);
      break;
    case kAudioListener:
      /* position (0,0,0), forward (0,0,-1) and up (0,1,0). */
      node->param_count = 9;
      node->params[0].reset(0.0);
      node->params[1].reset(0.0);
      node->params[2].reset(0.0);
      node->params[3].reset(0.0);
      node->params[4].reset(0.0);
      node->params[5].reset(-1.0);
      node->params[6].reset(0.0);
      node->params[7].reset(1.0);
      node->params[8].reset(0.0);
      break;
    case kAudioDestination:
    default:
      node->param_count = 0;
      break;
  }
  {
    std::lock_guard<std::mutex> pending_lock(pending_mutex_);
    pending_nodes_[node->id] = node;
  }
  post_command([this, node]() {
    const uint64_t id = node->id;
    nodes_[id] = node;
    if (node->kind == kAudioDestination) destination_id_ = id;
    if (node->kind == kAudioListener) listener_id_ = id;
    std::lock_guard<std::mutex> pending_lock(pending_mutex_);
    pending_nodes_.erase(id);
  });
  return node->id;
}

/* Graph mutations (`connect`, `start`, the per-node setup calls) live in
 * `audio_graph_commands.cpp`; they queue a command instead of taking the
 * render mutex. */

/* -- AudioMixer: clock ---------------------------------------------------- */

double AudioMixer::current_time() const {
  /* Lock-free: the audio thread holds `mutex_` while it renders a quantum, and
   * a page schedules sounds from `currentTime` on every frame. Waiting for that
   * lock here was measurable main-thread time with a convolver send running. */
  return static_cast<double>(frames_rendered_.load(std::memory_order_relaxed)) / kAudioMixRate;
}

void AudioMixer::shutdown() {
  close_device();
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  nodes_.clear();
  destination_id_ = 0;
  {
    std::lock_guard<std::mutex> pending_lock(pending_mutex_);
    pending_nodes_.clear();
  }
  if (profile_) {
    const double wall_ms = static_cast<double>(SDL_GetTicksNS() - start_ns_) / 1000000.0;
    const double wait_ms = static_cast<double>(lock_wait_ns_.load()) / 1000000.0;
    const double render_ms = static_cast<double>(render_ns_.load()) / 1000000.0;
    const double max_ms = static_cast<double>(max_render_ns_.load()) / 1000000.0;
    std::fprintf(stderr,
                 "xt_audio: wall=%.1fms lockWait=%.1fms render=%.1fms (%.1f%%) "
                 "quanta=%llu maxQuantum=%.3fms silent=%d\n",
                 wall_ms, wait_ms, render_ms,
                 wall_ms > 0.0 ? render_ms * 100.0 / wall_ms : 0.0,
                 (unsigned long long)render_quanta_.load(), max_ms, silent_ ? 1 : 0);
  }
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
  /* SDL may ask for many quanta in one callback (its stream buffer is tens of
   * milliseconds). Rendering the whole callback under one lock made a page's
   * audio-parameter update wait for all of it — with a convolver send that can
   * be several milliseconds — showing up as main-thread frame time. Hold the
   * graph lock for one quantum at a time instead; the mixer stays real-time and
   * a setter never waits for more than a quantum. */
  int done = 0;
  while (done < frames) {
    int chunk = frames - done;
    if (chunk > kAudioQuantum) chunk = kAudioQuantum;
    const uint64_t wait_start = profile_ ? SDL_GetTicksNS() : 0;
    std::unique_lock<std::mutex> lock(mutex_);
    const uint64_t locked_at = profile_ ? SDL_GetTicksNS() : 0;
    render_locked(out + static_cast<size_t>(done) * kAudioMixChannels, chunk);
    if (profile_) {
      const uint64_t end = SDL_GetTicksNS();
      lock_wait_ns_.fetch_add(locked_at - wait_start, std::memory_order_relaxed);
      render_ns_.fetch_add(end - locked_at, std::memory_order_relaxed);
      render_quanta_.fetch_add(1, std::memory_order_relaxed);
      uint64_t longest = max_render_ns_.load(std::memory_order_relaxed);
      const uint64_t took = end - locked_at;
      while (took > longest &&
             !max_render_ns_.compare_exchange_weak(longest, took, std::memory_order_relaxed)) {
      }
    }
    done += chunk;
  }
}

void AudioMixer::render_offline(float *out, int frames) {
  if (!offline_) return;
  std::lock_guard<std::mutex> lock(mutex_);
  render_locked(out, frames);
}

void AudioMixer::render_quantum_locked() {
  /* Apply whatever the page queued since the previous quantum. The callbacks
   * assume `mutex_` is held; they never block the main thread because the
   * setters only touch the command queue. */
  drain_commands_locked();
  serial_++;
  pending_.assign(kSamples, 0.0f);
  pending_offset_ = 0;
  if (!silent_) {
    AudioNode *destination = node_locked(destination_id_);
    if (destination != nullptr) {
      const float *mixed = pull_locked(destination);
      if (mixed != nullptr) std::memcpy(pending_.data(), mixed, sizeof(float) * kSamples);
    }
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
      node->kind == kAudioStereoPanner || node->kind == kAudioDelay ||
      node->kind == kAudioWaveShaper || node->kind == kAudioConvolver ||
      node->kind == kAudioAnalyser || node->kind == kAudioPanner) {
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
    } else if (node->kind == kAudioWaveShaper) {
      render_waveshaper(node, input);
    } else if (node->kind == kAudioConvolver) {
      render_convolver(node, input);
    } else if (node->kind == kAudioAnalyser) {
      render_analyser(node, input);
    } else if (node->kind == kAudioPanner) {
      AudioListenerState listener;
      listener_state_at(node_locked(listener_id_), quantum_start, &listener);
      render_panner(node, input, quantum_start, &listener);
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
