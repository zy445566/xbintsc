/*
 * xbintsc runtime — first-class values for imported module bindings.
 *
 * Imported extension functions are normally called or namespaced directly;
 * when the codegen wraps one as a value it stores the raw runtime pointer (or
 * a namespace dispatcher plus method name) in the closure env and uses one of
 * these trampolines. A raw pointer's tag bits are 0x0000, which no `xt_value`
 * tag uses, so the GC skips it safely.
 */

#include "rt_internal.h"

#include <stdint.h>

xt_value xt_fnptr_trampoline(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value raw = xt_closure_env(env, 0);
  xt_value (*fn)(int32_t, xt_value *) = (xt_value (*)(int32_t, xt_value *))(uintptr_t)raw;
  return fn(argc, argv);
}

xt_value xt_method_trampoline(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value name = xt_closure_env(env, 0);
  xt_value raw = xt_closure_env(env, 1);
  xt_value (*dispatcher)(xt_value, int32_t, xt_value *) =
      (xt_value (*)(xt_value, int32_t, xt_value *))(uintptr_t)raw;
  return dispatcher(name, argc, argv);
}
