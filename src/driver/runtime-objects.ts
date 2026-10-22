/**
 * Compiling the C runtime (and every extension's C sources) into linkable
 * objects, reusing cached objects keyed by the source/header content hash.
 *
 * `RUNTIME_SOURCES` is the list of core translation units. A feature that adds
 * a runtime file adds one line here and nowhere else.
 */

import { existsSync, readFileSync, readdirSync } from "node:fs";
import { basename, join, resolve } from "node:path";
import type { ExtensionRegistry } from "../extensions/registry.js";
import { hashParts } from "./cache.js";
import { findRuntimeLibrary } from "./runtime-lib.js";
import { compileC, optimizationFlags, RUNTIME_CFLAGS, type PgoMode, type Runner } from "./toolchain.js";
import { COMPILER_VERSION } from "./version.js";

/** Core runtime translation units (each compiled and cached independently). */
export const RUNTIME_SOURCES = [
  "xt_alloc.c",
  "xt_values.c",
  "xt_bigint.c",
  "xt_containers.c",
  "xt_weak.c",
  "xt_typed_array.c",
  "xt_stdlib.c",
  "xt_stdlib2.c",
  "xt_promise.c",
  "xt_async.c",
  "xt_loop.c",
  "xt_generator.c",
  "xt_coroutine.c",
  "xt_fetch.c",
  "xt_symbol.c",
  "xt_builtins.c",
  "xt_constructors.c",
  "xt_structured_clone.c",
  "xt_performance.c",
  "xt_text_codec.c",
  "xt_url.c",
  "xt_array_buffer.c",
  "xt_io.c",
] as const;

export interface RuntimeObjects {
  readonly runtimeObjects: readonly string[];
  readonly extensionObjects: readonly string[];
  /** Pre-built C++/Rust objects and archives contributed by native extensions. */
  readonly nativeObjects: readonly string[];
}

export interface RuntimeObjectsOptions {
  readonly runner: Runner;
  readonly clang: string;
  readonly runtimeDir: string;
  readonly cacheDir: string;
  readonly registry: ExtensionRegistry;
  readonly preferPrebuilt: boolean;
  readonly env: Record<string, string>;
  readonly lto: boolean;
  readonly pgo: PgoMode | undefined;
  readonly pgoDir: string;
}

/**
 * Compile the core runtime and every extension source, reusing cached object
 * files keyed on the C source hash. Returns the object paths to link.
 *
 * When `preferPrebuilt` is set, a shipped static archive
 * (`runtime/lib/<os>-<arch>/{core,ext_<name>}.a`) is used instead of compiling
 * that group's C sources. Missing archives fall back to compiling the sources.
 */
export function ensureRuntimeObjects(options: RuntimeObjectsOptions): RuntimeObjects {
  const { runner, clang, runtimeDir, cacheDir, registry, preferPrebuilt, env, lto, pgo, pgoDir } = options;
  const optimization = optimizationFlags(lto, pgo, pgoDir);
  /* Runtime objects depend on the shared headers (rt.h/rt_internal.h/...) and
     the `#include`d implementation fragments (`.inc`), so any change to them
     must invalidate every cached object. Hash them all. */
  const headerParts: string[] = [];
  const collectHeaders = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) collectHeaders(full);
      else if (entry.name.endsWith(".h") || entry.name.endsWith(".inc")) headerParts.push(readFileSync(full, "utf8"));
    }
  };
  collectHeaders(runtimeDir);
  const headerStamp = hashParts(headerParts);

  const compileOne = (sourcePath: string, tag: string): string => {
    const body = readFileSync(sourcePath, "utf8");
    const key = hashParts([
      "runtime",
      COMPILER_VERSION,
      RUNTIME_CFLAGS.join(" "),
      optimization.join(" "),
      tag,
      headerStamp,
      body,
      clang,
      process.platform,
    ]);
    const objectPath = join(cacheDir, `${tag}-${key}.o`);
    if (!existsSync(objectPath)) {
      compileC(runner, clang, sourcePath, objectPath, runtimeDir, env, optimization);
    }
    return objectPath;
  };

  const coreArchive = preferPrebuilt ? findRuntimeLibrary("core") : undefined;
  const runtimeObjects = coreArchive
    ? [coreArchive]
    : RUNTIME_SOURCES.map((source) => compileOne(join(runtimeDir, source), basename(source, ".c")));

  const extensionObjects: string[] = [];
  const seen = new Set<string>();
  for (const extension of registry.all()) {
    const sources = extension.runtimeSources ? extension.runtimeSources() : [];
    if (sources.length === 0) continue;
    const archive = preferPrebuilt ? findRuntimeLibrary(`ext_${extension.name}`) : undefined;
    if (archive) {
      extensionObjects.push(archive);
      continue;
    }
    for (const sourcePath of sources) {
      const dedupeKey = resolve(sourcePath);
      if (seen.has(dedupeKey)) continue;
      seen.add(dedupeKey);
      extensionObjects.push(compileOne(sourcePath, `ext_${extensionObjects.length}_${basename(sourcePath, ".c")}`));
    }
  }
  // A native archive may be shared by several manifests; link each once.
  return { runtimeObjects, extensionObjects, nativeObjects: [...new Set(registry.nativeObjects())] };
}
