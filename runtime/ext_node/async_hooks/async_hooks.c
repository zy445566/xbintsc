/*
 * Node.js `async_hooks` module for xbintsc.
 *
 * The runtime has no async-context tracking, so the async identifiers are
 * always zero and `createHook` is a no-op. `AsyncResource` is a constructible
 * base class whose `runInAsyncScope(fn, thisArg, ...args)` simply invokes `fn`
 * with the given receiver.
 */

#include "../node_common.h"

static xt_value xt_async_resource_proto(void);

static xt_value xt_async_resource_run(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value fn = xt_arg(argc, argv, 0);
  xt_value thisArg = xt_arg(argc, argv, 1);
  int32_t fnArgc = argc > 2 ? argc - 2 : 0;
  xt_value *fnArgs = argc > 2 ? argv + 2 : NULL;
  if (XT_IS_FUNCTION(fn)) return xt_call_with_this(fn, thisArg, fnArgc, fnArgs);
  return XT_UNDEFINED;
}

xt_value xt_async_resource_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_object_new_with_proto(xt_async_resource_proto());
}

static xt_value xt_async_resource_proto(void) {
  static xt_value proto = 0;
  xt_gc_add_root(&proto);
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_define_method(proto, "runInAsyncScope", (void *)xt_async_resource_run);
  xt_node_define_method(proto, "emitDestroy", (void *)xt_async_resource_run);
  return proto;
}

/* -- the stateless async-id / hook stubs ----------------------------------- */

xt_value xt_async_hooks_execution_id(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_number(0);
}

xt_value xt_async_hooks_trigger_id(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_number(0);
}

xt_value xt_async_hooks_create_hook(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}
