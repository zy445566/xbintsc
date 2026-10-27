/*
 * xbintsc GUI engine — the JavaScript AudioParam object.
 *
 * The automation timeline itself lives in the mixer (`audio_param.cpp`); this
 * unit is the object a page holds: a live `value` accessor plus the scheduling
 * methods, each forwarding to the native node slot it names. Splitting it out of
 * `audio.cpp` keeps both translation units within the file-size budget.
 */

#include "audio_internal.h"

#include <cstdint>
#include <memory>

namespace {

using namespace xtgui;

double number_prop(xt_value object, const char *name) {
  xt_value value = xt_object_get_cstr(object, name);
  return XT_IS_NUMBER(value) ? xt_to_number(value) : 0.0;
}

xt_value fn_of(void *fn) { return xt_closure_new(fn, 0, NULL); }

struct ParamRef {
  std::shared_ptr<AudioMixer> mixer;
  uint64_t node = 0;
  int index = 0;
};

bool param_ref(xt_value object, ParamRef *out) {
  out->mixer = audio_mixer_by_id(audio_context_id(object));
  out->node = static_cast<uint64_t>(number_prop(object, "__xt_audio_node"));
  out->index = static_cast<int>(number_prop(object, "__xt_audio_param"));
  return out->mixer != nullptr && out->node != 0;
}

xt_value param_value_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  ParamRef ref;
  if (!param_ref(self, &ref)) return xt_number(0);
  return xt_number(ref.mixer->param_value(ref.node, ref.index));
}

xt_value param_value_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_arg(argc, argv, 0);
  if (!XT_IS_NUMBER(value)) value = xt_number(xt_to_number(value));
  ParamRef ref;
  if (param_ref(self, &ref)) {
    ref.mixer->param_set(ref.node, ref.index, xt_to_number(value));
  }
  return value;
}

xt_value param_set_value_at_time(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  ParamRef ref;
  if (param_ref(self, &ref)) {
    double value = xt_to_number(xt_arg(argc, argv, 0));
    double time = argc > 1 ? xt_to_number(argv[1]) : ref.mixer->current_time();
    ref.mixer->param_set_at(ref.node, ref.index, value, time);
  }
  return self;
}

xt_value param_linear_ramp(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  ParamRef ref;
  if (param_ref(self, &ref)) {
    double value = xt_to_number(xt_arg(argc, argv, 0));
    double time = argc > 1 ? xt_to_number(argv[1]) : ref.mixer->current_time();
    ref.mixer->param_linear_ramp(ref.node, ref.index, value, time);
  }
  return self;
}

xt_value param_exponential_ramp(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  ParamRef ref;
  if (param_ref(self, &ref)) {
    double value = xt_to_number(xt_arg(argc, argv, 0));
    double time = argc > 1 ? xt_to_number(argv[1]) : ref.mixer->current_time();
    ref.mixer->param_exponential_ramp(ref.node, ref.index, value, time);
  }
  return self;
}

xt_value param_set_target(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  ParamRef ref;
  if (param_ref(self, &ref)) {
    double value = xt_to_number(xt_arg(argc, argv, 0));
    double time = argc > 1 ? xt_to_number(argv[1]) : ref.mixer->current_time();
    double constant = argc > 2 ? xt_to_number(argv[2]) : 0.0;
    ref.mixer->param_set_target(ref.node, ref.index, value, time, constant);
  }
  return self;
}

xt_value param_cancel(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  ParamRef ref;
  if (param_ref(self, &ref)) {
    double time = argc > 0 ? xt_to_number(argv[0]) : 0.0;
    ref.mixer->param_cancel(ref.node, ref.index, time, false);
  }
  return self;
}

xt_value param_cancel_and_hold(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  ParamRef ref;
  if (param_ref(self, &ref)) {
    double time = argc > 0 ? xt_to_number(argv[0]) : 0.0;
    ref.mixer->param_cancel(ref.node, ref.index, time, true);
  }
  return self;
}

void set_prop(xt_value target, const char *name, xt_value value) {
  xt_object_set(target, xt_string_from_cstr(name), value);
}

void set_method(xt_value target, const char *name, void *fn) { set_prop(target, name, fn_of(fn)); }

void set_number(xt_value target, const char *name, double value) {
  set_prop(target, name, xt_number(value));
}

}  // namespace

namespace xtgui {

xt_value audio_param_build(uint64_t context, uint64_t node, int index) {
  xt_value param = xt_object_new();
  set_number(param, "__xt_audio_ctx", static_cast<double>(context));
  set_number(param, "__xt_audio_node", static_cast<double>(node));
  set_number(param, "__xt_audio_param", index);
  xt_object_define_getter(param, xt_string_from_cstr("value"), fn_of((void *)param_value_get));
  xt_object_define_setter(param, xt_string_from_cstr("value"), fn_of((void *)param_value_set));
  set_method(param, "setValueAtTime", (void *)param_set_value_at_time);
  set_method(param, "linearRampToValueAtTime", (void *)param_linear_ramp);
  set_method(param, "exponentialRampToValueAtTime", (void *)param_exponential_ramp);
  set_method(param, "setTargetAtTime", (void *)param_set_target);
  set_method(param, "cancelScheduledValues", (void *)param_cancel);
  set_method(param, "cancelAndHoldAtTime", (void *)param_cancel_and_hold);
  set_number(param, "defaultValue", 0.0);
  return param;
}

}  // namespace xtgui
