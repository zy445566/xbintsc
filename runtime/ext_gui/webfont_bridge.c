/*
 * xbintsc GUI engine — `webfonts` link feature registration bridge.
 *
 * Compiled and linked only when the compiler detects that the program may use a
 * WOFF/WOFF2 `@font-face` source. Its constructor installs the decompressor and
 * references it, which pulls `text_webfont.cpp` (and the miniz/Brotli/woff2
 * members it calls) out of `gui.a`; without it a compressed font source is
 * skipped and the diagnostic names the missing feature.
 */

extern void xt_webfont_feature_register(void);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_webfont_feature_boot(void) {
  xt_webfont_feature_register();
}
#elif defined(_MSC_VER)
void xt_webfont_feature_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_webfont_feature_ctor)(void) =
    xt_webfont_feature_boot;
void xt_webfont_feature_boot(void) { xt_webfont_feature_register(); }
#endif
