/**
 * Oldest platform versions xbintsc supports, and the baseline every released
 * artifact is built against.
 *
 * The prebuilt runtime archives (`core`, `ext_node`, `gui`, `media`) and the
 * self-hosted compiler are built by CI with the **oldest toolchain each host
 * still offers**, targeting the floors below. A newer Xcode/Visual Studio on a
 * user's machine can link those older objects, but the reverse is what fails
 * (`LNK2038`, `LNK2019`, `Undefined symbols`, `was built for newer macOS
 * version`), which is why the direction matters.
 *
 * These values are the single source of truth for the archive build scripts,
 * `xbintsc doctor` and `doc/requirements.md`.
 */

/** Deployment target for every macOS artifact (also the Apple Silicon floor). */
export const MACOS_MINIMUM_VERSION = "11.0";

/**
 * Oldest Windows version the prebuilt objects and the compiler target.
 *
 * Windows 10 was the floor while the release jobs ran on `windows-2022`
 * (VS 2022 / MSVC v143). Those images cannot assemble a release: their bundled
 * `tar` has no built-in zstd, so `tar --zstd` hands compression to an external
 * program the step waits on forever. The release jobs therefore run on
 * `windows-2025`, whose toolset targets Windows 11, and the supported floor
 * moved with them.
 */
export const WINDOWS_MINIMUM_VERSION = "11";

/**
 * glibc the release archives are built and tested against: the GitHub
 * `ubuntu-24.04` image. Older distributions may link the npm package's
 * archives, but only this floor is supported.
 */
export const LINUX_MINIMUM_GLIBC = "2.39";

/** Human-readable support floor for the host running the compiler. */
export function supportFloor(platform: NodeJS.Platform = process.platform): string {
  switch (platform) {
    case "darwin":
      return `macOS ${MACOS_MINIMUM_VERSION}+`;
    case "win32":
      return `Windows ${WINDOWS_MINIMUM_VERSION}+`;
    case "linux":
      return `glibc ${LINUX_MINIMUM_GLIBC}+`;
    default:
      return `${platform} (unverified)`;
  }
}

/**
 * Make a macOS archive build target {@link MACOS_MINIMUM_VERSION}.
 *
 * Clang's Darwin driver reads `MACOSX_DEPLOYMENT_TARGET`, and CMake reads it as
 * the default for `CMAKE_OSX_DEPLOYMENT_TARGET`; setting both keeps the direct
 * engine compiles, the vendored CMake builds and FFmpeg's configure on the same
 * floor. An explicit value in the environment still wins, so a developer can
 * target an even older macOS deliberately. No-op on the other hosts.
 */
export function applyMacosDeploymentTarget(platform: NodeJS.Platform = process.platform): void {
  if (platform !== "darwin") return;
  if (!process.env.MACOSX_DEPLOYMENT_TARGET) {
    process.env.MACOSX_DEPLOYMENT_TARGET = MACOS_MINIMUM_VERSION;
  }
  if (!process.env.CMAKE_OSX_DEPLOYMENT_TARGET) {
    process.env.CMAKE_OSX_DEPLOYMENT_TARGET = MACOS_MINIMUM_VERSION;
  }
}
