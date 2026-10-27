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
  AudioParamState params[5];
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
