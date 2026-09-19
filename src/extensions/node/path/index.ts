/**
 * Node's `path` module: POSIX-style path helpers backed by
 * `runtime/ext_node/path`.
 *
 * `path` contributes no global builtins; the compiler lowers `path.<name>(...)`
 * to `xt_path_static(<name>, argc, argv)` (see `NAMESPACE_STATICS` in
 * `src/codegen/generator/tables.ts`). Importing the module aliases that
 * namespace (`import * as path from "path"`) and each named export dispatches
 * to the same runtime entry (`import { join } from "path"`).
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const methods = [
  "join",
  "resolve",
  "isAbsolute",
  "normalize",
  "dirname",
  "basename",
  "extname",
  "relative",
] as const;

const exports: ModuleExports = Object.fromEntries(
  methods.map((method) => [method, { namespace: "path", method }]),
);

export const pathModule: NodeModule = {
  name: "path",
  namespace: "path",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/path/path.c")],
  builtins: () => ({}),
  exports: () => exports,
};
