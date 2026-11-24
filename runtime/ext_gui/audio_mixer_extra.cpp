#include "audio_mixer.h"

#include "audio_extra.h"
#include "audio_fft.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace xtgui {

void AudioMixer::set_shaper_curve(uint64_t node, const std::vector<float> &curve, int oversample) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  /* A one-entry curve is a DC curve on the platform; keep the same length
   * semantics (length < 2 passes the signal through). */
  target->shaper_curve = curve.size() >= 2 ? curve : std::vector<float>();
  target->shaper_oversample = oversample > 1 ? oversample : 1;
}

void AudioMixer::set_shaper_oversample(uint64_t node, int oversample) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target != nullptr) target->shaper_oversample = oversample > 1 ? oversample : 1;
}

void AudioMixer::set_convolver_impulse(uint64_t node, std::shared_ptr<AudioClip> clip,
                                       bool normalize) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  prepare_convolver(target, clip, normalize);
}

void AudioMixer::set_analyser_fft(uint64_t node, int fft_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  if (fft_size < 32) fft_size = 32;
  if (fft_size > 32768) fft_size = 32768;
  target->analyser_fft = fft_size;
  target->analyser_ring.assign(static_cast<size_t>(fft_size), 0.0f);
  target->analyser_write = 0;
}

void AudioMixer::set_analyser_smoothing(uint64_t node, float smoothing) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  if (smoothing < 0.0f) smoothing = 0.0f;
  if (smoothing > 1.0f) smoothing = 1.0f;
  target->analyser_smoothing = smoothing;
}

bool AudioMixer::analyser_time_domain(uint64_t node, int count, std::vector<float> *out) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr || out == nullptr) return false;
  const size_t size = target->analyser_ring.size();
  if (size == 0 || count <= 0) return false;
  if (static_cast<size_t>(count) > size) count = static_cast<int>(size);
  out->assign(static_cast<size_t>(count), 0.0f);
  for (int i = 0; i < count; i++) {
    size_t index = (target->analyser_write + size - static_cast<size_t>(count) + static_cast<size_t>(i)) % size;
    (*out)[static_cast<size_t>(i)] = target->analyser_ring[index];
  }
  return true;
}

bool AudioMixer::analyser_spectrum(uint64_t node, std::vector<float> *out) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr || out == nullptr) return false;
  const size_t size = target->analyser_ring.size();
  const int bins = target->analyser_fft / 2;
  if (size == 0 || bins <= 0) return false;
  std::vector<float> re(static_cast<size_t>(target->analyser_fft), 0.0f);
  std::vector<float> im(re.size(), 0.0f);
  for (int i = 0; i < target->analyser_fft; i++) {
    size_t index = (target->analyser_write + size - static_cast<size_t>(target->analyser_fft) + static_cast<size_t>(i)) % size;
    /* The spec's Blackman window, so a tone does not smear over the bins. */
    double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(i) /
                   static_cast<double>(target->analyser_fft);
    double window = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
    re[static_cast<size_t>(i)] = static_cast<float>(target->analyser_ring[index] * window);
  }
  audio_fft(re, im, false);
  out->assign(static_cast<size_t>(bins), 0.0f);
  /* The first read reports the measured spectrum; priming the moving average
   * with silence would start every analyser far below the signal. */
  const bool prime = target->analyser_smoothed.size() != static_cast<size_t>(bins);
  if (prime) target->analyser_smoothed.assign(static_cast<size_t>(bins), 0.0f);
  const float smoothing = target->analyser_smoothing;
  for (int bin = 0; bin < bins; bin++) {
    double magnitude = std::sqrt(static_cast<double>(re[static_cast<size_t>(bin)]) * re[static_cast<size_t>(bin)] +
                                 static_cast<double>(im[static_cast<size_t>(bin)]) * im[static_cast<size_t>(bin)]);
    /* Amplitude of a full-scale sine is 1: normalize by half the transform. */
    double amplitude = 2.0 * magnitude / static_cast<double>(target->analyser_fft);
    double decibels = 20.0 * std::log10(std::max(amplitude, 1e-9));
    /* `smoothingTimeConstant` is a moving average over successive calls. */
    double previous = target->analyser_smoothed[static_cast<size_t>(bin)];
    double smoothed = prime ? decibels : smoothing * previous + (1.0 - smoothing) * decibels;
    target->analyser_smoothed[static_cast<size_t>(bin)] = static_cast<float>(smoothed);
    (*out)[static_cast<size_t>(bin)] = static_cast<float>(smoothed);
  }
  return true;
}

void AudioMixer::set_panner(uint64_t node, int distance_model, double ref_distance,
                            double max_distance, double rolloff, double cone_inner,
                            double cone_outer, double cone_gain, int panning_model) {
  std::lock_guard<std::mutex> lock(mutex_);
  drain_commands_locked();
  AudioNode *target = node_locked(node);
  if (target == nullptr) return;
  target->distance_model = distance_model;
  target->panning_model = panning_model;
  target->ref_distance = ref_distance > 0.0 ? ref_distance : 1.0;
  target->max_distance = max_distance > 0.0 ? max_distance : 10000.0;
  target->rolloff = rolloff >= 0.0 ? rolloff : 1.0;
  target->cone_inner = cone_inner;
  target->cone_outer = cone_outer;
  target->cone_gain = cone_gain;
}

}  // namespace xtgui
