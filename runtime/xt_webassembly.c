/*
 * xbintsc runtime — the `WebAssembly` namespace.
 *
 * The WebAssembly JavaScript API surface is recognized (so code that reaches
 * for `WebAssembly.compile`, `new WebAssembly.Module`, … compiles), but this
 * build has no WebAssembly interpreter: every entry point throws a clear
 * "not supported" error instead of executing bytecode.
 */

#include "rt_internal.h"

#include <string.h>

static xt_value xt_wasm_unsupported(xt_value thisValue, xt_value env, int32_t argc,
                                    xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: WebAssembly is not supported in this build"));
  return XT_UNDEFINED;
}

xt_value xt_webassembly_static(xt_value name, int32_t argc, xt_value *argv) {
  (void)name;
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: WebAssembly is not supported in this build"));
  return XT_UNDEFINED;
}

xt_value xt_webassembly_object(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value object = xt_object_new();
  static const char *const statics[] = {"compile", "instantiate", "validate"};
  static const char *const constructors[] = {"Module", "Instance", "Memory",
                                             "Table", "Global"};
  static const char *const errors[] = {"CompileError", "LinkError", "RuntimeError"};
  for (size_t i = 0; i < sizeof(statics) / sizeof(statics[0]); i++) {
    xt_object_set(object, xt_string_from_cstr(statics[i]),
                  xt_closure_new((void *)xt_wasm_unsupported, 0, NULL));
  }
  for (size_t i = 0; i < sizeof(constructors) / sizeof(constructors[0]); i++) {
    xt_object_set(object, xt_string_from_cstr(constructors[i]),
                  xt_closure_new((void *)xt_wasm_unsupported, 0, NULL));
  }
  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
    xt_object_set(object, xt_string_from_cstr(errors[i]),
                  xt_closure_new((void *)xt_wasm_unsupported, 0, NULL));
  }
  return object;
}
