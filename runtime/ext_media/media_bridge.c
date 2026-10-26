/*
 * xbintsc media extension — registration bridge.
 *
 * The decoders live in `media.a` (`media.lib` with the MSVC ABI on Windows), a
 * prebuilt C++ archive. A static archive is
 * only searched for members that satisfy an undefined symbol, so a backend that
 * merely had a constructor would never be extracted. This tiny C translation
 * unit is compiled and linked *directly* (it is the media extension's
 * `runtimeSources()` entry): its constructor references every backend entry
 * point, which pulls the archive member in, and installs the table into the GUI
 * engine before `main` runs.
 *
 * The bridge is intentionally C-only and has no FFmpeg (or SDL) dependency. If
 * the `gui` extension is not enabled, `xt_media_install` is unresolved; the
 * registry rejects that combination first with a "media requires gui"
 * diagnostic (see `src/extensions/registry.ts`).
 */

#include "../ext_gui/media_api.h"

static const XtMediaApi kMediaApi = {
    xt_media_backend_open,
    xt_media_backend_by_id,
    xt_media_backend_shutdown,
};

#if defined(__GNUC__) || defined(__clang__)
/* clang targeting Windows (the toolchain xbintsc uses there) implements this
 * attribute with a `.CRT$XCU` entry, which the MSVC CRT runs before `main`. */
__attribute__((constructor)) static void xt_media_register(void) {
  xt_media_install(&kMediaApi);
}
#elif defined(_MSC_VER)
/* A plain MSVC build has no constructor attribute; put the function pointer in
 * the CRT's initializer table by hand. `media.lib` itself is built with
 * clang-cl, so this branch is only for a cl.exe-built bridge. */
void xt_media_register(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_media_register_ctor)(void) = xt_media_register;
void xt_media_register(void) { xt_media_install(&kMediaApi); }
#endif
