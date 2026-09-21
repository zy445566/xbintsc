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
 *   xbintsc_CLANG  C compiler (default: clang)
 *   xbintsc_AR     archiver (default: llvm-ar, then ar)
 */

import { mkdirSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { RUNTIME_SOURCES } from "../src/driver/compiler.js";
import { platformSlug } from "../src/driver/paths.js";
import { nodeExtension } from "../src/extensions/node/index.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeDir = join(root, "runtime");
const outDir = join(runtimeDir, "lib", platformSlug());
const objDir = join(root, "build", "runtime-obj");
const clang = process.env.xbintsc_CLANG ?? "clang";

function isRunnable(candidate: string): boolean {
  // An existing tool may still exit non-zero for `--version` (Apple `ar` does);
  // we only care that spawning it did not fail with ENOENT.
  return !spawnSync(candidate, [], { encoding: "utf8" }).error;
}

function findArchiver(): string {
  const candidates = [process.env.xbintsc_AR, "llvm-ar", "ar"].filter(
    (candidate): candidate is string => Boolean(candidate),
  );
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
      { stdio: "inherit" },
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
  const result = spawnSync(ar, ["rcs", output, ...objects], { stdio: "inherit" });
  if (result.status !== 0) {
    console.error(`xbintsc: failed to archive ${name}`);
    process.exit(result.status ?? 1);
  }
  console.log(`xbintsc: wrote ${output}`);
}

const coreObjects = compile(RUNTIME_SOURCES.map((source) => join(runtimeDir, source)), "core");

if (process.platform === "win32") {
  // The current Windows CI uses the MSVC toolchain; static `.a` archives and
  // its import libraries are only wired up once the MinGW-w64 ABI lands.
  // See doc/self-contained-roadmap.md.
  console.log(
    "xbintsc: compiled runtime objects; skipped archives on Windows until the MinGW-w64 toolchain lands",
  );
  process.exit(0);
}

const ar = findArchiver();
archive("core.a", coreObjects, ar);

const nodeSources = nodeExtension.runtimeSources?.() ?? [];
if (nodeSources.length > 0) {
  archive("ext_node.a", compile(nodeSources, "node"), ar);
}
