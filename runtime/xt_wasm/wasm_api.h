/*
 * xbintsc runtime — WebAssembly feature registration table.
 *
 * The `WebAssembly` namespace lives in the core runtime (`xt_webassembly.c`),
 * but the interpreter behind it is optional: wasm3 and the bridge are compiled
 * and linked only for a program the driver's usage scan selects as a wasm user.
 * This tiny table is the seam between the two halves.
 *
 * The feature's `wasm_bridge.c` installs the table from a constructor before
 * `main` runs. When the feature is absent the pointer stays NULL and the
 * namespace keeps its original "not supported in this build" behaviour, so a
 * program that never touches WebAssembly pays nothing and one that does fails
 * with a runtime error rather than an unresolved linker symbol.
 */

#ifndef XT_WASM_API_H
#define XT_WASM_API_H

#include "../rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XtWasmApi {
  xt_value (*dispatch)(xt_value name, int32_t argc, xt_value *argv);
  xt_value (*object)(int32_t argc, xt_value *argv);
} XtWasmApi;

/** Install the wasm3-backed implementation (called by the feature bridge). */
void xt_wasm_install(const XtWasmApi *api);

#ifdef __cplusplus
}
#endif

#endif /* XT_WASM_API_H */
