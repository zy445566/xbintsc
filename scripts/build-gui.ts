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
 *   3. compile the engine translation units,
 *   4. merge everything into `gui.a` (libtool on macOS, `ar -M` elsewhere).
 *
 * Environment:
 *   xbintsc_CXX    C++ compiler override (default: clang++/c++/g++)
 *   xbintsc_CMAKE  CMake override (default: a vendored CMake, then `cmake`)
 *   SDL3_TAG       SDL3 git tag to check out (default: release-3.2.10)
 *   FREETYPE_VERSION  FreeType version (default: 2.13.3)
 *   HARFBUZZ_VERSION  HarfBuzz version (default: 10.1.0)
 *
 * Windows is not supported yet: it needs an MSVC-compatible `.lib` and a
 * D3D12/DXIL SDL3 build.
 */

import { existsSync, mkdirSync, readdirSync, renameSync, rmSync } from "node:fs";
import { spawnSync, type SpawnSyncReturns } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { platformSlug } from "../src/driver/paths.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeDir = join(root, "runtime");
const engineDir = join(runtimeDir, "ext_gui");
const vendorDir = join(root, "vendor");
const sdlSrc = join(vendorDir, "SDL");
const sdlBuild = join(sdlSrc, "build");
const sdlArchive = join(sdlBuild, "libSDL3.a");
const freetypeSrc = join(vendorDir, "freetype");
const freetypeBuild = join(vendorDir, "freetype-build");
const freetypeArchive = join(freetypeBuild, "libfreetype.a");
const harfbuzzSrc = join(vendorDir, "harfbuzz");
const harfbuzzBuild = join(vendorDir, "harfbuzz-build");
const harfbuzzArchive = join(harfbuzzBuild, "libharfbuzz.a");
const objDir = join(root, "build", "gui-obj");
const outDir = join(runtimeDir, "lib", platformSlug());
const output = join(outDir, "gui.a");

const SDL_TAG = process.env.SDL3_TAG || "release-3.2.10";
const FREETYPE_VERSION = process.env.FREETYPE_VERSION || "2.13.3";
const HARFBUZZ_VERSION = process.env.HARFBUZZ_VERSION || "10.1.0";
const FREETYPE_URL = `https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.xz`;
const HARFBUZZ_URL = `https://github.com/harfbuzz/harfbuzz/releases/download/${HARFBUZZ_VERSION}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz`;

function fail(message: string): never {
  console.error(`xbintsc: ${message}`);
  process.exit(1);
}

if (process.platform === "win32") {
  fail("building gui.a is not supported on Windows yet (needs an MSVC .lib + D3D12/DXIL SDL3 build)");
}

function run(
  command: string,
  args: readonly string[],
  options: { input?: string; quietStderr?: boolean } = {},
): SpawnSyncReturns<string> {
  const quiet = options.quietStderr === true;
  const result = spawnSync(command, args, {
    stdio: options.input !== undefined ? ["pipe", "inherit", "inherit"] : quiet ? ["ignore", "inherit", "pipe"] : "inherit",
    encoding: "utf8",
    input: options.input,
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

function findCxx(): string {
  const cxx = firstAvailable([process.env.xbintsc_CXX, "clang++", "c++", "g++"]);
  if (!cxx) fail("no C++ compiler found; install clang++ or set xbintsc_CXX");
  return cxx;
}

function findArchiver(): string {
  const ar = firstAvailable([process.env.xbintsc_AR, "llvm-ar", "ar"]);
  if (!ar) fail("no archiver found; set xbintsc_AR to llvm-ar or ar");
  return ar;
}

/* -- 1. SDL3 source ------------------------------------------------------- */

if (!existsSync(join(sdlSrc, "CMakeLists.txt"))) {
  console.log(`xbintsc: fetching SDL3 ${SDL_TAG} into vendor/SDL`);
  mkdirSync(vendorDir, { recursive: true });
  run("git", ["clone", "--depth", "1", "--branch", SDL_TAG, "https://github.com/libsdl-org/SDL.git", sdlSrc]);
}

/* -- 2. static SDL3 ------------------------------------------------------- */

if (!existsSync(sdlArchive)) {
  const cmake = findCmake();
  console.log(`xbintsc: building static SDL3 with ${cmake}`);
  const configureArgs = [
    "-S", sdlSrc,
    "-B", sdlBuild,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DSDL_SHARED=OFF",
    "-DSDL_STATIC=ON",
    "-DSDL_TESTS=OFF",
    "-DSDL_EXAMPLES=OFF",
  ];
  run(cmake, configureArgs);
  run(cmake, ["--build", sdlBuild, "--config", "Release", "--target", "SDL3-static", "--parallel"]);
} else {
  console.log("xbintsc: reusing existing vendor/SDL/build/libSDL3.a");
}

/* -- 2b. FreeType + HarfBuzz (text) --------------------------------------- */

/** Download a release tarball into `vendor/` and extract it under `name`.
 * Release tarballs are used (not git clones) because they ship generated
 * sources, so no ragel/autotools step is needed. */
function ensureTarballSource(name: string, url: string, extractedName: string): string {
  const destination = join(vendorDir, name);
  if (existsSync(join(destination, "CMakeLists.txt"))) return destination;
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

if (!existsSync(freetypeArchive)) {
  const cmake = findCmake();
  console.log("xbintsc: building static FreeType");
  run(cmake, [
    "-S", freetypeSrc,
    "-B", freetypeBuild,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_SHARED_LIBS=OFF",
    /* We only need TrueType/OpenType outlines; skip the optional codecs. */
    "-DFT_DISABLE_ZLIB=TRUE",
    "-DFT_DISABLE_BZIP2=TRUE",
    "-DFT_DISABLE_PNG=TRUE",
    "-DFT_DISABLE_HARFBUZZ=TRUE",
    "-DFT_DISABLE_BROTLI=TRUE",
  ]);
  run(cmake, ["--build", freetypeBuild, "--config", "Release", "--parallel"]);
} else {
  console.log("xbintsc: reusing existing vendor/freetype-build/libfreetype.a");
}

if (!existsSync(harfbuzzArchive)) {
  const cmake = findCmake();
  console.log("xbintsc: building static HarfBuzz");
  run(cmake, [
    "-S", harfbuzzSrc,
    "-B", harfbuzzBuild,
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
  run(cmake, ["--build", harfbuzzBuild, "--config", "Release", "--parallel"]);
} else {
  console.log("xbintsc: reusing existing vendor/harfbuzz-build/libharfbuzz.a");
}

/* -- 3. engine objects ---------------------------------------------------- */

const cxx = findCxx();
mkdirSync(objDir, { recursive: true });
const includeFlags = [
  "-I", runtimeDir,
  "-I", join(sdlSrc, "include"),
  "-I", join(freetypeSrc, "include"),
  "-I", join(freetypeBuild, "include"),
  "-I", join(harfbuzzSrc, "src"),
];
const sources = readdirSync(engineDir)
  .filter((entry) => entry.endsWith(".cpp"))
  .sort();

const objects: string[] = [];
for (const source of sources) {
  const object = join(objDir, source.replace(/\.cpp$/, ".o"));
  console.log(`xbintsc: compiling ${source}`);
  run(cxx, [
    "-std=c++17", "-O2", "-Wall", "-Wextra", "-fPIC",
    ...includeFlags,
    "-c", join(engineDir, source), "-o", object,
  ]);
  objects.push(object);
}

/* -- 4. merge into gui.a -------------------------------------------------- */

mkdirSync(outDir, { recursive: true });
if (process.platform === "darwin") {
  /* libtool warns about the empty object files SDL3's archive carries; hide it
   * unless the merge actually fails. */
  run("libtool", ["-static", "-o", output, ...objects, sdlArchive, freetypeArchive, harfbuzzArchive], {
    quietStderr: true,
  });
} else {
  const ar = findArchiver();
  const script = [
    `create ${output}`,
    ...objects.map((object) => `addmod ${object}`),
    `addlib ${sdlArchive}`,
    `addlib ${freetypeArchive}`,
    `addlib ${harfbuzzArchive}`,
    "save",
    "end",
  ].join("\n");
  run(ar, ["-M"], { input: script });
}

console.log(`xbintsc: wrote ${output}`);
