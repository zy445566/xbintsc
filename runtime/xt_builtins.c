/*
 * xbintsc runtime — builtins.
 *
 * `Object.*` statics and the free functions (`parseInt`, `isNaN`, `in`, ...),
 * along with the closure representation and calling convention.
 */

#include "rt_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Object methods and global functions                                       */
/* ------------------------------------------------------------------------- */

xt_value xt_object_values(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (!XT_IS_OBJECT(value)) return result;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  for (uint32_t i = 0; i < obj->count; i++) xt_array_push(result, obj->properties[i].value);
  return result;
}

xt_value xt_object_entries(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  if (!XT_IS_OBJECT(value)) return result;
  xt_object *obj = (xt_object *)XT_GET_PTR(value);
  for (uint32_t i = 0; i < obj->count; i++) {
    xt_value pair = xt_array_new(0, NULL);
    xt_array_push(pair, XT_FROM_PTR(XT_TAG_STRING, obj->properties[i].key));
    xt_array_push(pair, obj->properties[i].value);
    xt_array_push(result, pair);
  }
  return result;
}

xt_value xt_object_assign(int32_t argc, xt_value *argv) {
  if (argc <= 0) return xt_undefined();
  xt_value target = argv[0];
  if (!XT_IS_OBJECT(target)) return target;
  for (int32_t i = 1; i < argc; i++) {
    if (!XT_IS_OBJECT(argv[i])) continue;
    xt_object *source = (xt_object *)XT_GET_PTR(argv[i]);
    for (uint32_t j = 0; j < source->count; j++) {
      xt_object_set(target, XT_FROM_PTR(XT_TAG_STRING, source->properties[j].key), source->properties[j].value);
    }
  }
  return target;
}

/* Copy every own enumerable property of `source` onto `target` (`{...source}`). */
xt_value xt_object_spread(xt_value target, xt_value source) {
  xt_value args[2];
  args[0] = target;
  args[1] = source;
  return xt_object_assign(2, args);
}

xt_value xt_parse_int(int32_t argc, xt_value *argv) {
  xt_string *s = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
  int32_t radix = argc >= 2 ? xt_to_int32(argv[1]) : 0;
  char *end = NULL;
  long value = strtol(s->data, &end, radix);
  if (end == s->data) return xt_number(NAN);
  return xt_number((double)value);
}

xt_value xt_parse_float(int32_t argc, xt_value *argv) {
  xt_string *s = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
  char *end = NULL;
  double value = strtod(s->data, &end);
  if (end == s->data) return xt_number(NAN);
  return xt_number(value);
}

xt_value xt_is_nan(int32_t argc, xt_value *argv) {
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  return xt_bool(value != value);
}

xt_value xt_is_finite(int32_t argc, xt_value *argv) {
  double value = xt_to_number(xt_arg_at(argc, argv, 0));
  return xt_bool(!isnan(value) && !isinf(value));
}

xt_value xt_number_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_number(0);
  return xt_number(xt_to_number(argv[0]));
}

xt_value xt_string_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return xt_string_from_cstr("");
  return xt_to_string(argv[0]);
}

xt_value xt_boolean_ctor(int32_t argc, xt_value *argv) {
  if (argc == 0) return XT_FALSE;
  return xt_bool(xt_truthy(argv[0]));
}

xt_value xt_in(xt_value key, xt_value value) {
  if (XT_IS_OBJECT(value)) return xt_object_has(value, key);
  if (XT_IS_ARRAY(value)) {
    int32_t index = xt_to_int32(key);
    return xt_bool(index >= 0 && (uint32_t)index < xt_as_array(value)->length);
  }
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    xt_string *k = xt_as_string(xt_to_string(key));
    if (k->length == 6 && memcmp(k->data, "length", 6) == 0) return XT_TRUE;
    int32_t index = xt_to_int32(key);
    return xt_bool(index >= 0 && (uint32_t)index < s->length);
  }
  return XT_FALSE;
}

xt_value xt_delete(xt_value value, xt_value key) {
  if (XT_IS_OBJECT(value)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(value);
    xt_string *k = xt_as_string(xt_to_string(key));
    for (uint32_t i = 0; i < obj->count; i++) {
      if (xt_string_equals(obj->properties[i].key, k)) {
        for (uint32_t j = i + 1; j < obj->count; j++) obj->properties[j - 1] = obj->properties[j];
        obj->count--;
        break;
      }
    }
    return XT_TRUE;
  }
  if (XT_IS_ARRAY(value)) {
    xt_array_set(value, key, XT_UNDEFINED);
    return XT_TRUE;
  }
  return XT_TRUE;
}

xt_value xt_rest_args(int32_t argc, xt_value *argv, int32_t start) {
  if (start < 0) start = 0;
  int32_t count = argc - start;
  if (count < 0) count = 0;
  return xt_array_new(count, argv ? argv + start : NULL);
}


/* ------------------------------------------------------------------------- */
/* Functions                                                                 */
/* ------------------------------------------------------------------------- */

xt_value xt_arg(int32_t argc, xt_value *argv, int32_t index) {
  if (index < 0 || index >= argc) return XT_UNDEFINED;
  return argv[index];
}

xt_value xt_closure_new(void *fn, int32_t env_count, xt_value *env) {
  xt_function *function = (xt_function *)xt_alloc(sizeof(xt_function), XT_OBJECT_KIND_FUNCTION);
  function->code = (xt_code_fn)fn;
  function->environment_count = env_count;
  function->arity = -1;
  function->properties = NULL;
  function->environment = NULL;
  if (env_count > 0) {
    function->environment = (xt_value *)malloc(sizeof(xt_value) * (size_t)env_count);
    for (int32_t i = 0; i < env_count; i++) function->environment[i] = env[i];
  }
  return XT_FROM_PTR(XT_TAG_FUNCTION, function);
}

xt_value xt_closure_call(xt_value value, int32_t argc, xt_value *argv) {
  if (!XT_IS_FUNCTION(value)) {
    xt_throw(xt_string_from_cstr("TypeError: value is not a function"));
    return XT_UNDEFINED;
  }
  xt_function *function = (xt_function *)XT_GET_PTR(value);
  /* The closure value itself is threaded through as `env` so the callee can
   * read its captures with xt_closure_env(env, i). */
  return function->code(value, argc, argv);
}

xt_value xt_closure_env(xt_value value, int32_t index) {
  if (!XT_IS_FUNCTION(value)) return XT_UNDEFINED;
  xt_function *function = (xt_function *)XT_GET_PTR(value);
  if (index < 0 || index >= function->environment_count) return XT_UNDEFINED;
  return function->environment[index];
}

int32_t xt_closure_arity(xt_value value) {
  if (!XT_IS_FUNCTION(value)) return -1;
  return ((xt_function *)XT_GET_PTR(value))->arity;
}

