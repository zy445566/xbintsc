/*
 * xbintsc GUI engine — biquad and compressor DSP.
 *
 * Split from `audio_mixer.cpp`: both effects recompute their coefficients or
 * envelope once per 128-frame quantum and then process the summed input
 * quantum. Keeping them here leaves the mixer file to graph traversal and
 * sources.
 */

#include "audio_effects.h"

#include <algorithm>
#include <cmath>

namespace xtgui {

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp_value(double value, double low, double high) {
  if (!(value > low)) return low;
  if (value > high) return high;
  return value;
}

double notes_to_rate(double detune_cents) {
  return std::pow(2.0, detune_cents / 1200.0);
}

}  // namespace

void render_biquad(AudioNode *node, const float *input, double quantum_start) {
  double frequency = node->params[0].value_at(quantum_start) *
                     notes_to_rate(node->params[1].value_at(quantum_start));
  frequency = clamp_value(frequency, 10.0, kAudioMixRate * 0.49);
  double q = clamp_value(node->params[2].value_at(quantum_start), 0.0001, 1000.0);
  double gain_db = node->params[3].value_at(quantum_start);
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
  double w0 = 2.0 * kPi * frequency / kAudioMixRate;
  double cosw = std::cos(w0);
  double sinw = std::sin(w0);
  double alpha = sinw / (2.0 * q);
  double amplitude = std::pow(10.0, gain_db / 40.0);
  switch (node->biquad_type) {
    case kAudioBiquadHighpass:
      b0 = (1.0 + cosw) / 2.0;
      b1 = -(1.0 + cosw);
      b2 = b0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cosw;
      a2 = 1.0 - alpha;
      break;
    case kAudioBiquadBandpass:
      b0 = alpha;
      b1 = 0.0;
      b2 = -alpha;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cosw;
      a2 = 1.0 - alpha;
      break;
    case kAudioBiquadNotch:
      b0 = 1.0;
      b1 = -2.0 * cosw;
      b2 = 1.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cosw;
      a2 = 1.0 - alpha;
      break;
    case kAudioBiquadAllpass:
      b0 = 1.0 - alpha;
      b1 = -2.0 * cosw;
      b2 = 1.0 + alpha;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cosw;
      a2 = 1.0 - alpha;
      break;
    case kAudioBiquadPeaking:
      b0 = 1.0 + alpha * amplitude;
      b1 = -2.0 * cosw;
      b2 = 1.0 - alpha * amplitude;
      a0 = 1.0 + alpha / amplitude;
      a1 = -2.0 * cosw;
      a2 = 1.0 - alpha / amplitude;
      break;
    case kAudioBiquadLowshelf: {
      double shelf = 2.0 * std::sqrt(amplitude) * alpha;
      b0 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosw + shelf);
      b1 = 2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosw);
      b2 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosw - shelf);
      a0 = (amplitude + 1.0) + (amplitude - 1.0) * cosw + shelf;
      a1 = -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosw);
      a2 = (amplitude + 1.0) + (amplitude - 1.0) * cosw - shelf;
      break;
    }
    case kAudioBiquadHighshelf: {
      double shelf = 2.0 * std::sqrt(amplitude) * alpha;
      b0 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosw + shelf);
      b1 = -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosw);
      b2 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosw - shelf);
      a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosw + shelf;
      a1 = 2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosw);
      a2 = (amplitude + 1.0) - (amplitude - 1.0) * cosw - shelf;
      break;
    }
    case kAudioBiquadLowpass:
    default:
      b0 = (1.0 - cosw) / 2.0;
      b1 = 1.0 - cosw;
      b2 = b0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cosw;
      a2 = 1.0 - alpha;
      break;
  }
  b0 /= a0;
  b1 /= a0;
  b2 /= a0;
  a1 /= a0;
  a2 /= a0;
  for (int channel = 0; channel < kAudioMixChannels; channel++) {
    for (int frame = 0; frame < kAudioQuantum; frame++) {
      double x = input[frame * 2 + channel];
      double y = b0 * x + b1 * node->x1[channel] + b2 * node->x2[channel] -
                 a1 * node->y1[channel] - a2 * node->y2[channel];
      node->x2[channel] = node->x1[channel];
      node->x1[channel] = x;
      node->y2[channel] = node->y1[channel];
      node->y1[channel] = y;
      node->quantum[frame * 2 + channel] = static_cast<float>(y);
    }
  }
}

void render_compressor(AudioNode *node, const float *input, double quantum_start) {
  double threshold = node->params[0].value_at(quantum_start);
  double knee = std::max(node->params[1].value_at(quantum_start), 0.0);
  double ratio = std::max(node->params[2].value_at(quantum_start), 1.0);
  double attack = std::max(node->params[3].value_at(quantum_start), 1e-4);
  double release = std::max(node->params[4].value_at(quantum_start), 1e-4);
  double attack_coef = std::exp(-1.0 / (attack * kAudioMixRate));
  double release_coef = std::exp(-1.0 / (release * kAudioMixRate));
  double env = node->compressor_env;
  for (int frame = 0; frame < kAudioQuantum; frame++) {
    double peak = std::max(std::fabs(input[frame * 2]), std::fabs(input[frame * 2 + 1]));
    double db = 20.0 * std::log10(peak + 1e-9);
    double target_db = 0.0;
    if (knee > 0.0 && db > threshold - knee / 2.0 && db < threshold + knee / 2.0) {
      double over = db - threshold + knee / 2.0;
      target_db = (1.0 / ratio - 1.0) * over * over / (2.0 * knee);
    } else if (db >= threshold + knee / 2.0) {
      target_db = (threshold + (db - threshold) / ratio) - db;
    }
    double coefficient = target_db < env ? attack_coef : release_coef;
    env = coefficient * env + (1.0 - coefficient) * target_db;
    double gain = std::pow(10.0, env / 20.0);
    node->quantum[frame * 2] = static_cast<float>(input[frame * 2] * gain);
    node->quantum[frame * 2 + 1] = static_cast<float>(input[frame * 2 + 1] * gain);
  }
  node->compressor_env = env;
}

}  // namespace xtgui
