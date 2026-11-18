/*
 * xbintsc GUI engine — the Web Audio mixer.
 *
 * `audio.cpp` owns the JavaScript-facing objects (AudioContext, AudioNode,
 * AudioParam, AudioBuffer); this unit owns the graph those objects describe and
 * turns it into interleaved 48 kHz stereo float PCM. It knows nothing about
 * `xt_value`, the DOM or the garbage collector, which is what lets the SDL
 * audio callback render on its own thread without touching a runtime value.
 *
 * Rendering follows the Web Audio model: 128-frame render quanta, multiple
 * inputs summed at every node, and an AudioParam timeline evaluated per sample.
 * A node's quantum is cached for the quantum serial, so a node feeding two
 * destinations (fan-out) is computed once and a cycle renders silence instead
 * of recursing forever.
 *
 * Threading: the graph is mutated from the page thread (create/connect/start)
 * and read from the SDL audio thread (render). Every public method takes the
 * same mutex, so a mutation never interleaves with a render. No callback ever
 * touches an `xt_value`.
 */
#ifndef XT_GUI_AUDIO_MIXER_H
#define XT_GUI_AUDIO_MIXER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

struct SDL_AudioStream;

namespace xtgui {

/** Mix rate of every context (SDL converts to the device rate). */
constexpr int kAudioMixRate = 48000;
constexpr int kAudioMixChannels = 2;
/** Web Audio's render quantum, in frames. */
constexpr int kAudioQuantum = 128;

enum AudioNodeKind {
  kAudioDestination,
  kAudioGain,
  kAudioOscillator,
  kAudioBufferSource,
  kAudioBiquad,
  kAudioCompressor,
  kAudioStereoPanner,
  kAudioConstantSource,
  kAudioDelay,
  kAudioWaveShaper,
  kAudioConvolver,
  kAudioAnalyser,
  kAudioPanner,
  /** A media element / media stream source. The engine has no capture or media
   * pipeline, so the node is silent; it exists so `createMediaElementSource`
   * and friends behave like the platform instead of throwing. */
  kAudioMediaSource,
  /** The `AudioContext.listener`: no audio of its own, but it owns the nine
   * AudioParams (`positionX`…`upZ`) the panner reads. */
  kAudioListener,
};

enum AudioOscType { kAudioOscSine, kAudioOscSquare, kAudioOscSawtooth, kAudioOscTriangle };

enum AudioBiquadType {
  kAudioBiquadLowpass,
  kAudioBiquadHighpass,
  kAudioBiquadBandpass,
  kAudioBiquadLowshelf,
  kAudioBiquadHighshelf,
  kAudioBiquadPeaking,
  kAudioBiquadNotch,
  kAudioBiquadAllpass,
};

/** One decoded/created AudioBuffer's samples, in planar float.
 *
 * A buffer source takes a reference to one of these when it starts. Decoding
 * (or an AudioBuffer handed to `start`) copies the samples once, so playback
 * never reads a runtime value and a buffer can be collected while it plays. */
struct AudioClip {
  std::vector<std::vector<float>> channels;
  int sample_rate = kAudioMixRate;
  uint32_t length = 0;

  double duration() const {
    return sample_rate > 0 ? static_cast<double>(length) / sample_rate : 0.0;
  }
};

/** One AudioParam: a base value plus the scheduled automation events.
 *
 * Events are evaluated on demand at a sample time, which keeps scheduling and
 * rendering independent: a page may schedule seconds ahead, and a source may
 * start with a nonzero `when`. */
class AudioParamState {
 public:
  void reset(double value);
  /** The `value` setter: takes effect at `time` and overrides older events. */
  void set_value(double value, double time);
  void set_value_at_time(double value, double time);
  void linear_ramp(double value, double time);
  void exponential_ramp(double value, double time);
  void set_target(double value, double time, double time_constant);
  void cancel_scheduled(double time);
  void cancel_and_hold(double time);
  double value_at(double time) const;

 private:
  enum EventKind { kEventSet, kEventLinear, kEventExponential, kEventTarget };
  struct Event {
    double time;
    EventKind kind;
    double value;
    double time_constant;
  };

  double evaluate(size_t index, double time) const;
  void insert(Event event);

  double base_ = 0.0;
  std::vector<Event> events_;
};

/** One graph node. Only the fields a node's kind uses are meaningful. */
struct AudioNode {
  uint64_t id = 0;
  AudioNodeKind kind = kAudioGain;
  std::vector<uint64_t> inputs;
  /** Panner (6) and listener (9) are the widest users; effects use fewer. */
  static constexpr int kMaxParams = 9;
  AudioParamState params[kMaxParams];
  int param_count = 0;

  /* Per-quantum cache: 2 * kAudioQuantum interleaved floats. */
  std::vector<float> quantum;
  uint64_t serial = 0;
  bool rendering = false;

  /* Oscillator. */
  AudioOscType osc_type = kAudioOscSine;
  double phase = 0.0;

  /* Sources (oscillator and buffer). */
  bool started = false;
  bool began = false;
  bool ended = false;
  double start_time = 0.0;
  double stop_time = -1.0;
  std::shared_ptr<AudioClip> clip;
  double clip_offset = 0.0;
  double clip_duration = 0.0;
  double clip_pos = 0.0;
  bool loop = false;
  double loop_start = 0.0;
  double loop_end = 0.0;

  /* Biquad filter state, one set per channel. */
  AudioBiquadType biquad_type = kAudioBiquadLowpass;
  double x1[2] = {0.0, 0.0};
  double x2[2] = {0.0, 0.0};
  double y1[2] = {0.0, 0.0};
  double y2[2] = {0.0, 0.0};

  /* Compressor envelope, in dB of gain reduction. */
  double compressor_env = 0.0;

  /* Delay line, one ring per channel. `delay_size` is 0 until the JS factory
   * has supplied `maxDelayTime`. */
  std::vector<float> delay[2];
  size_t delay_size = 0;
  size_t delay_write = 0;
  double delay_max = 1.0;

  /* WaveShaper: the transfer curve sampled uniformly over [-1, 1]. A missing
   * curve passes the signal through unchanged, as on the platform.
   * `shaper_oversample` is 1, 2 or 4: the number of sub-samples averaged per
   * frame, which softens the aliasing a hard curve would fold back. */
  std::vector<float> shaper_curve;
  int shaper_oversample = 1;

  /* Convolver: uniformly partitioned overlap-save convolution. The impulse
   * response is cut into `conv_partitions` slices of `kAudioQuantum` frames,
   * each zero-padded to `2 * kAudioQuantum` and transformed once; the recent
   * input blocks are kept as spectra in a ring so each quantum costs one FFT,
   * one complex multiply-accumulate per partition, and one inverse FFT. */
  struct ConvolverState {
    bool ready = false;
    size_t partitions = 0;
    size_t ring_index = 0;
    float gain = 1.0f;
    /** Spectral history of the input, per channel, `partitions * spectrum` floats. */
    std::vector<float> input_re[2];
    std::vector<float> input_im[2];
    /** The impulse response's partition spectra, same layout. */
    std::vector<float> ir_re[2];
    std::vector<float> ir_im[2];
    /** The last `2 * kAudioQuantum` input samples, per channel (overlap-save). */
    std::vector<float> window[2];
  };
  ConvolverState convolver;

  /* Analyser: a ring of the most recent mono samples, plus the transform size
   * the page asked for (`fft_size`), which also bounds the ring. The smoothed
   * spectrum carries the previous call's magnitudes so
   * `smoothingTimeConstant` is a moving average, as on the platform. */
  int analyser_fft = 2048;
  std::vector<float> analyser_ring;
  size_t analyser_write = 0;
  std::vector<float> analyser_smoothed;
  float analyser_smoothing = 0.8f;

  /* Panner: spatialization parameters that are plain values (the moving parts —
   * position and orientation — are AudioParams, indices 0…5). */
  int panning_model = 0;       /* 0 = equal power, 1 = HRTF (same path here) */
  int distance_model = 1;      /* 0 = linear, 1 = inverse, 2 = exponential */
  double ref_distance = 1.0;
  double max_distance = 10000.0;
  double rolloff = 1.0;
  double cone_inner = 360.0;
  double cone_outer = 360.0;
  double cone_gain = 0.0;
};

/** One AudioContext's graph, its device stream and its clock. */
class AudioMixer {
 public:
  explicit AudioMixer(bool offline);
  ~AudioMixer();

  AudioMixer(const AudioMixer &) = delete;
  AudioMixer &operator=(const AudioMixer &) = delete;

  bool offline() const { return offline_; }

  /* -- graph ------------------------------------------------------------- */
  uint64_t create_node(AudioNodeKind kind);
  void connect(uint64_t source, uint64_t target);
  void disconnect(uint64_t source, uint64_t target);
  void disconnect_all(uint64_t source);
  void set_osc_type(uint64_t node, AudioOscType type);
  void set_biquad_type(uint64_t node, AudioBiquadType type);
  /** Allocate a delay line long enough for `seconds` (constructor argument of
   * `createDelay`). */
  void set_delay_max(uint64_t node, double seconds);
  void set_buffer(uint64_t node, std::shared_ptr<AudioClip> clip, bool loop, double loop_start,
                  double loop_end);
  void start_source(uint64_t node, double when, double offset, double duration);
  void stop_source(uint64_t node, double when);

  /* -- effect nodes ------------------------------------------------------- */
  /** WaveShaper: the curve (may be empty, which passes the signal through) and
   * the oversample factor (1, 2 or 4). */
  void set_shaper_curve(uint64_t node, const std::vector<float> &curve, int oversample);
  void set_shaper_oversample(uint64_t node, int oversample);
  /** Convolver: build the partition spectra from an impulse response. The
   * response is resampled to the mix rate and, when `normalize` is set, scaled
   * so the convolution does not change a white-noise input's level. */
  void set_convolver_impulse(uint64_t node, std::shared_ptr<AudioClip> clip, bool normalize);
  /** `fftSize` of an AnalyserNode (32…32768, power of two). */
  void set_analyser_fft(uint64_t node, int fft_size);
  /** `smoothingTimeConstant` of an AnalyserNode (0…1). */
  void set_analyser_smoothing(uint64_t node, float smoothing);
  /** Copy the analyser's most recent `count` mono samples, oldest first. */
  bool analyser_time_domain(uint64_t node, int count, std::vector<float> *out);
  /** Magnitude spectrum in dB of the analyser's window (`fft_size / 2` bins). */
  bool analyser_spectrum(uint64_t node, std::vector<float> *out);
  /** Panner values that are not AudioParams. */
  void set_panner(uint64_t node, int distance_model, double ref_distance, double max_distance,
                  double rolloff, double cone_inner, double cone_outer, double cone_gain,
                  int panning_model);
  /** The listener the panners are mixed against. */
  uint64_t listener_id() const { return listener_id_; }
  void set_listener(uint64_t node) { listener_id_ = node; }

  /* -- AudioParam -------------------------------------------------------- */
  double param_value(uint64_t node, int index);
  void param_set(uint64_t node, int index, double value);
  void param_set_at(uint64_t node, int index, double value, double time);
  void param_linear_ramp(uint64_t node, int index, double value, double time);
  void param_exponential_ramp(uint64_t node, int index, double value, double time);
  void param_set_target(uint64_t node, int index, double value, double time, double time_constant);
  void param_cancel(uint64_t node, int index, double time, bool hold);

  /* -- clock and device -------------------------------------------------- */
  double current_time();
  /** Best effort: a headless host has no device and playback stays silent. */
  bool start_device();
  void suspend_device();
  void resume_device();
  void close_device();
  bool has_device();
  /** Drop the graph; called by `AudioContext.close()`. */
  void shutdown();

  /** Render `frames` stereo frames (interleaved) from the graph. Safe to call
   * from the audio callback and from `render_offline`. */
  void render(float *out, int frames);
  /** Synchronous render for the offline diagnostic hook: no device involved. */
  void render_offline(float *out, int frames);

 private:
  AudioNode *node_locked(uint64_t id);
  const float *pull_locked(AudioNode *node);
  void render_quantum_locked();
  void render_locked(float *out, int frames);

  std::mutex mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<AudioNode>> nodes_;
  uint64_t next_node_id_ = 1;
  uint64_t destination_id_ = 0;
  uint64_t listener_id_ = 0;
  uint64_t serial_ = 0;
  uint64_t frames_rendered_ = 0;
  std::vector<float> pending_;
  size_t pending_offset_ = 0;

  const bool offline_;
  bool running_ = true;
  SDL_AudioStream *stream_ = nullptr;
  bool device_tried_ = false;
};

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_MIXER_H */
