#!/usr/bin/env node
/**
 * Build the GUI extension archive:
 *
 *   runtime/lib/<os>-<arch>/gui.a
 *
 * The archive bundles the engine (`runtime/ext_gui/*.cpp`) together with a
 * statically built SDL3, so a released compiler links GUI programs with no
 * external dependency beyond the OS frameworks declared by
 * `src/extensions/gui/index.ts`.
 *
 * Steps:
 *   1. fetch the pinned SDL3 source into `vendor/SDL` (gitignored),
 *   2. build `libSDL3.a` with CMake,
 *   2b. fetch + build static FreeType and HarfBuzz for text,
 *   2c. fetch + build static glslang (GLSL ES -> SPIR-V) and SPIRV-Cross
 *       (SPIR-V -> Metal Shading Language),
 *   3. compile the engine translation units,
 *   4. merge everything into `gui.a` (libtool on macOS, `ar -M` elsewhere;
 *      `gui.lib` on Windows, built from MSVC-compatible COFF objects).
 *
 * Media decoding is *not* part of this archive: it lives in the separate
 * `media` extension (`npm run media`, `runtime/ext_media/`), so a program that
 * only renders HTML/CSS never links FFmpeg.
 *
 * Environment:
 *   xbintsc_CXX    C++ compiler override (default: clang++ next to the resolved clang)
 *   xbintsc_CMAKE  CMake override (default: a vendored CMake, then `cmake`)
 *   xbintsc_AR     archiver override (default: llvm-ar next to clang, then llvm-ar)
 *   SDL3_TAG       SDL3 git tag to check out (default: release-3.2.10)
 *   FREETYPE_VERSION  FreeType version (default: 2.13.3)
 *   HARFBUZZ_VERSION  HarfBuzz version (default: 10.1.0)
 *   SPIRV_CROSS_VERSION  SPIRV-Cross tag (default: vulkan-sdk-1.4.313.0)
 *
 * The C/C++ compiler is the same clang the driver resolves (see
 * `src/driver/toolchain-provider.ts`), so the archive always matches the ABI of
 * the runtime and emitted objects. On Windows that is an MSVC-targeting clang,
 * normally the LLVM bundled with Visual Studio, and the result is a COFF
 * `gui.lib`.
 */

import { existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync } from "node:fs";
import { spawnSync, type SpawnSyncReturns } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { platformSlug } from "../src/driver/paths.js";
import { isMsvcTarget, probeClang, realRunner } from "../src/driver/toolchain.js";
import { resolveToolchain } from "../src/driver/toolchain-provider.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeDir = join(root, "runtime");
const engineDir = join(runtimeDir, "ext_gui");
const vendorDir = join(root, "vendor");
const sdlSrc = join(vendorDir, "SDL");
/* SDL's build tree is a *sibling* of its source, like FreeType's and HarfBuzz's,
 * never inside it: the CI cache keyed on a vendored library ships the whole
 * `vendor/SDL` tree, and a CMake build tree is not relocatable across machines
 * or platforms. `CMakeCache.txt` records the host's compiler and probes, and
 * SDL's video options are `option()`s — the first configure to run wins, so a
 * cached `SDL_X11=OFF` (e.g. from a Windows configure, where the option's
 * default is OFF) survives into a Linux configure and SDL then aborts with
 * "SDL could not find X11 or Wayland development libraries". Keeping the build
 * tree out of the cached source path makes each machine configure its own. */
const sdlBuild = join(vendorDir, "sdl-build");
const freetypeSrc = join(vendorDir, "freetype");
const freetypeBuild = join(vendorDir, "freetype-build");
const harfbuzzSrc = join(vendorDir, "harfbuzz");
const harfbuzzBuild = join(vendorDir, "harfbuzz-build");
/* glslang translates the GLSL ES that WebGL programs supply at run time; it is
 * built statically and merged into the archive, so a compiled binary needs no
 * Vulkan SDK. The SPIR-V headers are its only external dependency. */
const glslangSrc = join(vendorDir, "glslang");
const glslangBuild = join(vendorDir, "glslang-build");
const spirvHeadersSrc = join(glslangSrc, "External", "spirv-headers");
/* SPIRV-Cross turns the same SPIR-V into the Metal Shading Language that
 * SDL_GPU's Metal backend consumes at run time (macOS). It needs no external
 * headers: its own parser reads the module that glslang produced. */
const spirvCrossSrc = join(vendorDir, "spirv-cross");
const spirvCrossBuild = join(vendorDir, "spirv-cross-build");
const objDir = join(root, "build", "gui-obj");
const outDir = join(runtimeDir, "lib", platformSlug());

const isWindows = process.platform === "win32";
const objectSuffix = isWindows ? ".obj" : ".o";
const output = join(outDir, isWindows ? "gui.lib" : "gui.a");

/* CMake generator and ABI settings used for every vendored library. Ninja is
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
 * effect when policy CMP0091 is NEW, but FreeType and HarfBuzz declare
 * `cmake_minimum_required` below 3.15, which defaults the policy to OLD and
 * silently ignores it — hence `CMAKE_POLICY_DEFAULT_CMP0091`.
 *
 * The tool paths are absolute and come from the same clang the driver resolves
 * (on Windows, the LLVM bundled with Visual Studio when no newer one is on
 * `PATH`), so every object merged into the archive shares one ABI and one
 * optimization behaviour. Never leave these as bare `clang`/`clang++`: an older
 * compiler earlier on `PATH` may be picked up at build time. */
function cmakeToolchainArgs(): string[] {
  const compilers = [
    `-DCMAKE_C_COMPILER=${clang}`,
    `-DCMAKE_CXX_COMPILER=${clangxx}`,
  ];
  if (!isWindows) return compilers;
  return [
    "-G", "Ninja",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    ...compilers,
    `-DCMAKE_AR=${llvmAr}`,
  ];
}

const SDL_TAG = process.env.SDL3_TAG || "release-3.2.10";
const FREETYPE_VERSION = process.env.FREETYPE_VERSION || "2.13.3";
const HARFBUZZ_VERSION = process.env.HARFBUZZ_VERSION || "10.1.0";
const GLSLANG_VERSION = process.env.GLSLANG_VERSION || "16.6.0";
const SPIRV_HEADERS_VERSION = process.env.SPIRV_HEADERS_VERSION || "vulkan-sdk-1.4.363.0";
const SPIRV_CROSS_VERSION = process.env.SPIRV_CROSS_VERSION || "vulkan-sdk-1.4.313.0";
const FREETYPE_URL = `https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.xz`;
const HARFBUZZ_URL = `https://github.com/harfbuzz/harfbuzz/releases/download/${HARFBUZZ_VERSION}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz`;
const GLSLANG_URL = `https://codeload.github.com/KhronosGroup/glslang/tar.gz/refs/tags/${GLSLANG_VERSION}`;
const SPIRV_HEADERS_URL = `https://codeload.github.com/KhronosGroup/SPIRV-Headers/tar.gz/refs/tags/${SPIRV_HEADERS_VERSION}`;
const SPIRV_CROSS_URL = `https://codeload.github.com/KhronosGroup/SPIRV-Cross/tar.gz/refs/tags/${SPIRV_CROSS_VERSION}`;

function fail(message: string): never {
  console.error(`xbintsc: ${message}`);
  process.exit(1);
}

function run(
  command: string,
  args: readonly string[],
  options: { input?: string; quietStderr?: boolean; cwd?: string } = {},
): SpawnSyncReturns<string> {
  const quiet = options.quietStderr === true;
  const result = spawnSync(command, args, {
    stdio: options.input !== undefined ? ["pipe", "inherit", "inherit"] : quiet ? ["ignore", "inherit", "pipe"] : "inherit",
    encoding: "utf8",
    input: options.input,
    cwd: options.cwd,
  });
  if (result.status !== 0) {
    if (quiet && result.stderr) process.stderr.write(result.stderr);
    fail(`${command} ${args.join(" ")} failed with status ${result.status ?? "?"}`);
  }
  return result;
}

function isRunnable(candidate: string): boolean {
  return !spawnSync(candidate, ["--version"], { encoding: "utf8" }).error;
}

function firstAvailable(candidates: readonly (string | undefined)[]): string | undefined {
  for (const candidate of candidates) {
    if (candidate && isRunnable(candidate)) return candidate;
  }
  return undefined;
}

/** Vendored CMake from `vendor/tools`, then `cmake` on PATH. */
function findCmake(): string {
  const candidates: string[] = [];
  if (process.env.xbintsc_CMAKE) candidates.push(process.env.xbintsc_CMAKE);
  if (existsSync(join(vendorDir, "tools"))) {
    for (const entry of readdirSync(join(vendorDir, "tools"))) {
      if (!entry.startsWith("cmake-")) continue;
      candidates.push(join(vendorDir, "tools", entry, "CMake.app", "Contents", "bin", "cmake"));
      candidates.push(join(vendorDir, "tools", entry, "bin", "cmake"));
    }
  }
  candidates.push("cmake");
  const cmake = firstAvailable(candidates);
  if (!cmake) fail("no CMake found; install it or set xbintsc_CMAKE");
  return cmake;
}

/** The tool that sits next to `clang` in the same LLVM installation. */
function siblingTool(clangPath: string, name: string): string | undefined {
  if (!clangPath.includes("/") && !clangPath.includes("\\")) return undefined;
  const candidate = join(dirname(clangPath), `${name}${isWindows ? ".exe" : ""}`);
  return existsSync(candidate) ? candidate : undefined;
}

/** Resolve `clang++` from the same LLVM as the driver's clang. */
function findClangxx(clangPath: string): string {
  const override = process.env.xbintsc_CXX;
  if (override) {
    const probe = probeClang(realRunner, override);
    if (!probe.responds) fail(`xbintsc_CXX ${override} does not respond to --version`);
    if (isWindows && (probe.version === undefined || (probe.target !== undefined && !isMsvcTarget(probe.target)))) {
      fail(
        `xbintsc_CXX ${override} is not an MSVC-targeting clang++ ` +
          `(target ${probe.target ?? "unknown"}); the Windows GUI build requires one, for example ` +
          "x86_64-pc-windows-msvc.",
      );
    }
    return override;
  }
  const sibling = siblingTool(clangPath, "clang++");
  if (sibling) return sibling;
  const onPath = firstAvailable(["clang++"]);
  if (onPath) return onPath;
  fail("no clang++ found next to the resolved clang or on PATH; install LLVM or set xbintsc_CXX");
}

/** Resolve the archiver that matches the compiler, preferring LLVM's own. */
function findArchiver(clangPath: string): string {
  const sibling = siblingTool(clangPath, "llvm-ar");
  const archiver = firstAvailable([process.env.xbintsc_AR, sibling, "llvm-ar", isWindows ? undefined : "ar"]);
  if (!archiver) fail("no archiver found; install llvm-ar or set xbintsc_AR");
  return archiver;
}

/* One toolchain for the whole archive: the same clang the driver uses, its
 * sibling clang++, and the matching llvm-ar. No `gcc`/`g++` fallbacks, and
 * Windows never falls back to a non-LLVM archiver — mixing another C++ runtime
 * or object format would break the final link. */
function driverClang(): string {
  try {
    return resolveToolchain().clang;
  } catch (error) {
    fail(error instanceof Error ? error.message : String(error));
  }
}

const clang = driverClang();
const clangxx = findClangxx(clang);
const llvmAr = findArchiver(clang);


/** The compiler a previous CMake configure recorded for `buildDir`. */
function cachedCompiler(buildDir: string, variable: string): string | undefined {
  const cache = join(buildDir, "CMakeCache.txt");
  if (!existsSync(cache)) return undefined;
  const match = new RegExp(`^${variable}:[^=]*=(.*)$`, "m").exec(readFileSync(cache, "utf8"));
  return match?.[1]?.trim();
}

function sameToolPath(a: string, b: string): boolean {
  const normalize = (value: string): string => value.replace(/\\/g, "/").replace(/\/+$/, "");
  return process.platform === "win32"
    ? normalize(a).toLowerCase() === normalize(b).toLowerCase()
    : normalize(a) === normalize(b);
}

/**
 * Configure a vendored CMake project, discarding a build tree whose cached
 * compiler no longer matches the resolved toolchain. CMake refuses to switch
 * compilers in place, and a tree configured by another clang would mix ABIs in
 * the final archive.
 */
function configureCmake(
  name: string,
  source: string,
  build: string,
  extraArgs: readonly string[],
): string {
  const cachedC = cachedCompiler(build, "CMAKE_C_COMPILER");
  const cachedCxx = cachedCompiler(build, "CMAKE_CXX_COMPILER");
  const stale =
    (cachedC !== undefined && !sameToolPath(cachedC, clang)) ||
    (cachedCxx !== undefined && !sameToolPath(cachedCxx, clangxx));
  if (stale) {
    console.log(`xbintsc: toolchain changed; reconfiguring ${name}`);
    rmSync(build, { recursive: true, force: true });
  }
  const cmake = findCmake();
  run(cmake, ["-S", source, "-B", build, ...cmakeToolchainArgs(), ...extraArgs]);
  return cmake;
}

/** Directories a CMake `find_path` would search, relative to the filesystem
 * root, used to check that a development package is installed. */const HEADER_PREFIXES = ["/usr/include", "/usr/local/include", "/opt/homebrew/include"];

/** True on Debian and its derivatives, where the message below can name the
 * packages to install. Anywhere else the headers may well live under a prefix
 * this check does not know (`/usr/pkg`, `/nix/store`, …), so it stays quiet
 * instead of failing a build that would have worked. */
function isDebianLike(): boolean {
  return existsSync("/etc/debian_version");
}

/** Fail with the package names to install when a header SDL needs is missing,
 * rather than letting SDL configure itself without a window backend and then
 * abort with a summary that does not say which package is missing. */
function requireHeaders(feature: string, headers: readonly string[], packages: string): void {
  if (isWindows || !isDebianLike()) return;
  const missing = headers.filter(
    (header) => !HEADER_PREFIXES.some((prefix) => existsSync(join(prefix, header))),
  );
  if (missing.length === 0) return;
  const hint =
    process.env.xbintsc_SKIP_HEADER_CHECK === "1"
      ? ""
      : " (set xbintsc_SKIP_HEADER_CHECK=1 if they are installed under a prefix this check does not know)";
  fail(
    `${feature} needs ${missing.join(", ")}, which is not installed; ` +
      `install its development package first (Debian/Ubuntu: ${packages})${hint}`,
  );
}

/** First of `candidates` that exists on disk (CMake generators place static
 * libraries either at the build root or under `Release/`). */
function firstExisting(candidates: readonly string[]): string | undefined {
  for (const candidate of candidates) {
    if (existsSync(candidate)) return candidate;
  }
  return undefined;
}

/** Locate a library CMake produced, accepting the POSIX and MSVC names and
 * falling back to a version-suffixed MSVC name (e.g. `freetype-2.13.3.lib`). */
function builtLibrary(buildDir: string, names: readonly string[], pattern?: RegExp): string | undefined {
  const candidates: string[] = [];
  for (const name of names) {
    candidates.push(join(buildDir, name));
    candidates.push(join(buildDir, "Release", name));
    candidates.push(join(buildDir, "Debug", name));
  }
  const exact = firstExisting(candidates);
  if (exact || !pattern) return exact;
  for (const dir of [buildDir, join(buildDir, "Release"), join(buildDir, "Debug")]) {
    if (!existsSync(dir)) continue;
    for (const entry of readdirSync(dir)) {
      if (pattern.test(entry)) return join(dir, entry);
    }
  }
  return undefined;
}

function sdlArchive(): string | undefined {
  return builtLibrary(sdlBuild, ["libSDL3.a", "SDL3-static.lib", "SDL3.lib"], /^SDL3[-\w.]*\.lib$/);
}
function freetypeArchive(): string | undefined {
  return builtLibrary(freetypeBuild, ["libfreetype.a", "freetype.lib"], /^freetype[-\d.]*\.lib$/);
}
function harfbuzzArchive(): string | undefined {
  return builtLibrary(harfbuzzBuild, ["libharfbuzz.a", "harfbuzz.lib"], /^harfbuzz[-\d.]*\.lib$/);
}
function glslangArchive(): string | undefined {
  return builtLibrary(join(glslangBuild, "glslang"), ["libglslang.a", "glslang.lib"]);
}
function glslangLimitsArchive(): string | undefined {
  return builtLibrary(join(glslangBuild, "glslang"),
    ["libglslang-default-resource-limits.a", "glslang-default-resource-limits.lib"]);
}
/* SPIRV-Cross's MSL target pulls in its GLSL base class and the core module. */
function spirvCrossArchive(name: string): string | undefined {
  return builtLibrary(spirvCrossBuild, [`lib${name}.a`, `${name}.lib`]);
}

/* -- 1. SDL3 source ------------------------------------------------------- */

if (!existsSync(join(sdlSrc, "CMakeLists.txt"))) {
  console.log(`xbintsc: fetching SDL3 ${SDL_TAG} into vendor/SDL`);
  mkdirSync(vendorDir, { recursive: true });
  run("git", ["clone", "--depth", "1", "--branch", SDL_TAG, "https://github.com/libsdl-org/SDL.git", sdlSrc]);
}

/* -- 2. static SDL3 ------------------------------------------------------- */

const existingSdlArchive = sdlArchive();
if (!existingSdlArchive) {
  /* Older trees configured SDL inside its source (`vendor/SDL/build`) and a
   * cache from one of those would be reused by the next configure — the exact
   * failure `sdlBuild` above exists to prevent. */
  const legacyBuild = join(sdlSrc, "build");
  if (existsSync(join(legacyBuild, "CMakeCache.txt"))) {
    console.log("xbintsc: discarding the legacy in-source SDL build tree");
    rmSync(legacyBuild, { recursive: true, force: true });
  }

  /* SDL enables a window backend from the headers it finds, and aborts when it
   * finds neither X11 nor Wayland, so say which package is missing instead of
   * relaying its summary. */
  requireHeaders(
    "SDL3's X11 video driver",
    /* `Xlib.h` comes from libx11-dev; `Xext.h` is the header SDL gates HAVE_X11
     * on as well, and it comes from libxext-dev. */
    ["X11/Xlib.h", "X11/extensions/Xext.h"],
    "libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxfixes-dev",
  );
  requireHeaders("SDL3's Wayland video driver", ["wayland-client-core.h"],
    "libwayland-dev libxkbcommon-dev libdecor-0-dev");

  const cmake = configureCmake("SDL3", sdlSrc, sdlBuild, [
    "-DCMAKE_BUILD_TYPE=Release",
    "-DSDL_SHARED=OFF",
    "-DSDL_STATIC=ON",
    "-DSDL_TESTS=OFF",
    "-DSDL_EXAMPLES=OFF",
  ]);
  console.log(`xbintsc: building static SDL3 with ${cmake}`);
  run(cmake, ["--build", sdlBuild, "--config", "Release", "--target", "SDL3-static", "--parallel"]);
} else {
  console.log(`xbintsc: reusing existing ${existingSdlArchive}`);
}

/* -- 2b. FreeType + HarfBuzz (text) --------------------------------------- */

/** Download a release tarball into `vendor/` and extract it under `name`.
 * Release tarballs are used (not git clones) because they ship generated
 * sources, so no ragel/autotools step is needed. `marker` is a file that only
 * exists once the tarball has been extracted (e.g. `CMakeLists.txt`). */
function ensureTarballSource(name: string, url: string, extractedName: string, marker = "CMakeLists.txt"): string {
  const destination = join(vendorDir, name);
  if (existsSync(join(destination, marker))) return destination;
  const tarball = join(vendorDir, `${name}.tar.xz`);
  console.log(`xbintsc: fetching ${name} from ${url}`);
  mkdirSync(vendorDir, { recursive: true });
  run("curl", ["-sL", "--fail", "-o", tarball, url]);
  run("tar", ["-xf", tarball, "-C", vendorDir]);
  const extracted = join(vendorDir, extractedName);
  if (!existsSync(extracted)) fail(`expected ${extractedName} after extracting ${name}`);
  rmSync(destination, { recursive: true, force: true });
  renameSync(extracted, destination);
  rmSync(tarball, { force: true });
  return destination;
}

ensureTarballSource("freetype", FREETYPE_URL, `freetype-${FREETYPE_VERSION}`);
ensureTarballSource("harfbuzz", HARFBUZZ_URL, `harfbuzz-${HARFBUZZ_VERSION}`);

if (!existsSync(freetypeArchive() ?? "")) {
  const cmake = configureCmake("FreeType", freetypeSrc, freetypeBuild, [
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_SHARED_LIBS=OFF",
    /* We only need TrueType/OpenType outlines; skip the optional codecs. */
    "-DFT_DISABLE_ZLIB=TRUE",
    "-DFT_DISABLE_BZIP2=TRUE",
    "-DFT_DISABLE_PNG=TRUE",
    "-DFT_DISABLE_HARFBUZZ=TRUE",
    "-DFT_DISABLE_BROTLI=TRUE",
  ]);
  console.log("xbintsc: building static FreeType");
  run(cmake, ["--build", freetypeBuild, "--config", "Release", "--parallel"]);
} else {
  console.log(`xbintsc: reusing existing ${freetypeArchive()}`);
}

if (!existsSync(harfbuzzArchive() ?? "")) {
  const cmake = configureCmake("HarfBuzz", harfbuzzSrc, harfbuzzBuild, [
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DHB_BUILD_UTILS=OFF",
    "-DHB_BUILD_TESTS=OFF",
    "-DHB_BUILD_SUBSET=OFF",
    "-DHB_HAVE_FREETYPE=OFF",
    "-DHB_HAVE_GLIB=OFF",
    "-DHB_HAVE_ICU=OFF",
    "-DHB_HAVE_GRAPHITE2=OFF",
  ]);
  console.log("xbintsc: building static HarfBuzz");
  run(cmake, ["--build", harfbuzzBuild, "--config", "Release", "--parallel"]);
} else {
  console.log(`xbintsc: reusing existing ${harfbuzzArchive()}`);
}

/* -- 2c. glslang (WebGL GLSL ES -> SPIR-V) -------------------------------- */

ensureTarballSource("glslang", GLSLANG_URL, `glslang-${GLSLANG_VERSION}`);
ensureTarballSource(join("glslang", "External", "spirv-headers"), SPIRV_HEADERS_URL,
  `SPIRV-Headers-${SPIRV_HEADERS_VERSION}`);

if (!existsSync(glslangArchive() ?? "")) {
  const cmake = configureCmake("glslang", glslangSrc, glslangBuild, [
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DENABLE_OPT=OFF",
    "-DENABLE_HLSL=OFF",
    "-DENABLE_GLSLANG_BINARIES=OFF",
    "-DGLSLANG_TESTS=OFF",
  ]);
  console.log("xbintsc: building static glslang");
  run(cmake, ["--build", glslangBuild, "--config", "Release", "--parallel"]);
} else {
  console.log(`xbintsc: reusing existing ${glslangArchive()}`);
}

/* -- 2d. SPIRV-Cross (SPIR-V -> Metal Shading Language) ------------------- */

ensureTarballSource("spirv-cross", SPIRV_CROSS_URL, `SPIRV-Cross-${SPIRV_CROSS_VERSION}`);

if (!existsSync(spirvCrossArchive("spirv-cross-msl") ?? "")) {
  const cmake = configureCmake("SPIRV-Cross", spirvCrossSrc, spirvCrossBuild, [
    "-DCMAKE_BUILD_TYPE=Release",
    "-DSPIRV_CROSS_CLI=OFF",
    "-DSPIRV_CROSS_ENABLE_TESTS=OFF",
    /* Only the MSL target is used; skip the C++, HLSL, reflection and C APIs
     * so the archive stays small. MSL links against the GLSL base class. */
    "-DSPIRV_CROSS_ENABLE_HLSL=OFF",
    "-DSPIRV_CROSS_ENABLE_CPP=OFF",
    "-DSPIRV_CROSS_ENABLE_REFLECT=OFF",
    "-DSPIRV_CROSS_ENABLE_UTIL=OFF",
    "-DSPIRV_CROSS_ENABLE_C_API=OFF",
  ]);
  console.log("xbintsc: building static SPIRV-Cross");
  run(cmake, ["--build", spirvCrossBuild, "--config", "Release", "--parallel"]);
} else {
  console.log(`xbintsc: reusing existing ${spirvCrossArchive("spirv-cross-msl")}`);
}

/* -- 3. engine objects ---------------------------------------------------- */

mkdirSync(objDir, { recursive: true });
const includeFlags = [
  "-I", runtimeDir,
  "-I", join(sdlSrc, "include"),
  "-I", join(freetypeSrc, "include"),
  "-I", join(freetypeBuild, "include"),
  "-I", join(harfbuzzSrc, "src"),
  "-I", glslangSrc,
  "-I", join(spirvHeadersSrc, "include"),
  "-I", spirvCrossSrc,
];
const sources = readdirSync(engineDir)
  .filter((entry) => entry.endsWith(".cpp"))
  .sort();

const objects: string[] = [];
const cxxFlags = ["-std=c++17", "-O2", "-Wall", "-Wextra", ...includeFlags];
/* The engine is C++ and the MSVC C++ runtime must match the static C CRT that
 * clang links by default, so let every engine object record `libcpmt` as a
 * default library. The extension itself only contributes the DLL import library
 * (`-lmsvcprt`, `src/extensions/gui/index.ts`), which is inconsistent with that
 * CRT and makes the final link fail with `LNK2038`/unresolved `__imp_*`. */
if (isWindows) cxxFlags.push("-Xclang", "--dependent-lib=libcpmt");
/* The MSVC ABI rejects `-fPIC`; position independence is the default elsewhere. */
if (!isWindows) cxxFlags.splice(3, 0, "-fPIC");
for (const source of sources) {
  const object = join(objDir, source.replace(/\.cpp$/, objectSuffix));
  console.log(`xbintsc: compiling ${source}`);
  run(clangxx, [...cxxFlags, "-c", join(engineDir, source), "-o", object]);
  objects.push(object);
}

/* -- 4. merge into gui.a / gui.lib ---------------------------------------- */

const sdl = sdlArchive();
const freetype = freetypeArchive();
const harfbuzz = harfbuzzArchive();
const glslang = glslangArchive();
const glslangLimits = glslangLimitsArchive();
const spirvCrossCore = spirvCrossArchive("spirv-cross-core");
const spirvCrossGlsl = spirvCrossArchive("spirv-cross-glsl");
const spirvCrossMsl = spirvCrossArchive("spirv-cross-msl");
if (!sdl || !freetype || !harfbuzz || !glslang || !glslangLimits ||
    !spirvCrossCore || !spirvCrossGlsl || !spirvCrossMsl) {
  fail("one of the SDL3/FreeType/HarfBuzz/glslang/SPIRV-Cross archives was not found after building");
}
const libraries = [
  sdl, freetype, harfbuzz, glslang, glslangLimits,
  spirvCrossCore, spirvCrossGlsl, spirvCrossMsl,
];

mkdirSync(outDir, { recursive: true });
rmSync(output, { force: true });
if (process.platform === "darwin") {
  /* libtool warns about the empty object files SDL3's archive carries; hide it
   * unless the merge actually fails. */
  run("libtool", ["-static", "-o", output, ...objects, ...libraries], {
    quietStderr: true,
  });
} else {
  /* `ar -M` (GNU/LLVM) merges archives through an MRI script. Windows paths are
   * normalised to `/` so the MRI parser does not read `\` as an escape. */
  const mri = (path: string): string => path.replace(/\\/g, "/");
  const ar = llvmAr;
  const script = [
    `create ${mri(output)}`,
    ...objects.map((object) => `addmod ${mri(object)}`),
    ...libraries.map((library) => `addlib ${mri(library)}`),
    "save",
    "end",
  ].join("\n");
  run(ar, ["-M"], { input: script });
}

console.log(`xbintsc: wrote ${output}`);
