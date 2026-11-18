/*
 * xbintsc GUI engine — WaveShaper, Convolver, Analyser and Panner rendering.
 *
 * The interesting one is the convolver: a reverb impulse response is seconds
 * long, so the 128-frame quantum is convolved with a *partitioned* transform
 * (overlap-save, partition = one quantum, transform = 256 points). The impulse
 * response's partition spectra are built once when the page assigns `buffer`;
 * each quantum then costs one forward FFT, one complex multiply-accumulate per
 * partition, and one inverse FFT, which is what makes a 1.4-second response
 * affordable at 48 kHz.
 */

#include "audio_extra.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio_fft.h"

namespace xtgui {
namespace {

constexpr int kQuantumFrames = kAudioQuantum;
constexpr int kSpectrumSize = 2 * kQuantumFrames; /* 256-point transform */
constexpr int kSamplesPerQuantum = kAudioQuantum * kAudioMixChannels;

double clamp_value(double value, double low, double high) {
  if (!(value > low)) return low;
  if (value > high) return high;
  return value;
}

/** The shaped value of one sample through `curve` (Web Audio's mapping). */
float shape(const std::vector<float> &curve, double input) {
  const size_t size = curve.size();
  if (size == 0) return static_cast<float>(input);
  if (size == 1) return curve[0];
  double x = clamp_value(input, -1.0, 1.0);
  double position = (x + 1.0) * 0.5 * static_cast<double>(size - 1);
  size_t lower = static_cast<size_t>(position);
  if (lower >= size - 1) return curve[size - 1];
  double fraction = position - static_cast<double>(lower);
  return static_cast<float>(curve[lower] * (1.0 - fraction) + curve[lower + 1] * fraction);
}

/** Linear resampling of one channel to the mix rate. */
std::vector<float> resample_channel(const std::vector<float> &source, int source_rate) {
  if (source.empty() || source_rate <= 0 || source_rate == kAudioMixRate) return source;
  double ratio = static_cast<double>(source_rate) / kAudioMixRate;
  size_t length = static_cast<size_t>(static_cast<double>(source.size()) / ratio);
  if (length == 0) length = 1;
  std::vector<float> out(length, 0.0f);
  for (size_t i = 0; i < length; i++) {
    double position = static_cast<double>(i) * ratio;
    size_t lower = static_cast<size_t>(position);
    if (lower >= source.size()) lower = source.size() - 1;
    size_t upper = lower + 1 < source.size() ? lower + 1 : lower;
    double fraction = position - static_cast<double>(lower);
    out[i] = static_cast<float>(source[lower] * (1.0 - fraction) + source[upper] * fraction);
  }
  return out;
}

}  // namespace

/* -- listener -------------------------------------------------------------- */

void listener_state_at(const AudioNode *listener, double quantum_start, AudioListenerState *out) {
  if (listener == nullptr || listener->param_count < 9 || out == nullptr) return;
  out->x = listener->params[0].value_at(quantum_start);
  out->y = listener->params[1].value_at(quantum_start);
  out->z = listener->params[2].value_at(quantum_start);
  out->fx = listener->params[3].value_at(quantum_start);
  out->fy = listener->params[4].value_at(quantum_start);
  out->fz = listener->params[5].value_at(quantum_start);
  out->ux = listener->params[6].value_at(quantum_start);
  out->uy = listener->params[7].value_at(quantum_start);
  out->uz = listener->params[8].value_at(quantum_start);
}

/* -- WaveShaper ------------------------------------------------------------ */

void render_waveshaper(AudioNode *node, const float *input) {
  const std::vector<float> &curve = node->shaper_curve;
  const int oversample = node->shaper_oversample > 1 ? node->shaper_oversample : 1;
  for (int channel = 0; channel < kAudioMixChannels; channel++) {
    for (int frame = 0; frame < kQuantumFrames; frame++) {
      const int index = frame * kAudioMixChannels + channel;
      if (oversample == 1) {
        node->quantum[index] = shape(curve, input[index]);
        continue;
      }
      /* No polyphase filter: average the curve at evenly spaced points of the
       * frame. It cannot undo aliasing a hard curve introduces, but it removes
       * the worst of the stair-stepping, which is what a page asking for
       * "2x"/"4x" is after. */
      double sum = 0.0;
      for (int step = 0; step < oversample; step++) {
        double fraction = oversample > 1 ? static_cast<double>(step) / oversample : 0.0;
        double previous = frame > 0 ? input[index - kAudioMixChannels] : input[index];
        double sample = previous + (input[index] - previous) * fraction;
        sum += shape(curve, sample);
      }
      node->quantum[index] = static_cast<float>(sum / oversample);
    }
  }
}

/* -- Convolver ------------------------------------------------------------- */

void prepare_convolver(AudioNode *node, const std::shared_ptr<AudioClip> &clip, bool normalize) {
  AudioNode::ConvolverState &state = node->convolver;
  state.ready = false;
  state.partitions = 0;
  state.ring_index = 0;
  state.gain = 1.0f;
  for (int channel = 0; channel < 2; channel++) {
    state.input_re[channel].clear();
    state.input_im[channel].clear();
    state.ir_re[channel].clear();
    state.ir_im[channel].clear();
    state.window[channel].assign(kSpectrumSize, 0.0f);
  }
  if (!clip || clip->channels.empty() || clip->length == 0) return;

  std::vector<float> left = resample_channel(clip->channels[0], clip->sample_rate);
  std::vector<float> right =
      clip->channels.size() > 1 ? resample_channel(clip->channels[1], clip->sample_rate) : left;
  const size_t frames = std::min(left.size(), right.size());
  if (frames == 0) return;

  /* `normalize` (the default) scales the response so a white-noise input keeps
   * its level: the convolution's gain is the response's energy, so dividing by
   * the root of that energy is the Wiener-style normalization. */
  double power = 0.0;
  for (size_t i = 0; i < frames; i++) {
    power += static_cast<double>(left[i]) * left[i] + static_cast<double>(right[i]) * right[i];
  }
  if (normalize && power > 0.0) {
    state.gain = static_cast<float>(1.0 / std::sqrt(power));
  }
  if (!(state.gain > 0.0f) || !std::isfinite(state.gain)) state.gain = 1.0f;

  const size_t partitions = (frames + kQuantumFrames - 1) / kQuantumFrames;
  state.partitions = partitions;
  const size_t spectra = partitions * kSpectrumSize;
  for (int channel = 0; channel < 2; channel++) {
    state.input_re[channel].assign(spectra, 0.0f);
    state.input_im[channel].assign(spectra, 0.0f);
    state.ir_re[channel].assign(spectra, 0.0f);
    state.ir_im[channel].assign(spectra, 0.0f);
  }
  std::vector<float> block_re(kSpectrumSize, 0.0f);
  std::vector<float> block_im(kSpectrumSize, 0.0f);
  for (size_t partition = 0; partition < partitions; partition++) {
    const size_t start = partition * kQuantumFrames;
    for (int channel = 0; channel < 2; channel++) {
      const std::vector<float> &source = channel == 0 ? left : right;
      std::fill(block_re.begin(), block_re.end(), 0.0f);
      std::fill(block_im.begin(), block_im.end(), 0.0f);
      for (size_t i = 0; i < kQuantumFrames && start + i < frames; i++) {
        block_re[i] = source[start + i] * state.gain;
      }
      audio_fft(block_re, block_im, false);
      std::memcpy(&state.ir_re[channel][partition * kSpectrumSize], block_re.data(),
                  sizeof(float) * kSpectrumSize);
      std::memcpy(&state.ir_im[channel][partition * kSpectrumSize], block_im.data(),
                  sizeof(float) * kSpectrumSize);
    }
  }
  state.ready = true;
}

void render_convolver(AudioNode *node, const float *input) {
  AudioNode::ConvolverState &state = node->convolver;
  if (!state.ready || state.partitions == 0) {
    std::fill(node->quantum.begin(), node->quantum.end(), 0.0f);
    return;
  }
  const size_t partitions = state.partitions;
  const size_t slot = state.ring_index;

  /* A silent input (the common case for a send bus between sounds) produces a
   * silent output and a zero input spectrum, so the whole partitioned multiply
   * can be skipped; the window still has to slide to keep the timing right. */
  bool silent = true;
  for (int i = 0; i < kSamplesPerQuantum && silent; i++) {
    if (input[i] != 0.0f) silent = false;
  }
  if (silent) {
    for (int channel = 0; channel < 2; channel++) {
      std::vector<float> &window = state.window[channel];
      std::memmove(window.data(), window.data() + kQuantumFrames,
                   sizeof(float) * (kSpectrumSize - kQuantumFrames));
      std::memset(window.data() + kQuantumFrames, 0, sizeof(float) * kQuantumFrames);
      /* The ring must still hold this block's (all-zero) spectrum: leaving the
       * slot untouched would replay a block from `partitions` quanta ago when
       * the ring wraps around to it. */
      std::memset(&state.input_re[channel][slot * kSpectrumSize], 0,
                  sizeof(float) * kSpectrumSize);
      std::memset(&state.input_im[channel][slot * kSpectrumSize], 0,
                  sizeof(float) * kSpectrumSize);
    }
    std::fill(node->quantum.begin(), node->quantum.end(), 0.0f);
    state.ring_index = (slot + 1) % partitions;
    return;
  }

  /* Slide each window by one quantum and append the new samples. */
  std::vector<float> block_re(kSpectrumSize, 0.0f);
  std::vector<float> block_im(kSpectrumSize, 0.0f);
  for (int channel = 0; channel < 2; channel++) {
    std::vector<float> &window = state.window[channel];
    std::memmove(window.data(), window.data() + kQuantumFrames,
                 sizeof(float) * (kSpectrumSize - kQuantumFrames));
    for (int frame = 0; frame < kQuantumFrames; frame++) {
      window[kQuantumFrames + frame] = input[frame * kAudioMixChannels + channel];
    }
    std::memcpy(block_re.data(), window.data(), sizeof(float) * kSpectrumSize);
    std::fill(block_im.begin(), block_im.end(), 0.0f);
    audio_fft(block_re, block_im, false);
    std::memcpy(&state.input_re[channel][slot * kSpectrumSize], block_re.data(),
                sizeof(float) * kSpectrumSize);
    std::memcpy(&state.input_im[channel][slot * kSpectrumSize], block_im.data(),
                sizeof(float) * kSpectrumSize);
  }

  /* Accumulate Σ X[j-k] · H[k] into the output spectrum, one channel at a time. */
  std::vector<float> out_re(kSpectrumSize, 0.0f);
  std::vector<float> out_im(kSpectrumSize, 0.0f);
  for (int channel = 0; channel < 2; channel++) {
    std::fill(out_re.begin(), out_re.end(), 0.0f);
    std::fill(out_im.begin(), out_im.end(), 0.0f);
    for (size_t partition = 0; partition < partitions; partition++) {
      const size_t source_slot =
          (slot + partitions - (partition % partitions)) % partitions;
      const float *xr = &state.input_re[channel][source_slot * kSpectrumSize];
      const float *xi = &state.input_im[channel][source_slot * kSpectrumSize];
      const float *hr = &state.ir_re[channel][partition * kSpectrumSize];
      const float *hi = &state.ir_im[channel][partition * kSpectrumSize];
      for (int bin = 0; bin < kSpectrumSize; bin++) {
        out_re[bin] += xr[bin] * hr[bin] - xi[bin] * hi[bin];
        out_im[bin] += xr[bin] * hi[bin] + xi[bin] * hr[bin];
      }
    }
    audio_fft(out_re, out_im, true);
    /* Overlap-save: the last quantum of the inverse transform is the output. */
    for (int frame = 0; frame < kQuantumFrames; frame++) {
      node->quantum[frame * kAudioMixChannels + channel] = out_re[kQuantumFrames + frame];
    }
  }
  state.ring_index = (slot + 1) % partitions;
}

/* -- Analyser -------------------------------------------------------------- */

void render_analyser(AudioNode *node, const float *input) {
  std::memcpy(node->quantum.data(), input, sizeof(float) * kSamplesPerQuantum);
  const size_t size = node->analyser_ring.size();
  if (size == 0) return;
  for (int frame = 0; frame < kQuantumFrames; frame++) {
    float mono = 0.5f * (input[frame * kAudioMixChannels] + input[frame * kAudioMixChannels + 1]);
    node->analyser_ring[node->analyser_write] = mono;
    node->analyser_write = (node->analyser_write + 1) % size;
  }
}

/* -- Panner ---------------------------------------------------------------- */

void render_panner(AudioNode *node, const float *input, double quantum_start,
                   const AudioListenerState *listener) {
  AudioListenerState fallback;
  if (listener == nullptr) listener = &fallback;
  for (int frame = 0; frame < kQuantumFrames; frame++) {
    double time = quantum_start + static_cast<double>(frame) / kAudioMixRate;
    const int index = frame * kAudioMixChannels;
    double mono = 0.5 * (static_cast<double>(input[index]) + static_cast<double>(input[index + 1]));

    const double sx = node->params[0].value_at(time);
    const double sy = node->params[1].value_at(time);
    const double sz = node->params[2].value_at(time);
    const double ox = node->params[3].value_at(time);
    const double oy = node->params[4].value_at(time);
    const double oz = node->params[5].value_at(time);

    /* Direction from the listener to the source. */
    double dx = sx - listener->x;
    double dy = sy - listener->y;
    double dz = sz - listener->z;
    double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    double norm = distance > 1e-9 ? 1.0 / distance : 0.0;
    double ux = dx * norm;
    double uy = dy * norm;
    double uz = dz * norm;

    /* Distance attenuation, per the node's model. */
    double reference = node->ref_distance > 0.0 ? node->ref_distance : 1.0;
    double maximum = node->max_distance > reference ? node->max_distance : reference;
    double gain = 1.0;
    double clamped = std::max(distance, reference);
    switch (node->distance_model) {
      case 0: /* linear */
        gain = maximum > reference
                   ? 1.0 - node->rolloff * (clamped - reference) / (maximum - reference)
                   : 1.0;
        break;
      case 2: /* exponential */
        gain = std::pow(clamped / reference, -node->rolloff);
        break;
      case 1: /* inverse */
      default:
        gain = reference / (reference + node->rolloff * (clamped - reference));
        break;
    }
    gain = clamp_value(gain, 0.0, 1.0);

    /* Cone: a source radiates fully inside `cone_inner`, fades to
     * `cone_outerGain` by `cone_outer`, and stays there behind it. */
    if (node->cone_inner < 360.0 && distance > 1e-9) {
      double onorm = std::sqrt(ox * ox + oy * oy + oz * oz);
      if (onorm > 1e-9) {
        /* Angle between the source's own axis and the direction to the listener. */
        double toward = -(ux * ox + uy * oy + uz * oz) / onorm;
        double angle = std::acos(clamp_value(toward, -1.0, 1.0)) * 180.0 / kAudioPi;
        double inner = node->cone_inner * 0.5;
        double outer = node->cone_outer * 0.5;
        if (angle > outer) {
          gain *= node->cone_gain;
        } else if (angle > inner && outer > inner) {
          double t = (angle - inner) / (outer - inner);
          gain *= 1.0 + (node->cone_gain - 1.0) * t;
        }
      }
    }

    /* Equal-power pan: project the source direction on the listener's right
     * axis (forward × up) and steer the mono signal between the channels. */
    double rx = listener->fy * listener->uz - listener->fz * listener->uy;
    double ry = listener->fz * listener->ux - listener->fx * listener->uz;
    double rz = listener->fx * listener->uy - listener->fy * listener->ux;
    double rnorm = std::sqrt(rx * rx + ry * ry + rz * rz);
    double pan = 0.0;
    if (rnorm > 1e-9) pan = clamp_value((ux * rx + uy * ry + uz * rz) / rnorm, -1.0, 1.0);
    double angle = (pan + 1.0) * kAudioPi / 4.0;
    double left = mono * std::cos(angle) * gain;
    double right = mono * std::sin(angle) * gain;
    node->quantum[index] = static_cast<float>(left);
    node->quantum[index + 1] = static_cast<float>(right);
  }
}

}  // namespace xtgui
