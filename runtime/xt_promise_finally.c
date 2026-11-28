#include "rt_internal.h"

#include <stdint.h>

/*
 * Promise.prototype.finally.
 *
 * Split from `xt_promise.c` to keep each runtime file under the repository line
 * budget; the reaction machinery it builds on lives there.
 */

/*
 * `finally(fn)` is `then(value => { fn(); return value },
 *                      reason => { fn(); throw reason })`, which gives the
 * three properties callers rely on: the handler runs in a microtask, the
 * original value/reason is preserved, and a throwing handler rejects the
 * derived promise. The old approximation ran `fn` synchronously and returned
 * the receiver, so a throw escaped as an uncaught exception (coding-agent's
 * model-catalog refresh aborts from a `finally` callback).
 */

static xt_value xt_promise_finally_fulfilled(xt_value thisValue, xt_value env, int32_t argc,
                                             xt_value *argv) {
  (void)thisValue;
  xt_value handler = xt_closure_env(env, 0);
  if (XT_IS_FUNCTION(handler)) xt_closure_call(handler, 0, NULL);
  return xt_arg_at(argc, argv, 0);
}

static xt_value xt_promise_finally_rejected(xt_value thisValue, xt_value env, int32_t argc,
                                            xt_value *argv) {
  (void)thisValue;
  xt_value handler = xt_closure_env(env, 0);
  if (XT_IS_FUNCTION(handler)) xt_closure_call(handler, 0, NULL);
  xt_throw(xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

xt_value xt_promise_finally(xt_value target, xt_value handler) {
  xt_value environment[1] = {handler};
  xt_value onFulfilled = xt_closure_new((void *)xt_promise_finally_fulfilled, 1, environment);
  xt_value onRejected = xt_closure_new((void *)xt_promise_finally_rejected, 1, environment);
  xt_value args[2] = {onFulfilled, onRejected};
  int handled = 0;
  return xt_promise_method(target, "then", 2, args, &handled);
}

