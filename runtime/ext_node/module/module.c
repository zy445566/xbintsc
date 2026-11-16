/*
 * Node.js `module` module for xbintsc: `createRequire` and `isBuiltin`.
 *
 * A compiled binary has no module loader for arbitrary files, but the modules
 * linked into it are known: the code generator registers a table of every
 * enabled extension module's namespace object, so `require("fs")` (and
 * `require("node:fs")`) answer from it. Anything else throws a clear error.
 * `isBuiltin` reports whether a specifier is a known Node built-in, but since
 * the binary already bundles every reachable module it conservatively returns
 * false.
 */

#include "../node_common.h"

/* The linked-module table the code generator registers before the first
 * `createRequire(...)` (core runtime, `xt_module_values.c`). */
xt_value xt_module_registry_lookup(xt_value specifier);

static xt_value module_require(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value specifier = xt_arg_at(argc, argv, 0);
  xt_value found = xt_module_registry_lookup(xt_to_string(specifier));
  if (found != XT_UNDEFINED) return found;
  xt_throw(xt_error_from_message(
      "Error: require() of a module that is not linked into this binary is not supported"));
  return XT_UNDEFINED;
}

xt_value xt_module_create_require(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_closure_new((void *)module_require, 0, NULL);
}

xt_value xt_module_is_builtin(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_FALSE;
}
