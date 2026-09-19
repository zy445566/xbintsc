/**
 * Node's `path` module: POSIX-style path helpers backed by
 * `runtime/ext_node/path`.
 *
 * `path` contributes no global builtins; the compiler lowers `path.<name>(...)`
 * to `xt_path_static(<name>, argc, argv)` (see `NAMESPACE_STATICS` in
 * `src/codegen/generator/tables.ts`). This module only links the C source.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const pathModule: NodeModule = {
  name: "path",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/path/path.c")],
  builtins: () => ({}),
};
