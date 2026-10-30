/**
 * Size options shared by the GUI archive build.
 *
 * Kept apart from `build-gui.ts` so the orchestration stays under the
 * repository's per-file line budget and the "what do we compile out?" policy is
 * reviewable in one place.
 */

/**
 * CMake generator and ABI settings used for every vendored library. Ninja is
 * selected on Windows because the Visual Studio generator imports the
 * machine-wide MSBuild/vcpkg integration, whose `LIBPATH` shadows the Windows
 * SDK and makes the compiler probe fail with
 * `LNK1104: cannot open file 'ucrtd.lib'`.
 *
 * `CMAKE_MSVC_RUNTIME_LIBRARY` is pinned to the *static* CRT (`MultiThreaded`),
 * which is clang's Windows default (`-defaultlib:libcmt`) and what xbintsc uses
 * for its own runtime objects. Building the vendored libraries against the DLL
 * CRT instead fails the final link with
 * `LNK2038: mismatch detected for 'RuntimeLibrary'`. The variable only takes
 * effect when policy CMP0091 is NEW, but some vendored projects declare
 * `cmake_minimum_required` below 3.15, which defaults the policy to OLD and
 * silently ignores it — hence `CMAKE_POLICY_DEFAULT_CMP0091`.
 *
 * The tool paths are absolute and come from the same clang the driver resolves
 * (on Windows, the LLVM bundled with Visual Studio when no newer one is on
 * `PATH`), so every object merged into the archive shares one ABI and one
 * optimization behaviour. Never leave these as bare `clang`/`clang++`: an older
 * compiler earlier on `PATH` may be picked up at build time.
 */
export function cmakeToolchainArgs(toolchain: {
  readonly clang: string;
  readonly clangxx: string;
  readonly llvmAr: string;
  readonly isWindows: boolean;
}): string[] {
  const compilers = [
    `-DCMAKE_C_COMPILER=${toolchain.clang}`,
    `-DCMAKE_CXX_COMPILER=${toolchain.clangxx}`,
  ];
  if (!toolchain.isWindows) return compilers;
  return [
    "-G", "Ninja",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    ...compilers,
    `-DCMAKE_AR=${toolchain.llvmAr}`,
  ];
}

/** One function/data item per section, so the linker can drop what a program
 * never references (`--gc-sections`, `-dead_strip`, link.exe `/OPT:REF`). */
export const SECTION_FLAGS = ["-ffunction-sections", "-fdata-sections"] as const;

/** The same flags as CMake cache entries. */
export const SECTION_CMAKE_FLAGS = [
  `-DCMAKE_C_FLAGS=${SECTION_FLAGS.join(" ")}`,
  `-DCMAKE_CXX_FLAGS=${SECTION_FLAGS.join(" ")}`,
];

/**
 * HarfBuzz APIs the engine never uses (draw/paint/color/name/style/metrics/…).
 * `HB_NO_CFF`, `HB_NO_OPEN`, `HB_NO_VAR` and `HB_NO_HINTING` are deliberately
 * *not* set: the shaping backend must load files and keep CFF/OTF and
 * variable-font shaping. `HB_NO_MT` is safe because text is shaped on the main
 * thread.
 *
 * `HB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR` is needed because HarfBuzz raises its
 * "should never happen" warning set to errors inside `src/hb.hh`, *after* the
 * command line, so a lean define that leaves a helper unreferenced becomes a
 * hard compile error. macOS is where that bites: `HB_NO_MMAP` leaves
 * `_open_resource_fork` unused (`hb-blob.cc`) and `HB_NO_OT_FONT_GLYPH_NAMES`
 * leaves the CoreText glyph-name helpers unused (`hb-coretext-font.cc`). With
 * this define they are warnings again; every other lean define still applies.
 */
export const HB_LEAN_DEFINES = [
  "-DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR",
  "-DHB_NO_DRAW",
  "-DHB_NO_PAINT",
  "-DHB_NO_COLOR",
  "-DHB_NO_NAME",
  "-DHB_NO_STYLE",
  "-DHB_NO_METRICS",
  "-DHB_NO_MATH",
  "-DHB_NO_META",
  "-DHB_NO_BITMAP",
  "-DHB_NO_VERTICAL",
  "-DHB_NO_BUFFER_SERIALIZE",
  "-DHB_NO_BUFFER_VERIFY",
  "-DHB_NO_FACE_COLLECT_UNICODES",
  "-DHB_NO_MMAP",
  "-DHB_NO_OT_FONT_GLYPH_NAMES",
  "-DHB_NO_LAYOUT_FEATURE_PARAMS",
  "-DHB_NO_LAYOUT_COLLECT_GLYPHS",
  "-DHB_NO_ERRNO",
  "-DHB_NO_LANGUAGE_LONG",
  "-DHB_NO_LANGUAGE_PRIVATE_SUBTAG",
  "-DHB_NO_MT",
  "-DHB_NO_ATEXIT",
  "-DHB_NO_GETENV",
  "-DHB_NO_SETLOCALE",
];

/**
 * SDL subsystems the GUI engine never calls. Disabling them removes whole
 * translation units (joystick/gamepad/HIDAPI, haptics, sensors, camera, the
 * SDL_Render software blitter and the OpenGL/offscreen video drivers) from the
 * static archive. Audio, video, events, dialogs and SDL_GPU stay enabled.
 */
export const LEAN_SDL_OPTIONS = [
  "-DSDL_JOYSTICK=OFF",
  "-DSDL_HAPTIC=OFF",
  "-DSDL_SENSOR=OFF",
  "-DSDL_CAMERA=OFF",
  "-DSDL_RENDER=OFF",
  "-DSDL_OPENGL=OFF",
  "-DSDL_OPENGLES=OFF",
  "-DSDL_OFFSCREEN=OFF",
  "-DSDL_HIDAPI=OFF",
  "-DSDL_DIALOG=ON",
];

