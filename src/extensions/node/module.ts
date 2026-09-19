/**
 * A single Node.js module contributed by the `node` extension.
 *
 * Each module lives in its own folder (e.g. `node/fs`) and pairs the exports it
 * makes importable with the C runtime sources that implement them.
 */

import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { BuiltinFunction, ModuleExports } from "../registry.js";

export interface NodeModule {
  /** Module name, e.g. `fs`. */
  readonly name: string;
  /**
   * Runtime namespace used when the module is imported as a namespace, e.g.
   * `import * as path from "path"` aliases the `path` dispatcher. Modules that
   * only expose direct functions (like `fs`) leave this undefined.
   */
  readonly namespace?: string;
  /** C sources compiled and linked for this module. */
  runtimeSources(): readonly string[];
  /** Direct exports (runtime symbol bindings); also used by `import { x }`. */
  builtins(): Readonly<Record<string, BuiltinFunction>>;
  /** Named exports for `import { x } from "..."` (defaults to `builtins()`). */
  exports?(): ModuleExports;
}

/** Resolve a path relative to the folder of the module that calls this. */
export function resolveFrom(importMetaUrl: string, relative: string): string {
  return resolve(dirname(fileURLToPath(importMetaUrl)), relative);
}
