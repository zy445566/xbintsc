/**
 * A single Node.js module contributed by the `node` extension.
 *
 * Each module lives in its own folder (e.g. `node/fs`) and pairs the exports it
 * makes importable with the C runtime sources that implement them.
 */

import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { findRuntimeDir } from "../../driver/paths.js";
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

/**
 * Resolve a C runtime source that is written relative to an extension module
 * (e.g. `../../../../runtime/ext_node/fs/read_file.c`).
 *
 * When running from source the page is resolved against the calling module's
 * own URL, but inside a compiled xbintsc binary `import.meta.url` points at the
 * executable, so the module-relative form no longer resolves. Anchor on the
 * discovered runtime directory instead, which is correct in both worlds.
 */
export function resolveFrom(importMetaUrl: string, relative: string): string {
  const marker = "runtime/";
  const index = relative.indexOf(marker);
  if (index >= 0) return resolve(findRuntimeDir(), relative.slice(index + marker.length));
  return resolve(dirname(fileURLToPath(importMetaUrl)), relative);
}
