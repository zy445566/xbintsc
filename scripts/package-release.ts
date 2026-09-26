#!/usr/bin/env node
/**
 * Assemble a release archive for the current platform:
 *
 *   dist/release/xbintsc-<version>-<os>-<arch>.tar.gz        (or .tar.zst)
 *   dist/release/xbintsc-<version>-<os>-<arch>.tar.gz.sha256
 *
 * The version comes from package.json, so release assets from different
 * versions do not collide in a download folder.
 *
 * The unpacked staging tree is built under `build/release-stage/`, never under
 * `dist/release/`, so an upload glob over `dist/release/*` cannot accidentally
 * ship the (large) tree alongside the archive.
 *
 * xbintsc does not bundle a compiler: the archive holds only the self-hosted
 * binary and the C runtime sources plus prebuilt archives. Users provide the
 * toolchain themselves (see `doc/requirements.md`).
 *
 * Layout inside the archive:
 *
 *   xbintsc-<os>-<arch>/
 *     bin/xbintsc[.exe]     the self-hosted compiler
 *     runtime/              C runtime sources + prebuilt runtime/lib/<slug>
 *     README.md
 *
 * The compiler locates `runtime/` relative to its own executable (see
 * `findRuntimeDir`), so this tree runs without any bundled toolchain.
 *
 * Inputs:
 *   - the self-hosted binary staged at `dist/xbintsc[.exe]` (CI copies it there)
 *   - `runtime/lib/<slug>/`, built by `scripts/build-runtime.ts`
 */

import { createHash } from "node:crypto";
import {
  copyFileSync,
  cpSync,
  chmodSync,
  existsSync,
  mkdirSync,
  readFileSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { spawnSync } from "node:child_process";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  findRuntimeDir,
  releaseArchiveBase,
  releaseArchiveExtension,
  releaseArchiveFileName,
} from "../src/driver/paths.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const base = releaseArchiveBase();
const version = (
  JSON.parse(readFileSync(join(root, "package.json"), "utf8")) as { version: string }
).version;
const archiveBase = releaseArchiveFileName(version);
const exeSuffix = process.platform === "win32" ? ".exe" : "";

function fail(message: string): never {
  console.error(`xbintsc: ${message}`);
  process.exit(1);
}

/** First existing path from a list of candidates. */
function firstExisting(candidates: readonly string[]): string | undefined {
  for (const candidate of candidates) {
    if (existsSync(candidate)) return candidate;
  }
  return undefined;
}

const distDir = join(root, "dist");
const binary = firstExisting([
  join(distDir, `xbintsc${exeSuffix}`),
  join(distDir, "main" + exeSuffix),
  join(root, "scratch", "self", `main${exeSuffix}`),
]);
if (!binary) {
  fail(`no self-hosted binary found under ${distDir} (expected dist/xbintsc${exeSuffix})`);
}

// Archives go to `dist/release/`; the unpacked staging tree goes to
// `build/release-stage/` so the upload glob over `dist/release/*` cannot pick up
// the raw tree.
const releaseRoot = join(distDir, "release");
const stageRoot = join(root, "build", "release-stage");
const stage = join(stageRoot, base);
rmSync(stage, { recursive: true, force: true });
mkdirSync(join(stage, "bin"), { recursive: true });
mkdirSync(releaseRoot, { recursive: true });

// 1. The compiler binary.
const stagedBinary = join(stage, "bin", `xbintsc${exeSuffix}`);
copyFileSync(binary, stagedBinary);
if (process.platform !== "win32") chmodSync(stagedBinary, 0o755);
console.log(`xbintsc: packaged binary ${basename(binary)} (${statSync(binary).size} bytes)`);

// 2. The C runtime (sources fallback + prebuilt archives) next to the binary.
cpSync(findRuntimeDir(), join(stage, "runtime"), { recursive: true });
console.log("xbintsc: packaged runtime/");

// 3. Human-facing notes.
for (const name of ["README.md", "LICENSE"]) {
  const source = join(root, name);
  if (existsSync(source)) copyFileSync(source, join(stage, name));
}

// 4. Archive it. Prefer zstd when available, otherwise gzip.
const hasZstd = !spawnSync("zstd", ["--version"], { stdio: "ignore" }).error;
const extension = releaseArchiveExtension(hasZstd);
const archive = join(releaseRoot, `${archiveBase}${extension}`);
rmSync(archive, { force: true });
const tarArgs = ["-c", hasZstd ? "--zstd" : "-z", "-f", archive, "-C", stageRoot, base];
const tar = spawnSync("tar", tarArgs, { stdio: "inherit" });
if (tar.error) fail(`failed to run tar: ${tar.error.message}`);
if (tar.status !== 0) fail(`tar exited with ${tar.status}`);

// 5. Checksum, so users can verify the download.
const digest = createHash("sha256").update(readFileSync(archive)).digest("hex");
writeFileSync(`${archive}.sha256`, `${digest}  ${basename(archive)}\n`);

console.log(`xbintsc: wrote ${archive} (${statSync(archive).size} bytes)`);
console.log(`xbintsc: sha256 ${digest}`);
