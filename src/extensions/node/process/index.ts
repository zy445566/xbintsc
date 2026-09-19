/**
 * Node's `process` object backed by `runtime/ext_node/process`.
 *
 * The compiler lowers `process.<method>(...)` to
 * `xt_process_call(<method>, argc, argv)` and `process.<property>` to
 * `xt_process_get(<property>)` (see `NAMESPACE_STATICS` / `NAMESPACE_PROPERTIES`
 * in `src/codegen/generator/tables.ts`). This module only links the C source.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";

export const processModule: NodeModule = {
  name: "process",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/process/process.c")],
  builtins: () => ({}),
};
