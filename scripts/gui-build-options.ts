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

