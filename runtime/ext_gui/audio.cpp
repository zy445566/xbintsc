/*
 * xbintsc GUI engine — the Web Audio surface a page expects.
 *
 * This unit is the JavaScript-facing half of Web Audio: AudioContext,
 * AudioNode, AudioParam and AudioBuffer objects, all backed by the native graph
 * in `audio_mixer.cpp`. `new AudioContext()` creates a mixer; the first
 * `start()`/`resume()` opens the SDL audio device (best effort — a headless
 * host stays silent), and `decodeAudioData` decodes WAV in-engine or asks the
 * optional `media` extension for compressed formats (`audio_decode.cpp`).
 *
 * The offline diagnostic used by the tests is `new AudioContext({ __offline:
 * true })` plus `ctx.__render(frames)`: it renders the same graph synchronously
 * and returns interleaved stereo Float32 data without touching a device.
 */

#include "gui_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_buffer.h"
#include "audio_decode.h"
#include "audio_internal.h"
#include "audio_mixer.h"

namespace {

using namespace xtgui;

xt_value fn_of(void *fn) { return xt_closure_new(fn, 0, NULL); }

void set_prop(xt_value target, const char *name, xt_value value) {
  xt_object_set(target, xt_string_from_cstr(name), value);
}

void set_method(xt_value target, const char *name, void *fn) { set_prop(target, name, fn_of(fn)); }

void set_number(xt_value target, const char *name, double value) {
  set_prop(target, name, xt_number(value));
}

void set_string(xt_value target, const char *name, const char *value) {
  set_prop(target, name, xt_string_from_cstr(value));
}

double number_prop(xt_value object, const char *name) {
  xt_value value = xt_object_get_cstr(object, name);
  return XT_IS_NUMBER(value) ? xt_to_number(value) : 0.0;
}

std::mutex g_registry_mutex;
std::unordered_map<uint64_t, std::shared_ptr<AudioMixer>> g_mixers;
uint64_t g_next_context = 1;

std::shared_ptr<AudioMixer> mixer_of(xt_value object) {
  return audio_mixer_by_id(audio_context_id(object));
}

struct NodeRef {
  std::shared_ptr<AudioMixer> mixer;
  uint64_t node = 0;
};

bool node_ref(xt_value object, NodeRef *out) {
  out->mixer = mixer_of(object);
  out->node = static_cast<uint64_t>(number_prop(object, "__xt_audio_node"));
  return out->mixer != nullptr && out->node != 0;
}

xt_value make_error(const char *message) {
  xt_value text = xt_string_from_cstr(message);
  xt_value error = xt_error_ctor_by_name(xt_string_from_cstr("Error"), 1, &text);
  xt_object_set(error, xt_string_from_cstr("name"), xt_string_from_cstr("EncodingError"));
  return error;
}

/** Reject a `decodeAudioData` promise and invoke its optional error callback. */
xt_value decode_failure(xt_value callback, const std::string &message) {
  xt_value error = make_error(message.c_str());
  if (XT_IS_FUNCTION(callback)) {
    xt_value argument = error;
    xt_closure_call(callback, 1, &argument);
  }
  return xt_promise_reject(error);
}

/** Wrap interleaved float samples in a Float32Array (the `__render` hook). */
xt_value interleaved_array(const float *samples, uint32_t count) {
  xt_value array_buffer = xt_array_buffer_alloc(count * static_cast<uint32_t>(sizeof(float)));
  if (count > 0) {
    xt_array_buffer_copy_bytes_in(array_buffer, 0,
                                  reinterpret_cast<const uint8_t *>(samples),
                                  count * static_cast<uint32_t>(sizeof(float)));
  }
  xt_value argument = array_buffer;
  return xt_typed_array_ctor_by_name(xt_string_from_cstr("Float32Array"), 1, &argument);
}

/** decodeAudioData resamples to the context rate; the mixer also handles a
 * buffer at another rate, but matching the spec keeps `buffer.sampleRate`
 * equal to `context.sampleRate`. Linear interpolation is plenty for the
 * playback path that follows. */
void resample_decoded(DecodedAudio *audio, int target_rate) {
  if (audio->sample_rate <= 0 || audio->channels <= 0 || audio->frames == 0 ||
      audio->sample_rate == target_rate) {
    return;
  }
  double ratio = static_cast<double>(target_rate) / audio->sample_rate;
  uint32_t frames = static_cast<uint32_t>(std::ceil(audio->frames * ratio));
  std::vector<float> resampled(static_cast<size_t>(frames) * audio->channels, 0.0f);
  for (uint32_t frame = 0; frame < frames; frame++) {
    double position = frame / ratio;
    uint32_t lower = static_cast<uint32_t>(position);
    if (lower >= audio->frames) lower = audio->frames - 1;
    uint32_t upper = lower + 1 < audio->frames ? lower + 1 : lower;
    double fraction = position - lower;
    for (int channel = 0; channel < audio->channels; channel++) {
      double from = audio->interleaved[static_cast<size_t>(lower) * audio->channels + channel];
      double to = audio->interleaved[static_cast<size_t>(upper) * audio->channels + channel];
      resampled[static_cast<size_t>(frame) * audio->channels + channel] =
          static_cast<float>(from + (to - from) * fraction);
    }
  }
  audio->interleaved.swap(resampled);
  audio->frames = frames;
  audio->sample_rate = target_rate;
}

/* -- AudioNode ------------------------------------------------------------ */

xt_value node_connect(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value target = xt_arg(argc, argv, 0);
  NodeRef source;
  NodeRef destination;
  if (node_ref(self, &source) && node_ref(target, &destination) &&
      audio_context_id(self) == audio_context_id(target)) {
    source.mixer->connect(source.node, destination.node);
  }
  return target;
}

xt_value node_disconnect(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  NodeRef source;
  if (!node_ref(self, &source)) return XT_UNDEFINED;
  if (argc > 0 && XT_IS_OBJECT(argv[0])) {
    NodeRef target;
    if (node_ref(argv[0], &target)) source.mixer->disconnect(source.node, target.node);
  } else {
    source.mixer->disconnect_all(source.node);
  }
  return XT_UNDEFINED;
}

xt_value node_start(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  NodeRef ref;
  if (!node_ref(self, &ref)) return XT_UNDEFINED;
  int kind = static_cast<int>(number_prop(self, "__xt_audio_kind"));
  double when = argc > 0 && XT_IS_NUMBER(argv[0]) ? std::max(0.0, xt_to_number(argv[0]))
                                                  : ref.mixer->current_time();
  if (kind == kAudioBufferSource) {
    std::shared_ptr<AudioClip> clip = audio_clip_from_buffer(xt_object_get_cstr(self, "buffer"));
    if (!clip) {
      xt_value message = xt_string_from_cstr("AudioBufferSourceNode.start: buffer is not set");
      xt_throw(xt_error_ctor_by_name(xt_string_from_cstr("Error"), 1, &message));
      return XT_UNDEFINED;
    }
    double offset = argc > 1 ? xt_to_number(argv[1]) : 0.0;
    double duration = argc > 2 ? xt_to_number(argv[2]) : -1.0;
    bool loop = xt_truthy(xt_object_get_cstr(self, "loop"));
    double loop_start = number_prop(self, "loopStart");
    double loop_end = number_prop(self, "loopEnd");
    ref.mixer->set_buffer(ref.node, clip, loop, loop_start, loop_end);
    ref.mixer->start_source(ref.node, when, offset, duration);
  } else {
    ref.mixer->start_source(ref.node, when, 0.0, -1.0);
  }
  ref.mixer->start_device();
  set_prop(self, "started", XT_TRUE);
  return XT_UNDEFINED;
}

xt_value node_stop(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  NodeRef ref;
  if (!node_ref(self, &ref)) return XT_UNDEFINED;
  double when = argc > 0 && XT_IS_NUMBER(argv[0]) ? std::max(0.0, xt_to_number(argv[0]))
                                                  : ref.mixer->current_time();
  ref.mixer->stop_source(ref.node, when);
  set_prop(self, "started", XT_FALSE);
  return XT_UNDEFINED;
}

xt_value node_type_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value value = xt_object_get_cstr(self, "__xt_audio_type");
  return XT_IS_STRING(value) ? value : xt_string_from_cstr("");
}

xt_value node_type_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_to_string(xt_arg(argc, argv, 0));
  const char *name = XT_IS_STRING(value) ? xt_string_data(value) : "";
  int kind = static_cast<int>(number_prop(self, "__xt_audio_kind"));
  NodeRef ref;
  node_ref(self, &ref);
  if (kind == kAudioOscillator) {
    AudioOscType type = kAudioOscSine;
    if (std::strcmp(name, "square") == 0) type = kAudioOscSquare;
    else if (std::strcmp(name, "sawtooth") == 0) type = kAudioOscSawtooth;
    else if (std::strcmp(name, "triangle") == 0) type = kAudioOscTriangle;
    else if (std::strcmp(name, "sine") != 0) return value;
    if (ref.mixer) ref.mixer->set_osc_type(ref.node, type);
    set_prop(self, "__xt_audio_type", value);
  } else if (kind == kAudioBiquad) {
    AudioBiquadType type = kAudioBiquadLowpass;
    if (std::strcmp(name, "highpass") == 0) type = kAudioBiquadHighpass;
    else if (std::strcmp(name, "bandpass") == 0) type = kAudioBiquadBandpass;
    else if (std::strcmp(name, "lowshelf") == 0) type = kAudioBiquadLowshelf;
    else if (std::strcmp(name, "highshelf") == 0) type = kAudioBiquadHighshelf;
    else if (std::strcmp(name, "peaking") == 0) type = kAudioBiquadPeaking;
    else if (std::strcmp(name, "notch") == 0) type = kAudioBiquadNotch;
    else if (std::strcmp(name, "allpass") == 0) type = kAudioBiquadAllpass;
    else if (std::strcmp(name, "lowpass") != 0) return value;
    if (ref.mixer) ref.mixer->set_biquad_type(ref.node, type);
    set_prop(self, "__xt_audio_type", value);
  }
  return value;
}

xt_value build_node(uint64_t context, AudioNodeKind kind, const char *type,
                    const char *const *params, int param_count) {
  std::shared_ptr<AudioMixer> mixer = audio_mixer_by_id(context);
  if (!mixer) return XT_UNDEFINED;
  uint64_t id = mixer->create_node(kind);
  xt_value node = xt_object_new();
  set_number(node, "__xt_audio_ctx", static_cast<double>(context));
  set_number(node, "__xt_audio_node", static_cast<double>(id));
  set_number(node, "__xt_audio_kind", kind);
  if (type != nullptr) set_string(node, "__xt_audio_type", type);
  set_method(node, "connect", (void *)node_connect);
  set_method(node, "disconnect", (void *)node_disconnect);
  set_method(node, "start", (void *)node_start);
  set_method(node, "stop", (void *)node_stop);
  set_number(node, "channelCount", 2);
  for (int index = 0; index < param_count; index++) {
    xt_value param = audio_param_build(context, id, index);
    set_number(param, "defaultValue", mixer->param_value(id, index));
    set_prop(node, params[index], param);
  }
  if (kind == kAudioOscillator || kind == kAudioBiquad) {
    xt_object_define_getter(node, xt_string_from_cstr("type"), fn_of((void *)node_type_get));
    xt_object_define_setter(node, xt_string_from_cstr("type"), fn_of((void *)node_type_set));
  }
  if (kind == kAudioBufferSource) {
    set_prop(node, "buffer", XT_NULL);
    set_prop(node, "loop", XT_FALSE);
    set_number(node, "loopStart", 0);
    set_number(node, "loopEnd", 0);
  }
  if (kind == kAudioOscillator || kind == kAudioBufferSource ||
      kind == kAudioConstantSource) {
    set_prop(node, "started", XT_FALSE);
  }
  return node;
}

/* -- AudioContext --------------------------------------------------------- */

xt_value ctx_create_gain(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"gain"};
  return build_node(audio_context_id(self), kAudioGain, nullptr, params, 1);
}

xt_value ctx_create_oscillator(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"frequency", "detune"};
  return build_node(audio_context_id(self), kAudioOscillator, "sine", params, 2);
}

xt_value ctx_create_buffer_source(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"playbackRate", "detune"};
  return build_node(audio_context_id(self), kAudioBufferSource, nullptr, params, 2);
}

xt_value ctx_create_biquad(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"frequency", "detune", "Q", "gain"};
  return build_node(audio_context_id(self), kAudioBiquad, "lowpass", params, 4);
}

xt_value ctx_create_compressor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"threshold", "knee", "ratio", "attack", "release"};
  return build_node(audio_context_id(self), kAudioCompressor, nullptr, params, 5);
}

xt_value ctx_create_stereo_panner(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"pan"};
  return build_node(audio_context_id(self), kAudioStereoPanner, nullptr, params, 1);
}

xt_value ctx_create_constant_source(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"offset"};
  return build_node(audio_context_id(self), kAudioConstantSource, nullptr, params, 1);
}

xt_value ctx_create_delay(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  static const char *const params[] = {"delayTime"};
  uint64_t context = audio_context_id(self);
  xt_value node = build_node(context, kAudioDelay, nullptr, params, 1);
  std::shared_ptr<AudioMixer> mixer = audio_mixer_by_id(context);
  if (mixer && XT_IS_OBJECT(node)) {
    double maximum = argc > 0 && XT_IS_NUMBER(argv[0]) ? xt_to_number(argv[0]) : 1.0;
    mixer->set_delay_max(static_cast<uint64_t>(number_prop(node, "__xt_audio_node")), maximum);
  }
  return node;
}

/** `createBuffer(channels, length, sampleRate)`: real `Float32Array` channels. */
xt_value ctx_create_buffer(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  int channels = static_cast<int>(xt_to_number(xt_arg(argc, argv, 0)));
  int length = static_cast<int>(xt_to_number(xt_arg(argc, argv, 1)));
  double rate = xt_to_number(xt_arg(argc, argv, 2));
  if (channels < 1) channels = 1;
  if (channels > 32) channels = 32;
  if (length < 0) length = 0;
  std::vector<std::vector<float>> data(static_cast<size_t>(channels));
  for (std::vector<float> &channel : data) channel.assign(static_cast<size_t>(length), 0.0f);
  return audio_buffer_create(data, rate > 0 ? static_cast<int>(rate) : kAudioMixRate);
}

xt_value ctx_decode_audio_data(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  xt_value input = xt_arg(argc, argv, 0);
  xt_value success = xt_arg(argc, argv, 1);
  xt_value failure = xt_arg(argc, argv, 2);
  std::vector<uint8_t> bytes;
  if (!audio_bytes_from_value(input, &bytes)) {
    return decode_failure(failure, "Unable to decode audio data: expected an ArrayBuffer");
  }
  DecodedAudio decoded;
  std::string error;
  if (!decode_audio_bytes(bytes.data(), bytes.size(), &decoded, &error)) {
    return decode_failure(failure, "Unable to decode audio data: " + error);
  }
  resample_decoded(&decoded, kAudioMixRate);
  std::vector<std::vector<float>> channels(static_cast<size_t>(decoded.channels));
  for (int channel = 0; channel < decoded.channels; channel++) {
    std::vector<float> &samples = channels[static_cast<size_t>(channel)];
    samples.resize(decoded.frames);
    for (uint32_t frame = 0; frame < decoded.frames; frame++) {
      samples[frame] = decoded.interleaved[static_cast<size_t>(frame) * decoded.channels + channel];
    }
  }
  xt_value buffer = audio_buffer_create(channels, decoded.sample_rate);
  if (XT_IS_FUNCTION(success)) {
    xt_value argument = buffer;
    xt_closure_call(success, 1, &argument);
  }
  return xt_promise_resolve(buffer);
}

xt_value ctx_current_time_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  std::shared_ptr<AudioMixer> mixer = mixer_of(self);
  if (mixer) return xt_number(mixer->current_time());
  xt_value stored = xt_object_get_cstr(self, "__xt_audio_time");
  return XT_IS_NUMBER(stored) ? stored : xt_number(0);
}

xt_value ctx_resume(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  std::shared_ptr<AudioMixer> mixer = mixer_of(self);
  if (mixer) mixer->resume_device();
  set_string(self, "state", "running");
  return xt_promise_resolve(XT_UNDEFINED);
}

xt_value ctx_suspend(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  std::shared_ptr<AudioMixer> mixer = mixer_of(self);
  if (mixer) mixer->suspend_device();
  set_string(self, "state", "suspended");
  return xt_promise_resolve(XT_UNDEFINED);
}

xt_value ctx_close(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  uint64_t context = audio_context_id(self);
  std::shared_ptr<AudioMixer> mixer = audio_mixer_by_id(context);
  if (mixer) {
    set_number(self, "__xt_audio_time", mixer->current_time());
    mixer->close_device();
    mixer->shutdown();
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_mixers.erase(context);
  }
  set_string(self, "state", "closed");
  return xt_promise_resolve(XT_UNDEFINED);
}

/** Offline diagnostic: render `frames` stereo frames from an offline context
 * and return them as an interleaved Float32Array. */
xt_value ctx_render(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  std::shared_ptr<AudioMixer> mixer = mixer_of(self);
  if (!mixer || !mixer->offline()) return XT_NULL;
  double requested = xt_to_number(xt_arg(argc, argv, 0));
  if (!(requested > 0)) return interleaved_array(nullptr, 0);
  if (requested > 48000.0 * 600.0) requested = 48000.0 * 600.0;
  uint32_t frames = static_cast<uint32_t>(requested);
  std::vector<float> samples(static_cast<size_t>(frames) * kAudioMixChannels, 0.0f);
  mixer->render_offline(samples.data(), static_cast<int>(frames));
  return interleaved_array(samples.data(), frames * kAudioMixChannels);
}

/** The context prototype: also the `instanceof AudioContext` target, and the
 * prototype of the object `new AudioContext()` builds. */
xt_value audio_proto() {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  xt_value built = xt_object_new();
  set_method(built, "createGain", (void *)ctx_create_gain);
  set_method(built, "createOscillator", (void *)ctx_create_oscillator);
  set_method(built, "createBufferSource", (void *)ctx_create_buffer_source);
  set_method(built, "createBiquadFilter", (void *)ctx_create_biquad);
  set_method(built, "createDynamicsCompressor", (void *)ctx_create_compressor);
  set_method(built, "createStereoPanner", (void *)ctx_create_stereo_panner);
  set_method(built, "createConstantSource", (void *)ctx_create_constant_source);
  set_method(built, "createDelay", (void *)ctx_create_delay);
  set_method(built, "createBuffer", (void *)ctx_create_buffer);
  set_method(built, "decodeAudioData", (void *)ctx_decode_audio_data);
  set_method(built, "resume", (void *)ctx_resume);
  set_method(built, "suspend", (void *)ctx_suspend);
  set_method(built, "close", (void *)ctx_close);
  set_method(built, "__render", (void *)ctx_render);
  proto = built;
  xt_gc_add_root(&proto);
  return proto;
}

}  // namespace

namespace xtgui {

std::shared_ptr<AudioMixer> audio_mixer_by_id(uint64_t id) {
  if (id == 0) return nullptr;
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  auto it = g_mixers.find(id);
  return it == g_mixers.end() ? nullptr : it->second;
}

uint64_t audio_context_id(xt_value object) {
  xt_value value = xt_object_get_cstr(object, "__xt_audio_ctx");
  return XT_IS_NUMBER(value) ? static_cast<uint64_t>(xt_to_number(value)) : 0;
}

}  // namespace xtgui

xt_value xt_gui_audio_proto(void) { return audio_proto(); }

/** Close every live context before `SDL_Quit`. The mixers hold SDL audio
 * streams whose destructors must not run after the audio subsystem (and its
 * device objects) were torn down; keeping the registry alive until static
 * destruction was a crash on exit for every program that played a sound. */
void xt_gui_audio_shutdown(void) {
  std::vector<std::shared_ptr<AudioMixer>> mixers;
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    mixers.reserve(g_mixers.size());
    for (const auto &entry : g_mixers) mixers.push_back(entry.second);
    g_mixers.clear();
  }
  for (const std::shared_ptr<AudioMixer> &mixer : mixers) {
    mixer->close_device();
    mixer->shutdown();
  }
}

xt_value xt_gui_audio_create(int32_t argc, xt_value *argv) {
  bool offline = false;
  xt_value options = xt_arg(argc, argv, 0);
  if (XT_IS_OBJECT(options)) {
    offline = xt_truthy(xt_object_get_cstr(options, "__offline"));
  }
  uint64_t context = g_next_context++;
  std::shared_ptr<AudioMixer> mixer = std::make_shared<AudioMixer>(offline);
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_mixers[context] = mixer;
  }
  xt_value value = xt_object_new_with_proto(audio_proto());
  set_number(value, "__xt_audio_ctx", static_cast<double>(context));
  set_string(value, "state", "running");
  set_number(value, "sampleRate", kAudioMixRate);
  set_number(value, "baseLatency", 0.01);
  set_number(value, "outputLatency", 0.01);
  xt_object_define_getter(value, xt_string_from_cstr("currentTime"),
                          fn_of((void *)ctx_current_time_get));
  xt_value destination = build_node(context, kAudioDestination, nullptr, nullptr, 0);
  set_prop(value, "destination", destination);
  xt_value listener = xt_object_new();
  set_prop(value, "listener", listener);
  return value;
}
