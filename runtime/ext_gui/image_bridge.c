/*
 * xbintsc GUI engine — `images` link feature registration bridge.
 *
 * Compiled and linked only when the compiler detects that the program may draw
 * an image or SVG. Its constructor installs `XtImageApi` and references the
 * stb_image/NanoSVG members, pulling them out of `gui.a`; without it they are
 * never linked.
 */

extern void xt_image_feature_register(void);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_image_feature_boot(void) {
  xt_image_feature_register();
}
#elif defined(_MSC_VER)
void xt_image_feature_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_image_feature_ctor)(void) =
    xt_image_feature_boot;
void xt_image_feature_boot(void) { xt_image_feature_register(); }
#endif
