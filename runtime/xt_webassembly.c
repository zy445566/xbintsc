/*
 * xbintsc runtime — the `WebAssembly` namespace.
 *
 * The namespace object and the dispatch entry points are core: every build
 * recognises `WebAssembly`, so code that references it still compiles. The
 * interpreter behind them is an optional link feature (`wasm`): its bridge
 * installs `XtWasmApi` from a constructor before `main`. Without the feature
 * the pointers are NULL and every entry point throws the original "not
 * supported in this build" error, which is the behaviour a `--no-feature wasm`
 * build intentionally keeps.
 */

#include "rt_internal.h"

#include "xt_wasm/wasm_api.h"

#include <string.h>

static const XtWasmApi *g_wasm_api = NULL;

void xt_wasm_install(const XtWasmApi *api) { g_wasm_api = api; }

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
  if (g_wasm_api != NULL) return g_wasm_api->dispatch(name, argc, argv);
  (void)name;
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: WebAssembly is not supported in this build"));
  return XT_UNDEFINED;
}

xt_value xt_webassembly_object(int32_t argc, xt_value *argv) {
  if (g_wasm_api != NULL) return g_wasm_api->object(argc, argv);
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
