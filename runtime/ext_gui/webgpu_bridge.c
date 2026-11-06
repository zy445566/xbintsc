/*
 * xbintsc GUI engine — `webgpu` link feature registration bridge.
 *
 * Compiled and linked only when the compiler detects that the program may use
 * WebGPU. Its constructor calls `xt_webgpu_feature_register`, which installs
 * `XtWebgpuApi` and references the WebGPU runtime, pulling it out of `gui.a`.
 */

extern void xt_webgpu_feature_register(void);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_webgpu_feature_boot(void) {
  xt_webgpu_feature_register();
}
#elif defined(_MSC_VER)
void xt_webgpu_feature_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_webgpu_feature_ctor)(void) =
    xt_webgpu_feature_boot;
void xt_webgpu_feature_boot(void) { xt_webgpu_feature_register(); }
#endif
