/*
 * xbintsc GUI engine — the Web Audio node kinds beyond the classic set, part 1:
 * WaveShaper, Convolver, the two media sources and `AudioContext.listener`.
 *
 * `audio.cpp` builds gain/oscillator/buffer-source/biquad/compressor/pan/delay
 * and the context itself; the analysis and spatial nodes live in
 * `audio_nodes_spatial.cpp`. Every factory returns the same node object shape
 * (`audio_build_node`), and each settable property is an accessor that forwards
 * into the native mixer at assignment time — the mixer never reads an
 * `xt_value`.
 */

#include <cstring>
#include <string>
#include <vector>

#include "audio_buffer.h"
#include "audio_nodes_internal.h"

namespace xtgui {
namespace {

using xtgui::audio_clamp_number;
using xtgui::audio_define_accessor;
using xtgui::audio_node_set_prop;
using xtgui::audio_read_numeric_array;
using xtgui::audio_text_prop;

}  // namespace

/* -- WaveShaper ------------------------------------------------------------ */

namespace {

int shaper_oversample_factor(xt_value node) {
  std::string mode = audio_text_prop(node, "__xt_shaper_oversample");
  if (mode == "2x") return 2;
  if (mode == "4x") return 4;
  return 1;
}

void shaper_apply(xt_value node) {
  uint64_t context = 0;
  uint64_t id = 0;
  if (!audio_node_ref(node, &context, &id)) return;
  std::vector<float> curve;
  audio_read_numeric_array(xt_object_get_cstr(node, "__xt_shaper_curve"), &curve);
  audio_mixer_of(node)->set_shaper_curve(id, curve, shaper_oversample_factor(node));
}

xt_value shaper_curve_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_shaper_curve");
}

xt_value shaper_curve_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_arg(argc, argv, 0);
  audio_node_set_prop(self, "__xt_shaper_curve", value);
  shaper_apply(self);
  return value;
}

xt_value shaper_oversample_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value mode = xt_object_get_cstr(self, "__xt_shaper_oversample");
  return XT_IS_STRING(mode) ? mode : xt_string_from_cstr("none");
}

xt_value shaper_oversample_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_to_string(xt_arg(argc, argv, 0));
  const char *mode = XT_IS_STRING(value) ? xt_string_data(value) : nullptr;
  if (mode != nullptr && (std::strcmp(mode, "none") == 0 || std::strcmp(mode, "2x") == 0 ||
                          std::strcmp(mode, "4x") == 0)) {
    audio_node_set_prop(self, "__xt_shaper_oversample", value);
    shaper_apply(self);
  }
  return value;
}

}  // namespace

xt_value ctx_create_waveshaper(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value node = audio_build_node(audio_context_id(self), kAudioWaveShaper, nullptr, nullptr, 0);
  audio_node_set_prop(node, "__xt_shaper_curve", XT_NULL);
  audio_node_set_prop(node, "__xt_shaper_oversample", xt_string_from_cstr("none"));
  audio_define_accessor(node, "curve", (void *)shaper_curve_get, (void *)shaper_curve_set);
  audio_define_accessor(node, "oversample", (void *)shaper_oversample_get,
                        (void *)shaper_oversample_set);
  return node;
}

/* -- Convolver ------------------------------------------------------------- */

namespace {

/** Rebuild the partition spectra from the node's `buffer`/`normalize`. */
void convolver_apply(xt_value node) {
  uint64_t context = 0;
  uint64_t id = 0;
  if (!audio_node_ref(node, &context, &id)) return;
  std::shared_ptr<AudioClip> clip =
      audio_clip_from_buffer(xt_object_get_cstr(node, "__xt_convolver_buffer"));
  bool normalize = xt_truthy(xt_object_get_cstr(node, "__xt_convolver_normalize"));
  audio_mixer_of(node)->set_convolver_impulse(id, clip, normalize);
}

xt_value convolver_buffer_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_convolver_buffer");
}

xt_value convolver_buffer_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_arg(argc, argv, 0);
  audio_node_set_prop(self, "__xt_convolver_buffer", value);
  convolver_apply(self);
  return value;
}

xt_value convolver_normalize_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value value = xt_object_get_cstr(self, "__xt_convolver_normalize");
  return XT_IS_BOOL(value) ? value : XT_TRUE;
}

xt_value convolver_normalize_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_arg(argc, argv, 0);
  audio_node_set_prop(self, "__xt_convolver_normalize", xt_bool(xt_truthy(value)));
  convolver_apply(self);
  return value;
}

}  // namespace

xt_value ctx_create_convolver(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value node = audio_build_node(audio_context_id(self), kAudioConvolver, nullptr, nullptr, 0);
  audio_node_set_prop(node, "__xt_convolver_buffer", XT_NULL);
  audio_node_set_prop(node, "__xt_convolver_normalize", XT_TRUE);
  audio_define_accessor(node, "buffer", (void *)convolver_buffer_get,
                        (void *)convolver_buffer_set);
  audio_define_accessor(node, "normalize", (void *)convolver_normalize_get,
                        (void *)convolver_normalize_set);
  return node;
}

/* -- media sources --------------------------------------------------------- */

namespace {

/** The engine has no media capture or element-audio pipeline, so both media
 * source kinds are silent nodes: they behave like the platform (the program
 * keeps running and the graph is valid) instead of throwing. */
xt_value media_source_build(xt_value self, const char *type, const char *property,
                            xt_value argument) {
  xt_value node = audio_build_node(audio_context_id(self), kAudioMediaSource, nullptr, nullptr, 0);
  audio_node_set_prop(node, property, argument);
  audio_node_set_prop(node, "__xt_media_source_type", xt_string_from_cstr(type));
  return node;
}

}  // namespace

xt_value ctx_create_media_element_source(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return media_source_build(self, "element", "mediaElement", xt_arg(argc, argv, 0));
}

xt_value ctx_create_media_stream_source(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return media_source_build(self, "stream", "stream", xt_arg(argc, argv, 0));
}

/* -- AudioContext.listener ------------------------------------------------- */

namespace {

xt_value listener_set_position(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
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

xt_value listener_set_orientation(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
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

}  // namespace

xt_value audio_listener_object(xt_value context_value) {
  static const char *const params[] = {"positionX", "positionY", "positionZ", "forwardX",
                                       "forwardY",  "forwardZ",  "upX",       "upY",
                                       "upZ"};
  xt_value listener =
      audio_build_node(audio_context_id(context_value), kAudioListener, nullptr, params, 9);
  audio_set_method(listener, "setPosition", (void *)listener_set_position);
  audio_set_method(listener, "setOrientation", (void *)listener_set_orientation);
  return listener;
}

xt_value ctx_listener_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value existing = xt_object_get_cstr(self, "__xt_listener_ref");
  if (XT_IS_OBJECT(existing)) return existing;
  xt_value listener = audio_listener_object(self);
  audio_node_set_prop(self, "__xt_listener_ref", listener);
  return listener;
}

}  // namespace xtgui
