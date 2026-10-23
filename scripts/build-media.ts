#!/usr/bin/env node
/**
 * Build the media extension archive:
 *
 *   runtime/lib/<os>-<arch>/media.a
 *
 * The GUI engine (`gui.a`) deliberately contains no decoder. This archive adds
 * `<video>`/`<audio>` support: it bundles the FFmpeg-backed player from
 * `runtime/ext_media/media.cpp` together with a minimal static FFmpeg, and the
 * `media` extension links it only when the program asks for it. A program built
 * with `--ext gui` alone therefore stays FFmpeg-free.
 *
 * Steps:
 *   1. fetch the pinned FFmpeg release tarball into `vendor/ffmpeg`,
 *   2. configure and build a minimal static FFmpeg into
 *      `vendor/ffmpeg-build/dist` (reused on later runs),
 *   3. compile the player against those headers,
 *   4. merge the player object and the FFmpeg libraries into `media.a`
 *      (libtool on macOS, `ar -M` elsewhere).
 *
 * Environment:
 *   xbintsc_CXX       C++ compiler override (default: clang++ next to clang)
 *   xbintsc_AR        archiver override (default: llvm-ar next to clang, then ar)
 *   FFMPEG_VERSION    FFmpeg release (default: 7.1)
 *
 * Windows is not supported yet: FFmpeg needs its own `configure`/`make`, and an
 * MSVC-compatible build has not been produced (see `doc/gui/known-issues.md`).
 */

import { existsSync, mkdirSync, renameSync, rmSync } from "node:fs";
import { availableParallelism } from "node:os";
import { spawnSync, type SpawnSyncReturns } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { platformSlug } from "../src/driver/paths.js";
import { probeClang, realRunner } from "../src/driver/toolchain.js";
import { resolveToolchain } from "../src/driver/toolchain-provider.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeDir = join(root, "runtime");
const sourceDir = join(runtimeDir, "ext_media");
const vendorDir = join(root, "vendor");
const ffmpegSrc = join(vendorDir, "ffmpeg");
const ffmpegBuild = join(vendorDir, "ffmpeg-build");
const ffmpegInstall = join(ffmpegBuild, "dist");
const objDir = join(root, "build", "media-obj");
const outDir = join(runtimeDir, "lib", platformSlug());
const output = join(outDir, "media.a");

const FFMPEG_VERSION = process.env.FFMPEG_VERSION || "7.1";
const FFMPEG_URL = `https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz`;

/* A deliberately minimal FFmpeg: containers and decoders for the common web
 * media formats, file access only, no network, no encoder, no filter graph.
 * Disabling everything first keeps the static libraries small and the build
 * short. `--disable-x86asm` drops the nasm/yasm build dependency (the C
 * fallbacks are plenty for playback). This configuration is LGPL-only, so the
 * resulting archive stays redistributable. */
const FFMPEG_COMPONENTS: readonly string[] = [
  "--enable-protocol=file",
  "--enable-demuxer=mov,matroska,avi,flv,mpegts,mp3,ogg,wav,flac,aac,m4v,asf,mpeg,mpegvideo,h264,hevc,vc1",
  "--enable-decoder=h264,hevc,vp8,vp9,av1,aac,aac_latm,mp3,mp3float,opus,vorbis,flac,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_u8,pcm_f32le,pcm_alaw,pcm_mulaw,ac3,eac3,wmav2,mjpeg",
  "--enable-parser=h264,hevc,vp8,vp9,av1,aac,aac_latm,mpegaudio,opus,vorbis,mpeg4video,mjpeg,flac",
  "--enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,vp9_superframe,vp9_superframe_split,extract_extradata",
];

const FFMPEG_LIBRARY_NAMES = ["libavformat.a", "libavcodec.a", "libswscale.a", "libswresample.a", "libavutil.a"];

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

/** The tool that sits next to `clang` in the same LLVM installation. */
function siblingTool(clangPath: string, name: string): string | undefined {
  if (!clangPath.includes("/") && !clangPath.includes("\\")) return undefined;
  const candidate = join(dirname(clangPath), `${name}.exe`);
  if (existsSync(candidate)) return candidate;
  const bare = join(dirname(clangPath), name);
  return existsSync(bare) ? bare : undefined;
}

/** Resolve `clang++` from the same LLVM as the driver's clang. */
function findClangxx(clangPath: string): string {
  const override = process.env.xbintsc_CXX;
  if (override) {
    const probe = probeClang(realRunner, override);
    if (!probe.responds) fail(`xbintsc_CXX ${override} does not respond to --version`);
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
  const archiver = firstAvailable([process.env.xbintsc_AR, sibling, "llvm-ar", "ar"]);
  if (!archiver) fail("no archiver found; install llvm-ar or set xbintsc_AR");
  return archiver;
}

function driverClang(): string {
  try {
    return resolveToolchain().clang;
  } catch (error) {
    fail(error instanceof Error ? error.message : String(error));
  }
}

/** Download the FFmpeg release tarball and extract it under `vendor/ffmpeg`. */
function ensureFfmpegSource(): void {
  if (existsSync(join(ffmpegSrc, "configure"))) return;
  const tarball = join(vendorDir, "ffmpeg.tar.xz");
  console.log(`xbintsc: fetching FFmpeg ${FFMPEG_VERSION} from ${FFMPEG_URL}`);
  mkdirSync(vendorDir, { recursive: true });
  run("curl", ["-sL", "--fail", "-o", tarball, FFMPEG_URL]);
  run("tar", ["-xf", tarball, "-C", vendorDir]);
  const extracted = join(vendorDir, `ffmpeg-${FFMPEG_VERSION}`);
  if (!existsSync(extracted)) fail(`expected ffmpeg-${FFMPEG_VERSION} after extracting`);
  rmSync(ffmpegSrc, { recursive: true, force: true });
  renameSync(extracted, ffmpegSrc);
  rmSync(tarball, { force: true });
}

function ffmpegStaticLibraries(): string[] | undefined {
  const libraries = FFMPEG_LIBRARY_NAMES.map((name) => join(ffmpegInstall, "lib", name));
  return libraries.every((library) => existsSync(library)) ? libraries : undefined;
}

if (process.platform === "win32") {
  fail(
    "the media extension is not available on Windows yet: FFmpeg needs a POSIX " +
      "configure/make build (see doc/gui/known-issues.md)",
  );
}

/* One toolchain for the player and FFmpeg: the same clang the driver uses, its
 * sibling clang++, and the matching llvm-ar. Mixing another C++ runtime or
 * object format would break the final link. */
const clang = driverClang();
const clangxx = findClangxx(clang);
const archiver = findArchiver(clang);

/* -- 1. FFmpeg source ------------------------------------------------------ */

ensureFfmpegSource();

/* -- 2. minimal static FFmpeg --------------------------------------------- */

let ffmpegLibraries = ffmpegStaticLibraries();
if (!ffmpegLibraries) {
  console.log(`xbintsc: building minimal static FFmpeg ${FFMPEG_VERSION} with ${clang}`);
  mkdirSync(ffmpegBuild, { recursive: true });
  run(join(ffmpegSrc, "configure"), [
    `--prefix=${ffmpegInstall}`,
    "--enable-static", "--disable-shared",
    "--disable-programs", "--disable-doc",
    "--disable-htmlpages", "--disable-manpages", "--disable-podpages", "--disable-txtpages",
    "--disable-avdevice", "--disable-avfilter", "--disable-postproc",
    "--disable-everything",
    "--enable-avformat", "--enable-avcodec", "--enable-avutil", "--enable-swscale", "--enable-swresample",
    "--disable-network", "--disable-autodetect", "--disable-x86asm", "--disable-debug", "--enable-pic",
    `--cc=${clang}`,
    `--cxx=${clangxx}`,
    ...FFMPEG_COMPONENTS,
  ], { cwd: ffmpegBuild });
  run("make", ["-j", String(availableParallelism())], { cwd: ffmpegBuild });
  run("make", ["install"], { cwd: ffmpegBuild });
  ffmpegLibraries = ffmpegStaticLibraries();
  if (!ffmpegLibraries) fail("FFmpeg build did not produce the expected static libraries");
} else {
  console.log(`xbintsc: reusing existing ${ffmpegInstall}/lib`);
}

/* -- 3. player object ------------------------------------------------------ */

/* FFmpeg installs its headers under a prefix (rather than in the source tree)
 * because the *source* root contains a file literally named `version` that
 * would shadow the C++ standard header for any `.cpp` that includes it. */
mkdirSync(objDir, { recursive: true });
const object = join(objDir, "media.o");
const playerSource = join(sourceDir, "media.cpp");
console.log("xbintsc: compiling media.cpp");
run(clangxx, [
  "-std=c++17", "-O2", "-Wall", "-Wextra", "-fPIC",
  "-I", runtimeDir,
  "-I", join(ffmpegInstall, "include"),
  "-c", playerSource,
  "-o", object,
]);

/* -- 4. merge into media.a ------------------------------------------------- */

mkdirSync(outDir, { recursive: true });
rmSync(output, { force: true });
if (process.platform === "darwin") {
  run("libtool", ["-static", "-o", output, object, ...ffmpegLibraries], { quietStderr: true });
} else {
  /* `ar -M` (GNU/LLVM) merges archives through an MRI script. */
  const mri = (path: string): string => path.replace(/\\/g, "/");
  const script = [
    `create ${mri(output)}`,
    `addmod ${mri(object)}`,
    ...ffmpegLibraries.map((library) => `addlib ${mri(library)}`),
    "save",
    "end",
  ].join("\n");
  run(archiver, ["-M"], { input: script });
}

console.log(`xbintsc: wrote ${output}`);
