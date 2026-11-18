/*
 * xbintsc GUI engine — shared helpers for the Web Audio node builders.
 *
 * `audio_nodes.cpp` (WaveShaper/Convolver/media sources/listener) and
 * `audio_nodes_spatial.cpp` (Analyser/Panner) both need to read a typed array
 * the page supplied, forward a property into the mixer, and define accessors on
 * a node object. Those small pieces live here so neither file duplicates them.
 */
#ifndef XT_GUI_AUDIO_NODES_INTERNAL_H
#define XT_GUI_AUDIO_NODES_INTERNAL_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "audio_internal.h"

namespace xtgui {

inline void audio_node_set_prop(xt_value target, const char *name, xt_value value) {
  xt_object_set(target, xt_string_from_cstr(name), value);
}

/** Define `name` as a getter (and a setter when `setter` is non-null). */
inline void audio_define_accessor(xt_value target, const char *name, void *getter, void *setter) {
  xt_value key = xt_string_from_cstr(name);
  xt_object_define_getter(target, key, audio_fn_value(getter));
  if (setter != nullptr) xt_object_define_setter(target, key, audio_fn_value(setter));
}

inline std::string audio_text_prop(xt_value object, const char *name) {
  xt_value value = xt_object_get_cstr(object, name);
  const char *text = XT_IS_STRING(value) ? xt_string_data(value) : nullptr;
  return text != nullptr ? std::string(text) : std::string();
}

inline double audio_clamp_number(double value, double low, double high) {
  if (!(value > low)) return low;
  if (value > high) return high;
  return value;
}

/** The elements of a typed array as floats (a curve, a page-supplied array). */
inline bool audio_read_numeric_array(xt_value value, std::vector<float> *out) {
  out->clear();
  if (xt_typed_array_kind_of(value) < 0) return false;
  uint32_t length = xt_typed_array_length(value);
  out->resize(length, 0.0f);
  for (uint32_t i = 0; i < length; i++) {
    (*out)[i] = static_cast<float>(xt_to_number(xt_typed_array_read(value, i)));
  }
  return true;
}

/** Write one element of a typed array the page supplied. */
inline void audio_write_numeric_array(xt_value array, uint32_t index, double value) {
  xt_set(array, xt_number(static_cast<double>(index)), xt_number(value));
}

inline uint32_t audio_typed_length(xt_value value) {
  return xt_typed_array_kind_of(value) < 0 ? 0 : xt_typed_array_length(value);
}

/* -- factories (registered on the AudioContext prototype by `audio.cpp`) ---- */

xt_value ctx_create_waveshaper(xt_value self, xt_value env, int32_t argc, xt_value *argv);
xt_value ctx_create_convolver(xt_value self, xt_value env, int32_t argc, xt_value *argv);
xt_value ctx_create_analyser(xt_value self, xt_value env, int32_t argc, xt_value *argv);
xt_value ctx_create_panner(xt_value self, xt_value env, int32_t argc, xt_value *argv);
xt_value ctx_create_media_element_source(xt_value self, xt_value env, int32_t argc, xt_value *argv);
xt_value ctx_create_media_stream_source(xt_value self, xt_value env, int32_t argc, xt_value *argv);

/** The `AudioContext.listener` object (nine AudioParams). Declared in
 * `audio_internal.h` too, because `audio.cpp` builds it with the context. */
xt_value audio_listener_object(xt_value context_value);
xt_value ctx_listener_get(xt_value self, xt_value env, int32_t argc, xt_value *argv);

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_NODES_INTERNAL_H */
