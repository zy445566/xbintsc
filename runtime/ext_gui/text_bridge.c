/*
 * xbintsc GUI engine — `shaping` link feature registration bridge.
 *
 * Compiled and linked only when the compiler selects the shaping feature (or
 * the program is forced to). Its constructor installs the HarfBuzz + FreeType
 * backend and references it, pulling both libraries out of `gui.a`. Without
 * this object the core uses the stb_truetype default and neither library is
 * linked.
 */

extern void xt_text_feature_register(void);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_text_feature_boot(void) {
  xt_text_feature_register();
}
#elif defined(_MSC_VER)
void xt_text_feature_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_text_feature_ctor)(void) =
    xt_text_feature_boot;
void xt_text_feature_boot(void) { xt_text_feature_register(); }
#endif
