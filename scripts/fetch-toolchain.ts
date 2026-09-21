#!/usr/bin/env node
/**
 * Download the pinned toolchain that xbintsc bundles for this host into
 * `vendor/<os>-<arch>/`, so `build` needs no user-installed compiler:
 *
 *   Linux   -> official LLVM release (clang + lld + llvm-ar) plus the legacy
 *              `libtinfo.so.5` shared library the official build still needs
 *   Windows -> llvm-mingw (clang + lld + MinGW-w64 CRT/import libraries)
 *   macOS   -> nothing; the Xcode Command Line Tools are the prerequisite
 *
 * Requires `curl`, `tar` and (on Linux) `ar` on PATH; all are present on
 * GitHub-hosted runners. Pass `--force` to refetch an existing toolchain.
 */

import { copyFileSync, existsSync, mkdirSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { platformSlug, vendorRootDir } from "../src/driver/paths.js";
import { toolchainDownload, toolchainSupportLibraries } from "../src/driver/toolchain-download.js";

const force = process.argv.includes("--force");
const target = toolchainDownload();

if (!target) {
  console.log(
    `xbintsc: no bundled toolchain for ${platformSlug()}; using the system SDK (macOS: Xcode Command Line Tools)`,
  );
  process.exit(0);
}

const vendorDir = join(vendorRootDir(), platformSlug());
const clangName = process.platform === "win32" ? "clang.exe" : "clang";
const clangPath = join(vendorDir, "bin", clangName);

function run(command: string, args: readonly string[], cwd?: string): void {
  const result = spawnSync(command, [...args], { stdio: "inherit", cwd });
  if (result.status !== 0) {
    console.error(`xbintsc: command failed: ${command} ${args.join(" ")}`);
    process.exit(result.status ?? 1);
  }
}

/** Download the first reachable URL to `dest`, failing after the last one. */
function download(urls: readonly string[], dest: string): void {
  for (const url of urls) {
    console.log(`xbintsc: downloading ${url}`);
    const result = spawnSync("curl", ["-fL", "--retry", "3", "-o", dest, url], { stdio: "inherit" });
    if (result.status === 0) return;
    console.log("xbintsc: download failed, trying the next mirror");
  }
  console.error(`xbintsc: could not download ${dest}`);
  process.exit(1);
}

const workDir = join(tmpdir(), `xbintsc-toolchain-${process.pid}`);
rmSync(workDir, { recursive: true, force: true });
mkdirSync(workDir, { recursive: true });

if (existsSync(clangPath) && !force) {
  console.log(`xbintsc: toolchain already present at ${vendorDir}`);
} else {
  const archivePath = join(workDir, target.archive);
  download([target.url], archivePath);
  console.log(`xbintsc: extracting into ${vendorDir}`);
  mkdirSync(vendorDir, { recursive: true });
  // `tar` here is bsdtar on macOS/Windows and GNU tar on Linux; both accept
  // `--strip-components`, and bsdtar also unpacks the `.zip` used on Windows.
  run("tar", ["-xf", archivePath, "-C", vendorDir, `--strip-components=${target.stripComponents}`]);
}

// Shared libraries the bundled toolchain needs but the OS does not provide.
const supportLibraries = toolchainSupportLibraries();
if (supportLibraries.length > 0) {
  const libDir = join(vendorDir, "lib");
  mkdirSync(libDir, { recursive: true });
  for (const library of supportLibraries) {
    const dest = join(libDir, library.dest);
    if (existsSync(dest) && !force) {
      console.log(`xbintsc: already bundled ${library.dest}`);
      continue;
    }
    const debPath = join(workDir, library.archive);
    download(library.urls, debPath);
    const debDir = join(workDir, `deb-${library.dest}`);
    mkdirSync(debDir, { recursive: true });
    // A `.deb` is an `ar` archive whose `data.tar.*` member holds the files.
    run("ar", ["x", debPath], debDir);
    run("tar", ["-xf", join(debDir, "data.tar.xz"), "-C", debDir]);
    copyFileSync(join(debDir, library.member), dest);
    console.log(`xbintsc: bundled ${library.dest}`);
  }
}

rmSync(workDir, { recursive: true, force: true });

if (!existsSync(clangPath)) {
  console.error(`xbintsc: expected ${clangPath} after extraction`);
  process.exit(1);
}
console.log(`xbintsc: done -> ${clangPath}`);
