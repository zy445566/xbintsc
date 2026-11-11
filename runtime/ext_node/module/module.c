/*
 * Node.js `module` module for xbintsc: `createRequire` and `isBuiltin`.
 *
 * A compiled binary has no runtime module loader, so `createRequire` returns a
 * function that throws a clear error when called. `isBuiltin` reports whether a
 * specifier is a known Node built-in, but since the binary already bundles
 * every reachable module, it conservatively returns false.
 */

#include "../node_common.h"

static xt_value module_require_stub(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: require() is not supported in compiled binaries"));
  return XT_UNDEFINED;
}

xt_value xt_module_create_require(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_closure_new((void *)module_require_stub, 0, NULL);
}

xt_value xt_module_is_builtin(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_FALSE;
}
