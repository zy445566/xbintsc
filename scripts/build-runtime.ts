#!/usr/bin/env node
/**
 * Build the prebuilt runtime archives shipped with xbintsc:
 *
 *   runtime/lib/<os>-<arch>/core.a       compiled from RUNTIME_SOURCES
 *   runtime/lib/<os>-<arch>/ext_node.a   compiled from nodeExtension.runtimeSources()
 *
 * The driver prefers these archives (see `src/driver/runtime-lib.ts`) and falls
 * back to compiling the C sources on demand when they are missing. Run this in
 * CI (or locally) after the runtime sources change.
 *
 * Environment:
 *   xbintsc_CLANG  C compiler override (otherwise the resolved toolchain is used)
 *   xbintsc_AR     archiver override (otherwise `llvm-ar`, then `ar`)
 */

import { mkdirSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { RUNTIME_SOURCES } from "../src/driver/compiler.js";
import { platformSlug } from "../src/driver/paths.js";
import { resolveToolchain } from "../src/driver/toolchain-provider.js";
import { nodeExtension } from "../src/extensions/node/index.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeDir = join(root, "runtime");
const outDir = join(runtimeDir, "lib", platformSlug());
const objDir = join(root, "build", "runtime-obj");

if (process.platform === "win32") {
  // The Windows release ships no prebuilt archives yet: the current MSVC CI
  // cannot produce MinGW-compatible objects, so the driver compiles the C
  // sources on demand. Revisit once the bundled MinGW-w64 ABI lands.
  // See doc/self-contained-roadmap.md.
  console.log("xbintsc: skipped runtime archives on Windows until the MinGW-w64 toolchain lands");
  process.exit(0);
}

// Use the same toolchain the driver will: a bundled clang when present, with its
// `LD_LIBRARY_PATH` (the legacy `libtinfo.so.5` on Linux) threaded through.
const toolchain = resolveToolchain();
const clang = toolchain.clang;
const childEnv = Object.assign({}, process.env, toolchain.env);

function isRunnable(candidate: string): boolean {
  // An existing tool may still exit non-zero for `--version` (Apple `ar` does);
  // we only care that spawning it did not fail with ENOENT.
  return !spawnSync(candidate, [], { encoding: "utf8" }).error;
}

function findArchiver(clangPath: string): string {
  const dir = dirname(clangPath);
  const exe = process.platform === "win32" ? ".exe" : "";
  const candidates = [
    process.env.xbintsc_AR,
    // A bundled toolchain ships `llvm-ar` next to clang.
    dir === "." ? undefined : join(dir, `llvm-ar${exe}`),
    "llvm-ar",
    "ar",
  ].filter((candidate): candidate is string => Boolean(candidate));
  for (const candidate of candidates) {
    if (isRunnable(candidate)) return candidate;
  }
  console.error("xbintsc: no archiver found; set xbintsc_AR to llvm-ar or ar");
  process.exit(1);
}

function compile(sources: readonly string[], extension: string): string[] {
  mkdirSync(objDir, { recursive: true });
  const objects: string[] = [];
  sources.forEach((source, index) => {
    const object = join(objDir, `${extension}_${index}_${basename(source, ".c")}.o`);
    const result = spawnSync(
      clang,
      ["-O2", "-D_CRT_SECURE_NO_WARNINGS", "-c", source, "-o", object, `-I${runtimeDir}`],
      { stdio: "inherit", env: childEnv },
    );
    if (result.status !== 0) {
      console.error(`xbintsc: failed to compile ${source}`);
      process.exit(result.status ?? 1);
    }
    objects.push(object);
  });
  return objects;
}

function archive(name: string, objects: string[], ar: string): void {
  mkdirSync(outDir, { recursive: true });
  const output = join(outDir, name);
  rmSync(output, { force: true });
  const result = spawnSync(ar, ["rcs", output, ...objects], { stdio: "inherit", env: childEnv });
  if (result.status !== 0) {
    console.error(`xbintsc: failed to archive ${name}`);
    process.exit(result.status ?? 1);
  }
  console.log(`xbintsc: wrote ${output}`);
}

console.log(`xbintsc: runtime toolchain ${clang} (${toolchain.source})`);
const coreObjects = compile(RUNTIME_SOURCES.map((source) => join(runtimeDir, source)), "core");

const ar = findArchiver(clang);
archive("core.a", coreObjects, ar);

const nodeSources = nodeExtension.runtimeSources?.() ?? [];
if (nodeSources.length > 0) {
  archive("ext_node.a", compile(nodeSources, "node"), ar);
}
