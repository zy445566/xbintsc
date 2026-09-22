/**
 * Which toolchain xbintsc bundles for each host.
 *
 * The end goal is that `build` needs no user-installed compiler. Per the
 * recorded decisions (see `doc/requirements.md`):
 *
 *   Linux   -> the official LLVM release (clang + lld + llvm-ar)
 *   Windows -> llvm-mingw (clang + lld + the MinGW-w64 sysroot/CRT/import libs)
 *   macOS   -> nothing; the Xcode Command Line Tools are the prerequisite
 *
 * This module only maps a host to a pinned download; the actual fetch lives in
 * `scripts/fetch-toolchain.ts`.
 */

/** Pinned LLVM release used for the Linux bundle. */
export const LLVM_VERSION = "18.1.8";
/** Pinned llvm-mingw release used for the Windows bundle. */
export const LLVM_MINGW_VERSION = "20260908";

export interface ToolchainDownload {
  /** Download URL. */
  readonly url: string;
  /** Suggested local file name for the archive. */
  readonly archive: string;
  /** Archive type. Both are unpacked with `tar` (bsdtar reads zip). */
  readonly kind: "tar.xz" | "zip";
  /** Leading path segments to strip while extracting. */
  readonly stripComponents: number;
}

/**
 * The toolchain to bundle for a host, or undefined when the host's own SDK is
 * used instead (macOS: the Xcode Command Line Tools).
 */
export function toolchainDownload(
  platform: string = process.platform,
  arch: string = process.arch,
): ToolchainDownload | undefined {
  if (platform === "linux") {
    if (arch === "x64") {
      return {
        url: `https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-ubuntu-18.04.tar.xz`,
        archive: `clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu.tar.xz`,
        kind: "tar.xz",
        stripComponents: 1,
      };
    }
    if (arch === "arm64") {
      return {
        url: `https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/clang+llvm-${LLVM_VERSION}-aarch64-linux-gnu.tar.xz`,
        archive: `clang+llvm-${LLVM_VERSION}-aarch64-linux-gnu.tar.xz`,
        kind: "tar.xz",
        stripComponents: 1,
      };
    }
    return undefined;
  }

  if (platform === "win32" && arch === "x64") {
    return {
      url: `https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-x86_64.zip`,
      archive: `llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-x86_64.zip`,
      kind: "zip",
      stripComponents: 1,
    };
  }

  if (platform === "win32" && arch === "arm64") {
    // The llvm-mingw `aarch64` package is the Windows-on-ARM host build; its
    // clang targets aarch64 by default, so a native Windows ARM64 runner gets
    // native MinGW-w64 binaries with no MSVC dependency.
    return {
      url: `https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-aarch64.zip`,
      archive: `llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-aarch64.zip`,
      kind: "zip",
      stripComponents: 1,
    };
  }

  // macOS (and any other host) uses the system SDK; nothing to bundle.
  return undefined;
}

export interface SupportLibrary {
  /** Candidate download URLs, tried in order. */
  readonly urls: readonly string[];
  /** Local file name for the downloaded `.deb`. */
  readonly archive: string;
  /** Path inside the extracted `.deb` data tree. */
  readonly member: string;
  /** File name placed in `vendor/<os>-<arch>/lib/`. */
  readonly dest: string;
}

/**
 * Extra shared libraries the bundled toolchain needs but that are not part of
 * the OS C library. The official Linux LLVM build links the legacy
 * `libtinfo.so.5` soname, which current distributions no longer ship, so we
 * bundle it (from the Ubuntu `libtinfo5` package) into `vendor/<os>-<arch>/lib`
 * and point `LD_LIBRARY_PATH` at that directory.
 */
export function toolchainSupportLibraries(
  platform: string = process.platform,
  arch: string = process.arch,
): readonly SupportLibrary[] {
  if (platform === "linux" && arch === "x64") {
    return [
      {
        urls: [
          "https://security.ubuntu.com/ubuntu/pool/main/n/ncurses/libtinfo5_6.1-1ubuntu1.18.04.1_amd64.deb",
          "https://archive.ubuntu.com/ubuntu/pool/main/n/ncurses/libtinfo5_6.1-1ubuntu1.18.04.1_amd64.deb",
        ],
        archive: "libtinfo5.deb",
        member: "lib/x86_64-linux-gnu/libtinfo.so.5.9",
        dest: "libtinfo.so.5",
      },
    ];
  }
  if (platform === "linux" && arch === "arm64") {
    // The aarch64 release build links the same removed `libtinfo.so.5` soname as
    // the x86_64 one; arm64 packages live in the Ubuntu ports archive.
    return [
      {
        urls: [
          "https://ports.ubuntu.com/ubuntu-ports/pool/main/n/ncurses/libtinfo5_6.1-1ubuntu1.18.04.1_arm64.deb",
          "http://ports.ubuntu.com/ubuntu-ports/pool/main/n/ncurses/libtinfo5_6.1-1ubuntu1.18.04.1_arm64.deb",
        ],
        archive: "libtinfo5.deb",
        member: "lib/aarch64-linux-gnu/libtinfo.so.5.9",
        dest: "libtinfo.so.5",
      },
    ];
  }
  return [];
}
