/*
 * xbintsc GUI engine — the Web Audio surface a page expects.
 *
 * There is no audio backend yet: the graph is created, values are stored and
 * read back, but nothing is rendered to the sound device. That is enough for
 * the common pattern of building the graph once (`createGain`, `connect`,
 * `AudioParam.setValueAtTime`, `createBuffer` + `getChannelData`) and then
 * driving it with `start`/`stop` — the program keeps running instead of
 * failing at `new AudioContext()`, and a real mixer can be dropped in behind
 * this interface later without touching the page.
 *
 * `createBuffer` hands out real `Float32Array` channels, so a page that fills
 * a buffer procedurally (noise beds, hand-built waveforms) works unchanged.
 */

#include "gui_engine.h"
#include "dom_api_internal.h"

#include <string>

namespace {

xt_value fn_of(void *fn) { return xt_closure_new(fn, 0, NULL); }

void set_prop(xt_value target, const char *name, xt_value value) {
  xt_object_set(target, xt_string_from_cstr(name), value);
}

void set_method(xt_value target, const char *name, void *fn) { set_prop(target, name, fn_of(fn)); }

void set_number(xt_value target, const char *name, double value) {
  set_prop(target, name, xt_number(value));
}

xt_value noop(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

/** `connect(node)` returns the destination, so `a.connect(b).connect(c)` chains
 * the same way it does in a browser. */
xt_value node_connect(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  xt_value target = xt_arg(argc, argv, 0);
  if (XT_IS_OBJECT(target)) {
    xt_value inputs = xt_object_get_cstr(target, "__xt_audio_inputs");
    if (!XT_IS_OBJECT(inputs)) {
      inputs = xt_object_new();
      xt_object_set(target, xt_string_from_cstr("__xt_audio_inputs"), inputs);
    }
  }
  return target;
}

xt_value node_start(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  set_prop(self, "started", XT_TRUE);
  return XT_UNDEFINED;
}

xt_value node_stop(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  set_prop(self, "started", XT_FALSE);
  return XT_UNDEFINED;
}

/* -- AudioParam ----------------------------------------------------------- */

xt_value param_value_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value value = xt_object_get_cstr(self, "__xt_audio_value");
  return XT_IS_NUMBER(value) ? value : xt_number(0);
}

xt_value param_value_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)self;
  xt_value value = xt_arg(argc, argv, 0);
  if (XT_IS_NUMBER(value)) set_prop(self, "__xt_audio_value", value);
  return value;
}

xt_value param_set_value_at_time(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  return param_value_set(self, env, 1, argv);
}

/** A ramp/curve call only records its target: nothing schedules yet. */
xt_value param_ramp(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc < 1) return self;
  xt_value value = xt_arg(argc, argv, 0);
  if (XT_IS_NUMBER(value)) set_prop(self, "__xt_audio_value", value);
  return self;
}

xt_value audio_param_build(double initial) {
  xt_value param = xt_object_new();
  set_number(param, "__xt_audio_value", initial);
  xt_object_define_getter(param, xt_string_from_cstr("value"), fn_of((void *)param_value_get));
  xt_object_define_setter(param, xt_string_from_cstr("value"), fn_of((void *)param_value_set));
  set_method(param, "setValueAtTime", (void *)param_set_value_at_time);
  set_method(param, "linearRampToValueAtTime", (void *)param_ramp);
  set_method(param, "exponentialRampToValueAtTime", (void *)param_ramp);
  set_method(param, "setTargetAtTime", (void *)param_ramp);
  set_method(param, "cancelScheduledValues", (void *)noop);
  set_method(param, "cancelAndHoldAtTime", (void *)noop);
  return param;
}

/** A node with the standard `AudioNode` shape. `params` names the AudioParams
 * the node owns (`gain`, `frequency`, `Q`, …). */
xt_value audio_node_build(const char *type, const char *const *params, int param_count) {
  xt_value node = xt_object_new();
  if (type != NULL) set_prop(node, "type", xt_string_from_cstr(type));
  for (int index = 0; index < param_count; index++) {
    set_prop(node, params[index], audio_param_build(0.0));
  }
  set_method(node, "connect", (void *)node_connect);
  set_method(node, "disconnect", (void *)noop);
  set_method(node, "start", (void *)node_start);
  set_method(node, "stop", (void *)node_stop);
  set_number(node, "channelCount", 2);
  return node;
}

/* -- AudioContext --------------------------------------------------------- */

xt_value buffer_get_channel_data(xt_value self, xt_value env, int32_t argc, xt_value *argv);

xt_value ctx_create_gain(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"gain"};
  return audio_node_build("gain", params, 1);
}

xt_value ctx_create_oscillator(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"frequency", "detune"};
  xt_value node = audio_node_build(NULL, params, 2);
  set_prop(node, "type", xt_string_from_cstr("sine"));
  return node;
}

xt_value ctx_create_buffer_source(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"playbackRate", "detune"};
  xt_value node = audio_node_build(NULL, params, 2);
  set_prop(node, "buffer", XT_NULL);
  set_prop(node, "loop", XT_FALSE);
  set_number(node, "loopStart", 0);
  set_number(node, "loopEnd", 0);
  return node;
}

xt_value ctx_create_biquad(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"frequency", "detune", "Q", "gain"};
  xt_value node = audio_node_build(NULL, params, 4);
  set_prop(node, "type", xt_string_from_cstr("lowpass"));
  return node;
}

xt_value ctx_create_compressor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"threshold", "knee",  "ratio",
                                       "attack",    "release"};
  return audio_node_build(NULL, params, 5);
}

/** `createBuffer(channels, length, sampleRate)`: real `Float32Array` channels. */
xt_value ctx_create_buffer(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  int channels = (int)xt_to_number(xt_arg(argc, argv, 0));
  int length = (int)xt_to_number(xt_arg(argc, argv, 1));
  double rate = xt_to_number(xt_arg(argc, argv, 2));
  if (channels < 1) channels = 1;
  if (length < 0) length = 0;
  xt_value array = xt_array_new(0, NULL);
  for (int index = 0; index < channels; index++) {
    xt_value size = xt_number((double)length);
    xt_value channel = xt_typed_array_ctor_by_name(xt_string_from_cstr("Float32Array"), 1, &size);
    xt_array_push(array, channel);
  }
  xt_value buffer = xt_object_new();
  set_prop(buffer, "__xt_audio_channels", array);
  set_method(buffer, "getChannelData", (void *)buffer_get_channel_data);
  set_number(buffer, "numberOfChannels", channels);
  set_number(buffer, "length", length);
  set_number(buffer, "sampleRate", rate);
  if (rate > 0) set_number(buffer, "duration", (double)length / rate);
  return buffer;
}

/** An empty `Float32Array` — the answer for a channel index a buffer does not
 * have, and for a buffer nothing filled. */
xt_value empty_channel(void) {
  xt_value zero = xt_number(0);
  return xt_typed_array_ctor_by_name(xt_string_from_cstr("Float32Array"), 1, &zero);
}

xt_value buffer_get_channel_data(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  /* Channels live in an *array* value (`XT_TAG_ARRAY`), which
   * `XT_IS_OBJECT` does not match. */
  xt_value channels = xt_object_get_cstr(self, "__xt_audio_channels");
  int index = (int)xt_to_number(xt_arg(argc, argv, 0));
  if (!XT_IS_ARRAY(channels) && !XT_IS_OBJECT(channels)) return empty_channel();
  if (index < 0 || index >= xt_array_size(channels)) return empty_channel();
  return xt_array_get(channels, xt_number((double)index));
}

xt_value ctx_decode_audio_data(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  /* No decoder: resolve with an empty buffer so a `await decodeAudioData(…)`
   * finishes instead of hanging the page. */
  xt_value size = xt_number(0);
  xt_value buffer = ctx_create_buffer(XT_UNDEFINED, XT_UNDEFINED, 3,
                                      (xt_value[]){xt_number(1), size, xt_number(44100)});
  (void)argc;
  (void)argv;
  return xt_promise_resolve(buffer);
}

xt_value ctx_resume(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  set_prop(self, "state", xt_string_from_cstr("running"));
  return xt_promise_resolve(XT_UNDEFINED);
}

xt_value ctx_suspend(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  set_prop(self, "state", xt_string_from_cstr("suspended"));
  return xt_promise_resolve(XT_UNDEFINED);
}

xt_value ctx_close(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  set_prop(self, "state", xt_string_from_cstr("closed"));
  return xt_promise_resolve(XT_UNDEFINED);
}

/** The context prototype: also the `instanceof AudioContext` target, and the
 * prototype of the object `new AudioContext()` builds. */
xt_value audio_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  xt_value built = xt_object_new();
  set_method(built, "createGain", (void *)ctx_create_gain);
  set_method(built, "createOscillator", (void *)ctx_create_oscillator);
  set_method(built, "createBufferSource", (void *)ctx_create_buffer_source);
  set_method(built, "createBiquadFilter", (void *)ctx_create_biquad);
  set_method(built, "createDynamicsCompressor", (void *)ctx_create_compressor);
  set_method(built, "createBuffer", (void *)ctx_create_buffer);
  set_method(built, "decodeAudioData", (void *)ctx_decode_audio_data);
  set_method(built, "resume", (void *)ctx_resume);
  set_method(built, "suspend", (void *)ctx_suspend);
  set_method(built, "close", (void *)ctx_close);
  proto = built;
  xt_gc_add_root(&proto);
  return proto;
}

}  // namespace

xt_value xt_gui_audio_proto(void) { return audio_proto(); }

xt_value xt_gui_audio_create(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value context = xt_object_new_with_proto(audio_proto());
  xt_value destination = audio_node_build(NULL, NULL, 0);
  set_prop(context, "state", xt_string_from_cstr("running"));
  set_prop(context, "destination", destination);
  set_number(context, "sampleRate", 48000);
  set_number(context, "currentTime", 0);
  set_number(context, "baseLatency", 0.01);
  set_number(context, "outputLatency", 0.01);
  xt_value listener = xt_object_new();
  set_prop(context, "listener", listener);
  return context;
}
