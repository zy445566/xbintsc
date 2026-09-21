#!/usr/bin/env node
/**
 * Assemble a self-contained release archive for the current platform:
 *
 *   dist/release/xbintsc-<os>-<arch>.tar.gz        (or .tar.zst)
 *   dist/release/xbintsc-<os>-<arch>.tar.gz.sha256
 *
 * The unpacked staging tree is built under `build/release-stage/`, never under
 * `dist/release/`, so an upload glob over `dist/release/*` cannot accidentally
 * ship the (multi-GB) tree alongside the archive.
 *
 * Layout inside the archive:
 *
 *   xbintsc-<os>-<arch>/
 *     bin/xbintsc[.exe]     the self-hosted compiler
 *     runtime/              C runtime sources + prebuilt runtime/lib/<slug>
 *     vendor/<slug>/        the bundled clang+lld toolchain (Linux/Windows)
 *     README.md
 *
 * The compiler locates `runtime/` and `vendor/` relative to its own executable
 * (see `findRuntimeDir`/`findVendorDir`), so this tree runs with no system
 * toolchain installed.
 *
 * Inputs:
 *   - the self-hosted binary staged at `dist/xbintsc[.exe]` (CI copies it there)
 *   - `vendor/<slug>/`, fetched by `scripts/fetch-toolchain.ts`
 *   - `runtime/lib/<slug>/`, built by `scripts/build-runtime.ts`
 */

import { createHash } from "node:crypto";
import {
  copyFileSync,
  cpSync,
  chmodSync,
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  realpathSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { spawnSync } from "node:child_process";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  findRuntimeDir,
  platformSlug,
  releaseArchiveBase,
  releaseArchiveExtension,
} from "../src/driver/paths.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const slug = platformSlug();
const base = releaseArchiveBase();
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

/**
 * Copy only the parts of the fetched toolchain that xbintsc actually runs.
 *
 * The official LLVM release unpacks to ~7 GB: every MLIR/Flang/lldb tool, the
 * static archives and the full C++ headers. The compiler only shells out to the
 * clang driver, the lld linker and llvm-ar, and clang additionally needs its own
 * resource directory (`lib/clang/<version>`, the builtin headers and
 * compiler-rt). Copying just those keeps the archive ~100x smaller.
 */
function stageToolchain(vendor: string, dest: string): void {
  const exe = process.platform === "win32" ? ".exe" : "";
  mkdirSync(join(dest, "bin"), { recursive: true });

  const real = (...candidates: readonly string[]): string | undefined => {
    const found = firstExisting(candidates.map((candidate) => join(vendor, candidate)));
    return found ? realpathSync(found) : undefined;
  };
  const copyAs = (source: string | undefined, name: string): void => {
    if (!source) return;
    const target = join(dest, "bin", name);
    copyFileSync(source, target);
    if (process.platform !== "win32") chmodSync(target, 0o755);
  };

  // In the official release `clang`/`ld.lld` are symlinks into the bin/ dir;
  // copy the resolved targets under the names the toolchain provider (and
  // clang's `-fuse-ld=lld`) expect next to `bin/clang`.
  copyAs(real(`bin/clang${exe}`, `bin/clang-18${exe}`), `clang${exe}`);
  copyAs(real(`bin/ld.lld${exe}`, `bin/lld${exe}`), `ld.lld${exe}`);
  copyAs(real(`bin/llvm-ar${exe}`, "bin/llvm-ar"), `llvm-ar${exe}`);

  // clang's resource directory: builtin headers + compiler-rt archives.
  const resourceDir = join(vendor, "lib", "clang");
  if (existsSync(resourceDir)) {
    cpSync(resourceDir, join(dest, "lib", "clang"), { recursive: true });
  }

  // Shared libraries the fetch placed next to the toolchain (on Linux that is
  // the legacy `libtinfo.so.5` the official build still links).
  const libDir = join(vendor, "lib");
  if (existsSync(libDir)) {
    for (const entry of readdirSync(libDir, { withFileTypes: true })) {
      if (/^(.*\.so(\.\d+)*|.*\.dylib|.*\.dll)$/.test(entry.name)) {
        cpSync(join(libDir, entry.name), join(dest, "lib", entry.name), { recursive: true });
      }
    }
  }
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
// the raw tree (which would ship the toolchain twice, and uncompressed).
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

// 3. The bundled toolchain, when one was fetched for this platform.
const vendor = join(root, "vendor", slug);
if (existsSync(join(vendor, "bin"))) {
  if (process.platform === "win32") {
    // llvm-mingw spreads its MinGW-w64 CRT/import libraries across the tree, so
    // bundling it whole is the safe default there for now.
    cpSync(vendor, join(stage, "vendor", slug), { recursive: true });
  } else {
    stageToolchain(vendor, join(stage, "vendor", slug));
  }
  console.log(`xbintsc: packaged vendor/${slug}/`);
} else if (process.platform === "linux" || process.platform === "win32") {
  console.warn(
    `xbintsc: warning: no vendor/${slug}/ found; the release will rely on a system toolchain. ` +
      "Run `npm run fetch-toolchain` first for a self-contained archive.",
  );
} else {
  console.log(`xbintsc: no bundled toolchain for ${slug}; relying on the system SDK (macOS)`);
}

// 4. Human-facing notes.
for (const name of ["README.md", "LICENSE"]) {
  const source = join(root, name);
  if (existsSync(source)) copyFileSync(source, join(stage, name));
}

// 5. Archive it. Prefer zstd when available, otherwise gzip.
const hasZstd = !spawnSync("zstd", ["--version"], { stdio: "ignore" }).error;
const extension = releaseArchiveExtension(hasZstd);
const archive = join(releaseRoot, `${base}${extension}`);
rmSync(archive, { force: true });
const tarArgs = ["-c", hasZstd ? "--zstd" : "-z", "-f", archive, "-C", stageRoot, base];
const tar = spawnSync("tar", tarArgs, { stdio: "inherit" });
if (tar.error) fail(`failed to run tar: ${tar.error.message}`);
if (tar.status !== 0) fail(`tar exited with ${tar.status}`);

// 6. Checksum, so users can verify the download.
const digest = createHash("sha256").update(readFileSync(archive)).digest("hex");
writeFileSync(`${archive}.sha256`, `${digest}  ${basename(archive)}\n`);

console.log(`xbintsc: wrote ${archive} (${statSync(archive).size} bytes)`);
console.log(`xbintsc: sha256 ${digest}`);
