/**
 * A single Node.js module contributed by the `node` extension.
 *
 * Each module lives in its own folder (e.g. `node/fs`) and pairs the globally
 * visible builtins it exposes with the C runtime sources that implement them.
 */

import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { BuiltinFunction } from "../registry.js";

export interface NodeModule {
  /** Module name, e.g. `fs`. */
  readonly name: string;
  /** C sources compiled and linked for this module. */
  runtimeSources(): readonly string[];
  /** Global identifiers that resolve to this module's runtime symbols. */
  builtins(): Readonly<Record<string, BuiltinFunction>>;
}

/** Resolve a path relative to the folder of the module that calls this. */
export function resolveFrom(importMetaUrl: string, relative: string): string {
  return resolve(dirname(fileURLToPath(importMetaUrl)), relative);
}
