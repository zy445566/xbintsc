/*
 * Node.js `timers/promises` module for xbintsc.
 *
 * `setTimeout(ms[, value])` and `setImmediate([value])` return promises that
 * settle through the core event loop's timers/immediates.
 */

#include "../node_common.h"

static xt_value timers_promises_resolve(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value resolve = xt_closure_env(env, 0);
  xt_value value = xt_closure_env(env, 1);
  if (XT_IS_FUNCTION(resolve)) xt_closure_call(resolve, 1, &value);
  return XT_UNDEFINED;
}

static xt_value timers_promises_timeout_executor(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value resolve = xt_arg(argc, argv, 0);
  xt_value value = xt_closure_env(env, 0);
  double ms = xt_to_number(xt_closure_env(env, 1));
  xt_value callbackEnv[2] = { resolve, value };
  xt_value callback = xt_closure_new((void *)timers_promises_resolve, 2, callbackEnv);
  xt_value timeoutArgs[2] = { callback, xt_number(ms) };
  xt_set_timeout(2, timeoutArgs);
  return XT_UNDEFINED;
}

xt_value xt_timers_promises_set_timeout(int32_t argc, xt_value *argv) {
  double ms = argc > 0 ? xt_to_number(argv[0]) : 0;
  xt_value value = argc > 1 ? argv[1] : XT_UNDEFINED;
  xt_value env[2] = { value, xt_number(ms) };
  xt_value executor = xt_closure_new((void *)timers_promises_timeout_executor, 2, env);
  xt_value ctorArgs[1] = { executor };
  return xt_promise_ctor(1, ctorArgs);
}

static xt_value timers_promises_immediate_executor(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value resolve = xt_arg(argc, argv, 0);
  xt_value value = xt_closure_env(env, 0);
  xt_value callbackEnv[2] = { resolve, value };
  xt_value callback = xt_closure_new((void *)timers_promises_resolve, 2, callbackEnv);
  xt_value immediateArgs[1] = { callback };
  xt_set_immediate(1, immediateArgs);
  return XT_UNDEFINED;
}

xt_value xt_timers_promises_set_immediate(int32_t argc, xt_value *argv) {
  xt_value value = argc > 0 ? argv[0] : XT_UNDEFINED;
  xt_value env[1] = { value };
  xt_value executor = xt_closure_new((void *)timers_promises_immediate_executor, 1, env);
  xt_value ctorArgs[1] = { executor };
  return xt_promise_ctor(1, ctorArgs);
}
