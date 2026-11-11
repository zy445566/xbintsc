/*
 * Node.js `readline/promises` module for xbintsc.
 *
 * `createInterface` wraps the callback-based `readline` interface and exposes
 * `question(query)` as a promise, plus the delegated `on`/`off`/`close`.
 */

#include "../node_common.h"

xt_value xt_readline_create_interface(int32_t argc, xt_value *argv);

static xt_value rl_promises_question_cb(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value resolve = xt_closure_env(env, 0);
  xt_value answer = xt_arg(argc, argv, 0);
  if (XT_IS_FUNCTION(resolve)) xt_closure_call(resolve, 1, &answer);
  return XT_UNDEFINED;
}

static xt_value rl_promises_question_executor(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value base = xt_closure_env(env, 0);
  xt_value query = xt_closure_env(env, 1);
  xt_value resolve = xt_arg(argc, argv, 0);
  xt_value cbEnv[1] = { resolve };
  xt_value cb = xt_closure_new((void *)rl_promises_question_cb, 1, cbEnv);
  xt_value question = xt_object_get_cstr(base, "question");
  if (XT_IS_FUNCTION(question)) {
    xt_value args[2] = { query, cb };
    xt_call_with_this(question, base, 2, args);
  }
  return XT_UNDEFINED;
}

static xt_value rl_promises_question(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value base = xt_closure_env(env, 0);
  xt_value query = xt_arg(argc, argv, 0);
  xt_value executorEnv[2] = { base, query };
  xt_value executor = xt_closure_new((void *)rl_promises_question_executor, 2, executorEnv);
  xt_value ctorArgs[1] = { executor };
  return xt_promise_ctor(1, ctorArgs);
}

xt_value xt_readline_promises_create_interface(int32_t argc, xt_value *argv) {
  xt_value base = xt_readline_create_interface(argc, argv);
  xt_value iface = xt_object_new();

  xt_value on = xt_object_get_cstr(base, "on");
  xt_value off = xt_object_get_cstr(base, "off");
  xt_value close = xt_object_get_cstr(base, "close");
  if (XT_IS_FUNCTION(on)) xt_node_set(iface, "on", on);
  if (XT_IS_FUNCTION(off)) xt_node_set(iface, "off", off);
  if (XT_IS_FUNCTION(close)) xt_node_set(iface, "close", close);

  xt_value env[1] = { base };
  xt_node_set(iface, "question", xt_closure_new((void *)rl_promises_question, 1, env));
  return iface;
}
