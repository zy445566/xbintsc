/*
 * xbintsc GUI engine — `gpu` link feature registration bridge.
 *
 * This tiny C translation unit is compiled and linked *only* when the compiler
 * decides the program may compile GLSL (`getContext("webgl…")`,
 * `mix-blend-mode`, or markup built at run time). Its constructor calls
 * `xt_gpu_feature_register` in `gpu_feature.cpp`, which installs the shader
 * table and references glslang/SPIRV-Cross; those references are what make the
 * linker extract the translator members from `gui.a`.
 *
 * When the feature is inactive this object is absent, so nothing references the
 * translator and the table in `gpu_shader.cpp` stays null.
 */

extern void xt_gpu_feature_register(void);

#if defined(__GNUC__) || defined(__clang__)
/* clang targeting Windows (the toolchain xbintsc uses there) implements this
 * attribute with a `.CRT$XCU` entry, which the MSVC CRT runs before `main`. */
__attribute__((constructor)) static void xt_gpu_feature_boot(void) {
  xt_gpu_feature_register();
}
#elif defined(_MSC_VER)
void xt_gpu_feature_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_gpu_feature_ctor)(void) = xt_gpu_feature_boot;
void xt_gpu_feature_boot(void) { xt_gpu_feature_register(); }
#endif
