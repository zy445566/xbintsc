#!/usr/bin/env node
/**
 * Build the media extension archive:
 *
 *   runtime/lib/<os>-<arch>/media.a     (POSIX: ELF/Mach-O objects)
 *   runtime/lib/<os>-<arch>/media.lib   (Windows: MSVC/COFF objects)
 *
 * The GUI engine (`gui.a` / `gui.lib`) deliberately contains no decoder. This
 * archive adds `<video>`/`<audio>` support: it bundles the FFmpeg-backed player
 * from `runtime/ext_media/media.cpp` together with a minimal static FFmpeg, and
 * the `media` extension links it only when the program asks for it. A program
 * built with `--ext gui` alone therefore stays FFmpeg-free.
 *
 * Steps:
 *   1. fetch the pinned FFmpeg release tarball into `vendor/ffmpeg`,
 *   2. configure and build a minimal static FFmpeg into
 *      `vendor/ffmpeg-build/dist` (reused on later runs),
 *   3. compile the player against those headers,
 *   4. merge the player object and the FFmpeg libraries into the archive
 *      (libtool on macOS, `ar -M`/`llvm-ar -M` elsewhere).
 *
 * Windows uses FFmpeg's own MSVC toolchain mode: `configure --toolchain=msvc
 * --cc=clang-cl` produces COFF objects compiled against the static MSVC CRT,
 * which is the ABI `gui.lib`, the xbintsc runtime and the final link use. The
 * compiler chain is entirely Visual Studio (clang-cl, cl.exe, lib.exe and
 * llvm-ar); no additional C/C++ toolchain is involved.
 *
 * FFmpeg's `configure` is a POSIX shell script and its Makefiles require GNU
 * make, so those two build drivers remain. The shell comes from Git for Windows
 * (already required to clone SDL3), and make is a native GNU make on PATH or
 * `xbintsc_MAKE`; no additional POSIX environment is required. Because native
 * make cannot pass the MSVC `/showIncludes` awk pipeline through the Windows
 * command line unchanged, the generated `SRC_PATH` is rewritten to a
 * drive-letter path and dependency generation is skipped. Headers/libraries are
 * copied into the prefix instead of running `make install`.
 *
 * Environment:
 *   xbintsc_CXX       C++ compiler override (default: clang++ next to clang)
 *   xbintsc_AR        archiver override (default: llvm-ar next to clang, then ar)
 *   xbintsc_SHELL     POSIX shell override (Windows; default: Git for Windows bash)
 *   xbintsc_MAKE      GNU make override (Windows; default: `make` on PATH)
 *   FFMPEG_VERSION    FFmpeg release (default: 7.1)
 */

import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { spawnSync, type SpawnSyncReturns } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { platformSlug } from "../src/driver/paths.js";
import { isMsvcTarget, probeClang, realRunner } from "../src/driver/toolchain.js";
import { resolveToolchain } from "../src/driver/toolchain-provider.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeDir = join(root, "runtime");
const sourceDir = join(runtimeDir, "ext_media");
const vendorDir = join(root, "vendor");
const ffmpegSrc = join(vendorDir, "ffmpeg");
const isWindows = process.platform === "win32";
/* The MSVC configure/build tree is kept apart from the POSIX one: a shared tree
 * would carry `CC=clang`/`CC=clang-cl` and a POSIX/Windows `SRC_PATH` from the
 * other platform into the next configure. */
const ffmpegBuild = join(vendorDir, isWindows ? "ffmpeg-build-msvc" : "ffmpeg-build");
const ffmpegInstall = join(ffmpegBuild, "dist");
const objDir = join(root, "build", "media-obj");
const outDir = join(runtimeDir, "lib", platformSlug());
const output = join(outDir, isWindows ? "media.lib" : "media.a");

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
  options: { input?: string; quietStderr?: boolean; cwd?: string; env?: NodeJS.ProcessEnv } = {},
): SpawnSyncReturns<string> {
  const quiet = options.quietStderr === true;
  const result = spawnSync(command, args, {
    stdio: options.input !== undefined ? ["pipe", "inherit", "inherit"] : quiet ? ["ignore", "inherit", "pipe"] : "inherit",
    encoding: "utf8",
    input: options.input,
    cwd: options.cwd,
    env: options.env,
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
  const candidate = join(dirname(clangPath), `${name}${isWindows ? ".exe" : ""}`);
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
    if (isWindows && (probe.version === undefined || (probe.target !== undefined && !isMsvcTarget(probe.target)))) {
      fail(
        `xbintsc_CXX ${override} is not an MSVC-targeting clang++ (target ${probe.target ?? "unknown"}); ` +
          "the Windows media build requires one, for example x86_64-pc-windows-msvc.",
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
  /* Windows always uses llvm-ar: a non-LLVM `ar` there may write a different
   * archive flavour than the COFF one the linker wants. */
  const archiver = firstAvailable([process.env.xbintsc_AR, sibling, "llvm-ar", isWindows ? undefined : "ar"]);
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

/** True when `dist` holds both the libraries and the installed headers. */
function ffmpegInstalled(): boolean {
  return ffmpegStaticLibraries() !== undefined && existsSync(join(ffmpegInstall, "include", "libavutil", "avutil.h"));
}

/** Arguments shared by the POSIX and Windows FFmpeg configure runs. */
function ffmpegConfigureArgs(): string[] {
  const prefix = isWindows ? ffmpegInstall.replace(/\\/g, "/") : ffmpegInstall;
  const args = [
    `--prefix=${prefix}`,
    "--enable-static", "--disable-shared",
    "--disable-programs", "--disable-doc",
    "--disable-htmlpages", "--disable-manpages", "--disable-podpages", "--disable-txtpages",
    "--disable-avdevice", "--disable-avfilter", "--disable-postproc",
    "--disable-everything",
    "--enable-avformat", "--enable-avcodec", "--enable-avutil", "--enable-swscale", "--enable-swresample",
    "--disable-network", "--disable-autodetect", "--disable-x86asm", "--disable-debug",
    ...FFMPEG_COMPONENTS,
  ];
  /* `-fPIC` is meaningless to the MSVC ABI; FFmpeg's msvc flag filter drops it,
   * but omitting it keeps the configuration honest. */
  if (!isWindows) args.splice(2, 0, "--enable-pic");
  return args;
}

/* -- Windows: a POSIX shell + GNU make driving clang-cl -------------------- */

/** Drive-letter path in the `/d/dir/file` form the POSIX shell and configure use. */
function toPosixPath(path: string): string {
  const absolute = resolve(path);
  const match = /^([A-Za-z]):[\\/](.*)$/.exec(absolute);
  if (!match) return absolute.replace(/\\/g, "/");
  return `/${match[1].toLowerCase()}/${match[2].replace(/\\/g, "/")}`;
}

/** `D:\dir` form for the native make parser (it does not understand `/d/dir`). */
function toWindowsPath(path: string): string {
  return resolve(path).replace(/\\/g, "/");
}

/** Quote one word for a POSIX shell command line built by hand. */
function shellQuote(word: string): string {
  return `'${word.replace(/'/g, `'\\''`)}'`;
}

/** Git for Windows' bash, which supplies the POSIX shell FFmpeg's configure needs. */
function findPosixShell(): string {
  const programFiles = process.env.ProgramFiles ?? "C:\\Program Files";
  const localAppData = process.env.LOCALAPPDATA ?? "";
  const git = resolveOnPath("git");
  const candidates = [
    process.env.xbintsc_SHELL,
    git ? resolve(dirname(git), "..", "bin", "bash.exe") : undefined,
    join(programFiles, "Git", "bin", "bash.exe"),
    join(localAppData, "Programs", "Git", "bin", "bash.exe"),
    join(programFiles, "Git", "usr", "bin", "bash.exe"),
  ].filter((candidate): candidate is string => Boolean(candidate));
  const shell = firstAvailable(candidates);
  if (!shell) {
    fail(
      "the Windows media build needs a POSIX shell to run FFmpeg's configure; " +
        "install Git for Windows (which also supplies git) or set xbintsc_SHELL.",
    );
  }
  return shell;
}

/** First match for `name` on PATH, as an absolute path. */
function resolveOnPath(name: string): string | undefined {
  const result = spawnSync("where.exe", [name], { encoding: "utf8" });
  if (result.status !== 0) return undefined;
  return result.stdout.split(/\r?\n/).map((line) => line.trim()).find((line) => line.length > 0);
}

/** Native GNU make to drive the FFmpeg build. */
function findWindowsMake(): string {
  const candidates = [process.env.xbintsc_MAKE, resolveOnPath("make")].filter(
    (candidate): candidate is string => Boolean(candidate),
  );
  for (const candidate of candidates) {
    if (isRunnable(candidate)) return candidate;
  }
  fail(
    "the Windows media build needs GNU make to run FFmpeg's Makefile; install it " +
      "(`choco install make`) or set xbintsc_MAKE to a GNU make binary.",
  );
}

/** Resolve `clang-cl` from the same LLVM as the driver's clang. */
function findClangCl(clangPath: string): string {
  const sibling = siblingTool(clangPath, "clang-cl");
  if (sibling) return sibling;
  const onPath = resolveOnPath("clang-cl");
  if (onPath) return onPath;
  fail(
    `no clang-cl found next to ${clangPath} or on PATH; the Windows media build needs the LLVM bundled ` +
      "with Visual Studio (VC\\Tools\\Llvm\\<arch>\\bin\\clang-cl.exe) so FFmpeg can use the MSVC ABI.",
  );
}

/** Installation path reported by `vswhere`, when it is available. */
function vswhereInstallation(): string | undefined {
  const programFilesX86 = process.env["ProgramFiles(x86)"] ?? "C:\\Program Files (x86)";
  const vswhere = join(programFilesX86, "Microsoft Visual Studio", "Installer", "vswhere.exe");
  if (!existsSync(vswhere)) return undefined;
  const result = spawnSync(
    vswhere,
    [
      "-latest", "-products", "*",
      "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
      "-property", "installationPath",
    ],
    { encoding: "utf8" },
  );
  const root = result.status === 0 ? result.stdout.split(/\r?\n/)[0]?.trim() : undefined;
  return root && existsSync(root) ? root : undefined;
}

/** Directory holding `cl.exe` (and `lib.exe`), which FFmpeg's msvc toolchain probes. */
function findMsvcBin(clangPath: string): string {
  const roots = [process.env.ProgramFiles, process.env["ProgramFiles(x86)"], process.env.ProgramW6432]
    .filter((root): root is string => Boolean(root));
  /* A clang from Visual Studio's own LLVM names the installation it belongs to;
   * vswhere covers a clang from a separate LLVM when VS is installed elsewhere. */
  const fromClang = /^(.*[\\/]Microsoft Visual Studio[\\/]\d{4}[\\/][^\\/]+)[\\/]VC[\\/]Tools[\\/]Llvm[\\/]/i.exec(
    clangPath,
  )?.[1];
  const fromVswhere = vswhereInstallation();
  const msvcDirs: string[] = [];
  for (const extra of [fromClang, fromVswhere]) {
    if (extra) msvcDirs.push(join(extra, "VC", "Tools", "MSVC"));
  }
  for (const root of roots) {
    for (const year of ["2022", "2019"]) {
      for (const edition of ["BuildTools", "Community", "Professional", "Enterprise"]) {
        msvcDirs.push(join(root, "Microsoft Visual Studio", year, edition, "VC", "Tools", "MSVC"));
      }
    }
  }
  const host = process.arch === "arm64" ? "Hostarm64" : "Hostx64";
  const target = process.arch === "arm64" ? "arm64" : "x64";
  for (const msvc of msvcDirs) {
    if (!existsSync(msvc)) continue;
    const versions = readdirSync(msvc).sort((a, b) => b.localeCompare(a, undefined, { numeric: true }));
    for (const version of versions) {
      const bin = join(msvc, version, "bin", host, target);
      if (existsSync(join(bin, "cl.exe"))) return bin;
    }
  }
  fail(
    "the Windows media build needs the MSVC C++ toolchain (FFmpeg's configure probes cl.exe/lib.exe); " +
      "install the 'Desktop development with C++' workload in Visual Studio or the Build Tools.",
  );
}

interface WindowsBuildTools {
  readonly shell: string;
  readonly make: string;
  readonly env: NodeJS.ProcessEnv;
}

/** Environment for configure/make: the resolved clang-cl and MSVC tools first. */
function windowsBuildEnv(shell: string, clangCl: string, msvcBin: string): NodeJS.ProcessEnv {
  const env: NodeJS.ProcessEnv = { ...process.env };
  const pathKey = Object.keys(env).find((key) => key.toUpperCase() === "PATH") ?? "Path";
  const usrBin = resolve(dirname(shell), "..", "usr", "bin");
  const parts = [dirname(clangCl), msvcBin, existsSync(usrBin) ? usrBin : "", env[pathKey] ?? ""];
  env[pathKey] = parts.filter((part) => part.length > 0).join(";");
  return env;
}

/** Rewrite the POSIX `SRC_PATH` configure wrote so a native make can parse it. */
function patchNativeMakePaths(): void {
  const posixSource = toPosixPath(ffmpegSrc);
  const posixInstall = toPosixPath(ffmpegInstall);
  for (const file of [join(ffmpegBuild, "Makefile"), join(ffmpegBuild, "ffbuild", "config.mak")]) {
    if (!existsSync(file)) continue;
    const patched = readFileSync(file, "utf8")
      .split(posixSource).join(toWindowsPath(ffmpegSrc))
      .split(posixInstall).join(toWindowsPath(ffmpegInstall));
    writeFileSync(file, patched);
  }
}

/** Copy every public `.h` under `source` into `destination`, recursively. */
function copyHeaders(source: string, destination: string): void {
  mkdirSync(destination, { recursive: true });
  for (const entry of readdirSync(source, { withFileTypes: true })) {
    const from = join(source, entry.name);
    const to = join(destination, entry.name);
    if (entry.isDirectory()) {
      copyHeaders(from, to);
    } else if (entry.isFile() && entry.name.endsWith(".h")) {
      copyFileSync(from, to);
    }
  }
}

/** Copy FFmpeg headers and static libraries into the `dist` prefix. */
function installFfmpegWindows(): void {
  const includeDir = join(ffmpegInstall, "include");
  for (const name of FFMPEG_LIBRARY_NAMES) {
    const library = name.replace(/\.a$/, "");
    copyHeaders(join(ffmpegSrc, library), join(includeDir, library));
  }
  /* Generated headers live in the build tree, not in the sources. */
  for (const generated of ["avconfig.h", "ffversion.h"]) {
    const from = join(ffmpegBuild, "libavutil", generated);
    if (existsSync(from)) copyFileSync(from, join(includeDir, "libavutil", generated));
  }
  const libDir = join(ffmpegInstall, "lib");
  mkdirSync(libDir, { recursive: true });
  for (const name of FFMPEG_LIBRARY_NAMES) {
    copyFileSync(join(ffmpegBuild, name.replace(/\.a$/, ""), name), join(libDir, name));
  }
}

function buildFfmpegWindows(shell: string, clangCl: string, msvcBin: string): void {
  const make = findWindowsMake();
  const tools: WindowsBuildTools = {
    shell,
    make,
    env: windowsBuildEnv(shell, clangCl, msvcBin),
  };
  console.log(`xbintsc: configuring FFmpeg ${FFMPEG_VERSION} for the MSVC ABI with clang-cl`);
  mkdirSync(ffmpegBuild, { recursive: true });
  run(tools.shell, [
    toPosixPath(join(ffmpegSrc, "configure")),
    "--toolchain=msvc",
    "--cc=clang-cl", "--cxx=clang-cl", "--ld=clang-cl", "--ar=lib.exe",
    ...ffmpegConfigureArgs(),
  ], { cwd: ffmpegBuild, env: tools.env });

  /* Native make cannot pass FFmpeg's `/showIncludes` awk pipeline (the MSVC
   * backslash escapes are consumed by the Windows command line); the `.d` files
   * only affect incremental rebuilds, so generation is skipped. The source path
   * configure wrote is POSIX-shaped and is rewritten for native make. */
  patchNativeMakePaths();
  const depOverrides = " CCDEP=true CXXDEP=true ASDEP=true HOSTCCDEP=true";
  console.log(`xbintsc: building minimal static FFmpeg ${FFMPEG_VERSION} with clang-cl`);
  run(tools.shell, ["-lc", `${shellQuote(tools.make)} -j ${availableParallelism()}${depOverrides}`], {
    cwd: ffmpegBuild,
    env: tools.env,
  });
  installFfmpegWindows();
}

/* -- 1. FFmpeg source ------------------------------------------------------ */

ensureFfmpegSource();

/* One toolchain for the player and FFmpeg: the same clang the driver uses, its
 * sibling clang++, and the matching llvm-ar. Mixing another C++ runtime or
 * object format would break the final link. */
const clang = driverClang();
const clangxx = findClangxx(clang);
const archiver = findArchiver(clang);
const windowsTools = isWindows
  ? {
      shell: findPosixShell(),
      clangCl: findClangCl(clang),
      msvcBin: findMsvcBin(clang),
    }
  : undefined;

/* -- 2. minimal static FFmpeg --------------------------------------------- */

if (!ffmpegInstalled()) {
  if (windowsTools) {
    buildFfmpegWindows(windowsTools.shell, windowsTools.clangCl, windowsTools.msvcBin);
  } else {
    console.log(`xbintsc: building minimal static FFmpeg ${FFMPEG_VERSION} with ${clang}`);
    mkdirSync(ffmpegBuild, { recursive: true });
    run(join(ffmpegSrc, "configure"), ffmpegConfigureArgs(), { cwd: ffmpegBuild });
    run("make", ["-j", String(availableParallelism())], { cwd: ffmpegBuild });
    run("make", ["install"], { cwd: ffmpegBuild });
  }
  if (!ffmpegInstalled()) fail("FFmpeg build did not produce the expected static libraries");
} else {
  console.log(`xbintsc: reusing existing ${ffmpegInstall}/lib`);
}

/* -- 3. player object ------------------------------------------------------ */

/* FFmpeg installs its headers under a prefix (rather than in the source tree)
 * because the *source* root contains a file literally named `version` that
 * would shadow the C++ standard header for any `.cpp` that includes it. */
mkdirSync(objDir, { recursive: true });
/* Two translation units: the file-oriented player and the in-memory decoder
 * `decodeAudioData` uses. Both are merged into the archive; the bridge object
 * references every entry point, so each member is extracted on demand. */
const playerSources = ["media.cpp", "decode_audio.cpp"];
const playerObjects: string[] = [];
const cxxFlags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-I", runtimeDir, "-I", join(ffmpegInstall, "include")];
/* The engine and the player are C++ with the static CRT (`libcpmt`), matching
 * FFmpeg's clang-cl objects and the xbintsc runtime; see `scripts/build-gui.ts`. */
if (isWindows) cxxFlags.push("-Xclang", "--dependent-lib=libcpmt");
else cxxFlags.push("-fPIC");
for (const name of playerSources) {
  const source = join(sourceDir, name);
  const compiled = join(objDir, isWindows ? name.replace(/\.cpp$/, ".obj") : name.replace(/\.cpp$/, ".o"));
  console.log(`xbintsc: compiling ${name}`);
  run(clangxx, [...cxxFlags, "-c", source, "-o", compiled]);
  playerObjects.push(compiled);
}

/* -- 4. merge into the archive -------------------------------------------- */

mkdirSync(outDir, { recursive: true });
rmSync(output, { force: true });
if (process.platform === "darwin") {
  run("libtool", ["-static", "-o", output, ...playerObjects, ...ffmpegStaticLibraries()!], { quietStderr: true });
} else {
  /* `ar -M` (GNU/LLVM) merges archives through an MRI script. Windows paths are
   * normalised to `/` so the MRI parser does not read `\` as an escape. */
  const mri = (path: string): string => path.replace(/\\/g, "/");
  const script = [
    `create ${mri(output)}`,
    ...playerObjects.map((object) => `addmod ${mri(object)}`),
    ...ffmpegStaticLibraries()!.map((library) => `addlib ${mri(library)}`),
    "save",
    "end",
  ].join("\n");
  run(archiver, ["-M"], { input: script });
}

console.log(`xbintsc: wrote ${output}`);
