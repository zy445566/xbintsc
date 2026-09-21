/**
 * Prebuilt runtime archives.
 *
 * The C runtime is normally compiled once into a static archive by
 * `npm run runtime`:
 *
 *   runtime/lib/<os>-<arch>/core.a        (the core `xt_*.c` runtime)
 *   runtime/lib/<os>-<arch>/ext_<name>.a  (per-extension sources, e.g. ext_node.a)
 *
 * Shipping these lets `build` link without compiling C, and shortens the first
 * build. When an archive is missing the driver transparently falls back to
 * compiling the `.c` sources, so the archives are an optimization, not a hard
 * dependency.
 */

import { existsSync } from "node:fs";
import { join } from "node:path";
import { findRuntimeDir, platformSlug } from "./paths.js";

/** Directory holding the prebuilt archives for the current host. */
export function runtimeLibDir(): string {
  return join(findRuntimeDir(), "lib", platformSlug());
}

/** Absolute path to a prebuilt archive (`core`, `ext_node`, …), if present. */
export function findRuntimeLibrary(name: string): string | undefined {
  const base = join(runtimeLibDir(), name);
  for (const candidate of [`${base}.a`, `${base}.lib`]) {
    if (existsSync(candidate)) return candidate;
  }
  return undefined;
}
