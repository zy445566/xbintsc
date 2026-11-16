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

/* `import(<expression>)` in a compiled binary.
 *
 * The build links a fixed set of modules; `table` maps every enabled extension
 * module name — bare and with the `node:` prefix — to its namespace object. A
 * specifier that names one of them resolves to that namespace (this is how
 * `import("node:" + "fs")`, the common "load Node built-ins lazily" idiom,
 * works); anything else rejects with a message that names it. */
xt_value xt_dynamic_import(xt_value table, xt_value specifier) {
  if (XT_IS_STRING(specifier)) {
    xt_value found = xt_get(table, specifier);
    if (found != XT_UNDEFINED && found != XT_NULL) return xt_promise_resolve(found);
  }
  xt_value message = xt_add(
      xt_string_from_cstr("dynamic import of '"),
      xt_add(xt_to_string(specifier),
             xt_string_from_cstr("' is not supported in compiled binaries: "
                                 "only modules linked into this build can be loaded")));
  xt_value arguments[1];
  arguments[0] = message;
  return xt_promise_reject(xt_error_ctor_by_name(xt_string_from_cstr("Error"), 1, arguments));
}

/* Like `xt_fnptr_trampoline`, for a binding declared `void` (the console
 * output methods): call it for its effect and answer `undefined`. */
xt_value xt_voidptr_trampoline(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value raw = xt_closure_env(env, 0);
  void (*fn)(int32_t, xt_value *) = (void (*)(int32_t, xt_value *))(uintptr_t)raw;
  fn(argc, argv);
  return XT_UNDEFINED;
}
