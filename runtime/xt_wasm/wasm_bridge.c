/*
 * xbintsc wasm link feature — registration bridge.
 *
 * wasm3 is a plain C library, not a prebuilt archive, but keeping the install
 * in a separate translation unit mirrors the other optional features: the core
 * runtime stays unaware of wasm3, and this object is linked only when the
 * driver's usage scan selects the `wasm` feature.
 */

#include "wasm.h"
#include "wasm_api.h"

static const XtWasmApi kXtWasmApi = {
    xt_wasm_dispatch_static,
    xt_wasm_build_object,
};

#if defined(__GNUC__) || defined(__clang__)
/* clang targeting Windows implements this attribute with a `.CRT$XCU` entry,
 * which the MSVC CRT runs before `main`. */
__attribute__((constructor)) static void xt_wasm_feature_boot(void) {
  xt_wasm_install(&kXtWasmApi);
}
#elif defined(_MSC_VER)
void xt_wasm_feature_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_wasm_feature_ctor)(void) =
    xt_wasm_feature_boot;
void xt_wasm_feature_boot(void) { xt_wasm_install(&kXtWasmApi); }
#endif
