/*
 * xbintsc runtime — `performance.now()`.
 *
 * The namespace is dispatched two ways: `performance.now()` lowers to
 * xt_performance_call(name, argc, argv), while reading the method as a value
 * (`const now = performance.now`) goes through xt_performance_get(name), which
 * returns a closure with the standard ABI. Both read the same monotonic clock.
 */

#include "rt_internal.h"

#include <string.h>

static xt_value xt_performance_now_value(xt_value thisValue, xt_value env, int32_t argc,
                                         xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(xt_monotonic_ms());
}

xt_value xt_performance_get(xt_value name) {
  const char *key = xt_string_data(name);
  if (key != NULL && strcmp(key, "now") == 0) {
    return xt_closure_new((void *)xt_performance_now_value, 0, NULL);
  }
  return XT_UNDEFINED;
}

xt_value xt_performance_call(xt_value name, int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  const char *key = xt_string_data(name);
  if (key != NULL && strcmp(key, "now") == 0) return xt_number(xt_monotonic_ms());
  return XT_UNDEFINED;
}
