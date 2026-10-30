/*
 * xbintsc GUI engine — `webgl` link feature registration bridge.
 *
 * Compiled and linked only when the compiler detects that the program may ask
 * for a WebGL context. Its constructor calls `xt_webgl_feature_register`, which
 * installs `XtWebglApi` and references the WebGL context/DOM bindings, pulling
 * them out of `gui.a`. Without this object the table stays null and those
 * members are never linked.
 */

extern void xt_webgl_feature_register(void);

#if defined(__GNUC__) || defined(__clang__)
/* clang targeting Windows (the toolchain xbintsc uses there) implements this
 * attribute with a `.CRT$XCU` entry, which the MSVC CRT runs before `main`. */
__attribute__((constructor)) static void xt_webgl_feature_boot(void) {
  xt_webgl_feature_register();
}
#elif defined(_MSC_VER)
void xt_webgl_feature_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_webgl_feature_ctor)(void) =
    xt_webgl_feature_boot;
void xt_webgl_feature_boot(void) { xt_webgl_feature_register(); }
#endif
