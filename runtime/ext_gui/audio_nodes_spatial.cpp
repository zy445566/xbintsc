/*
 * xbintsc GUI engine — the Web Audio node kinds beyond the classic set, part 2:
 * Analyser and Panner.
 *
 * The Analyser's getters pull a spectrum or a time window out of the mixer
 * (which keeps the samples the render thread produced) and convert it to the
 * decibel/byte ranges the Web Audio API specifies. The Panner exposes position
 * and orientation as AudioParams (indices 0…5) so automation works, and keeps
 * everything else — distance model, cone — as plain values that are pushed into
 * the mixer whenever they change.
 */

#include <cmath>
#include <string>
#include <vector>

#include "audio_nodes_internal.h"

namespace xtgui {
namespace {

using xtgui::audio_clamp_number;
using xtgui::audio_define_accessor;
using xtgui::audio_node_set_prop;
using xtgui::audio_text_prop;
using xtgui::audio_write_numeric_array;
using xtgui::audio_typed_length;

/** Round down to a power of two inside the AnalyserNode range. */
int analyser_fft_size(double requested) {
  int size = 32;
  while (size < 32768 && size * 2 <= static_cast<int>(requested)) size *= 2;
  return size;
}

xt_value analyser_fft_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(audio_number_prop(self, "__xt_analyser_fft"));
}

xt_value analyser_fft_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int size = analyser_fft_size(xt_to_number(xt_arg(argc, argv, 0)));
  audio_node_set_prop(self, "__xt_analyser_fft", xt_number(static_cast<double>(size)));
  uint64_t context = 0;
  uint64_t id = 0;
  if (audio_node_ref(self, &context, &id)) audio_mixer_of(self)->set_analyser_fft(id, size);
  return xt_number(static_cast<double>(size));
}

xt_value analyser_bin_count_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(audio_number_prop(self, "__xt_analyser_fft") / 2.0);
}

xt_value analyser_smoothing_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value stored = xt_object_get_cstr(self, "__xt_analyser_smoothing");
  return XT_IS_NUMBER(stored) ? stored : xt_number(0.8);
}

xt_value analyser_smoothing_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  double value = audio_clamp_number(xt_to_number(xt_arg(argc, argv, 0)), 0.0, 1.0);
  audio_node_set_prop(self, "__xt_analyser_smoothing", xt_number(value));
  uint64_t context = 0;
  uint64_t id = 0;
  if (audio_node_ref(self, &context, &id)) {
    audio_mixer_of(self)->set_analyser_smoothing(id, static_cast<float>(value));
  }
  return xt_number(value);
}

xt_value analyser_min_db_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value stored = xt_object_get_cstr(self, "__xt_analyser_min_db");
  return XT_IS_NUMBER(stored) ? stored : xt_number(-100);
}

xt_value analyser_min_db_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  double value = xt_to_number(xt_arg(argc, argv, 0));
  double maximum = audio_number_prop(self, "__xt_analyser_max_db");
  if (maximum <= value) value = maximum - 1.0;
  audio_node_set_prop(self, "__xt_analyser_min_db", xt_number(value));
  return xt_number(value);
}

xt_value analyser_max_db_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value stored = xt_object_get_cstr(self, "__xt_analyser_max_db");
  return XT_IS_NUMBER(stored) ? stored : xt_number(-30);
}

xt_value analyser_max_db_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  double value = xt_to_number(xt_arg(argc, argv, 0));
  double minimum = audio_number_prop(self, "__xt_analyser_min_db");
  if (value <= minimum) value = minimum + 1.0;
  audio_node_set_prop(self, "__xt_analyser_max_db", xt_number(value));
  return xt_number(value);
}

/** `getByteFrequencyData(array)`: decibels mapped onto 0…255. */
xt_value analyser_byte_frequency(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint64_t context = 0;
  uint64_t id = 0;
  xt_value array = xt_arg(argc, argv, 0);
  if (!audio_node_ref(self, &context, &id) || audio_typed_length(array) == 0) return XT_UNDEFINED;
  std::vector<float> spectrum;
  if (!audio_mixer_of(self)->analyser_spectrum(id, &spectrum)) return XT_UNDEFINED;
  double minimum = audio_number_prop(self, "__xt_analyser_min_db");
  double maximum = audio_number_prop(self, "__xt_analyser_max_db");
  double span = maximum - minimum > 0 ? maximum - minimum : 1.0;
  uint32_t count = audio_typed_length(array);
  for (uint32_t i = 0; i < count; i++) {
    double decibels = i < spectrum.size() ? spectrum[i] : minimum;
    double scaled = (decibels - minimum) / span * 255.0;
    audio_write_numeric_array(array, i, audio_clamp_number(scaled, 0.0, 255.0));
  }
  return XT_UNDEFINED;
}

/** `getFloatFrequencyData(array)`: decibels, one per bin. */
xt_value analyser_float_frequency(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint64_t context = 0;
  uint64_t id = 0;
  xt_value array = xt_arg(argc, argv, 0);
  if (!audio_node_ref(self, &context, &id) || audio_typed_length(array) == 0) return XT_UNDEFINED;
  std::vector<float> spectrum;
  if (!audio_mixer_of(self)->analyser_spectrum(id, &spectrum)) return XT_UNDEFINED;
  uint32_t count = audio_typed_length(array);
  for (uint32_t i = 0; i < count; i++) {
    audio_write_numeric_array(array, i, i < spectrum.size() ? spectrum[i] : -100.0);
  }
  return XT_UNDEFINED;
}

/** `getByteTimeDomainData(array)`: samples as unsigned bytes around 128. */
xt_value analyser_byte_time(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint64_t context = 0;
  uint64_t id = 0;
  xt_value array = xt_arg(argc, argv, 0);
  if (!audio_node_ref(self, &context, &id) || audio_typed_length(array) == 0) return XT_UNDEFINED;
  std::vector<float> samples;
  int count = static_cast<int>(audio_typed_length(array));
  if (!audio_mixer_of(self)->analyser_time_domain(id, count, &samples)) return XT_UNDEFINED;
  for (uint32_t i = 0; i < static_cast<uint32_t>(count) && i < samples.size(); i++) {
    double value = samples[i] * 128.0 + 128.0;
    audio_write_numeric_array(array, i, audio_clamp_number(value, 0.0, 255.0));
  }
  return XT_UNDEFINED;
}

/** `getFloatTimeDomainData(array)`: the same window as floats. */
xt_value analyser_float_time(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint64_t context = 0;
  uint64_t id = 0;
  xt_value array = xt_arg(argc, argv, 0);
  if (!audio_node_ref(self, &context, &id) || audio_typed_length(array) == 0) return XT_UNDEFINED;
  std::vector<float> samples;
  int count = static_cast<int>(audio_typed_length(array));
  if (!audio_mixer_of(self)->analyser_time_domain(id, count, &samples)) return XT_UNDEFINED;
  for (uint32_t i = 0; i < static_cast<uint32_t>(count) && i < samples.size(); i++) {
    audio_write_numeric_array(array, i, samples[i]);
  }
  return XT_UNDEFINED;
}

/* -- Panner ---------------------------------------------------------------- */

void panner_apply(xt_value self) {
  uint64_t context = 0;
  uint64_t id = 0;
  if (!audio_node_ref(self, &context, &id)) return;
  std::string distance_model = audio_text_prop(self, "__xt_panner_distance_model");
  int model = 1; /* inverse */
  if (distance_model == "linear") model = 0;
  else if (distance_model == "exponential") model = 2;
  int panning = audio_text_prop(self, "__xt_panner_panning_model") == "HRTF" ? 1 : 0;
  double ref_distance = audio_number_prop(self, "__xt_panner_ref_distance");
  if (xt_object_get_cstr(self, "__xt_panner_ref_distance") == XT_UNDEFINED) ref_distance = 1.0;
  double max_distance = audio_number_prop(self, "__xt_panner_max_distance");
  if (xt_object_get_cstr(self, "__xt_panner_max_distance") == XT_UNDEFINED) max_distance = 10000.0;
  double rolloff = audio_number_prop(self, "__xt_panner_rolloff");
  if (xt_object_get_cstr(self, "__xt_panner_rolloff") == XT_UNDEFINED) rolloff = 1.0;
  double cone_inner = audio_number_prop(self, "__xt_panner_cone_inner");
  if (xt_object_get_cstr(self, "__xt_panner_cone_inner") == XT_UNDEFINED) cone_inner = 360.0;
  double cone_outer = audio_number_prop(self, "__xt_panner_cone_outer");
  if (xt_object_get_cstr(self, "__xt_panner_cone_outer") == XT_UNDEFINED) cone_outer = 360.0;
  double cone_gain = audio_number_prop(self, "__xt_panner_cone_gain");
  audio_mixer_of(self)->set_panner(id, model, ref_distance, max_distance, rolloff, cone_inner,
                                   cone_outer, cone_gain, panning);
}

/** `panningModel`/`distanceModel` and the numeric distance/cone settings share
 * the same shape: store the value under `key`, then re-apply the node. */
xt_value panner_text_set(xt_value self, const char *key, const char *const *allowed,
                         int allowed_count, xt_value value) {
  xt_value text = xt_to_string(value);
  const char *name = XT_IS_STRING(text) ? xt_string_data(text) : nullptr;
  bool ok = name == nullptr;
  for (int i = 0; !ok && i < allowed_count; i++) {
    if (std::string(allowed[i]) == name) ok = true;
  }
  if (ok) {
    audio_node_set_prop(self, key, text);
    panner_apply(self);
  }
  return text;
}

xt_value panner_panning_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value stored = xt_object_get_cstr(self, "__xt_panner_panning_model");
  return XT_IS_STRING(stored) ? stored : xt_string_from_cstr("equalpower");
}

xt_value panner_panning_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  static const char *const allowed[] = {"equalpower", "HRTF"};
  return panner_text_set(self, "__xt_panner_panning_model", allowed, 2, xt_arg(argc, argv, 0));
}

xt_value panner_distance_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value stored = xt_object_get_cstr(self, "__xt_panner_distance_model");
  return XT_IS_STRING(stored) ? stored : xt_string_from_cstr("inverse");
}

xt_value panner_distance_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  static const char *const allowed[] = {"linear", "inverse", "exponential"};
  return panner_text_set(self, "__xt_panner_distance_model", allowed, 3, xt_arg(argc, argv, 0));
}

}  // namespace

xt_value ctx_create_analyser(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value node = audio_build_node(audio_context_id(self), kAudioAnalyser, nullptr, nullptr, 0);
  audio_node_set_prop(node, "__xt_analyser_fft", xt_number(2048));
  audio_node_set_prop(node, "__xt_analyser_smoothing", xt_number(0.8));
  audio_node_set_prop(node, "__xt_analyser_min_db", xt_number(-100));
  audio_node_set_prop(node, "__xt_analyser_max_db", xt_number(-30));
  uint64_t context = 0;
  uint64_t id = 0;
  if (audio_node_ref(node, &context, &id)) {
    audio_mixer_of(node)->set_analyser_fft(id, 2048);
    audio_mixer_of(node)->set_analyser_smoothing(id, 0.8f);
  }
  audio_define_accessor(node, "fftSize", (void *)analyser_fft_get, (void *)analyser_fft_set);
  audio_define_accessor(node, "frequencyBinCount", (void *)analyser_bin_count_get, nullptr);
  audio_define_accessor(node, "smoothingTimeConstant", (void *)analyser_smoothing_get,
                        (void *)analyser_smoothing_set);
  audio_define_accessor(node, "minDecibels", (void *)analyser_min_db_get,
                        (void *)analyser_min_db_set);
  audio_define_accessor(node, "maxDecibels", (void *)analyser_max_db_get,
                        (void *)analyser_max_db_set);
  audio_set_method(node, "getByteFrequencyData", (void *)analyser_byte_frequency);
  audio_set_method(node, "getFloatFrequencyData", (void *)analyser_float_frequency);
  audio_set_method(node, "getByteTimeDomainData", (void *)analyser_byte_time);
  audio_set_method(node, "getFloatTimeDomainData", (void *)analyser_float_time);
  return node;
}

namespace {

xt_value panner_set_position(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint64_t context = 0;
  uint64_t id = 0;
  if (!audio_node_ref(self, &context, &id)) return XT_UNDEFINED;
  std::shared_ptr<AudioMixer> mixer = audio_mixer_of(self);
  for (int index = 0; index < 3 && index < argc; index++) {
    mixer->param_set(id, index, xt_to_number(argv[index]));
  }
  return XT_UNDEFINED;
}

xt_value panner_set_orientation(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  uint64_t context = 0;
  uint64_t id = 0;
  if (!audio_node_ref(self, &context, &id)) return XT_UNDEFINED;
  std::shared_ptr<AudioMixer> mixer = audio_mixer_of(self);
  for (int index = 0; index < 3 && index < argc; index++) {
    mixer->param_set(id, 3 + index, xt_to_number(argv[index]));
  }
  return XT_UNDEFINED;
}

/** The numeric distance/cone properties are all the same accessor pair with a
 * fixed key and default. */
#define XT_PANNER_NUMBER(key, default_value)                                     \
  xt_value panner_##key##_get(xt_value self, xt_value env, int32_t argc,          \
                              xt_value *argv) {                                   \
    (void)env;                                                                    \
    (void)argc;                                                                   \
    (void)argv;                                                                   \
    xt_value stored = xt_object_get_cstr(self, "__xt_panner_" #key);              \
    return XT_IS_NUMBER(stored) ? stored : xt_number(default_value);              \
  }                                                                               \
  xt_value panner_##key##_set(xt_value self, xt_value env, int32_t argc,          \
                              xt_value *argv) {                                   \
    (void)env;                                                                    \
    double value = xt_to_number(xt_arg(argc, argv, 0));                           \
    audio_node_set_prop(self, "__xt_panner_" #key, xt_number(value));             \
    panner_apply(self);                                                           \
    return xt_number(value);                                                      \
  }

XT_PANNER_NUMBER(ref_distance, 1.0)
XT_PANNER_NUMBER(max_distance, 10000.0)
XT_PANNER_NUMBER(rolloff, 1.0)
XT_PANNER_NUMBER(cone_inner, 360.0)
XT_PANNER_NUMBER(cone_outer, 360.0)
XT_PANNER_NUMBER(cone_gain, 0.0)

#undef XT_PANNER_NUMBER

}  // namespace

xt_value ctx_create_panner(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  static const char *const params[] = {"positionX", "positionY", "positionZ",
                                       "orientationX", "orientationY", "orientationZ"};
  xt_value node = audio_build_node(audio_context_id(self), kAudioPanner, nullptr, params, 6);
  audio_node_set_prop(node, "__xt_panner_panning_model", xt_string_from_cstr("equalpower"));
  audio_node_set_prop(node, "__xt_panner_distance_model", xt_string_from_cstr("inverse"));
  audio_set_method(node, "setPosition", (void *)panner_set_position);
  audio_set_method(node, "setOrientation", (void *)panner_set_orientation);
  audio_define_accessor(node, "panningModel", (void *)panner_panning_get,
                        (void *)panner_panning_set);
  audio_define_accessor(node, "distanceModel", (void *)panner_distance_get,
                        (void *)panner_distance_set);
  audio_define_accessor(node, "refDistance", (void *)panner_ref_distance_get,
                        (void *)panner_ref_distance_set);
  audio_define_accessor(node, "maxDistance", (void *)panner_max_distance_get,
                        (void *)panner_max_distance_set);
  audio_define_accessor(node, "rolloffFactor", (void *)panner_rolloff_get,
                        (void *)panner_rolloff_set);
  audio_define_accessor(node, "coneInnerAngle", (void *)panner_cone_inner_get,
                        (void *)panner_cone_inner_set);
  audio_define_accessor(node, "coneOuterAngle", (void *)panner_cone_outer_get,
                        (void *)panner_cone_outer_set);
  audio_define_accessor(node, "coneOuterGain", (void *)panner_cone_gain_get,
                        (void *)panner_cone_gain_set);
  panner_apply(node);
  return node;
}

}  // namespace xtgui
