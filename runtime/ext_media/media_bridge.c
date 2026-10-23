/*
 * xbintsc media extension — registration bridge.
 *
 * The decoders live in `media.a`, a prebuilt C++ archive. A static archive is
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
__attribute__((constructor)) static void xt_media_register(void) {
  xt_media_install(&kMediaApi);
}
#elif defined(_MSC_VER)
/* COFF has no `constructor` attribute. `media.lib` is not produced on Windows
 * yet; keep the table referenced so the translation unit is not empty. */
void xt_media_register(void);
void xt_media_register(void) { xt_media_install(&kMediaApi); }
#endif
