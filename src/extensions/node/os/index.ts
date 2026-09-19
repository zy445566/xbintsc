/**
 * Node's `os` module: host information backed by `runtime/ext_node/os`.
 *
 * `os` contributes no global builtins; the compiler lowers `os.<name>(...)` to
 * `xt_os_static(<name>, argc, argv)` (see `NAMESPACE_STATICS` in
 * `src/codegen/generator/tables.ts`). This module only links the C source.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const osModule: NodeModule = {
  name: "os",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/os/os.c")],
  builtins: () => ({}),
};
