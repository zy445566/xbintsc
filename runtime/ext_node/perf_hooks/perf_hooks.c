/*
 * Node.js `perf_hooks` module for xbintsc: the `performance` object.
 */

#include "../node_common.h"

static xt_value perf_now(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(xt_monotonic_ms());
}

xt_value xt_perf_hooks_performance_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value object = xt_object_new();
  xt_node_set(object, "now", xt_closure_new((void *)perf_now, 0, NULL));
  xt_node_set(object, "timeOrigin", xt_number(0));
  return object;
}
